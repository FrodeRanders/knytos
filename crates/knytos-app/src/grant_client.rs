//! Bounded, owned application client for the capability-grant controller.
use knytos_rt::{
    Context,
    ShutdownRequest,
    abi::IpcRights,
    call::{
        Admission,
        AdmissionError,
        InterruptibleWait,
        ObservationError,
    },
    owned::{
        Connection,
        ConnectionRef,
        Endpoint,
        LaunchMemoryRef,
        MemoryError,
        OwnedPayload,
        PayloadError,
    },
};

use crate::{
    deadline::Deadline,
    grant,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    Memory(MemoryError),
    Payload(PayloadError),
    Admission(AdmissionError),
    Observation(ObservationError),
    InvalidRequest,
    Service(i64),
    Shutdown(ShutdownRequest),
}

pub fn acquire(
    controller: ConnectionRef<'_>,
    descriptor: &LaunchMemoryRef<'_>,
    service: &[u8],
    rights: u16,
) -> Result<Connection, Error> {
    acquire_until(controller, descriptor, service, rights, &Deadline::after(5_000))
}

pub fn acquire_until(
    controller: ConnectionRef<'_>,
    descriptor: &LaunchMemoryRef<'_>,
    service: &[u8],
    rights: u16,
    deadline: &Deadline,
) -> Result<Connection, Error> {
    acquire_inner(None, controller, descriptor, service, rights, deadline, None)
}

/// Acquisition alone retries a confirmed remote unavailable reply, never an
/// accepted-call timeout or rejected wait. Publication readiness has no
/// subscription yet: availability backoff is bounded to 100 ms, with lifecycle
/// rechecked between attempts. Every attempt shares the caller's deadline.
pub fn acquire_with_context(
    context: &Context,
    controller: ConnectionRef<'_>,
    descriptor: &LaunchMemoryRef<'_>,
    service: &[u8],
    rights: u16,
    deadline: &Deadline,
) -> Result<Connection, Error> {
    acquire_inner(Some(context), controller, descriptor, service, rights, deadline, None)
}

pub(crate) fn acquire_interruptible(
    context: &Context,
    controller: ConnectionRef<'_>,
    descriptor: &LaunchMemoryRef<'_>,
    service: &[u8],
    rights: u16,
    deadline: &Deadline,
    wait: &InterruptibleWait<'_>,
) -> Result<Connection, Error> {
    acquire_inner(Some(context), controller, descriptor, service, rights, deadline, Some(wait))
}

fn request(
    descriptor: &LaunchMemoryRef<'_>,
    service: &[u8],
    rights: u16,
) -> Result<OwnedPayload, Error> {
    let mapping = descriptor.map_read_only().map_err(Error::Memory)?;
    OwnedPayload::encode(4096, 4096, |output| {
        grant::encode_request(service, rights, mapping.as_slice(), output)
    })
    .map_err(Error::Payload)
}

fn acquire_inner(
    context: Option<&Context>,
    controller: ConnectionRef<'_>,
    descriptor: &LaunchMemoryRef<'_>,
    service: &[u8],
    rights: u16,
    deadline: &Deadline,
    wait: Option<&InterruptibleWait<'_>>,
) -> Result<Connection, Error> {
    loop {
        if let Some(wait) = wait {
            wait.check().map_err(ObservationError::Interrupted).map_err(Error::Observation)?;
        }
        if let Some(shutdown) = context.and_then(|c| c.lifecycle().shutdown_requested()) {
            return Err(Error::Shutdown(shutdown));
        }
        if deadline.expired() {
            return Err(Error::Admission(AdmissionError::Deadline));
        }
        let (memory, len) = request(descriptor, service, rights)?.into_parts();
        let admission = match context {
            Some(context) => Admission::with_lifecycle(deadline, context.lifecycle()),
            None => Admission::new(deadline),
        };
        let admission = match wait {
            Some(wait) => admission.interruptible(wait),
            None => admission,
        };
        let pending = admission
            .call_move(controller, grant::OP_ACQUIRE, len as u64, memory)
            .map_err(|(_, error)| Error::Admission(error))?;
        let reply = match wait {
            Some(wait) => wait.observe(pending, deadline),
            None => pending.observe_until(deadline).cancel_on_failure(),
        }
        .map_err(Error::Observation)?;
        if reply.memory.is_some() {
            return Err(Error::InvalidRequest);
        }
        if reply.result == grant::ERR_UNAVAILABLE
            && reply.connection.is_none()
            && !deadline.expired()
        {
            // There is no publication-readiness producer contract to subscribe to.
            match wait {
                Some(wait) => wait
                    .pause(deadline, 100)
                    .map_err(ObservationError::Interrupted)
                    .map_err(Error::Observation)?,
                None => crate::sleep_ms(deadline.remaining_ms().min(100)),
            }
            continue;
        }
        if reply.result < 0 {
            return Err(Error::Service(reply.result));
        }
        return reply.connection.ok_or(Error::InvalidRequest);
    }
}

pub fn publish(
    controller: ConnectionRef<'_>,
    descriptor: &LaunchMemoryRef<'_>,
    service: &[u8],
    endpoint: &Endpoint,
) -> Result<i64, Error> {
    publish_until(controller, descriptor, service, endpoint, &Deadline::after(5_000))
}

/// Publish once. An accepted timeout is uncertain and is never replayed.
pub fn publish_until(
    controller: ConnectionRef<'_>,
    descriptor: &LaunchMemoryRef<'_>,
    service: &[u8],
    endpoint: &Endpoint,
    deadline: &Deadline,
) -> Result<i64, Error> {
    if deadline.expired() {
        return Err(Error::Admission(AdmissionError::Deadline));
    }
    let payload = request(descriptor, service, knytos_launch::deployment::RIGHT_PUBLISH)?;
    let pending = controller
        .call_connection_copy(
            grant::OP_PUBLISH,
            payload.len() as u64,
            endpoint,
            IpcRights::SEND | IpcRights::CALL | IpcRights::MINT_CONNECTION,
            payload.memory(),
        )
        .map_err(|e| Error::Admission(AdmissionError::Rejected(e)))?;
    let reply = pending.observe_until(deadline).cancel_on_failure().map_err(Error::Observation)?;
    if reply.connection.is_some() || reply.memory.is_some() {
        return Err(Error::InvalidRequest);
    }
    if reply.result < 1 {
        Err(Error::Service(reply.result))
    } else {
        Ok(reply.result)
    }
}
