#include <buddy_pmm.h>
#include <pmm.h>
#include <stdio.h>

static void buddy_init(void) {
    // DO NOTHING
}

static void dbg_buddy() {
    cprintf("[dbg_buddy] order: ");
    for (int order = BUDDY_MAX_ORDER - 1; order >= 0; order--) {
        cprintf("%2d ", order);
    }
    cprintf("\n");
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
    assert(order <= BUDDY_MAX_ORDER);

    buddy_zone.free_area[order].nr_free += page->property;

    if (list_empty(&buddy_zone.free_area[order].free_list)) {
        // initialize the list.
        list_add(&buddy_zone.free_area[order].free_list, &(page->page_link));
    } else {
        // iterator
        list_entry_t *list_entry = &buddy_zone.free_area[order].free_list;

        while ((list_entry = list_next(list_entry)) !=
               &buddy_zone.free_area[order].free_list) {
            // get the corresponding page.
            struct Page *p = le2page(list_entry, page_link);
            // keep the list sorted.
            if (page < p) {
                // insert before the current page.
                list_add_before(list_entry, &(page->page_link));
                break;
            } else if (list_next(list_entry) ==
                       &buddy_zone.free_area[order].free_list) {
                // insert at the end of the list.
                list_add(list_entry, &(page->page_link));
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

    cprintf("[buddy_init_memmap] base: %016lx\n", page2pa(base));
    cprintf("[buddy_init_memmap] n:    %016lx\n", n);

    // Construct the zone

    p = base;

    for (int order = BUDDY_MAX_ORDER - 1; order >= 0; order--) {
        list_init(&buddy_zone.free_area[order].free_list);
        buddy_zone.free_area[order].nr_free = 0;

        size_t page_cnt = 1 << order;
        size_t block_cnt = n / page_cnt;

        cprintf(
            "[buddy_init_memmap] initializing order: %2d, page_cnt: %4lu, "
            "block_cnt: %3lu\n",
            order, page_cnt, block_cnt);

        for (size_t i = 0; i < block_cnt; i++) {
            struct Page *page = p + i * page_cnt;
            page->property = page_cnt;
            SetPageProperty(page);
            add_to_free_list(page, order);
        }

        n -= block_cnt * page_cnt;
        p += block_cnt * page_cnt;
    }

    dbg_buddy();
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

    cprintf("[buddy_alloc_pages] order: %d, order_min: %d\n", order, order_min);

    list_entry_t *list_entry =
        list_next(&buddy_zone.free_area[order].free_list);

    while (order > order_min) {
        // split the page.
        size_t page_cnt = 1 << (order - 1);

        // remove the page from the free list.
        list_del(list_entry);
        buddy_zone.free_area[order].nr_free -= page_cnt << 1;

        struct Page *lhs_page = le2page(list_entry, page_link);
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
        list_entry = list_next(&buddy_zone.free_area[order].free_list);
    }

    // update the number of free pages.
    struct Page *page = le2page(list_entry, page_link);
    buddy_zone.free_area[order].nr_free -= page->property;

    // set the flags.
    ClearPageProperty(page);

    // remove the page from the free list.
    list_del(list_entry);

    return page;
}

static void buddy_free_pages(struct Page *base, size_t n) {

    int order = (int)nearest_order(n);

    cprintf("[buddy_free_pages] n: %d, order: %d\n", n, order);

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

    while (order < BUDDY_MAX_ORDER - 1) {
        // check if this can be combined.

        list_entry_t *list_entry = list_prev(&(page->page_link));

        if (list_entry != &(buddy_zone.free_area[order].free_list)) {
            struct Page *prev_page = le2page(list_entry, page_link);
            if (prev_page + prev_page->property == page) {
                // combine.
                list_del(list_entry);
                buddy_zone.free_area[order].nr_free -= prev_page->property;
                prev_page->property <<= 1;
                // set the flags.
                ClearPageProperty(page);
                // add prev_page to the free list in the larger order.
                order++;
                page = prev_page;
                continue;
            }
        }

        list_entry = list_next(&(page->page_link));

        if (list_entry != &(buddy_zone.free_area[order].free_list)) {
            struct Page *next_page = le2page(list_entry, page_link);
            if (page + page->property == next_page) {
                // combine.
                list_del(list_entry);
                buddy_zone.free_area[order].nr_free -= next_page->property;
                page->property <<= 1;
                // set the flags.
                ClearPageProperty(next_page);
                // add page to the free list in the larger order.
                order++;
                continue;
            }
        }

        break;
    }

    // add to the free list.
    add_to_free_list(page, order);
}

static size_t buddy_nr_free_pages(void) {
    size_t total_cnt = 0;
    for (size_t i = 0; i < BUDDY_MAX_ORDER; i++) {
        total_cnt += buddy_zone.free_area[i].nr_free;
    }
    return total_cnt;
}

static void buddy_check(void) {
    struct Page *p0 = alloc_pages(3);
    dbg_buddy();
    struct Page *p1 = alloc_pages(1024);
    dbg_buddy();
    struct Page *p2 = alloc_pages(513);
    dbg_buddy();
    struct Page *p3 = alloc_pages(65);
    dbg_buddy();

    free_pages(p0, 3);
    dbg_buddy();
    free_pages(p1, 1024);
    dbg_buddy();
    free_pages(p2, 513);
    dbg_buddy();
    free_pages(p3, 65);
    dbg_buddy();
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