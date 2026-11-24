//! Bounded C worker roster. The startup thread is the sole joiner. Workers
//! own no kernel capability and borrow their argument through exact exit.
use core::{
    ffi::c_void,
    sync::atomic::{
        AtomicBool,
        AtomicU64,
        AtomicUsize,
        Ordering,
    },
};

use knytos_rt::owned::ThreadHandle;
use lock_api::Mutex;

use crate::{
    BUSY,
    INVALID,
    OK,
    OVERFLOW,
    SHUTDOWN,
    UNAVAILABLE,
    WAIT_REJECTED,
    native::wait_on_queue,
};

const MAX_WORKERS: usize = 3;
static MAIN_TID: AtomicU64 = AtomicU64::new(u64::MAX);
static WORKER_LIMIT: AtomicUsize = AtomicUsize::new(0);
static STARTS_CLOSED: AtomicBool = AtomicBool::new(false);
static NEXT_GENERATION: AtomicU64 = AtomicU64::new(1);

/// Runtime-local participant identity. Neither part is a kernel handle.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct Participant {
    pub(crate) slot: u32,
    generation: u64,
}

impl Participant {
    pub(crate) const fn bit(self) -> u8 {
        1 << self.slot
    }
}

#[derive(Clone, Copy)]
struct Start {
    entry: unsafe extern "C" fn(*mut c_void),
    argument: *mut c_void,
}

// The C caller promises the callback and its argument remain valid and may be
// accessed by the worker until join. The pointer's provenance stays intact.
unsafe impl Send for Start {}

struct Worker {
    start: Start,
    generation: u64,
    tid: Option<u64>,
    handle: Option<ThreadHandle>,
}

static WORKERS: Mutex<knytos_rt::CountingMutex, [Option<Worker>; MAX_WORKERS]> =
    Mutex::new([const { None }; MAX_WORKERS]);

pub(crate) fn initialize(context: &knytos_rt::Context) {
    MAIN_TID.store(knytos_rt::abi::get_tid(), Ordering::Relaxed);
    let queues = context.shard_cq_count();
    if context.shard_cq_base().is_some() {
        // CQ zero remains the application reactor. CQ one belongs to startup.
        WORKER_LIMIT.store(queues.saturating_sub(1).min(MAX_WORKERS), Ordering::Relaxed);
    }
}

pub(crate) fn is_main() -> bool {
    knytos_rt::abi::get_tid() == MAIN_TID.load(Ordering::Relaxed)
}

pub(crate) fn participant() -> Option<Participant> {
    let tid = knytos_rt::abi::get_tid();
    if tid == MAIN_TID.load(Ordering::Relaxed) {
        return Some(Participant {
            slot: 0,
            generation: 0,
        });
    }
    WORKERS.lock().iter().enumerate().find_map(|(index, worker)| {
        let worker = worker.as_ref()?;
        (worker.tid == Some(tid)).then_some(Participant {
            slot: index as u32 + 1,
            generation: worker.generation,
        })
    })
}

fn worker_index(slot: u32) -> Option<usize> {
    let index = (slot as usize).checked_sub(1)?;
    (index < WORKER_LIMIT.load(Ordering::Relaxed)).then_some(index)
}

extern "C" fn worker_entry<const INDEX: usize>() {
    let tid = knytos_rt::abi::get_tid();
    let start = {
        let mut workers = WORKERS.lock();
        let worker = workers[INDEX].as_mut().expect("worker must have prepared start");
        assert!(worker.tid.replace(tid).is_none());
        worker.start
    };
    // SAFETY: the startup thread published Start before spawning and must
    // retain the callback's argument until this exact thread has exited.
    unsafe { (start.entry)(start.argument) };
    // Stop advertising this non-owning TID before the kernel can recycle it.
    WORKERS.lock()[INDEX].as_mut().expect("running worker").tid = None;
    // The callback returned, so its C frames and local Rust owners are gone.
    unsafe { knytos_rt::abi::thread_exit() }
}

