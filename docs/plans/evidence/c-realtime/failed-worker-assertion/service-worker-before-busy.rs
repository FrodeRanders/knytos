//! Scoped calls on a native worker, outside the VM scheduler.
use core::borrow::Borrow;

use knytos_app::{
    Application,
    grant,
    grant_client,
};
use knytos_rt::{
    Context,
    call::{
        Admission,
        AdmissionError,
        InterruptError,
        InterruptibleWait,
        ObservationError,
    },
    cq::WaitError,
    deadline::Deadline,
    owned::{
        CallResult,
        Completion,
        Connection,
        OwnedPayload,
        PendingCall,
    },
};
use lock_api::Mutex;

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
const SUBSCRIBE: u32 = 5;
const UNSUBSCRIBE: u32 = 6;

// The close watch has its own completion owner. Registration and destruction
// run on the worker. The scheduler observes it at the CQ-zero checkpoint.
// The lock covers only custody transfer, never a syscall or a destructor.
static CLOSE_WATCH: Mutex<knytos_rt::CountingMutex, Option<Completion>> = Mutex::new(None);

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

fn interrupt_status(error: InterruptError) -> i32 {
    match error {
        InterruptError::Cancelled => CANCELLED,
        InterruptError::Wait(WaitError::AdmissionRejected) => WAIT_REJECTED,
        InterruptError::Wait(_) => UNAVAILABLE,
    }
}

fn admission_status(error: AdmissionError) -> i32 {
    match error {
        AdmissionError::Deadline => DEADLINE,
        AdmissionError::Shutdown(_) => SHUTDOWN,
        AdmissionError::Interrupted(error) => interrupt_status(error),
        _ => UNAVAILABLE,
    }
}

