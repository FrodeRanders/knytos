//! Admission and observation share a caller-owned monotonic deadline.
//! There is no automatic retry of accepted work or business mutations.

use crate::{
    Lifecycle,
    ShutdownRequest,
    cq,
    deadline::Deadline,
    owned::{
        CallResult,
        Completion,
        CompletionError,
        ConnectionRef,
        IpcError,
        OwnedMemory,
        PendingCall,
    },
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AdmissionError {
    Deadline,
    Shutdown(ShutdownRequest),
    Rejected(IpcError),
    Backoff(CompletionError),
    BackoffResult(i64),
    Interrupted(InterruptError),
}

/// Bounded IPC admission. Queue capacity has no producer subscription yet, so
/// rejected QUEUE_FULL submissions use one-millisecond backoff.
/// Other failures return immediately. Lifecycle is rechecked between attempts.
pub struct Admission<'a> {
    deadline: &'a Deadline,
    lifecycle: Option<Lifecycle<'a>>,
    interrupt: Option<&'a InterruptibleWait<'a>>,
}

impl<'a> Admission<'a> {
    pub const fn new(deadline: &'a Deadline) -> Self {
        Self {
            deadline,
            lifecycle: None,
            interrupt: None,
        }
    }

    pub fn with_lifecycle(deadline: &'a Deadline, lifecycle: Lifecycle<'a>) -> Self {
        Self {
            deadline,
            lifecycle: Some(lifecycle),
            interrupt: None,
        }
    }

    pub fn interruptible(mut self, wait: &'a InterruptibleWait<'a>) -> Self {
        self.interrupt = Some(wait);
        self
    }

