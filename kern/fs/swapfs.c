#include <swap.h>
#include <swapfs.h>
#include <mmu.h>
#include <fs.h>
#include <ide.h>
#include <pmm.h>
#include <assert.h>

// init swap file system
// make some check and initialize `max_swap_offset`
void swapfs_init(void) {
    static_assert((PGSIZE % SECTSIZE) == 0);
    if (!ide_device_valid(SWAP_DEV_NO)) {
        panic("swap fs isn't available.\n");
    }
    // update max swap offset (in pages) defined in `swap.h`
    max_swap_offset = ide_device_size(SWAP_DEV_NO) / PAGE_NSECT;
}

// read a page content from swap file 
// entry: page table entry
// page: read the content to the page
int swapfs_read(swap_entry_t entry, struct Page *page) {
    return ide_read_secs(SWAP_DEV_NO, swap_offset(entry) * PAGE_NSECT, page2kva(page), PAGE_NSECT);
}

// write a page content to swap file
// entry: page table entry
// page: the page that needs to be saved
int swapfs_write(swap_entry_t entry, struct Page *page) {
    return ide_write_secs(SWAP_DEV_NO, swap_offset(entry) * PAGE_NSECT, page2kva(page), PAGE_NSECT);
}