fn observation_status(error: ObservationError) -> i32 {
    match error {
        ObservationError::Deadline => DEADLINE,
        ObservationError::WaitRejected(_) => WAIT_REJECTED,
        ObservationError::Ipc(_) => UNAVAILABLE,
        ObservationError::Interrupted(error) => interrupt_status(error),
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

fn acquire(
    service: &[u8],
    deadline: &Deadline,
    wait: &InterruptibleWait<'_>,
) -> Result<Connection, i32> {
    let app = Application::from_context(&Context).map_err(|_| UNAVAILABLE)?;
    app.acquire_interruptible(service, knytos_launch::deployment::RIGHT_CALL, deadline, wait)
        .map_err(grant_status)
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

fn store_watch(watch: Completion) {
    let previous = CLOSE_WATCH.lock().replace(watch);
    if previous.is_some() {
        knytos_rt::domain_abort();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_service_watch_poll() -> i32 {
    let watch = CLOSE_WATCH.lock().take();
    let Some(mut watch) = watch else {
        return UNAVAILABLE;
    };
    match watch.poll() {
        Ok(Some(result)) if result == knytos_rt::abi::IPC_REPLY_ENDPOINT_CLOSED => OK,
        Ok(Some(_)) => UNAVAILABLE,
        Ok(None) => {
            store_watch(watch);
            BUSY
        }
        Err(_) => {
            // The scheduler will detach delivery and send the owner back to
            // the worker for cancellation if observation preserved it.
            store_watch(watch);
            UNAVAILABLE
        }
    }
}

fn invoke<C: Borrow<Connection>>(
    connection: C,
    command: &ServiceCommand,
    bytes: Option<&mut [u8; MAX_PAYLOAD_BYTES]>,
    deadline: &Deadline,
    wait: &InterruptibleWait<'_>,
) -> Result<Reply, i32> {
    wait.check().map_err(interrupt_status)?;
    let admission = Admission::with_lifecycle(deadline, Context.lifecycle()).interruptible(wait);
    let target = connection.borrow().as_ref();
    let opcode = command.opcode;
    let argument = command.argument as u64;
    let pending = if let Some(bytes) = bytes.as_deref() {
        let input = bytes.get(..command.bytes_len).ok_or(INVALID)?;
        let payload = OwnedPayload::copy_from(input, MAX_PAYLOAD_BYTES).map_err(|_| UNAVAILABLE)?;
        let (memory, _) = payload.into_parts();
        admission.call_move(target, opcode, argument, memory).map_err(|(_, error)| error)
    } else {
        admission.call(target, opcode, argument)
    }
    .map_err(admission_status)?;
    let operation = InFlightCall {
        pending,
        _connection: connection,
    };
    let reply = wait.observe(operation.pending, deadline).map_err(observation_status)?;
    copy_reply(reply, bytes)
}

/// A fresh authorized query, using the caller's private CQ and one deadline
/// across grant acquisition and observation. No connection or reply owner is
/// stored globally, and returned capabilities are disposed on every exit.
pub(crate) fn realtime_ns() -> Result<u64, i32> {
    use knytos_protocol_time as time;

    let participant = workers::participant().ok_or(INVALID)?;
    let interrupted = || false;
    let wait = InterruptibleWait::new(participant.queue(), &interrupted);
    let deadline = Deadline::after(REQUEST_BUDGET_MS);
    let connection = acquire(b"time", &deadline, &wait)?;
    let admission = Admission::with_lifecycle(&deadline, Context.lifecycle()).interruptible(&wait);
    let pending = admission.call(connection.as_ref(), time::OP_NOW, 0).map_err(admission_status)?;
    let operation = InFlightCall {
        pending,
        _connection: connection,
    };
    let reply = wait.observe(operation.pending, &deadline).map_err(observation_status)?;
    if reply.connection.is_some() {
        return Err(UNEXPECTED_REPLY);
    }
    if reply.result < 0 && reply.memory.is_none() {
        return Err(UNAVAILABLE);
    }
    if reply.result != time::SNAPSHOT_LEN as i64 {
        return Err(UNEXPECTED_REPLY);
    }
    let memory = reply.memory.ok_or(UNEXPECTED_REPLY)?;
    if !(time::SNAPSHOT_LEN..=MAX_PAYLOAD_BYTES).contains(&memory.len()) {
        return Err(UNEXPECTED_REPLY);
    }
    let mapping = memory.map_read_only().map_err(|(_, _)| UNAVAILABLE)?;
    let result = crate::realtime::decode(&mapping.as_slice()[..time::SNAPSHOT_LEN]);
    drop(mapping.unmap().map_err(|(_, _)| UNAVAILABLE)?);
    result
}

/// # Safety
/// The callbacks run on this worker. For lookup and named call commands, the
/// producer retains the name buffer until the completion callback. Any nonnull
/// byte buffer is exclusively accessible to the worker until completion. The
/// producer writes the command for calls, lookup and subscription. Stop,
/// release and unsubscribe require no initialized command.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_service_worker_run(
    next: unsafe extern "C" fn(*mut ServiceCommand) -> u32,
    complete: extern "C" fn(u32, i32, i64),
    cancelled: extern "C" fn() -> bool,
) {
    let Some(worker) = workers::participant().filter(|worker| worker.slot != 0) else {
        knytos_rt::domain_abort();
    };
    let interrupted = || cancelled();
    let wait = InterruptibleWait::new(worker.queue(), &interrupted);
    let mut session = None;
    loop {
        let mut command = core::mem::MaybeUninit::uninit();
        let kind = unsafe { next(command.as_mut_ptr()) };
        match kind {
            STOP | RELEASE | UNSUBSCRIBE => {
                let watch = CLOSE_WATCH.lock().take();
                drop(watch);
                if kind == STOP {
                    return;
                }
                if kind == RELEASE {
                    // The worker reaches here after settling its pending call.
                    drop(session.take());
                }
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
                    .and_then(|name| acquire(name, &deadline, &wait))
                    .and_then(|connection| invoke(connection, &command, bytes, &deadline, &wait)),
                LOOKUP if session.is_none() => unsafe { command.name() }
                    .and_then(|name| acquire(name, &deadline, &wait))
                    .map(|connection| {
                        session = Some(connection);
                        Reply::Scalar(0)
                    }),
                LOOKUP => Err(BUSY),
                SUBSCRIBE => session.as_ref().ok_or(UNAVAILABLE).and_then(|connection| {
                    let watch = connection.watch_closed().map_err(|_| WAIT_REJECTED)?;
                    store_watch(watch);
                    Ok(Reply::Scalar(0))
                }),
                BOUND_CALL => session
                    .as_ref()
                    .ok_or(UNAVAILABLE)
                    .and_then(|connection| invoke(connection, &command, bytes, &deadline, &wait)),
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