    pub fn call(
        &self,
        connection: ConnectionRef<'_>,
        opcode: u32,
        arg0: u64,
    ) -> Result<PendingCall<'static>, AdmissionError> {
        self.submit(|| connection.call(opcode, arg0))
    }

    pub fn call_copy(
        &self,
        connection: ConnectionRef<'_>,
        opcode: u32,
        arg0: u64,
        memory: &OwnedMemory,
    ) -> Result<PendingCall<'static>, AdmissionError> {
        self.submit(|| connection.call_copy(opcode, arg0, memory))
    }

    /// Rejected admission returns the original request owner. Once accepted,
    /// no subsequent failure in this method can cause another submission.
    pub fn call_move(
        &self,
        connection: ConnectionRef<'_>,
        opcode: u32,
        arg0: u64,
        memory: OwnedMemory,
    ) -> Result<PendingCall<'static>, (OwnedMemory, AdmissionError)> {
        let mut request = Some(memory);
        self.submit(|| {
            match connection.call_move(opcode, arg0, request.take().expect("unstarted request")) {
                Ok(pending) => Ok(pending),
                Err((memory, error)) => {
                    request = Some(memory);
                    Err(error)
                }
            }
        })
        .map_err(|error| (request.expect("rejected request"), error))
    }

    fn submit<T>(
        &self,
        mut submit: impl FnMut() -> Result<T, IpcError>,
    ) -> Result<T, AdmissionError> {
        loop {
            if let Some(wait) = self.interrupt {
                wait.check().map_err(AdmissionError::Interrupted)?;
            }
            if let Some(request) = self.lifecycle.and_then(|l| l.shutdown_requested()) {
                return Err(AdmissionError::Shutdown(request));
            }
            let remaining = self.deadline.remaining_ms();
            if remaining == 0 {
                return Err(AdmissionError::Deadline);
            }
            match submit() {
                Ok(owner) => return Ok(owner),
                Err(IpcError::Status(knytos_syscall::ipc_status::QUEUE_FULL)) => {
                    if let Some(wait) = self.interrupt {
                        wait.pause(self.deadline, 1).map_err(AdmissionError::Interrupted)?;
                        continue;
                    }
                    let pause =
                        Completion::timer(self.deadline.remaining_ms().min(remaining).min(1))
                            .map_err(AdmissionError::Backoff)?;
                    let result = pause.wait().map_err(AdmissionError::Backoff)?;
                    if result != 0 {
                        return Err(AdmissionError::BackoffResult(result));
                    }
                }
                Err(error) => return Err(AdmissionError::Rejected(error)),
            }
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum InterruptError {
    Cancelled,
    Wait(cq::WaitError),
}

/// Observe a reply and a caller-owned cancellation predicate on the same CQ.
/// The producer must publish cancellation before waking this queue.
pub struct InterruptibleWait<'a> {
    queue: u32,
    cancelled: &'a dyn Fn() -> bool,
}

impl<'a> InterruptibleWait<'a> {
    pub const fn new(queue: u32, cancelled: &'a dyn Fn() -> bool) -> Self {
        Self {
            queue,
            cancelled,
        }
    }

    pub fn check(&self) -> Result<(), InterruptError> {
        if (self.cancelled)() {
            return Err(InterruptError::Cancelled);
        }
        Ok(())
    }

    fn wait(&self, remaining: u64) -> Result<(), InterruptError> {
        cq::wait(Some(remaining), self.queue).map(|_| ()).map_err(InterruptError::Wait)
    }

    /// Keep a bounded backoff for a source with no readiness contract. Only
    /// expiry of the original retry delay or total budget ends the pause.
    pub fn pause(&self, deadline: &Deadline, delay_ms: u64) -> Result<(), InterruptError> {
        let pause = Deadline::after(delay_ms);
        loop {
            self.check()?;
            let remaining = pause.remaining_ms().min(deadline.remaining_ms());
            if remaining == 0 {
                return Ok(());
            }
            self.wait(remaining)?;
        }
    }

    /// Consume the exact pending owner. Observation failure cancels through
    /// its fail-closed Drop policy, which requires an unguarded caller scope.
    pub fn observe(
        &self,
        mut pending: PendingCall<'_>,
        deadline: &Deadline,
    ) -> Result<CallResult, ObservationError> {
        self.check().map_err(ObservationError::Interrupted)?;
        pending.bind_cq(self.queue).map_err(|error| match error {
            IpcError::Status(knytos_syscall::ipc_status::RESOURCE_LIMIT) => {
                ObservationError::WaitRejected(error)
            }
            _ => ObservationError::Ipc(error),
        })?;
        loop {
            self.check().map_err(ObservationError::Interrupted)?;
            if let Some(reply) = pending.poll().map_err(ObservationError::Ipc)? {
                self.check().map_err(ObservationError::Interrupted)?;
                return Ok(reply);
            }
            let remaining = deadline.remaining_ms();
            if remaining == 0 {
                return Err(ObservationError::Deadline);
            }
            self.wait(remaining).map_err(ObservationError::Interrupted)?;
        }
    }
}

/// Deadline and rejected-wait cases keep the complete pending owner and loans.
#[must_use = "retain or explicitly cancel the accepted operation"]
#[derive(Debug)]
pub enum CallObservation<'memory> {
    Reply(CallResult),
    Deadline(PendingCall<'memory>),
    WaitRejected {
        pending: PendingCall<'memory>,
        error: IpcError,
    },
    Failed(IpcError),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ObservationError {
    Deadline,
    WaitRejected(IpcError),
    Ipc(IpcError),
    Interrupted(InterruptError),
}

impl CallObservation<'_> {
    /// Explicit convenience policy: cancel through PendingCall's existing
    /// fail-closed Drop on observation failure. Cleanup may outlive the budget;
    /// rejected cleanup aborts with the loan retained. This never authorizes replay.
    pub fn cancel_on_failure(self) -> Result<CallResult, ObservationError> {
        match self {
            Self::Reply(reply) => Ok(reply),
            Self::Deadline(pending) => {
                drop(pending);
                Err(ObservationError::Deadline)
            }
            Self::WaitRejected {
                pending,
                error,
            } => {
                drop(pending);
                Err(ObservationError::WaitRejected(error))
            }
            Self::Failed(error) => Err(ObservationError::Ipc(error)),
        }
    }
}

impl<'memory> PendingCall<'memory> {
    /// Observe even at an expired deadline: a zero-budget wait rechecks terminal
    /// state. Expiry and wait-admission rejection retain this exact owner.
    pub fn observe_until(mut self, deadline: &Deadline) -> CallObservation<'memory> {
        match self.wait_timeout(deadline.remaining_ms()) {
            Ok(Some(reply)) => CallObservation::Reply(reply),
            Ok(None) => CallObservation::Deadline(self),
            Err(error @ IpcError::Status(knytos_syscall::ipc_status::RESOURCE_LIMIT)) => {
                CallObservation::WaitRejected {
                    pending: self,
                    error,
                }
            }
            Err(error) => CallObservation::Failed(error),
        }
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use core::cell::Cell;
    use std::collections::VecDeque;

    use super::*;
    use crate::owned::{
        Connection,
        kernel::{
            self,
            Event,
        },
    };

    #[test]
    fn permanent_admission_rejection_is_never_retried() {
        let _guard = kernel::serial();
        kernel::reset();
        let deadline = Deadline::at(0, 1000, 10);
        let admission = Admission::new(&deadline);
        let mut attempts = 0;
        let error = admission.submit::<()>(|| {
            attempts += 1;
            Err(IpcError::Status(99))
        });
        assert_eq!(error, Err(AdmissionError::Rejected(IpcError::Status(99))));
        assert_eq!(attempts, 1);
        assert!(kernel::events().is_empty());
    }

    #[test]
    fn queue_backoff_retains_original_deadline_and_stops_without_resubmitting() {
        let _guard = kernel::serial();
        kernel::reset();
        kernel::update(|s| s.clocks = VecDeque::from([(0, 1000), (1, 1000), (10, 1000)]));
        let deadline = Deadline::at(0, 1000, 10);
        let mut attempts = 0;
        let error = Admission::new(&deadline).submit::<()>(|| {
            attempts += 1;
            Err(IpcError::Status(knytos_syscall::ipc_status::QUEUE_FULL))
        });
        assert_eq!(error, Err(AdmissionError::Deadline));
        assert_eq!(attempts, 1);
    }

    #[test]
    fn interrupted_queue_backoff_stops_before_resubmission() {
        let _guard = kernel::serial();
        kernel::reset();
        let deadline = Deadline::at(0, 1000, 10);
        let cancelled = Cell::new(false);
        let interrupted = || cancelled.get();
        let wait = InterruptibleWait::new(2, &interrupted);
        let mut attempts = 0;
        let error = Admission::new(&deadline).interruptible(&wait).submit::<()>(|| {
            attempts += 1;
            cancelled.set(true);
            Err(IpcError::Status(knytos_syscall::ipc_status::QUEUE_FULL))
        });
        assert_eq!(error, Err(AdmissionError::Interrupted(InterruptError::Cancelled)));
        assert_eq!(attempts, 1);
        assert!(kernel::events().is_empty());
    }

    #[test]
    fn interruption_after_binding_closes_the_exact_pending_call() {
        let _guard = kernel::serial();
        kernel::reset();
        let connection = unsafe { Connection::from_raw(8) }.unwrap();
        let memory = OwnedMemory::allocate(1).unwrap();
        let pending = connection.call_borrow_read(1, 0, &memory).unwrap();
        let checked = Cell::new(false);
        let interrupted = || checked.replace(true);
        let wait = InterruptibleWait::new(2, &interrupted);
        let result = wait.observe(pending, &Deadline::at(0, 1000, 10));
        assert!(matches!(result, Err(ObservationError::Interrupted(InterruptError::Cancelled))));
        assert_eq!(kernel::events(), [Event::IpcReplyBindCq(30, 2), Event::IpcClose(30)]);
        drop(memory);
        assert_eq!(kernel::events().last(), Some(&Event::MemoryClose(10)));
    }

    #[test]
    fn timeout_and_wait_rejection_keep_pending_loan_until_cancellation() {
        let _guard = kernel::serial();
        for status in [1, knytos_syscall::ipc_status::RESOURCE_LIMIT] {
            kernel::reset();
            kernel::update(|s| s.ipc_reply.push_back((status, 0, 0, 0)));
            let connection = unsafe { Connection::from_raw(8) }.unwrap();
            let memory = OwnedMemory::allocate(1).unwrap();
            let pending = connection.call_borrow_read(1, 0, &memory).unwrap();
            let observation = pending.observe_until(&Deadline::at(0, 1000, 10));
            assert!(!kernel::events().contains(&Event::IpcClose(30)));
            let pending = match observation {
                CallObservation::Deadline(p) if status == 1 => p,
                CallObservation::WaitRejected {
                    pending,
                    ..
                } if status != 1 => pending,
                other => panic!("unexpected {other:?}"),
            };
            pending.close().unwrap();
            assert!(kernel::events().contains(&Event::IpcClose(30)));
            assert!(!kernel::events().contains(&Event::MemoryClose(10)));
            drop(connection);
        }
    }

    #[test]
    fn expired_budget_still_observes_reply_and_disposes_returned_owners_once() {
        let _guard = kernel::serial();
        kernel::reset();
        kernel::update(|s| s.ipc_reply.push_back((0, 7, 41, 10)));
        let connection = unsafe { Connection::from_raw(8) }.unwrap();
        let reply = connection
            .call(1, 0)
            .unwrap()
            .observe_until(&Deadline::at(0, 1000, 0))
            .cancel_on_failure()
            .unwrap();
        assert_eq!(reply.result, 7);
        assert!(kernel::events().contains(&Event::IpcWaitTimeout(30, 0)));
        drop(reply);
        drop(connection);
        assert_eq!(kernel::events().iter().filter(|e| **e == Event::MemoryClose(10)).count(), 1);
        assert_eq!(kernel::events().iter().filter(|e| **e == Event::IpcClose(41)).count(), 1);
    }
}
