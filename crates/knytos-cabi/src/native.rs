use core::{
    ptr,
    sync::atomic::{
        AtomicBool,
        AtomicUsize,
        Ordering,
    },
};

use lock_api::Mutex;

use super::*;

#[global_allocator]
static HEAP: knytos_rt::HeapLock = knytos_rt::heap();
static EXITING: AtomicBool = AtomicBool::new(false);
static ENTRY_SP: AtomicUsize = AtomicUsize::new(0);
static LOW_SP: AtomicUsize = AtomicUsize::new(usize::MAX);
#[cfg(feature = "native-workers")]
static REACTOR_WAITING: AtomicBool = AtomicBool::new(false);

struct Console {
    bytes: [u8; 256],
    length: usize,
}
static CONSOLE: Mutex<knytos_rt::CountingMutex, Console> = Mutex::new(Console {
    bytes: [0; 256],
    length: 0,
});

unsafe extern "C" {
    fn knytos_c_main(argc: i32, argv: *mut *mut u8) -> i32;
    fn exit(status: i32) -> !;
    // Linker boundaries may alias when an array is empty. Zero-sized symbols
    // prevent the optimizer assuming distinct nonzero Rust static objects.
    static __init_array_start: [u8; 0];
    static __init_array_end: [u8; 0];
    static __fini_array_start: [u8; 0];
    static __fini_array_end: [u8; 0];
}

#[panic_handler]
fn panic(info: &core::panic::PanicInfo<'_>) -> ! {
    knytos_rt::logln!("[c-runtime] panic: {}", info);
    knytos_rt::domain_abort()
}

fn sample_stack() {
    #[cfg(feature = "native-workers")]
    if !crate::workers::is_main() {
        return;
    }
    #[cfg(target_arch = "aarch64")]
    {
        let sp: usize;
        unsafe {
            core::arch::asm!("mov {}, sp", out(reg) sp, options(nomem, nostack, preserves_flags));
        }
        LOW_SP.fetch_min(sp, Ordering::Relaxed);
    }
}

/// The generated linker script provides aligned arrays of valid C functions.
unsafe fn callbacks(
    start: *const unsafe extern "C" fn(),
    end: *const unsafe extern "C" fn(),
    reverse: bool,
) {
    let bytes = (end as usize).checked_sub(start as usize).expect("reversed C init array");
    assert!(bytes.is_multiple_of(core::mem::size_of::<unsafe extern "C" fn()>()));
    let functions = unsafe {
        core::slice::from_raw_parts(start, bytes / core::mem::size_of::<unsafe extern "C" fn()>())
    };
    if reverse {
        for callback in functions.iter().rev() {
            unsafe { callback() };
        }
    } else {
        for callback in functions {
            unsafe { callback() };
        }
    }
}

#[unsafe(no_mangle)]
#[unsafe(link_section = ".text._start")]
pub extern "C" fn _start() -> ! {
    knytos_rt::run_main(run)
}

