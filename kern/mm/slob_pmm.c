#include <slob_pmm.h>
#include <pmm.h>
#include <buddy_pmm.h>
#include <stdio.h>

#define ALIGNUP(x, a) (((x) + (a - 1)) & ~(a - 1))

void buddy_init(void){
    buddy_pmm_manager.init();
}

void buddy_init_memmap(struct Page *base, size_t n){
    buddy_pmm_manager.init_memmap(base, n);
}

struct Page *buddy_alloc_pages(size_t n){
    return buddy_pmm_manager.alloc_pages(n);
}

void buddy_free_pages(struct Page *base, size_t n){
    buddy_pmm_manager.free_pages(base, n);
}

size_t buddy_nr_free_pages(void){
    return buddy_pmm_manager.nr_free_pages();
}

static void slob_debug(void) {
    struct Page *page = NULL;
    list_entry_t *le = NULL;
    cprintf(">>>[slob_debug] go through the page list...\n");
    // go through the page list
    list_entry_t *head = &slob_manager.free_slob_small.free_list;
    for (le = list_next(head); le != head; le = list_next(le)) {
        page = le2page(le, page_link);
        // display the page
        
    }
    cprintf(">>>[slob_debug] end of page list.\n");
}

static void slob_init(void) {
    cprintf("[debug] Initializing SLOB...\n");
    // init buddy system
    buddy_init();
    cprintf("[debug] Buddy system initialized.\n");

    // init slob manager
    // init all three free_area_t as empty
    list_init(&slob_manager.free_slob_small.free_list);
    slob_manager.free_slob_small.nr_free = 0;
    list_init(&slob_manager.free_slob_medium.free_list);
    slob_manager.free_slob_medium.nr_free = 0;
    list_init(&slob_manager.free_slob_large.free_list);
    slob_manager.free_slob_large.nr_free = 0;
    cprintf("[debug] SLOB manager initialized.\n");
}

static void slob_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    // init buddy system
    buddy_init_memmap(base, n);

    struct Page *p = base;
    for (; p != base + n; p++) {
        p->virtual = (void *)(page2pa(p) + va_pa_offset);
        p->slob_units_left = 2048;
    }
}

static struct Page *slob_alloc_pages(size_t n) {
    // wrap to buddy system
    return buddy_alloc_pages(n);
}

static void slob_free_pages(struct Page *base, size_t n) {
    // wrap to buddy system
    buddy_free_pages(base, n);
}

static size_t slob_nr_free_pages(void) {
    // wrap to buddy system
    return buddy_nr_free_pages();
}

static void *slob_alloc_bytes_from_page(struct Page *page, size_t n) {

    uint32_t slob_units = (n + SLOB_UNIT) / SLOB_UNIT - 1 + 2; // number of slob units needed

    // find the first slob_t that can hold the size
    slob_t *prev = NULL;
    slob_t *cur = (slob_t *)page->virtual;
    for (; ; prev = cur, cur = (slob_t *)((uintptr_t)cur + cur->slob_next_offset)) {
        // TODO: alignment
        // slob_t *aligned = (slob_t *)ALIGNUP((uintptr_t)cur, ALIGN_SIZE);
        // uint32_t delta = (uintptr_t)aligned - (uintptr_t)cur;
        if (cur->slob_units_left >= slob_units) {
            if (cur->slob_units_left == slob_units) {
                // cprintf("[slob_debug] No need to split slob_t...\n");
                if (prev) {
                    prev->slob_next_offset += cur->slob_next_offset;
                } else {
                    page->virtual = (slob_t *)((uintptr_t)cur + cur->slob_next_offset);
                }
            } else {
                // cprintf("[slob_debug] Splitting slob_t...\n");
                if (prev) {
                    prev->slob_next_offset += slob_units * SLOB_UNIT;
                } else {
                    page->virtual = (slob_t *)((uintptr_t)cur + slob_units * SLOB_UNIT);
                }
                slob_t *next = (slob_t *)((uintptr_t)cur + slob_units * SLOB_UNIT);
                next->slob_units_left = cur->slob_units_left - slob_units;
                next->slob_next_offset = cur->slob_next_offset - slob_units * SLOB_UNIT;
            }
            cur->slob_units_left = 0;
            page->slob_units_left -= slob_units;
            if (page->slob_units_left == 0) {
                list_del(&page->slob_link);
                // TODO: here actually we should slob_free_area->nr_free--,
                cprintf("[slob_debug] Page is full, removing from free list...\n");
            }
            return (void *)cur;
        }
        // if we reach the end of the page, then return NULL
        if (cur->slob_next_offset + (uintptr_t)cur >= page2pa(page) + PGSIZE) {
            // cprintf("[slob_debug] reached end of page\n");
            return NULL;
        }
    }
    cprintf("[slob_debug]: reached end of slob_alloc_bytes_from_page\n");
    return NULL;
}

