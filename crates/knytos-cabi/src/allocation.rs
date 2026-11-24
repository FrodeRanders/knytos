//! Private C heap blocks. The list owns allocations while C borrows their byte
//! pointers. All allocation/deallocation uses Rust's one global allocator.
use alloc::alloc::{
    alloc,
    dealloc,
};
use core::{
    alloc::Layout,
    cmp,
    ptr,
};

use lock_api::Mutex;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct AllocationStats {
    pub blocks: u64,
    pub requested_bytes: u64,
    pub peak_requested_bytes: u64,
}

#[repr(C)]
struct Header {
    base: *mut u8,
    layout: Layout,
    size: usize,
    alignment: usize,
    previous: *mut Header,
    next: *mut Header,
}

struct List {
    head: *mut Header,
    stats: AllocationStats,
}

// SAFETY: the private list and its allocation headers are only accessed while
// its mutex is held. C may access distinct user bytes, not these headers.
unsafe impl Send for List {}

pub(crate) struct Allocations(Mutex<knytos_rt::CountingMutex, List>);

impl Allocations {
    pub const fn new() -> Self {
        Self(Mutex::new(List {
            head: ptr::null_mut(),
            stats: AllocationStats {
                blocks: 0,
                requested_bytes: 0,
                peak_requested_bytes: 0,
            },
        }))
    }

    pub fn allocate(&self, size: usize, alignment: usize) -> *mut u8 {
        let alignment = alignment.max(core::mem::align_of::<Header>());
        if !alignment.is_power_of_two() {
            return ptr::null_mut();
        }
        let Some(total) = size
            .max(1)
            .checked_add(core::mem::size_of::<Header>())
            .and_then(|value| value.checked_add(alignment - 1))
        else {
            return ptr::null_mut();
        };
        let Ok(layout) = Layout::from_size_align(total, core::mem::align_of::<Header>()) else {
            return ptr::null_mut();
        };
        // SAFETY: valid nonzero layout; the complete block is installed in the
        // list below before its user bytes cross the C ABI boundary.
        let base = unsafe { alloc(layout) };
        if base.is_null() {
            return base;
        }
        let address =
            (base as usize + core::mem::size_of::<Header>() + alignment - 1) & !(alignment - 1);
        let user = address as *mut u8;
        let header = unsafe { user.sub(core::mem::size_of::<Header>()).cast::<Header>() };
        let mut list = self.0.lock();
        unsafe {
            header.write(Header {
                base,
                layout,
                size,
                alignment,
                previous: ptr::null_mut(),
                next: list.head,
            });
            if !list.head.is_null() {
                (*list.head).previous = header;
            }
        }
        list.head = header;
        list.stats.blocks += 1;
        list.stats.requested_bytes += size as u64;
        list.stats.peak_requested_bytes =
            cmp::max(list.stats.peak_requested_bytes, list.stats.requested_bytes);
        user
    }

    /// SAFETY: caller owns a still-live C allocation from this exact list.
    pub unsafe fn release(&self, user: *mut u8) {
        if user.is_null() {
            return;
        }
        let header = unsafe { user.sub(core::mem::size_of::<Header>()).cast::<Header>() };
        let (base, layout) = {
            let mut list = self.0.lock();
            unsafe {
                if (*header).previous.is_null() {
                    list.head = (*header).next;
                } else {
                    (*(*header).previous).next = (*header).next;
                }
                if !(*header).next.is_null() {
                    (*(*header).next).previous = (*header).previous;
                }
                list.stats.blocks -= 1;
                list.stats.requested_bytes -= (*header).size as u64;
                ((*header).base, (*header).layout)
            }
        };
        // The allocation's original layout remains intact until this exact
        // deallocation; no header or C alias is used afterwards.
        unsafe { dealloc(base, layout) };
    }

    pub unsafe fn resize(&self, user: *mut u8, size: usize) -> *mut u8 {
        if user.is_null() {
            return self.allocate(size, 16);
        }
        if size == 0 {
            unsafe { self.release(user) };
            return ptr::null_mut();
        }
        // Do not retain a shared header reference while insertion mutates
        // list links (the old allocation may currently be the list head).
        let (original_size, alignment) = unsafe {
            let header = &*user.sub(core::mem::size_of::<Header>()).cast::<Header>();
            (header.size, header.alignment)
        };
        let replacement = self.allocate(size, alignment);
        if replacement.is_null() {
            return replacement;
        }
        unsafe {
            ptr::copy_nonoverlapping(user, replacement, cmp::min(size, original_size));
            self.release(user);
        }
        replacement
    }

    pub fn stats(&self) -> AllocationStats {
        self.0.lock().stats
    }

    /// Consume all remaining C allocations after C exit hooks have finished.
    /// No C code or outstanding borrowed operation may run after this point.
    #[cfg(any(test, target_os = "none"))]
    pub unsafe fn release_all(&self) {
        loop {
            let head = self.0.lock().head;
            if head.is_null() {
                break;
            }
            let user = unsafe { head.cast::<u8>().add(core::mem::size_of::<Header>()) };
            unsafe { self.release(user) };
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn aligned_blocks_realloc_failure_and_unordered_release() {
        let allocations = Allocations::new();
        let first = allocations.allocate(137, 4096);
        let second = allocations.allocate(0, 16);
        assert!(!first.is_null() && !second.is_null());
        assert_eq!(first as usize % 4096, 0);
        unsafe {
            first.write_bytes(0xa5, 137);
            assert!(allocations.resize(first, usize::MAX).is_null());
            assert!((0..137).all(|i| first.add(i).read() == 0xa5));
            let grown = allocations.resize(first, 300);
            assert!(!grown.is_null());
            assert_eq!(grown as usize % 4096, 0);
            assert!((0..137).all(|i| grown.add(i).read() == 0xa5));
            allocations.release(second);
            let shrunk = allocations.resize(grown, 17);
            assert!((0..17).all(|i| shrunk.add(i).read() == 0xa5));
            assert!(allocations.resize(shrunk, 0).is_null());
        }
        assert_eq!(allocations.stats().blocks, 0);
        assert_eq!(allocations.stats().requested_bytes, 0);
    }

    #[test]
    fn rejected_layout_does_not_publish_or_allocate() {
        let allocations = Allocations::new();
        assert!(allocations.allocate(usize::MAX, 16).is_null());
        assert!(allocations.allocate(1, usize::MAX).is_null());
        assert_eq!(allocations.stats(), AllocationStats::default());
    }

    #[test]
    fn terminal_cleanup_releases_all_retained_blocks() {
        let allocations = Allocations::new();
        assert!(!allocations.allocate(32, 16).is_null());
        assert!(!allocations.allocate(4096, 4096).is_null());
        unsafe { allocations.release_all() };
        assert_eq!(allocations.stats().blocks, 0);
        assert_eq!(allocations.stats().requested_bytes, 0);
    }
}
