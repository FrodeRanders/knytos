//! Scoped calls on a native worker, outside the VM scheduler.
use core::borrow::Borrow;

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
        CallResult,
        Connection,
        IpcError,
        OwnedPayload,
        PendingCall,
    },
};

use crate::{
    BUSY,
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
const BYTE_REPLY: i32 = 12;
const REQUEST_BUDGET_MS: u64 = 1_000;
const MAX_PAYLOAD_BYTES: usize = 4096;

const STOP: u32 = 0;
const NAMED_CALL: u32 = 1;
const LOOKUP: u32 = 2;
const BOUND_CALL: u32 = 3;
const RELEASE: u32 = 4;

#[repr(C)]
pub struct ServiceCommand {
    name: *const u8,
    length: usize,
    argument: i64,
    opcode: u32,
    bytes: *mut [u8; MAX_PAYLOAD_BYTES],
    bytes_len: usize,
}

impl ServiceCommand {
    /// The scheduler's name storage stays live until result publication.
    unsafe fn name(&self) -> Result<&[u8], i32> {
        if self.name.is_null()
            || self.length == 0
            || self.length > knytos_launch::deployment::MAX_SERVICE_NAME_LEN
        {
            return Err(INVALID);
        }
        let name = unsafe { core::slice::from_raw_parts(self.name, self.length) };
        if name.iter().any(|byte| !(0x21..=0x7e).contains(byte)) {
            return Err(INVALID);
        }
        Ok(name)
    }
}

struct InFlightCall<C> {
    pending: PendingCall<'static>,
    _connection: C,
}

impl<C> InFlightCall<C> {
    fn observe(
        mut self,
        worker: workers::Participant,
        deadline: &Deadline,
        cancelled: extern "C" fn() -> bool,
    ) -> Result<CallResult, i32> {
        // The worker holds no VM or runtime metadata guard. Early returns
        // close the exact pending owner within the connection owner's scope.
        self.pending.bind_cq(worker.queue()).map_err(|error| match error {
            IpcError::Status(ipc_status::RESOURCE_LIMIT) => WAIT_REJECTED,
            _ => UNAVAILABLE,
        })?;
        loop {
            if cancelled() {
                return Err(CANCELLED);
            }
            if let Some(reply) = self.pending.poll().map_err(|_| UNAVAILABLE)? {
                return Ok(reply);
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

fn acquire(context: &Context, service: &[u8], deadline: &Deadline) -> Result<Connection, i32> {
    let app = Application::from_context(context).map_err(|_| UNAVAILABLE)?;
    app.acquire(service, knytos_launch::deployment::RIGHT_CALL, deadline).map_err(grant_status)
}

enum Reply {
    Scalar(i64),
    Bytes(usize),
}

fn copy_reply(
    reply: CallResult,
    bytes: Option<&mut [u8; MAX_PAYLOAD_BYTES]>,
) -> Result<Reply, i32> {
    if reply.connection.is_some() {
        return Err(UNEXPECTED_REPLY);
    }
    let Some(memory) = reply.memory else {
        return Ok(Reply::Scalar(reply.result));
    };
    let bytes = bytes.ok_or(UNEXPECTED_REPLY)?;
    let length = usize::try_from(reply.result).map_err(|_| UNEXPECTED_REPLY)?;
    // Mapping attaches the whole object. Bound its backing as well as the
    // initialized prefix reported by the service before mapping anything.
    if memory.len() > bytes.len() || length > memory.len() {
        return Err(UNEXPECTED_REPLY);
    }
    if length != 0 {
        let mapping = memory.map_read_only().map_err(|(_, _)| UNAVAILABLE)?;
        bytes[..length].copy_from_slice(&mapping.as_slice()[..length]);
        drop(mapping.unmap().map_err(|(_, _)| UNAVAILABLE)?);
    }
    Ok(Reply::Bytes(length))
}

fn invoke<C: Borrow<Connection>>(
    worker: workers::Participant,
    connection: C,
    command: &ServiceCommand,
    bytes: Option<&mut [u8; MAX_PAYLOAD_BYTES]>,
    deadline: &Deadline,
    cancelled: extern "C" fn() -> bool,
) -> Result<Reply, i32> {
    if cancelled() {
        return Err(CANCELLED);
    }
    let admission = Admission::with_lifecycle(deadline, Context.lifecycle());
    let target = connection.borrow().as_ref();
    let opcode = command.opcode;
    let argument = command.argument as u64;
    let pending = if let Some(bytes) = bytes.as_deref() {
        let input = bytes.get(..command.bytes_len).ok_or(INVALID)?;
        let payload = OwnedPayload::copy_from(input, MAX_PAYLOAD_BYTES).map_err(|_| UNAVAILABLE)?;
        if cancelled() {
            return Err(CANCELLED);
        }
        let (memory, _) = payload.into_parts();
        admission.call_move(target, opcode, argument, memory).map_err(|(_, error)| error)
    } else {
        admission.call(target, opcode, argument)
    }
    .map_err(admission_status)?;
    let reply = InFlightCall {
        pending,
        _connection: connection,
    }
    .observe(worker, deadline, cancelled)?;
    copy_reply(reply, bytes)
}

/// # Safety
/// The callbacks run on this worker. For lookup and named call commands, the
/// producer retains the name buffer until the completion callback. Any nonnull
/// byte buffer is exclusively accessible to the worker until completion. The
/// producer writes the command before returning any operation other than stop
/// or release.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_service_worker_run(
    next: unsafe extern "C" fn(*mut ServiceCommand) -> u32,
    complete: extern "C" fn(u32, i32, i64),
    cancelled: extern "C" fn() -> bool,
) {
    let Some(worker) = workers::participant().filter(|worker| worker.slot != 0) else {
        knytos_rt::domain_abort();
    };
    let mut session = None;
    loop {
        let mut command = core::mem::MaybeUninit::uninit();
        let kind = unsafe { next(command.as_mut_ptr()) };
        match kind {
            STOP => return,
            RELEASE => {
                // Only this thread can borrow the session for IPC. Its exact
                // pending call settles before the worker can reach this arm.
                drop(session.take());
                complete(kind, OK, 0);
                continue;
            }
            _ => {}
        }
        let result = if cancelled() {
            Err(CANCELLED)
        } else {
            let command = unsafe { command.assume_init() };
            let bytes = unsafe { command.bytes.as_mut() };
            let deadline = Deadline::after(REQUEST_BUDGET_MS);
            match kind {
                NAMED_CALL => unsafe { command.name() }
                    .and_then(|name| acquire(&Context, name, &deadline))
                    .and_then(|connection| {
                        invoke(worker, connection, &command, bytes, &deadline, cancelled)
                    }),
                LOOKUP if session.is_none() => unsafe { command.name() }
                    .and_then(|name| acquire(&Context, name, &deadline))
                    .map(|connection| {
                        session = Some(connection);
                        Reply::Scalar(0)
                    }),
                LOOKUP => Err(BUSY),
                BOUND_CALL => session.as_ref().ok_or(UNAVAILABLE).and_then(|connection| {
                    invoke(worker, connection, &command, bytes, &deadline, cancelled)
                }),
                _ => Err(INVALID),
            }
        };
        match result {
            Ok(Reply::Scalar(result)) => complete(kind, OK, result),
            Ok(Reply::Bytes(length)) => complete(kind, BYTE_REPLY, length as i64),
            Err(status) => complete(kind, status, 0),
        }
    }
}
