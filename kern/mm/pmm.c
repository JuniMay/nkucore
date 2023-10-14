
#include <../sync/sync.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <riscv.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>

#define PMM_FIRST_FIT 0
#define PMM_BEST_FIT 1
#define PMM_BUDDY_SYSTEM 2
#define PMM_SLOB_ALLOCATOR 3

#define PMM_MANAGER PMM_FIRST_FIT

#if PMM_MANAGER == PMM_FIRST_FIT
#include <default_pmm.h>
#elif PMM_MANAGER == PMM_BEST_FIT
#include <best_fit_pmm.h>
#elif PMM_MANAGER == PMM_BUDDY_SYSTEM
#include <buddy_pmm.h>
#elif PMM_MANAGER == PMM_SLOB_ALLOCATOR
#include <buddy_pmm.h>
#include <slob_pmm.h>
#endif

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

#if PMM_MANAGER == PMM_BEST_FIT || PMM_MANAGER == PMM_FIRST_FIT
free_area_t free_area;
#elif PMM_MANAGER == PMM_BUDDY_SYSTEM
buddy_zone_t buddy_zone;
#elif PMM_MANAGER == PMM_SLOB_ALLOCATOR
buddy_zone_t buddy_zone;
slob_manager_t slob_manager;
#endif

// physical memory management
const struct pmm_manager *pmm_manager;

static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {

#if PMM_MANAGER == PMM_FIRST_FIT
    pmm_manager = &default_pmm_manager;
#elif PMM_MANAGER == PMM_BEST_FIT
    pmm_manager = &best_fit_pmm_manager;
#elif PMM_MANAGER == PMM_BUDDY_SYSTEM
    pmm_manager = &buddy_pmm_manager;
#elif PMM_MANAGER == PMM_SLOB_ALLOCATOR
    pmm_manager = &slob_pmm_manager;
#endif

    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    { page = pmm_manager->alloc_pages(n); }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    { pmm_manager->free_pages(base, n); }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    { ret = pmm_manager->nr_free_pages(); }
    local_intr_restore(intr_flag);
    return ret;
}

void *alloc_bytes(size_t n) {
    void *ret = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    { ret = pmm_manager->alloc_bytes(n); }
    local_intr_restore(intr_flag);
    return ret;
}

void free_bytes(void *ptr, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    { pmm_manager->free_bytes(ptr, n); }
    local_intr_restore(intr_flag);
}

static void page_init(void) {
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    uint64_t mem_begin = KERNEL_BEGIN_PADDR;
    uint64_t mem_size = PHYSICAL_MEMORY_END - KERNEL_BEGIN_PADDR;
    uint64_t mem_end = PHYSICAL_MEMORY_END;  // 硬编码取代
                                             // sbi_query_memory()接口

    cprintf("physcial memory map:\n");
    cprintf("  memory: 0x%016lx, [0x%016lx, 0x%016lx].\n", mem_size, mem_begin,
            mem_end - 1);

    uint64_t maxpa = mem_end;

    if (maxpa > KERNTOP) {
        maxpa = KERNTOP;
    }

    cprintf("maxpa: 0x%016lx\n", maxpa);

    extern char end[];

    npage = maxpa / PGSIZE;

    cprintf("npage: 0x%x\n", npage);
    cprintf("nbase: 0x%x\n", nbase);

    // kernel在end[]结束, pages是剩下的页的开始
    pages = (struct Page *)ROUNDUP((void *)end, PGSIZE);

    for (size_t i = 0; i < npage - nbase; i++) {
        // cprintf("setting Page*: 0x%016lx\n", (uint64_t)(pages + i));
        SetPageReserved(pages + i);
    }

    uintptr_t freemem =
        PADDR((uintptr_t)pages + sizeof(struct Page) * (npage - nbase));

    mem_begin = ROUNDUP(freemem, PGSIZE);
    mem_end = ROUNDDOWN(mem_end, PGSIZE);

    cprintf("kern_end:  0x%016lx\n", (uint64_t)PADDR(end));
    cprintf("pages:     0x%016lx\n", (uint64_t)PADDR(pages));
    cprintf("freemem:   0x%016lx\n", freemem);
    cprintf("mem_begin: 0x%016lx\n", mem_begin);
    cprintf("mem_end:   0x%016lx\n", mem_end);

    if (freemem < mem_end) {
        init_memmap(pa2page(mem_begin), (mem_end - mem_begin) / PGSIZE);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other
    // size). So a framework of physical memory manager (struct pmm_manager)is
    // defined in pmm.h First we should init a physical memory manager(pmm)
    // based on the framework. Then pmm can alloc/free the physical memory. Now
    // the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a
    // pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t *)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n",
            satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