static void *slob_alloc_bytes(size_t n) {
    cprintf("[slob_debug] Allocating 0x%lx bytes using SLOB...\n", n);
    assert(n > 0);

    // if size after align is larger than a page, then use buddy system
    // if (ALIGNUP(n, ALIGN_SIZE) + SLOB_UNIT > PGSIZE) {
    if (n + sizeof(slob_t) > PGSIZE) {
        // wrap to buddy system
        struct Page *p = buddy_alloc_pages(n / PGSIZE + 1);
        cprintf("[slob -> buddy] Bytes allocated on buddy at address %p.\n", (void *)p->virtual);
        return p->virtual;
    } else {
        // find the first free_area_t that can hold the size
        free_area_t *slob_free_area = NULL;
        if (n <= SLOB_SMALL) {
            cprintf("[slob_debug] Allocating small size.\n");
            slob_free_area = &slob_manager.free_slob_small;
        } else if (n <= SLOB_MEDIUM) {
            cprintf("[slob_debug] Allocating medium size.\n");
            slob_free_area = &slob_manager.free_slob_medium;
        } else {
            cprintf("[slob_debug] Allocating large size.\n");
            slob_free_area = &slob_manager.free_slob_large;
        }
        
        uint32_t slob_units = (n + SLOB_UNIT) / SLOB_UNIT - 1 + 2; // number of slob units needed
        slob_t *allocated_slob = NULL;
        
        // find the first page that can hold the size
        list_entry_t *le = &slob_free_area->free_list;
        while ((le = list_next(le)) != &slob_free_area->free_list) {
            struct Page *page = le2page(le, slob_link);
            if (page->slob_units_left < slob_units) {
                continue;
            }
            
            // attempt to allocate
            list_entry_t *prev = list_prev(le);
            allocated_slob = slob_alloc_bytes_from_page(page, n);
            page->slob_units_left -= slob_units;
            if (page->slob_units_left == 0) {
                list_del(&page->slob_link);
                slob_free_area->nr_free--;
                cprintf("[slob_debug] Page is full, removing from free list...\n");
            }

            if (allocated_slob) {
                break; // success
            }  
        }

        if (allocated_slob == NULL) {
            cprintf("[slob_debug] No page can hold the size, allocating a new page...\n");
            // if no page can hold the size, then allocate a new page from buddy system
            struct Page *p = buddy_alloc_pages(1);
            if (p == NULL) {
                return NULL; // no memory left
            }

            cprintf("[slob_debug] New page allocated at address %p.\n", (void *)p->virtual);

            // these're redundant
            p->slob_units_left = 2048;
            p->virtual = (void *)(page2pa(p) + va_pa_offset);

            list_add_after(&slob_free_area->free_list, &(p->slob_link));
            slob_free_area->nr_free++;
            // NOTE: page order in slob_link is not maintained

            // write first slob_t
            slob_t *slob = (slob_t *)p->virtual;
            slob->slob_units_left = 2048;
            slob->slob_next_offset = 4096;

            // attempt to allocate
            allocated_slob = slob_alloc_bytes_from_page(p, n);
            if (allocated_slob == NULL) {
                cprintf("[slob_debug] Failed to allocate from new page.\n");
                return NULL; // no memory left
            }

            p->slob_units_left -= slob_units;
            if (p->slob_units_left == 0) {
                cprintf("[slob_debug] Page is full, removing from free list...\n");
                list_del(&p->slob_link);
                slob_free_area->nr_free--;
            }

            cprintf("[slob_debug] Bytes allocated at address %p on new page.\n", (void *)allocated_slob);
            return (void *)allocated_slob;
        } else {

            cprintf("[slob_debug] Bytes allocated at address %p on exist page.\n", (void *)allocated_slob);
            return (void *)allocated_slob;
        }
        // debug
        cprintf("[slob_debug]: allocated_slob = %p reached end of slob_alloc_bytes\n", allocated_slob);
        return NULL;
    }

}

