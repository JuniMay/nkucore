#include <buddy_pmm.h>
#include <pmm.h>
#include <stdio.h>

static void buddy_init(void) {
    // DO NOTHING
}

static void dbg_buddy() {
    // for (int order = BUDDY_MAX_ORDER - 1; order >= 0; order--) {
    //     // print linked list
    //     list_entry_t *le = &buddy_zone.free_area[order].free_list;

    //     cprintf("[dbg_buddy] list: %016x --> ", le);

    //     while ((le = list_next(le)) != &buddy_zone.free_area[order].free_list) {
    //         cprintf("%016lx --> ", le2page(le, page_link));
    //     }

    //     cprintf("\n");
    // }

    cprintf("[dbg_buddy] block: ");
    for (int order = BUDDY_MAX_ORDER - 1; order >= 0; order--) {
        cprintf("%2d ", buddy_zone.free_area[order].nr_free / (1 << order));
    }
    cprintf("\n");
}

static size_t nearest_order(size_t n) {
    size_t order = 0;
    for (size_t i = 0; i < BUDDY_MAX_ORDER; i++) {
        if (n <= (1 << i)) {
            order = i;
            break;
        }
    }

    return order;
}

static void add_to_free_list(struct Page *page, size_t order) {
    assert(order < BUDDY_MAX_ORDER);

    buddy_zone.free_area[order].nr_free += page->property;

    // cprintf("[add_to_free_list] page: 0x%016lx, order: %d, nr_free: %d\n", page,
    //         order, buddy_zone.free_area[order].nr_free);

    if (list_empty(&buddy_zone.free_area[order].free_list)) {
        // cprintf("[add_to_free_list] list empty\n");
        // initialize the list.
        list_add(&buddy_zone.free_area[order].free_list, &(page->page_link));
    } else {
        // iterator
        list_entry_t *le = &buddy_zone.free_area[order].free_list;

        while ((le = list_next(le)) != &buddy_zone.free_area[order].free_list) {
            // cprintf("[add_to_free_list] list next: %016lx, ", le);
            // cprintf("free list: %016lx\n",
            // &buddy_zone.free_area[order].free_list);

            // get the corresponding page.
            struct Page *p = le2page(le, page_link);
            // keep the list sorted.
            if (page < p) {
                // insert before the current page.
                // cprintf("[add_to_free_list] insert before: %016lx\n", p);
                list_add_before(le, &(page->page_link));
                break;
            } else if (list_next(le) ==
                       &buddy_zone.free_area[order].free_list) {
                // cprintf("[add_to_free_list] insert at the end\n");
                // insert at the end of the list.
                list_add(le, &(page->page_link));
                break;
            }
        }
    }
}

