//! Bounded synchronization for the opt-in C worker roster. Internal locks
//! serialize only plain metadata. Kernel waits and wakes run outside them.
#[cfg(feature = "worker-libc")]
pub(crate) mod libc;
mod rwlock;

use knytos_rt::{
    CountingMutex,
    deadline::Deadline,
};
use lock_api::Mutex;

use crate::{
    ALLOCATIONS,
    BUSY,
    DEADLINE,
    INVALID,
    OK,
    OVERFLOW,
    workers::{
        Participant,
        park,
        park_event,
        participant,
        wake_waiters,
    },
};

struct LockState {
    owner: Option<Participant>,
    depth: u32,
    waiting: u8,
}

pub struct NativeMutex(Mutex<CountingMutex, LockState>);

impl NativeMutex {
    const fn new() -> Self {
        Self(Mutex::new(LockState {
            owner: None,
            depth: 0,
            waiting: 0,
        }))
    }

    fn try_lock(&self, caller: Participant) -> i32 {
        let mut state = self.0.lock();
        match state.owner {
            None => {
                state.owner = Some(caller);
                state.depth = 1;
                OK
            }
            Some(owner) if owner == caller => INVALID,
            Some(_) => BUSY,
        }
    }

    fn lock(&self, caller: Participant, deadline: &Deadline) -> i32 {
        self.lock_inner(caller, deadline, false)
    }

    fn lock_inner(&self, caller: Participant, deadline: &Deadline, recursive: bool) -> i32 {
        let bit = caller.bit();
        loop {
            let remaining = deadline.remaining_ms();
            {
                let mut state = self.0.lock();
                match state.owner {
                    None => {
                        state.waiting &= !bit;
                        state.owner = Some(caller);
                        state.depth = 1;
                        return OK;
                    }
                    Some(owner) if owner == caller && recursive => {
                        let Some(depth) = state.depth.checked_add(1) else {
                            return OVERFLOW;
                        };
                        state.depth = depth;
                        return OK;
                    }
                    Some(owner) if owner == caller => return INVALID,
                    Some(_) if remaining == 0 => {
                        state.waiting &= !bit;
                        return DEADLINE;
                    }
                    // Observe held and register on the same state under the
                    // same guard. Later unlocks must post a retained CQ wake.
                    Some(_) => state.waiting |= bit,
                }
            }
            let status = park(caller, deadline.remaining_ms());
            if status != OK && status != DEADLINE {
                self.0.lock().waiting &= !bit;
                return status;
            }
        }
    }

    fn unlock(&self, caller: Participant) -> i32 {
        let waiting = {
            let mut state = self.0.lock();
            if state.owner != Some(caller) {
                return INVALID;
            }
            state.depth -= 1;
            if state.depth != 0 {
                return OK;
            }
            state.owner = None;
            state.waiting
        };
        wake_waiters(waiting);
        OK
    }
}

#[derive(Default)]
struct ConditionState {
    pending: u8,
    signaled: u8,
}

pub struct Condition(Mutex<CountingMutex, ConditionState>);

impl Condition {
    fn wait(
        &self,
        mutex: &NativeMutex,
        caller: Participant,
        deadline: Option<&Deadline>,
        reacquire_ms: u32,
    ) -> i32 {
        let bit = caller.bit();
        let waiting = {
            // Register while still owning the application mutex. This is
            // the only operation taking both guards, mutex then condition.
            let mut held = mutex.0.lock();
            if held.owner != Some(caller) || held.depth != 1 {
                return INVALID;
            }
            let mut state = self.0.lock();
            assert_eq!((state.pending | state.signaled) & bit, 0);
            state.pending |= bit;
            held.owner = None;
            held.depth = 0;
            held.waiting
        };
        wake_waiters(waiting);

        loop {
            let remaining = deadline.map(Deadline::remaining_ms);
            {
                let mut state = self.0.lock();
                if state.signaled & bit != 0 {
                    state.signaled &= !bit;
                    break;
                }
                if remaining == Some(0) {
                    state.pending &= !bit;
                    return DEADLINE;
                }
            }
            let status = park_event(caller, remaining);
            if status != OK && status != DEADLINE {
                let mut state = self.0.lock();
                state.pending &= !bit;
                state.signaled &= !bit;
                return status;
            }
        }
        // Timed callers share their original budget with reacquisition.
        // Event-only callers start a separate lock budget upon selection.
        let lock_deadline;
        let deadline = match deadline {
            Some(deadline) => deadline,
            None => {
                lock_deadline = Deadline::after(u64::from(reacquire_ms));
                &lock_deadline
            }
        };
        mutex.lock(caller, deadline)
    }

