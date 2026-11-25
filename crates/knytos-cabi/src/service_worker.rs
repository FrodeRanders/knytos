//! Scoped scalar calls on a native worker, outside the VM scheduler.
use knytos_app::{
    Application,
    grant,
    grant_client,
};
use knytos_rt::{
    Context,
    abi::ipc_status,
    call::{
        Admission,
        AdmissionError,
        ObservationError,
    },
    deadline::Deadline,
    owned::{
        Connection,
        IpcError,
        PendingCall,
    },
};

use crate::{
    DEADLINE,
    INVALID,
    OK,
    SHUTDOWN,
    UNAVAILABLE,
    WAIT_REJECTED,
    workers,
};

const UNAUTHORIZED: i32 = 8;
const UNEXPECTED_REPLY: i32 = 9;
const CANCELLED: i32 = 10;
const REQUEST_BUDGET_MS: u64 = 1_000;

struct InFlightScalar {
    pending: PendingCall<'static>,
    _connection: Connection,
}

impl InFlightScalar {
    fn observe(
        mut self,
        worker: workers::Participant,
        deadline: &Deadline,
        cancelled: extern "C" fn() -> bool,
    ) -> Result<i64, i32> {
        // The worker holds no VM or runtime metadata guard. Early returns
        // close the exact pending owner before releasing the connection.
        self.pending.bind_cq(worker.queue()).map_err(|error| match error {
            IpcError::Status(ipc_status::RESOURCE_LIMIT) => WAIT_REJECTED,
            _ => UNAVAILABLE,
        })?;
        loop {
            if cancelled() {
                return Err(CANCELLED);
            }
            if let Some(reply) = self.pending.poll().map_err(|_| UNAVAILABLE)? {
                if reply.connection.is_some() || reply.memory.is_some() {
                    return Err(UNEXPECTED_REPLY);
                }
                return Ok(reply.result);
            }
            let remaining = deadline.remaining_ms();
            if remaining == 0 {
                return Err(DEADLINE);
            }
            // Both the native reply and cancellation signal this CQ. Either
            // wake, including expiry, requires a fresh check of both sources.
            match workers::park(worker, remaining) {
                OK | DEADLINE => {}
                WAIT_REJECTED => return Err(WAIT_REJECTED),
                SHUTDOWN => return Err(SHUTDOWN),
                _ => return Err(UNAVAILABLE),
            }
        }
    }
}

fn admission_status(error: AdmissionError) -> i32 {
    match error {
        AdmissionError::Deadline => DEADLINE,
        AdmissionError::Shutdown(_) => SHUTDOWN,
        _ => UNAVAILABLE,
    }
}

fn observation_status(error: ObservationError) -> i32 {
    match error {
        ObservationError::Deadline => DEADLINE,
        ObservationError::WaitRejected(_) => WAIT_REJECTED,
        ObservationError::Ipc(_) => UNAVAILABLE,
    }
}

fn grant_status(error: grant_client::Error) -> i32 {
    match error {
        grant_client::Error::Service(grant::ERR_UNAUTHORIZED) => UNAUTHORIZED,
        grant_client::Error::Admission(error) => admission_status(error),
        grant_client::Error::Observation(error) => observation_status(error),
        grant_client::Error::Shutdown(_) => SHUTDOWN,
        _ => UNAVAILABLE,
    }
}

fn invoke(
    worker: workers::Participant,
    service: &[u8],
    opcode: u32,
    argument: i64,
    cancelled: extern "C" fn() -> bool,
) -> Result<i64, i32> {
    if cancelled() {
        return Err(CANCELLED);
    }
    let context = Context;
    let app = Application::from_context(&context).map_err(|_| UNAVAILABLE)?;
    let deadline = Deadline::after(REQUEST_BUDGET_MS);
    let connection = app
        .acquire(service, knytos_launch::deployment::RIGHT_CALL, &deadline)
        .map_err(grant_status)?;
    if cancelled() {
        return Err(CANCELLED);
    }
    let pending = Admission::with_lifecycle(&deadline, context.lifecycle())
        .call(connection.as_ref(), opcode, argument as u64)
        .map_err(admission_status)?;
    InFlightScalar {
        pending,
        _connection: connection,
    }
    .observe(worker, &deadline, cancelled)
}

/// # Safety
/// The name and output are separate live C buffers for the call's duration.
/// The callback is nonblocking and leaves those buffers live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_worker_call_scalar(
    name: *const u8,
    length: usize,
    opcode: u32,
    argument: i64,
    cancelled: Option<extern "C" fn() -> bool>,
    output: *mut i64,
) -> i32 {
    let Some(cancelled) = cancelled else {
        return INVALID;
    };
    let Some(worker) = workers::participant().filter(|worker| worker.slot != 0) else {
        return INVALID;
    };
    if name.is_null()
        || length == 0
        || length > knytos_launch::deployment::MAX_SERVICE_NAME_LEN
        || output.is_null()
        || !(output as usize).is_multiple_of(core::mem::align_of::<i64>())
    {
        return INVALID;
    }
    let service = unsafe { core::slice::from_raw_parts(name, length) };
    if service.iter().any(|byte| !(0x21..=0x7e).contains(byte)) {
        return INVALID;
    }
    match invoke(worker, service, opcode, argument, cancelled) {
        Ok(result) => {
            unsafe { output.write(result) };
            OK
        }
        Err(status) => status,
    }
}
