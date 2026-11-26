//! Manual-reset event for one registered native waiter.
use knytos_rt::{
    CountingMutex,
    deadline::Deadline,
};
use lock_api::Mutex;

use super::publish;
use crate::{
    ALLOCATIONS,
    BUSY,
    DEADLINE,
    INVALID,
    OK,
    workers::{
        Participant,
        park_event,
        participant,
        wake_waiters,
    },
};

struct State {
    set: bool,
    waiter: Option<Participant>,
}

pub struct Event(Mutex<CountingMutex, State>);

impl Event {
    fn wait(&self, caller: Participant, deadline: Option<Deadline>) -> i32 {
        loop {
            {
                let mut state = self.0.lock();
                if state.waiter.is_some_and(|waiter| waiter != caller) {
                    return BUSY;
                }
                if state.set {
                    state.waiter = None;
                    return OK;
                }
                if deadline.as_ref().is_some_and(Deadline::expired) {
                    state.waiter = None;
                    return DEADLINE;
                }
                // Registration and readiness observation share the producer
                // guard. A later set publishes before posting the CQ hint.
                state.waiter = Some(caller);
            }
            let status = park_event(caller, deadline.as_ref().map(Deadline::remaining_ms));
            if status != OK && status != DEADLINE {
                self.0.lock().waiter = None;
                return status;
            }
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_event_create() -> *mut Event {
    publish(Event(Mutex::new(State {
        set: false,
        waiter: None,
    })))
}

/// Safety: the object is live and external lifetime synchronization excludes
/// new calls during destruction. An existing waiter makes destruction busy.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_event_destroy(event: *mut Event) -> i32 {
    let Some(object) = (unsafe { event.as_ref() }) else {
        return INVALID;
    };
    if object.0.lock().waiter.is_some() {
        return BUSY;
    }
    unsafe { ALLOCATIONS.release(event.cast()) };
    OK
}

/// Safety: the object remains live throughout this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_event_set(event: *const Event) -> i32 {
    let (Some(object), Some(_)) = (unsafe { event.as_ref() }, participant()) else {
        return INVALID;
    };
    let waiter = {
        let mut state = object.0.lock();
        state.set = true;
        state.waiter
    };
    if let Some(waiter) = waiter {
        wake_waiters(waiter.bit());
    }
    OK
}

/// Safety: the object remains live throughout this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_event_reset(event: *const Event) -> i32 {
    let (Some(object), Some(_)) = (unsafe { event.as_ref() }, participant()) else {
        return INVALID;
    };
    object.0.lock().set = false;
    OK
}

/// Safety: retain the event through this wait. For an indefinite wait the
/// caller must arrange a producer and a terminal shutdown path.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_event_wait_on(event: *const Event, timeout_ms: i64) -> i32 {
    let (Some(object), Some(caller)) = (unsafe { event.as_ref() }, participant()) else {
        return INVALID;
    };
    let deadline = match timeout_ms {
        -1 => None,
        0.. => Some(Deadline::after(timeout_ms as u64)),
        _ => return INVALID,
    };
    object.wait(caller, deadline)
}
