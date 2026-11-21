use core::arch::asm;

use super::interrupts::load_ivt;
use crate::{
    cpu::isa::interface::init::InitInterface,
    early_logln,
};

pub struct IsaInitializer;

const SCTLR_EL1_WXN: u64 = 1 << 19;

#[derive(Debug)]
pub enum Error {
    // Error type for the aarch64 architecture
}

fn clear_write_execute_never() -> (u64, u64) {
    let before: u64;
    unsafe {
        asm!("mrs {}, sctlr_el1", out(reg) before, options(nomem, nostack, preserves_flags));
    }
    let after = before & !SCTLR_EL1_WXN;
    if after != before {
        unsafe {
            asm!(
                "msr sctlr_el1, {sctlr}",
                "isb",
                sctlr = in(reg) after,
                options(nostack, preserves_flags),
            );
        }
    }
    (before, after)
}

impl InitInterface for IsaInitializer {
    type Error = Error;

    #[inline(always)]
    fn init_bsp() -> Result<(), Self::Error> {
        let (sctlr_before, sctlr_after) = clear_write_execute_never();
        if sctlr_before != sctlr_after {
            early_logln!("[boot] cleared SCTLR_EL1.WXN: {:#x} -> {:#x}", sctlr_before, sctlr_after);
        }
        load_ivt();
        crate::cpu::execution_target::record_current_lp();
        Ok(())
    }

    fn init_ap() -> Result<(), Self::Error> {
        clear_write_execute_never();
        load_ivt();
        crate::cpu::execution_target::record_current_lp();
        Ok(())
    }

    fn deinit() -> Result<(), Self::Error> {
        // Deinitialization code for the aarch64 architecture
        Ok(())
    }
}