/// # Safety
/// The entry and argument must remain valid until an exact successful join.
/// Any shared mutable state must be synchronized by the caller.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn knytos_thread_start(
    slot: u32,
    lp: u32,
    entry: Option<unsafe extern "C" fn(*mut c_void)>,
    argument: *mut c_void,
) -> i32 {
    let (Some(index), Some(entry)) = (worker_index(slot), entry) else {
        return INVALID;
    };
    if !is_main() {
        return INVALID;
    }
    if STARTS_CLOSED.load(Ordering::Relaxed) {
        return SHUTDOWN;
    }
    {
        let mut workers = WORKERS.lock();
        if workers[index].is_some() {
            return BUSY;
        }
        let Ok(generation) =
            NEXT_GENERATION
                .try_update(Ordering::Relaxed, Ordering::Relaxed, |value| value.checked_add(1))
        else {
            return OVERFLOW;
        };
        workers[index] = Some(Worker {
            start: Start {
                entry,
                argument,
            },
            generation,
            tid: None,
            handle: None,
        });
    }
    const TRAMPOLINES: [extern "C" fn(); MAX_WORKERS] =
        [worker_entry::<0>, worker_entry::<1>, worker_entry::<2>];
    // The fixed trampoline can enter immediately on the target LP. It takes
    // the prepared Start from the matching slot after our guard has left.
    let submitted = unsafe { ThreadHandle::spawn(TRAMPOLINES[index] as usize, lp) };
    let mut workers = WORKERS.lock();
    match submitted {
        Ok(handle) => {
            workers[index].as_mut().expect("prepared worker").handle = Some(handle);
            OK
        }
        Err(_) => {
            assert!(workers[index].as_ref().is_some_and(|worker| worker.tid.is_none()));
            workers[index] = None;
            UNAVAILABLE
        }
    }
}

/// The startup thread retains the same slot and generation on join failure.
#[unsafe(no_mangle)]
pub extern "C" fn knytos_thread_join(slot: u32) -> i32 {
    let Some(index) = worker_index(slot) else {
        return INVALID;
    };
    if !is_main() {
        return INVALID;
    }
    let handle = {
        let mut workers = WORKERS.lock();
        let Some(handle) = workers[index].as_mut().and_then(|worker| worker.handle.take()) else {
            return INVALID;
        };
        handle
    };
    // Holding the roster guard during this wait would prevent worker entry.
    match handle.join_preserving() {
        Ok(_) => {
            WORKERS.lock()[index] = None;
            OK
        }
        Err((handle, error)) => {
            WORKERS.lock()[index].as_mut().expect("joining worker").handle = Some(handle);
            knytos_rt::logln!("[c-runtime] worker join rejected: {:?}", error);
            WAIT_REJECTED
        }
    }
}

/// Private CQ readiness is only a hint. Callers recheck their own predicate.
#[unsafe(no_mangle)]
pub extern "C" fn knytos_thread_wait(timeout_ms: u32) -> i32 {
    participant().map_or(INVALID, |caller| park(caller, u64::from(timeout_ms)))
}

#[unsafe(no_mangle)]
pub extern "C" fn knytos_thread_signal(slot: u32) -> i32 {
    if participant().is_none() {
        return INVALID;
    }
    if slot == 0 {
        if WORKER_LIMIT.load(Ordering::Relaxed) == 0 {
            return INVALID;
        }
    } else if !worker_index(slot).is_some_and(|index| WORKERS.lock()[index].is_some()) {
        return INVALID;
    }
    // No queue resource is adopted. Publication precedes this syscall. A
    // delayed hint during slot reuse cannot prove the new predicate is true.
    knytos_rt::abi::cq_wake(slot + 1);
    OK
}

pub(crate) fn park(caller: Participant, timeout_ms: u64) -> i32 {
    if WORKER_LIMIT.load(Ordering::Relaxed) == 0 {
        return INVALID;
    }
    wait_on_queue(Some(timeout_ms), caller.slot + 1)
}

/// Signal slots captured from a synchronization object's exact waiter set.
/// The producer has already released the object's metadata guard.
pub(crate) fn wake_waiters(mut waiters: u8) {
    while waiters != 0 {
        let slot = waiters.trailing_zeros();
        knytos_rt::abi::cq_wake(slot + 1);
        waiters &= waiters - 1;
    }
}

pub(crate) fn require_joined() {
    // Only startup may submit more workers. Fence that path before running
    // C finalizers so an exit callback cannot invalidate the join check.
    STARTS_CLOSED.store(true, Ordering::Relaxed);
    let workers = WORKERS.lock();
    let joined = workers.iter().all(Option::is_none);
    drop(workers);
    if !is_main() || !joined {
        knytos_rt::logln!("[c-runtime] exit with an unjoined worker");
        knytos_rt::domain_abort();
    }
}
