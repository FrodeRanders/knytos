//! # Initialization Module
use alloc::boxed::Box;

use crate::{
    cpu::{
        isa::{
            init::IsaInitializer,
            interface::init::InitInterface,
            lp,
            lp::ops::get_lp_id,
        },
        scheduler::{
            lp_schedulers::round_robin::RoundRobin,
            system_scheduler::SYSTEM_SCHEDULER,
        },
    },
    klib::time::duration::ExtDuration,
    memory::{
        PHYSICAL_FRAME_ALLOCATOR,
        allocators::global_allocator::init_primary_allocator,
    },
};

pub fn bsp_init() {
    match IsaInitializer::init_bsp() {
        Ok(_) => {}
        Err(e) => {
            // initialization failure is irrecoverable
            panic!("LP 0: ISA specific initialization failed: {e:?}");
        }
    }
    match PHYSICAL_FRAME_ALLOCATOR.try_lock() {
        Some(_pfa) => {}
        None => {
            panic!("LP 0: Failed to acquire lock on PhysicalFrameAllocator.");
        }
    }
    init_primary_allocator();
    crate::memory::budget::initialize_kernel();
    // Record the BSP's APIC ID now that the heap allocator is ready (BTreeMap requires the heap).
    #[cfg(target_arch = "x86_64")]
    crate::cpu::isa::interrupts::x2apic::X2Apic::record_id();
    // Pre-create all LP schedulers in LP ID order (0..lp_count) while single-threaded.
    // This ensures lp_schedulers[i] is always LP i's scheduler, regardless of AP init order.
    let sched = Box::new(RoundRobin::new(get_lp_id(), ExtDuration::from_millis(10)));
    unsafe {
        SYSTEM_SCHEDULER.write().set_lp_scheduler(sched);
    }
}

pub fn ap_init() {
    let lp_id = lp::ops::get_lp_id();
    match IsaInitializer::init_ap() {
        Ok(_) => {}
        Err(e) => {
            // initialization failure is irrecoverable
            panic!("LP {lp_id}: ISA specific initialization failed: {e:?}");
        }
    }
    // Build the scheduler (which creates this LP's idle thread) before taking
    // the system-scheduler write lock, so thread creation does not run under it.
    let sched = Box::new(RoundRobin::new(get_lp_id(), ExtDuration::from_millis(10)));
    unsafe {
        SYSTEM_SCHEDULER.write().set_lp_scheduler(sched);
    }
}
