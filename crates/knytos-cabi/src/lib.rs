//! C application boundary. C pointers are borrowed within this native domain;
//! they are not kernel capabilities. The initial profile is single-threaded.
#![cfg_attr(target_os = "none", no_std)]

extern crate alloc;

mod allocation;
mod clock;
#[cfg(target_os = "none")]
mod native;
#[cfg(any(all(feature = "service-worker", target_os = "none"), test))]
mod realtime;
#[cfg(all(target_os = "none", feature = "service-worker"))]
mod service_worker;
#[cfg(all(target_os = "none", feature = "native-workers"))]
mod sync;
#[cfg(all(target_os = "none", feature = "native-workers"))]
mod tsd;
#[cfg(all(target_os = "none", feature = "native-workers"))]
mod workers;

use core::ptr;

pub use allocation::AllocationStats;
pub use clock::RealtimeSample;

pub const OK: i32 = 0;
pub const INVALID: i32 = 1;
pub const UNAVAILABLE: i32 = 2;
pub const OVERFLOW: i32 = 3;
pub const DEADLINE: i32 = 4;
pub const WAIT_REJECTED: i32 = 5;
pub const SHUTDOWN: i32 = 6;
pub const BUSY: i32 = 7;

pub(crate) static ALLOCATIONS: allocation::Allocations = allocation::Allocations::new();

/// C ABI version; independent from the signed kernel runtime ABI.
#[unsafe(no_mangle)]
pub extern "C" fn knytos_abi_version() -> u32 {
    1
}

/// Allocate C storage. Zero size is a freeable allocation; failure is null.
#[unsafe(no_mangle)]
pub extern "C" fn knytos_malloc(size: usize) -> *mut u8 {
    ALLOCATIONS.allocate(size, 16)
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_calloc(count: usize, size: usize) -> *mut u8 {
    let Some(bytes) = count.checked_mul(size) else {
        return ptr::null_mut();
    };
    let pointer = knytos_malloc(bytes);
    if !pointer.is_null() {
        // SAFETY: this boundary exclusively owns the newly allocated block.
        unsafe { pointer.write_bytes(0, bytes) };
    }
    pointer
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_aligned_alloc(alignment: usize, size: usize) -> *mut u8 {
    if !alignment.is_power_of_two() || !size.is_multiple_of(alignment) {
        return ptr::null_mut();
    }
    ALLOCATIONS.allocate(size, alignment.max(16))
}

/// # Safety
/// `pointer` is null or a live block from this C allocator. Ownership is
/// consumed exactly once at this C ABI boundary; aliases become invalid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_free(pointer: *mut u8) {
    unsafe { ALLOCATIONS.release(pointer) };
}

/// # Safety
/// Same pointer contract as `knytos_free`. Failure preserves the original
/// allocation. Success consumes it; size zero frees it and returns null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_realloc(pointer: *mut u8, size: usize) -> *mut u8 {
    unsafe { ALLOCATIONS.resize(pointer, size) }
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct AbiPair {
    pub unsigned_value: u64,
    pub signed_value: i64,
    pub floating_value: f64,
}

/// Exercises the ordinary C struct/integer/FP calling convention in both
/// directions. It owns no resources and is also usable by host ABI probes.
#[unsafe(no_mangle)]
pub extern "C" fn knytos_abi_transform(value: AbiPair, delta: u64) -> AbiPair {
    AbiPair {
        unsigned_value: value.unsigned_value.wrapping_add(delta),
        signed_value: value.signed_value.wrapping_sub(delta as i64),
        floating_value: value.floating_value * 2.0,
    }
}

pub fn counter_nanoseconds(ticks: u64, frequency: u64) -> Result<u64, i32> {
    if frequency == 0 {
        return Err(UNAVAILABLE);
    }
    u64::try_from(u128::from(ticks) * 1_000_000_000 / u128::from(frequency)).map_err(|_| OVERFLOW)
}

/// # Safety
/// `output` points to writable, aligned storage for one AllocationStats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_allocation_stats(output: *mut AllocationStats) -> i32 {
    if output.is_null()
        || !(output as usize).is_multiple_of(core::mem::align_of::<AllocationStats>())
    {
        return INVALID;
    }
    unsafe { output.write(ALLOCATIONS.stats()) };
    OK
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn counter_conversion_handles_fraction_zero_and_overflow() {
        assert_eq!(counter_nanoseconds(125, 250), Ok(500_000_000));
        assert_eq!(counter_nanoseconds(1, 3), Ok(333_333_333));
        assert_eq!(counter_nanoseconds(0, 0), Err(UNAVAILABLE));
        assert_eq!(counter_nanoseconds(u64::MAX, 1), Err(OVERFLOW));
        assert_eq!(counter_nanoseconds(u64::MAX, u64::MAX), Ok(1_000_000_000));
    }
}