    fn signal(&self, all: bool) {
        let selected = {
            let mut state = self.0.lock();
            let selected = if all {
                state.pending
            } else {
                state.pending & state.pending.wrapping_neg()
            };
            state.pending &= !selected;
            state.signaled |= selected;
            selected
        };
        wake_waiters(selected);
    }
}

// Objects contain plain metadata only. The tracked C allocator owns their
// storage through explicit destruction or terminal C heap disposal.
fn publish<T>(value: T) -> *mut T {
    const { assert!(!core::mem::needs_drop::<T>()) };
    let pointer =
        ALLOCATIONS.allocate(core::mem::size_of::<T>(), core::mem::align_of::<T>()).cast::<T>();
    if !pointer.is_null() {
        unsafe { pointer.write(value) };
    }
    pointer
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_mutex_create() -> *mut NativeMutex {
    publish(NativeMutex::new())
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_condition_create() -> *mut Condition {
    publish(Condition(Mutex::new(ConditionState::default())))
}

/// Safety: the argument, unless null, is a live object owned by C. External
/// lifetime synchronization prevents any concurrent entry into its methods.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_mutex_destroy(mutex: *mut NativeMutex) -> i32 {
    let Some(object) = (unsafe { mutex.as_ref() }) else {
        return INVALID;
    };
    {
        let state = object.0.lock();
        if state.owner.is_some() || state.waiting != 0 {
            return BUSY;
        }
    }
    unsafe { ALLOCATIONS.release(mutex.cast()) };
    OK
}

/// Safety: the same exclusive lifetime contract as mutex destruction applies.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_condition_destroy(condition: *mut Condition) -> i32 {
    let Some(object) = (unsafe { condition.as_ref() }) else {
        return INVALID;
    };
    {
        let state = object.0.lock();
        if state.pending != 0 || state.signaled != 0 {
            return BUSY;
        }
    }
    unsafe { ALLOCATIONS.release(condition.cast()) };
    OK
}

/// Safety: unless null, the pointer names a live object throughout the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_mutex_try_lock(mutex: *const NativeMutex) -> i32 {
    let (Some(object), Some(caller)) = (unsafe { mutex.as_ref() }, participant()) else {
        return INVALID;
    };
    object.try_lock(caller)
}

/// Safety: the object stays live through the entire wait.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_mutex_lock(mutex: *const NativeMutex, timeout_ms: u32) -> i32 {
    let (Some(object), Some(caller)) = (unsafe { mutex.as_ref() }, participant()) else {
        return INVALID;
    };
    object.lock(caller, &Deadline::after(u64::from(timeout_ms)))
}

/// Safety: unless null, the pointer names a live object throughout the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_mutex_unlock(mutex: *const NativeMutex) -> i32 {
    let (Some(object), Some(caller)) = (unsafe { mutex.as_ref() }, participant()) else {
        return INVALID;
    };
    object.unlock(caller)
}

/// Safety: both objects stay live through the wait and mutex reacquisition.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_condition_wait(
    condition: *const Condition,
    mutex: *const NativeMutex,
    timeout_ms: u32,
) -> i32 {
    let (Some(condition), Some(mutex), Some(caller)) =
        (unsafe { condition.as_ref() }, unsafe { mutex.as_ref() }, participant())
    else {
        return INVALID;
    };
    condition.wait(mutex, caller, Some(&Deadline::after(u64::from(timeout_ms))), 0)
}

/// Safety: both objects stay live through the wait and mutex reacquisition.
/// The caller provides a shutdown path for this event-only private wait.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_condition_wait_event(
    condition: *const Condition,
    mutex: *const NativeMutex,
    reacquire_ms: u32,
) -> i32 {
    let (Some(condition), Some(mutex), Some(caller)) =
        (unsafe { condition.as_ref() }, unsafe { mutex.as_ref() }, participant())
    else {
        return INVALID;
    };
    condition.wait(mutex, caller, None, reacquire_ms)
}

/// Safety: unless null, the pointer names a live object throughout the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_condition_signal(condition: *const Condition, all: u32) -> i32 {
    let (Some(object), Some(_)) = (unsafe { condition.as_ref() }, participant()) else {
        return INVALID;
    };
    object.signal(all != 0);
    OK
}
