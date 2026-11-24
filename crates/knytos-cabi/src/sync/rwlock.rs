//! Read/write locks share the worker roster's private, generation-safe waits.
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
    OVERFLOW,
    workers::{
        PARTICIPANT_COUNT,
        Participant,
        park,
        participant,
        wake_waiters,
    },
};

#[derive(Clone, Copy)]
enum Access {
    Read,
    Write,
}

struct Reader {
    owner: Participant,
    depth: u32,
}

#[derive(Default)]
struct State {
    readers: [Option<Reader>; PARTICIPANT_COUNT],
    writer: Option<Participant>,
    waiting_readers: u8,
    waiting_writers: u8,
}

impl State {
    fn waiters(&mut self, access: Access) -> &mut u8 {
        match access {
            Access::Read => &mut self.waiting_readers,
            Access::Write => &mut self.waiting_writers,
        }
    }

    fn try_acquire(&mut self, caller: Participant, access: Access) -> i32 {
        if let Some(writer) = self.writer {
            return if writer == caller {
                INVALID
            } else {
                BUSY
            };
        }
        let slot = caller.slot as usize;
        if let Some(reader) = &mut self.readers[slot] {
            if reader.owner != caller || matches!(access, Access::Write) {
                return INVALID;
            }
            let Some(depth) = reader.depth.checked_add(1) else {
                return OVERFLOW;
            };
            reader.depth = depth;
            return OK;
        }
        match access {
            Access::Read if self.waiting_writers == 0 => {
                self.readers[slot] = Some(Reader {
                    owner: caller,
                    depth: 1,
                });
                OK
            }
            Access::Write if self.readers.iter().all(Option::is_none) => {
                self.writer = Some(caller);
                OK
            }
            _ => BUSY,
        }
    }

    // Called while publishing unlock or a waiter departure. The returned
    // mask is only a wake hint. Recipients still observe the exact lock state.
    fn eligible_waiters(&self) -> u8 {
        if self.writer.is_some() {
            0
        } else if self.waiting_writers == 0 {
            self.waiting_readers
        } else if self.readers.iter().all(Option::is_none) {
            self.waiting_writers
        } else {
            0
        }
    }

    fn withdraw(&mut self, caller: Participant, access: Access) -> u8 {
        let waiting = self.waiters(access);
        if *waiting & caller.bit() == 0 {
            return 0;
        }
        *waiting &= !caller.bit();
        self.eligible_waiters()
    }
}

pub struct NativeRwLock(Mutex<CountingMutex, State>);

impl NativeRwLock {
    fn acquire(&self, caller: Participant, access: Access, deadline: &Deadline) -> i32 {
        loop {
            let remaining = deadline.remaining_ms();
            let (status, wake) = {
                let mut state = self.0.lock();
                let status = state.try_acquire(caller, access);
                if status == BUSY && remaining != 0 {
                    // Register against the same state read by unlock. A
                    // subsequent release leaves a retained CQ generation.
                    *state.waiters(access) |= caller.bit();
                    (BUSY, 0)
                } else {
                    let wake = state.withdraw(caller, access);
                    (
                        if status == BUSY {
                            DEADLINE
                        } else {
                            status
                        },
                        wake,
                    )
                }
            };
            wake_waiters(wake);
            if status != BUSY {
                return status;
            }
            let status = park(caller, deadline.remaining_ms());
            if status != OK && status != DEADLINE {
                let wake = self.0.lock().withdraw(caller, access);
                wake_waiters(wake);
                return status;
            }
        }
    }

    fn unlock(&self, caller: Participant) -> i32 {
        let wake = {
            let mut state = self.0.lock();
            if state.writer == Some(caller) {
                state.writer = None;
            } else {
                let slot = &mut state.readers[caller.slot as usize];
                let Some(reader) = slot.as_mut().filter(|reader| reader.owner == caller) else {
                    return INVALID;
                };
                reader.depth -= 1;
                if reader.depth != 0 {
                    return OK;
                }
                *slot = None;
            }
            state.eligible_waiters()
        };
        wake_waiters(wake);
        OK
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_rwlock_create() -> *mut NativeRwLock {
    publish(NativeRwLock(Mutex::new(State::default())))
}

/// Safety: unless null, this is a live object with externally exclusive
/// access to its lifetime. No concurrent or subsequent call may use it
/// after successful destruction.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_rwlock_destroy(lock: *mut NativeRwLock) -> i32 {
    let Some(object) = (unsafe { lock.as_ref() }) else {
        return INVALID;
    };
    {
        let state = object.0.lock();
        if state.writer.is_some()
            || state.readers.iter().any(Option::is_some)
            || state.waiting_readers != 0
            || state.waiting_writers != 0
        {
            return BUSY;
        }
    }
    unsafe { ALLOCATIONS.release(lock.cast()) };
    OK
}

/// Safety: unless null, the object remains live for the entire call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_rwlock_try_read(lock: *const NativeRwLock) -> i32 {
    let (Some(lock), Some(caller)) = (unsafe { lock.as_ref() }, participant()) else {
        return INVALID;
    };
    lock.0.lock().try_acquire(caller, Access::Read)
}

/// Safety: unless null, the object remains live through the entire wait.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_rwlock_read(lock: *const NativeRwLock, timeout_ms: u32) -> i32 {
    let (Some(lock), Some(caller)) = (unsafe { lock.as_ref() }, participant()) else {
        return INVALID;
    };
    lock.acquire(caller, Access::Read, &Deadline::after(u64::from(timeout_ms)))
}

/// Safety: unless null, the object remains live through the entire wait.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_rwlock_write(lock: *const NativeRwLock, timeout_ms: u32) -> i32 {
    let (Some(lock), Some(caller)) = (unsafe { lock.as_ref() }, participant()) else {
        return INVALID;
    };
    lock.acquire(caller, Access::Write, &Deadline::after(u64::from(timeout_ms)))
}

/// Safety: unless null, the object remains live for the entire call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_rwlock_unlock(lock: *const NativeRwLock) -> i32 {
    let (Some(lock), Some(caller)) = (unsafe { lock.as_ref() }, participant()) else {
        return INVALID;
    };
    lock.unlock(caller)
}