static void slob_free_bytes(void *ptr, size_t n) {
    cprintf("[slob_debug] Freeing 0x%lx bytes from address %p... using SLOB\n", n, ptr);
    assert(ptr);
    assert(n > 0);

    // If the size after alignment is larger than a page, then use buddy system
    if (n + sizeof(slob_t) > PGSIZE) {
        cprintf("[slob -> buddy] Freeing bytes on buddy...\n");
        // Wrap to buddy system
        struct Page *p = pa2page((uintptr_t)ptr - va_pa_offset);
        buddy_free_pages(p, (n / PGSIZE) + ((n % PGSIZE) ? 1 : 0));
        return;
    }

    // find the first free_area_t that can hold the size
    free_area_t *slob_free_area = NULL;
    if (n <= SLOB_SMALL) {
        cprintf("[slob_debug] Freeing small size.\n");
        slob_free_area = &slob_manager.free_slob_small;
    } else if (n <= SLOB_MEDIUM) {
        cprintf("[slob_debug] Freeing medium size.\n");
        slob_free_area = &slob_manager.free_slob_medium;
    } else {
        cprintf("[slob_debug] Freeing large size.\n");
        slob_free_area = &slob_manager.free_slob_large;
    }

    slob_t *slob = (slob_t *)ptr;
    struct Page *page = pa2page((uintptr_t)slob - va_pa_offset);
    uint32_t slob_units = (n + SLOB_UNIT) / SLOB_UNIT - 1 + 2; // Number of slob units needed

    // If this slob makes the page completely free, free the page instead
    if (page->slob_units_left + slob_units == 2048) {
        // cprintf("[slob_debug] The page is completely free, freeing the page instead...\n");
        list_del(&page->slob_link);
        slob_manager.free_slob_small.nr_free--;
        buddy_free_pages(page, 1);
        return;
    }

    slob_t *cur = (slob_t *)page->virtual;
    slob_t *prev = NULL, *next = NULL;


    // if the page is fulled before, then add it to the free list
    if (page->slob_units_left == 0) {
        // cprintf("[slob_debug] The page was full before, adding it to the free list...\n");
        list_add_after(&slob_free_area->free_list, &page->slob_link);
        slob_free_area->nr_free++;
        page->slob_units_left = slob_units;
        page->virtual = (void *)slob;
        slob->slob_units_left = slob_units;
        slob->slob_next_offset = 2048 - slob_units;
        return;
    }

    // cprintf("[slob_debug] The page partially free before, freeing the slob...\n");
    page->slob_units_left += slob_units;

    if ((void *)slob < page->virtual) {
        // cprintf("%lx, %lx", (void *)slob + slob_units * SLOB_UNIT, page->virtual);
        if ((void *)slob + slob_units * SLOB_UNIT == page->virtual) {
            // cprintf("[slob_debug] Merging with next slob...\n");
            slob->slob_units_left += ((slob_t *)page->virtual)->slob_units_left;
            slob->slob_next_offset = ((slob_t *)page->virtual)->slob_next_offset;
            page->virtual = (void *)slob;
        } else {
            // cprintf("[slob_debug] Inserting before the first slob...\n");
            slob->slob_units_left = slob_units;
            slob->slob_next_offset = (uintptr_t)page->virtual - (uintptr_t)slob;
            page->virtual = (void *)slob;
        }
    } else {
        slob_t *prev = page->virtual;
        next = (slob_t *)((uintptr_t)prev + prev->slob_next_offset);
        // find the slob before the slob to be freed and the slob after the slob to be freed
        while (next < slob) {
            prev = next;
            next = (slob_t *)((uintptr_t)next + next->slob_next_offset);
            if (next->slob_next_offset == 0) return;
        }
        if ((void *)prev + prev->slob_units_left * SLOB_UNIT == (void *)slob) {
            // cprintf("[slob_debug] Merging with the previous slob...\n");
            prev->slob_units_left += slob_units;
        } else if ((void *)slob + slob_units * SLOB_UNIT == (void *)next) {
            // cprintf("[slob_debug] Merging with the next slob...\n");
            slob->slob_units_left = next->slob_units_left + slob_units;
            slob->slob_next_offset = next->slob_next_offset + (uintptr_t)next - (uintptr_t)slob; // Merge with the next slob
            prev->slob_next_offset = (uintptr_t)slob - (uintptr_t)prev; // Update the previous slob's offset
        } else {
            // cprintf("[slob_debug] Inserting after the previous slob...\n");
            slob->slob_units_left = slob_units;
            slob->slob_next_offset = (uintptr_t)next - (uintptr_t)slob; // Insert after the previous slob
            prev->slob_next_offset = (uintptr_t)slob - (uintptr_t)prev; // Update the previous slob's offset
        }
    }
    cprintf("[slob_debug] Slob freed.\n");
    return;
}

