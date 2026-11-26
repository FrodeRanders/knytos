//! Borrowed C pointers indexed by a live key and registered participant.
use core::{
    ffi::c_void,
    ptr,
    sync::atomic::{
        AtomicPtr,
        Ordering,
    },
};

use lock_api::Mutex;

use crate::{
    INVALID,
    OK,
    OVERFLOW,
    UNAVAILABLE,
    workers::{
        PARTICIPANT_COUNT,
        participant,
    },
};

const MAX_KEYS: usize = 16;
const DESTRUCTOR_PASSES: usize = 4;
type Destructor = unsafe extern "C" fn(*mut c_void);

struct Key {
    identity: u64,
    destructor: Option<Destructor>,
    values: [AtomicPtr<c_void>; PARTICIPANT_COUNT],
}

impl Key {
    const fn new() -> Self {
        Self {
            identity: 0,
            destructor: None,
            values: [const { AtomicPtr::new(ptr::null_mut()) }; PARTICIPANT_COUNT],
        }
    }
}

struct Table {
    next_identity: u64,
    keys: [Key; MAX_KEYS],
}

static TABLE: Mutex<knytos_rt::CountingMutex, Table> = Mutex::new(Table {
    next_identity: 1,
    keys: [const { Key::new() }; MAX_KEYS],
});

/// # Safety
/// The output must point to writable, aligned u64 storage for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_tsd_key_create(output: *mut u64) -> i32 {
    unsafe { knytos_tsd_key_create_with_destructor(output, None) }
}

/// # Safety
/// The output has the same borrow as key_create. A supplied destructor must
/// remain callable through all users' worker exits. Non-null values must
/// remain valid for the destructor after the worker callback returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_tsd_key_create_with_destructor(
    output: *mut u64,
    destructor: Option<Destructor>,
) -> i32 {
    if output.is_null() || participant().is_none() {
        return INVALID;
    }
    let mut table = TABLE.lock();
    let Some(index) = table.keys.iter().position(|key| key.identity == 0) else {
        return UNAVAILABLE;
    };
    let identity = table.next_identity;
    let Some(next) = identity.checked_add(1) else {
        return OVERFLOW;
    };
    table.next_identity = next;
    table.keys[index].identity = identity;
    table.keys[index].destructor = destructor;
    // SAFETY: the ABI caller supplies the output borrow.
    unsafe { output.write(identity) };
    OK
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_tsd_key_delete(identity: u64) -> i32 {
    if identity == 0 || participant().is_none() {
        return INVALID;
    }
    let mut table = TABLE.lock();
    let Some(key) = table.keys.iter_mut().find(|key| key.identity == identity) else {
        return INVALID;
    };
    for value in &key.values {
        value.store(ptr::null_mut(), Ordering::Relaxed);
    }
    key.identity = 0;
    key.destructor = None;
    OK
}

/// The pointer is borrowed, never dereferenced or released by the runtime.
#[unsafe(no_mangle)]
pub extern "C" fn knytos_tsd_set(identity: u64, value: *mut c_void) -> i32 {
    let Some(caller) = participant() else {
        return INVALID;
    };
    let table = TABLE.lock();
    let Some(key) = table.keys.iter().find(|key| identity != 0 && key.identity == identity) else {
        return INVALID;
    };
    key.values[caller.slot as usize].store(value, Ordering::Relaxed);
    OK
}

/// Null represents an unset value, stale key or unregistered caller.
#[unsafe(no_mangle)]
pub extern "C" fn knytos_tsd_get(identity: u64) -> *mut c_void {
    let Some(caller) = participant() else {
        return ptr::null_mut();
    };
    let table = TABLE.lock();
    table
        .keys
        .iter()
        .find(|key| identity != 0 && key.identity == identity)
        .map_or(ptr::null_mut(), |key| key.values[caller.slot as usize].load(Ordering::Relaxed))
}

pub(crate) fn finish_worker(slot: usize) {
    // The callback returned. Startup still retains this exact worker owner,
    // so no later participant can use the slot until join and fresh start.
    for _ in 0..DESTRUCTOR_PASSES {
        for index in 0..MAX_KEYS {
            let pending = {
                let table = TABLE.lock();
                let key = &table.keys[index];
                key.destructor.and_then(|callback| {
                    let value = key.values[slot].swap(ptr::null_mut(), Ordering::Relaxed);
                    (!value.is_null()).then_some((callback, value))
                })
            };
            if let Some((callback, value)) = pending {
                // The caller retains callback code and the pointee through
                // exit cleanup. Both table and roster guards have left.
                unsafe { callback(value) };
            }
        }
    }
    let table = TABLE.lock();
    let unfinished = table
        .keys
        .iter()
        .any(|key| key.destructor.is_some() && !key.values[slot].load(Ordering::Relaxed).is_null());
    if unfinished {
        drop(table);
        knytos_rt::logln!("[c-runtime] TSD destructor iteration limit");
        knytos_rt::domain_abort();
    }
    for key in &table.keys {
        key.values[slot].store(ptr::null_mut(), Ordering::Relaxed);
    }
}
