mod ap;
pub(crate) mod boot;
mod bsp;
pub mod gdt;

use crate::cpu::isa::{
    interface::init::InitInterface,
    memory::paging::PAGE_SIZE,
};

const INTERRUPT_STACK_SIZE: usize = PAGE_SIZE * 4;

pub struct IsaInitializer;

impl InitInterface for IsaInitializer {
    type Error = core::convert::Infallible;

    fn init_bsp() -> Result<(), Self::Error> {
        // Initialize TSS, GDT, and IDT
        bsp::init_bsp();
        // return success
        crate::cpu::execution_target::record_current_lp();
        Ok(())
    }

    fn init_ap() -> Result<(), Self::Error> {
        // Initialize TSS, GDT, and IDT
        ap::init_ap();
        crate::cpu::execution_target::record_current_lp();
        Ok(())
    }

    fn deinit() -> Result<(), Self::Error> {
        // Nothing to do here yet
        Ok(())
    }
}