static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    struct Page *p = base;
    for (; p != base + n; p++) {
        // check if the page is reserved.
        assert(PageReserved(p));
        // reset flags.
        p->flags = p->property = 0;
        // reset reference counter.
        set_page_ref(p, 0);
    }

    // cprintf("[buddy_init_memmap] base: 0x%016lx\n", page2pa(base));
    // cprintf("[buddy_init_memmap] n:    0x%016lx\n", n);

    // Construct the zone

    p = base;

    for (int order = BUDDY_MAX_ORDER - 1; order >= 0; order--) {
        list_init(&buddy_zone.free_area[order].free_list);
        buddy_zone.free_area[order].nr_free = 0;

        size_t page_cnt = 1 << order;
        size_t block_cnt = n / page_cnt;

        // cprintf(
        //     "[buddy_init_memmap] initializing order: %2d, page_cnt: %4lu, "
        //     "block_cnt: %3lu\n",
        //     order, page_cnt, block_cnt);

        for (size_t i = 0; i < block_cnt; i++) {
            struct Page *page = p + i * page_cnt;
            page->property = page_cnt;
            SetPageProperty(page);
            add_to_free_list(page, order);
        }

        n -= block_cnt * page_cnt;
        p += block_cnt * page_cnt;
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    int order_min = (int)nearest_order(n);
    int order = order_min;

    while (order < BUDDY_MAX_ORDER) {
        if (!list_empty(&buddy_zone.free_area[order].free_list)) {
            break;
        }
        order++;
    }

    if (order >= BUDDY_MAX_ORDER) {
        return NULL;
    }

    // cprintf("[buddy_alloc_pages] order: %d, order_min: %d\n", order, order_min);

    list_entry_t *le = list_next(&buddy_zone.free_area[order].free_list);

    while (order > order_min) {
        // split the page.
        size_t page_cnt = 1 << (order - 1);

        // remove the page from the free list.
        list_del(le);
        buddy_zone.free_area[order].nr_free -= page_cnt << 1;

        struct Page *lhs_page = le2page(le, page_link);
        struct Page *rhs_page = lhs_page + page_cnt;

        // set the property.
        lhs_page->property = page_cnt;
        rhs_page->property = page_cnt;

        // set the flags. lhs is already set.
        SetPageProperty(rhs_page);

        order--;

        // add to the free list in the smaller order.
        add_to_free_list(lhs_page, order);
        add_to_free_list(rhs_page, order);

        // update the list entry.
        le = list_next(&buddy_zone.free_area[order].free_list);

        assert(le != &buddy_zone.free_area[order].free_list);
    }

    // update the number of free pages.
    struct Page *page = le2page(le, page_link);
    buddy_zone.free_area[order].nr_free -= page->property;

    // set the flags.
    ClearPageProperty(page);

    // remove the page from the free list.
    list_del(le);

    return page;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    int order = (int)nearest_order(n);

    // cprintf("[buddy_free_pages] n: %d, order: %d\n", n, order);

    n = 1 << order;

    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }

    base->property = n;
    SetPageProperty(base);

    struct Page *page = base;

    add_to_free_list(page, order);

    while (order < BUDDY_MAX_ORDER - 1) {
        // check if this can be combined and hoisted.

        list_entry_t *le = list_prev(&(page->page_link));

        if (le != &(buddy_zone.free_area[order].free_list)) {
            struct Page *prev_page = le2page(le, page_link);

            if (prev_page + prev_page->property == page) {
                // combine.
                list_del(le);
                // cprintf("[buddy_free_pages] del: 0x%016lx, order: %d\n",
                //         le2page(le, page_link), order);
                list_del(&(page->page_link));
                // cprintf("[buddy_free_pages] del: 0x%016lx, order: %d\n", page,
                //         order);

                buddy_zone.free_area[order].nr_free -= 1 << (order + 1);
                prev_page->property <<= 1;
                // set the flags.
                ClearPageProperty(page);
                // add prev_page to the free list in the larger order.
                order++;
                page = prev_page;
                add_to_free_list(page, order);
                continue;
            }
        }

        le = list_next(&(page->page_link));

        if (le != &(buddy_zone.free_area[order].free_list)) {
            struct Page *next_page = le2page(le, page_link);

            if (page + page->property == next_page) {
                // combine.
                list_del(le);
                // cprintf("[buddy_free_pages] del: 0x%016lx, order: %d\n",
                //         le2page(le, page_link), order);
                list_del(&(page->page_link));
                // cprintf("[buddy_free_pages] del: 0x%016lx, order: %d\n", page,
                //         order);
                buddy_zone.free_area[order].nr_free -= 1 << (order + 1);
                page->property <<= 1;
                // set the flags.
                ClearPageProperty(next_page);
                // add page to the free list in the larger order.
                order++;
                add_to_free_list(page, order);
                continue;
            }
        }

        break;
    }
}

static size_t buddy_nr_free_pages(void) {
    size_t total_cnt = 0;
    for (size_t i = 0; i < BUDDY_MAX_ORDER; i++) {
        total_cnt += buddy_zone.free_area[i].nr_free;
    }
    return total_cnt;
}

static void buddy_check_0() {

#define ALLOC_PAGE_NUM 1036

    cprintf("[buddy_check_0] >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");

    size_t initial_nr_free_pages = nr_free_pages();

    cprintf("[buddy_check_0] before alloc: ");
    dbg_buddy();


    cprintf("[buddy_check_0] trying to alloc %d * 1 pages\n", ALLOC_PAGE_NUM);

    struct Page *pages[ALLOC_PAGE_NUM];

    for (int i = 0; i < ALLOC_PAGE_NUM; i++) {
        pages[i] = alloc_pages(1);
        assert(pages[i] != NULL);
    }

    assert(nr_free_pages() == initial_nr_free_pages - ALLOC_PAGE_NUM);

    cprintf("[buddy_check_0] after alloc:  ");
    dbg_buddy();

    for (int i = 0; i < ALLOC_PAGE_NUM; i++) {
        free_pages(pages[i], 1);
    }

    assert(nr_free_pages() == initial_nr_free_pages);

    cprintf("[buddy_check_0] after free:   ");
    dbg_buddy();

    cprintf("[buddy_check_0] <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");       
}

static void buddy_check_1() {
    cprintf("[buddy_check_1] >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");

    struct Page* p0 = alloc_pages(512);

    assert(p0 != NULL);
    assert(p0->property == 512);

    cprintf("[buddy_check_1] after alloc 512 pages: ");
    dbg_buddy();

    struct Page* p1 = alloc_pages(513);

    assert(p1 != NULL);
    assert(p1->property == 1024);

    cprintf("[buddy_check_1] after alloc 513 pages: ");
    dbg_buddy();

    struct Page* p2 = alloc_pages(79);

    assert(p2 != NULL);
    assert(p2->property == 128);

    cprintf("[buddy_check_1] after alloc 79 pages:  ");
    dbg_buddy();

    struct Page* p3 = alloc_pages(37);

    assert(p3 != NULL);
    assert(p3->property == 64);

    cprintf("[buddy_check_1] after alloc 37 pages:  ");
    dbg_buddy();

    free_pages(p0, 512);
    free_pages(p2, 79);
    free_pages(p3, 37);
    free_pages(p1, 513);

    cprintf("[buddy_check_1] after free:            ");
    dbg_buddy();

    cprintf("[buddy_check_1] <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
}

static void buddy_check(void) {
    buddy_check_0();
    buddy_check_1();
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};