static void slob_check(void) {
    cprintf("[slob_check]: >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");

    size_t sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    void *ptrs[sizeof(sizes)/sizeof(size_t)];

    cprintf(">>>[slob_check]: Test case 1: Basic allocation.<<<\n");
    // Test case 1: Basic allocation and deallocation
    for (int i = 0; i < sizeof(sizes)/sizeof(size_t); i++) {
        ptrs[i] = slob_alloc_bytes(sizes[i]);
        assert(ptrs[i]);  // Assertion to check if allocation was successful
        cprintf("[slob_test] Successfully allocated %lx bytes at address %p.\n", sizes[i], ptrs[i]);
    }

    cprintf(">>>[slob_check]: Test case 2: Basic deallocation.<<<\n");
    // Test case 2: Basic deallocation
    for (int i = 0; i < sizeof(sizes)/sizeof(size_t); i++) {
        slob_free_bytes(ptrs[i], sizes[i]);
        cprintf("[slob_test] Successfully deallocated memory at address %p.\n", ptrs[i]);
    }

    cprintf(">>>[slob_check]: Test case 3: Allocate and partially deallocate memory blocks.<<<\n");
    // Test case 3: Allocate and partially deallocate memory blocks
    for (int i = 0; i < sizeof(sizes)/sizeof(size_t); i++) {
        ptrs[i] = slob_alloc_bytes(sizes[i]);
        assert(ptrs[i]);
        cprintf("[slob_test] Successfully allocated %lx bytes at address %p.\n", sizes[i], ptrs[i]);
    }

    // Partial deallocation
    for (int i = 0; i < sizeof(sizes)/sizeof(size_t); i += 2) {  // Freeing alternate blocks
        slob_free_bytes(ptrs[i], sizes[i]);
        cprintf("[slob_test] Successfully deallocated memory at address %p.\n", ptrs[i]);
        ptrs[i] = NULL;  // Mark the pointer as NULL after deallocation
    }

    cprintf(">>>[slob_check]: Test case 4: Fragmentation test.<<<\n");
    // Test case 4: Fragmentation test
    // Allocate smaller blocks to check if they occupy the freed spaces efficiently
    for (int i = 0; i < sizeof(sizes)/sizeof(size_t); i += 2) {
        // slob_debug();
        ptrs[i] = slob_alloc_bytes(sizes[i]/2);  // Allocating smaller sizes
        assert(ptrs[i]);
        cprintf("[slob_test] Successfully allocated %lx bytes at address %p.\n", sizes[i]/2, ptrs[i]);
    }

    cprintf(">>>[slob_check]: Test case 5: Complete deallocation.<<<\n");
    // Test case 5: Complete deallocation
    for (int i = 0; i < sizeof(sizes)/sizeof(size_t); i++) {
        if (ptrs[i]) {
            slob_free_bytes(ptrs[i], (i % 2 == 0) ? sizes[i]/2 : sizes[i]);
            cprintf("[slob_test] Successfully deallocated memory at address %p.\n", ptrs[i]);
        }
    }

    cprintf(">>>[slob_check]: Test case 6: Allocate memory blocks larger than a page.<<<\n");
    // Test case 6: Allocate memory blocks larger than a page
    for (int i = 0; i < sizeof(sizes)/sizeof(size_t); i++) {
        size_t large_size = sizes[i] + PGSIZE;  // Sizes larger than a page
        ptrs[i] = slob_alloc_bytes(large_size);
        assert(ptrs[i]);
        cprintf("[slob_test] Successfully allocated %lx bytes at address %p.\n", large_size, ptrs[i]);
        slob_free_bytes(ptrs[i], large_size);
        cprintf("[slob_test] Successfully deallocated memory at address %p.\n", ptrs[i]);
    }

    cprintf("[slob_check]: <<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
}

const struct pmm_manager slob_pmm_manager = {
    .name = "slob_pmm_manager",
    .init = slob_init,
    .init_memmap = slob_init_memmap,
    .alloc_pages = slob_alloc_pages,
    .free_pages = slob_free_pages,
    .nr_free_pages = slob_nr_free_pages,
    .alloc_bytes = slob_alloc_bytes,
    .free_bytes = slob_free_bytes,
    .check = slob_check,
};