fn run(_context: knytos_rt::Context) -> ! {
    #[cfg(feature = "native-workers")]
    crate::workers::initialize(&_context);
    sample_stack();
    ENTRY_SP.store(LOW_SP.load(Ordering::Relaxed), Ordering::Relaxed);
    // No owned launch capability is adopted. argc/argv storage belongs to this
    // startup frame, contains no heap allocation and lasts until termination.
    let mut program = *b"knytos-c\0";
    let mut argv = [program.as_mut_ptr(), ptr::null_mut()];
    unsafe {
        callbacks(
            ptr::addr_of!(__init_array_start).cast(),
            ptr::addr_of!(__init_array_end).cast(),
            false,
        );
        let status = knytos_c_main(1, argv.as_mut_ptr());
        exit(status)
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_console_write(bytes: *const u8, length: usize) -> i32 {
    sample_stack();
    if (bytes.is_null() && length != 0) || length > 65536 {
        return INVALID;
    }
    if length == 0 {
        return OK;
    }
    // SAFETY: C supplies a valid readable buffer for the call's duration.
    let input = unsafe { core::slice::from_raw_parts(bytes, length) };
    let mut console = CONSOLE.lock();
    for &byte in input {
        if byte == b'\n' {
            flush(&mut console);
            continue;
        }
        let index = console.length;
        console.bytes[index] = byte;
        console.length += 1;
        if console.length == console.bytes.len() {
            flush(&mut console);
        }
    }
    OK
}

fn flush(console: &mut Console) {
    if console.length != 0 {
        // Logging owns no closeable capability. The fixed console buffer stays
        // borrowed through this synchronous logging syscall.
        knytos_rt::abi::el0_log_str(console.bytes.as_ptr(), console.length);
        console.length = 0;
    }
}

/// Single-threaded C reactor borrows launch CQ zero. No resource is adopted.
#[unsafe(no_mangle)]
pub extern "C" fn knytos_event_wait(timeout_ms: i64) -> i32 {
    #[cfg(feature = "native-workers")]
    if !crate::workers::is_main() {
        return INVALID;
    }
    wait_for_event(timeout_ms)
}

/// Electing a reactor is the caller's responsibility. Reject an overlapping
/// borrow of the shared CQ zero cursor, even from the startup-only API.
#[cfg(feature = "native-workers")]
#[unsafe(no_mangle)]
pub extern "C" fn knytos_worker_event_wait(timeout_ms: i64) -> i32 {
    if crate::workers::participant().is_none() {
        return INVALID;
    }
    wait_for_event(timeout_ms)
}

fn wait_for_event(timeout_ms: i64) -> i32 {
    let timeout = match timeout_ms {
        -1 => None,
        0.. => Some(timeout_ms as u64),
        _ => return INVALID,
    };
    #[cfg(feature = "native-workers")]
    {
        if REACTOR_WAITING.swap(true, Ordering::Acquire) {
            return BUSY;
        }
        let result = wait_on_queue(timeout, 0);
        REACTOR_WAITING.store(false, Ordering::Release);
        result
    }
    #[cfg(not(feature = "native-workers"))]
    wait_on_queue(timeout, 0)
}

/// Observe the authenticated lifecycle request at an application checkpoint.
#[unsafe(no_mangle)]
pub extern "C" fn knytos_shutdown_requested() -> bool {
    knytos_rt::Context.lifecycle().shutdown_requested().is_some()
}

pub(crate) fn wait_on_queue(timeout: Option<u64>, queue: u32) -> i32 {
    #[cfg(feature = "native-workers")]
    let check_shutdown = queue == 0 || !crate::workers::reactor_owns_shutdown();
    #[cfg(not(feature = "native-workers"))]
    let check_shutdown = true;
    if check_shutdown && knytos_shutdown_requested() {
        return SHUTDOWN;
    }
    let result = knytos_rt::cq::wait(timeout, queue);
    if check_shutdown && knytos_shutdown_requested() {
        return SHUTDOWN;
    }
    match result {
        Ok(knytos_rt::cq::WaitOutcome::Ready) => OK,
        Ok(knytos_rt::cq::WaitOutcome::Deadline) => DEADLINE,
        Err(knytos_rt::cq::WaitError::AdmissionRejected) => WAIT_REJECTED,
        Err(_) => INVALID,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_event_signal() {
    // CQ access has no closeable owner; the kernel retains a work generation
    // even if the sole reactor has not entered its wait yet.
    knytos_rt::abi::cq_wake(0);
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_console_flush() {
    flush(&mut CONSOLE.lock());
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_clock_monotonic_ns(output: *mut u64) -> i32 {
    sample_stack();
    if output.is_null() || !(output as usize).is_multiple_of(8) {
        return INVALID;
    }
    let (ticks, frequency) = knytos_rt::abi::monotonic_clock();
    match counter_nanoseconds(ticks, frequency) {
        Ok(value) => {
            unsafe { output.write(value) };
            OK
        }
        Err(status) => status,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_clock_realtime_ns(output: *mut u64) -> i32 {
    if output.is_null() || !(output as usize).is_multiple_of(8) {
        INVALID
    } else {
        UNAVAILABLE
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_c_abort() -> ! {
    knytos_console_flush();
    knytos_rt::logln!("[c-runtime] abort");
    knytos_rt::domain_abort()
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_c_terminate(status: i32, run_finalizers: i32) -> ! {
    terminate(status, run_finalizers != 0, None)
}

/// The caller has stopped application work and disposed of any external
/// resources. This consumes the startup thread after C terminal cleanup.
#[unsafe(no_mangle)]
pub extern "C" fn knytos_shutdown_complete() -> ! {
    let Some(request) = knytos_rt::Context.lifecycle().shutdown_requested() else {
        knytos_rt::logln!("[c-runtime] shutdown completion without a request");
        knytos_c_abort();
    };
    terminate(0, true, Some(request))
}

fn terminate(status: i32, run_finalizers: bool, shutdown: Option<knytos_rt::ShutdownRequest>) -> ! {
    #[cfg(feature = "native-workers")]
    crate::workers::require_joined();
    if EXITING.swap(true, Ordering::AcqRel) {
        knytos_c_abort();
    }
    sample_stack();
    if run_finalizers {
        unsafe {
            callbacks(
                ptr::addr_of!(__fini_array_start).cast(),
                ptr::addr_of!(__fini_array_end).cast(),
                true,
            )
        };
    }
    knytos_console_flush();
    let stats = ALLOCATIONS.stats();
    // The worker profile checks join receipts before reaching this boundary.
    // No asynchronous operation or external resource token exposes C bytes.
    unsafe { ALLOCATIONS.release_all() };
    let remaining = ALLOCATIONS.stats();
    assert_eq!(remaining.blocks, 0);
    let peak: u64 = unsafe { knytos_rt::config::read(knytos_launch::heap_status::PEAK_OFFSET) };
    knytos_rt::logln!(
        "[c-runtime] exit={} released_blocks={} remaining_blocks={} heap_peak={} sampled_stack={}",
        status,
        stats.blocks,
        remaining.blocks,
        peak,
        ENTRY_SP.load(Ordering::Relaxed).saturating_sub(LOW_SP.load(Ordering::Relaxed))
    );
    if status != 0 {
        knytos_rt::domain_abort();
    }
    if let Some(request) = shutdown {
        request.complete();
    }
    // All local C allocations are released; no remote owner is bypassed.
    unsafe { knytos_rt::abi::thread_exit() }
}
