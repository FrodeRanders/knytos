//! Picolibc hooks for the separately configured worker profile.
use core::{
    cell::UnsafeCell,
    ffi::{
        c_int,
        c_void,
    },
};

use knytos_rt::deadline::Deadline;

use super::NativeMutex;
use crate::{
    OK,
    workers::{
        PARTICIPANT_COUNT,
        participant,
    },
};

struct Errnos([UnsafeCell<c_int>; PARTICIPANT_COUNT]);

// Each live participant exclusively uses its own entry. Startup joins an
// old worker before reuse, and the new trampoline initializes the entry
// before calling C. C must keep errno pointers within that lifetime.
unsafe impl Sync for Errnos {}

static ERRNOS: Errnos = Errnos([const { UnsafeCell::new(0) }; PARTICIPANT_COUNT]);

#[unsafe(export_name = "__lock___libc_recursive_mutex")]
pub static LIBC_LOCK: NativeMutex = NativeMutex::new();
#[unsafe(export_name = "knytos_stdio_output_lock")]
pub static OUTPUT_LOCK: NativeMutex = NativeMutex::new();
#[unsafe(export_name = "knytos_stdio_input_lock")]
pub static INPUT_LOCK: NativeMutex = NativeMutex::new();

/// Safety: the trampoline has exclusive access to this slot and no C
/// callback for this generation has started.
pub(crate) unsafe fn reset_errno(slot: usize) {
    unsafe { ERRNOS.0[slot].get().write(0) };
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_errno_location() -> *mut c_int {
    let Some(caller) = participant() else {
        abort("errno outside the worker roster");
    };
    ERRNOS.0[caller.slot as usize].get()
}

fn abort(reason: &str) -> ! {
    // The caller can already hold a libc or stream lock. Leave the lock and
    // related C data for domain retirement rather than entering libc again.
    knytos_rt::logln!("[worker-libc] {}", reason);
    knytos_rt::domain_abort()
}

// Picolibc's CMake source list omits the lazy initializer. Fixed streams
// already have locks and local formatting streams opt out. Reject any
// other stream that reaches this path rather than publish a lazy lock.
#[unsafe(no_mangle)]
pub extern "C" fn __flockfile_init(_stream: *mut c_void) -> ! {
    abort("dynamic stdio stream is unsupported")
}

const LOCK_BUDGET_MS: u64 = 5000;
fn acquire(lock: &NativeMutex, recursive: bool) {
    let Some(caller) = participant() else {
        abort("lock outside the worker roster");
    };
    if lock.lock_inner(caller, &Deadline::after(LOCK_BUDGET_MS), recursive) != OK {
        abort("libc lock acquisition failed");
    }
}

/// Picolibc supplies a live retarget lock. A recursive call may nest for the
/// same participant generation.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn __retarget_lock_acquire_recursive(lock: &NativeMutex) {
    acquire(lock, true);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn __retarget_lock_acquire(lock: &NativeMutex) {
    acquire(lock, false);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn __retarget_lock_release(lock: &NativeMutex) {
    let Some(caller) = participant() else {
        abort("unlock outside the worker roster");
    };
    if lock.unlock(caller) != OK {
        abort("libc lock release failed");
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn __retarget_lock_release_recursive(lock: &NativeMutex) {
    unsafe { __retarget_lock_release(lock) };
}

#[unsafe(no_mangle)]
pub extern "C" fn __retarget_lock_init(_lock: *mut *mut NativeMutex) -> ! {
    abort("dynamic libc lock initialization is unsupported")
}

#[unsafe(no_mangle)]
pub extern "C" fn __retarget_lock_init_recursive(_lock: *mut *mut NativeMutex) -> ! {
    abort("dynamic libc lock initialization is unsupported")
}

#[unsafe(no_mangle)]
pub extern "C" fn __retarget_lock_close(_lock: *mut NativeMutex) -> ! {
    abort("libc lock close is unsupported")
}

#[unsafe(no_mangle)]
pub extern "C" fn __retarget_lock_close_recursive(_lock: *mut NativeMutex) -> ! {
    abort("libc lock close is unsupported")
}
