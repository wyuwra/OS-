#include <buddy_pmm.h>
#include <list.h>
#include <stdio.h>
#include <string.h>

/*
 * All coordinates are absolute physical page numbers, never Page* addresses.
 * There is one list for each representable size_t power of two; max_order is
 * computed after page_init has set npage, so no usable tail is lost to a cap.
 *
 * Private descriptor state (public headers and struct Page stay unchanged):
 *   free head: PageProperty == 1, property == order, page_link is on that list;
 *   allocated head: bit 2 set, property == order, flags >> 3 == requested n;
 *   interior: flags == property == 0, page_link is self-linked.
 * Bit 0/1 retain their standard reserved/free-head meanings. Encoding n in
 * flags leaves 61 bits, more than a 64-bit physical address with 4KiB pages
 * can need. alloc_pages still checks the encoding bound before shifting.
 * Only heads have allocation records. Partial frees and a rounded n supplied
 * instead of the original n are forbidden. Like the other Lab2 managers,
 * callers must serialize access; this module does not add an SMP lock.
 */
#define BUDDY_ORDERS (sizeof(size_t) * 8)
#define BUDDY_ALLOCATED (1ULL << 2)
#define BUDDY_REQUEST_SHIFT 3

static list_entry_t buddy_free_lists[BUDDY_ORDERS];
static size_t buddy_nr_free;
static unsigned int buddy_max_order;

static size_t
buddy_order_size(unsigned int order) {
    assert(order < BUDDY_ORDERS);
    return (size_t)1 << order;
}

/* Check the whole extent before turning a candidate PPN into a descriptor. */
static bool
buddy_valid_block(ppn_t ppn, unsigned int order) {
    if (order >= BUDDY_ORDERS || ppn < nbase || ppn >= npage) {
        return 0;
    }
    size_t count = buddy_order_size(order);
    return count <= npage - ppn && (ppn & (count - 1)) == 0;
}

/* Validate a caller's descriptor address before pointer subtraction/access. */
static ppn_t
buddy_base_ppn(struct Page *base) {
    assert(pages != NULL && npage > nbase);
    uintptr_t address = (uintptr_t)base, start = (uintptr_t)pages;
    assert(address >= start);
    uintptr_t offset = address - start;
    assert(offset % sizeof(struct Page) == 0);
    size_t index = offset / sizeof(struct Page);
    assert(index < npage - nbase);
    return nbase + index;
}

/* These helpers move blocks only; accounting belongs to their callers. */
static void
buddy_insert(struct Page *base, unsigned int order) {
    assert(buddy_valid_block(page2ppn(base), order));
    assert(base->flags == 0 && base->property == 0);
    base->property = order;
    SetPageProperty(base);
    list_add(&buddy_free_lists[order], &base->page_link);
}

static void
buddy_remove(struct Page *base) {
    assert(PageProperty(base));
    list_del_init(&base->page_link);
    ClearPageProperty(base);
    base->property = 0;
}

/* base is not on a list yet. A free head certifies an entirely delivered block,
 * so an undelivered/reserved hole can never participate in a merge. */
static void
buddy_merge_insert(struct Page *base, unsigned int order) {
    ppn_t ppn = page2ppn(base);
    assert(buddy_valid_block(ppn, order));
    while (order < buddy_max_order) {
        ppn_t other_ppn = ppn ^ buddy_order_size(order);
        if (!buddy_valid_block(other_ppn, order)) {
            break;
        }
        struct Page *other = &pages[other_ppn - nbase];
        if (PageReserved(other) || !PageProperty(other) ||
            other->property != order) {
            break;
        }
        buddy_remove(other);
        if (other_ppn < ppn) {
            ppn = other_ppn;
            base = other;
        }
        order++;
    }
    buddy_insert(base, order);
}

static void
buddy_init(void) {
    for (unsigned int order = 0; order < BUDDY_ORDERS; order++) {
        list_init(&buddy_free_lists[order]);
    }
    buddy_nr_free = 0;
    buddy_max_order = 0;
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    ppn_t ppn = buddy_base_ppn(base);
    assert(n <= npage - ppn);
    assert(buddy_nr_free <= npage - nbase - n);
    /* Validate the entire handoff first, including overlap with earlier calls. */
    for (size_t i = 0; i < n; i++) {
        assert(PageReserved(base + i));
    }
    buddy_max_order = 0;
    for (size_t count = npage - nbase; count > 1; count >>= 1) {
        buddy_max_order++;
    }
    for (size_t i = 0; i < n; i++) {
        struct Page *p = base + i;
        p->flags = p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
    }

    size_t remaining = n;
    while (remaining != 0) {
        unsigned int order = 0;
        /* No additions for rounding, and no shift by the word width. */
        while (order < buddy_max_order) {
            size_t next = buddy_order_size(order + 1);
            if (next > remaining || (ppn & (next - 1)) != 0) {
                break;
            }
            order++;
        }
        size_t count = buddy_order_size(order);
        buddy_merge_insert(&pages[ppn - nbase], order);
        ppn += count;
        remaining -= count;
    }
    buddy_nr_free += n;
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > buddy_nr_free || n > (~(uint64_t)0 >> BUDDY_REQUEST_SHIFT)) {
        return NULL;
    }
    size_t count = 1;
    unsigned int order = 0;
    while (count < n) {
        if (order == buddy_max_order) {
            return NULL;
        }
        count <<= 1;
        order++;
    }
    if (count > buddy_nr_free) {
        return NULL;
    }
    unsigned int found = order;
    while (found <= buddy_max_order && list_empty(&buddy_free_lists[found])) {
        found++;
    }
    if (found > buddy_max_order) {
        return NULL;
    }

    struct Page *base = le2page(list_next(&buddy_free_lists[found]), page_link);
    buddy_remove(base);
    while (found > order) {
        found--;
        /* Keep the lower half and register the upper half at each step. */
        buddy_insert(base + buddy_order_size(found), found);
    }
    base->property = order;
    base->flags = ((uint64_t)n << BUDDY_REQUEST_SHIFT) | BUDDY_ALLOCATED;
    buddy_nr_free -= count;
    return base;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    ppn_t ppn = buddy_base_ppn(base);
    /* Whole-block release: exactly the returned head and original request. */
    assert(!PageReserved(base) && !PageProperty(base));
    assert((base->flags & BUDDY_ALLOCATED) != 0);
    assert((base->flags >> BUDDY_REQUEST_SHIFT) == n);
    unsigned int order = base->property;
    assert(order <= buddy_max_order && buddy_valid_block(ppn, order));
    size_t count = buddy_order_size(order);
    assert(n <= count && (order == 0 || n > (count >> 1)));
    assert(buddy_nr_free <= npage - nbase - count);
    for (size_t i = 0; i < count; i++) {
        struct Page *p = base + i;
        assert(!PageReserved(p) && !PageProperty(p));
        assert(i == 0 || (p->flags == 0 && p->property == 0));
    }
    for (size_t i = 0; i < count; i++) {
        struct Page *p = base + i;
        p->flags = p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
    }
    buddy_merge_insert(base, order);
    buddy_nr_free += count;
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_nr_free;
}

/*
 * Live-kernel tests. Borrow a real, aligned 128-page allocation, use its lower
 * 64 pages as the arena and reserve the upper 64 as a coalescing guard. Even
 * though init_memmap computes the real maximum order, a valid arena block
 * cannot merge into the detached real allocator. No fake Page array, npage
 * change, or alternate allocator is used.
 *
 * The oracle consists of delivered/owner maps and literal block sizes. Its
 * rounding uses multiplication and its buddy checks use parent division,
 * independently of buddy_order_size/buddy_valid_block and the production XOR.
 * Large snapshots live in bounded static storage, not the 8KiB kernel stack.
 */
#define BT_PAGES 64
#define BT_LOAN 128
#define BT_RECORDS 64
#define BT_REAL_PAGES ((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE)

struct buddy_test_allocation {
    struct Page *page;
    size_t requested, count;
};

static struct Page *bt_arena;
static struct Page bt_loan_save[BT_LOAN], bt_failure_pages[BT_LOAN];
static struct Page bt_canary_save[4];
static list_entry_t bt_saved_lists[BUDDY_ORDERS];
/* Failure snapshots of heads are compared only, never used to restore lists. */
static list_entry_t bt_failure_lists[BUDDY_ORDERS];
static struct buddy_test_allocation bt_records[BT_RECORDS];
static uint8_t bt_delivered[BT_PAGES], bt_seen[BT_PAGES], bt_heads[BT_PAGES];
static unsigned int bt_owner[BT_PAGES], bt_histogram[7];
/* 0 = not free; 1..64 = free head's order+1; 255 = free interior. */
static uint8_t bt_real_layout[BT_REAL_PAGES];
static uint8_t bt_real_seen[(BT_REAL_PAGES + 7) / 8];
static const size_t bt_sizes[7] = {1, 2, 4, 8, 16, 32, 64};
static size_t bt_operations, bt_structure_checks;

/* Relocate a circular list, including both endpoint references to its sentinel.
 * Copying just *to = *from would leave nodes pointing at the old sentinel. */
static void
bt_move_list(list_entry_t *from, list_entry_t *to) {
    assert(list_empty(to));
    if (!list_empty(from)) {
        to->next = from->next;
        to->prev = from->prev;
        to->next->prev = to;
        to->prev->next = to;
        list_init(from);
    }
}

/* Validate an intrusive node before dereferencing it as a Page. */
static struct Page *
bt_node_page(list_entry_t *node, struct Page *origin, size_t capacity) {
    uintptr_t first = (uintptr_t)&origin->page_link;
    uintptr_t address = (uintptr_t)node;
    assert(address >= first);
    size_t delta = address - first;
    assert(delta % sizeof(struct Page) == 0);
    assert(delta / sizeof(struct Page) < capacity);
    return origin + delta / sizeof(struct Page);
}

static size_t
bt_index(struct Page *page) {
    uintptr_t first = (uintptr_t)bt_arena, address = (uintptr_t)page;
    assert(address >= first && (address - first) % sizeof(struct Page) == 0);
    size_t index = (address - first) / sizeof(struct Page);
    assert(index < BT_PAGES);
    return index;
}

/* Zero denotes unrepresentable rounding. No production helper is called. */
static size_t
bt_rounded(size_t n) {
    assert(n > 0);
    size_t result = 1;
    while (result < n) {
        if (result > (size_t)-1 / 2) {
            return 0;
        }
        result *= 2;
    }
    return result;
}

static unsigned int
bt_order(size_t count) {
    unsigned int order = 0;
    while (count > 1) {
        assert(count % 2 == 0);
        count /= 2;
        order++;
    }
    return order;
}

static size_t
bt_expected_free(void) {
    size_t count = 0;
    for (size_t i = 0; i < BT_PAGES; i++) {
        count += bt_delivered[i] && bt_owner[i] == 0;
    }
    return count;
}

/* Existence of an aligned free run is determined solely by the page oracle. */
static bool
bt_can_allocate(size_t n) {
    size_t count = bt_rounded(n);
    if (count == 0 || count > BT_PAGES) {
        return 0;
    }
    for (size_t i = 0; i <= BT_PAGES - count; i++) {
        if ((page2ppn(bt_arena) + i) % count != 0) {
            continue;
        }
        size_t j = 0;
        while (j < count && bt_delivered[i + j] && bt_owner[i + j] == 0) {
            j++;
        }
        if (j == count) {
            return 1;
        }
    }
    return 0;
}

/* Audit all sandbox descriptors, allocations and lists after each operation. */
static void
bt_verify(void) {
    memset(bt_seen, 0, sizeof(bt_seen));
    memset(bt_heads, 0, sizeof(bt_heads));
    memset(bt_histogram, 0, sizeof(bt_histogram));
    size_t total = 0, nodes = 0;
    for (unsigned int order = 0; order < BUDDY_ORDERS; order++) {
        list_entry_t *head = &buddy_free_lists[order], *previous = head;
        list_entry_t *node = head->next;
        while (node != head) {
            assert(++nodes <= BT_PAGES && order < 7);
            struct Page *page = bt_node_page(node, bt_arena, BT_PAGES);
            size_t index = bt_index(page), count = bt_sizes[order];
            assert(node->prev == previous);
            assert(page->flags == (1ULL << PG_property));
            assert(page->property == order && page_ref(page) == 0);
            assert(page2ppn(page) % count == 0 && count <= BT_PAGES - index);
            assert(!bt_heads[index]);
            bt_heads[index] = order + 1;
            bt_histogram[order]++;
            for (size_t j = index; j < index + count; j++) {
                assert(bt_delivered[j] && bt_owner[j] == 0 && !bt_seen[j]);
                bt_seen[j] = 1;
            }
            total += count;
            previous = node;
            node = node->next;
        }
        assert(head->prev == previous);
    }
    assert(total == bt_expected_free() && total == nr_free_pages());
    assert(total == buddy_nr_free);
    for (size_t i = 0; i < BT_PAGES; i++) {
        struct Page *page = bt_arena + i;
        assert(page_ref(page) == 0);
        if (!bt_delivered[i]) {
            assert(bt_owner[i] == 0 && !bt_seen[i]);
            assert(page->flags == (1ULL << PG_reserved) && page->property == 0);
        } else if (bt_owner[i] != 0) {
            unsigned int id = bt_owner[i] - 1;
            assert(id < BT_RECORDS && bt_records[id].page != NULL);
            struct buddy_test_allocation *record = &bt_records[id];
            size_t start = bt_index(record->page);
            assert(i >= start && i - start < record->count && !bt_seen[i]);
            if (i == start) {
                assert(page->flags == ((uint64_t)record->requested << 3 | 4));
                assert(page->property == bt_order(record->count));
            } else {
                assert(page->flags == 0 && page->property == 0);
            }
        } else {
            assert(bt_seen[i]);
            if (bt_heads[i]) {
                unsigned int order = bt_heads[i] - 1;
                size_t count = bt_sizes[order], ppn = page2ppn(page);
                /* Independent partner derivation from the parent interval. */
                size_t parent = ppn / (2 * count) * (2 * count);
                size_t other = ppn == parent ? ppn + count : ppn - count;
                size_t arena_ppn = page2ppn(bt_arena);
                if (other >= arena_ppn && other - arena_ppn < BT_PAGES) {
                    assert(bt_heads[other - arena_ppn] != order + 1);
                }
            } else {
                assert(page->flags == 0 && page->property == 0);
            }
        }
        if (!bt_heads[i]) {
            assert(page->page_link.next == &page->page_link);
            assert(page->page_link.prev == &page->page_link);
        }
    }
    for (size_t id = 0; id < BT_RECORDS; id++) {
        struct buddy_test_allocation *record = &bt_records[id];
        if (record->page != NULL) {
            size_t start = bt_index(record->page);
            assert(record->count == bt_rounded(record->requested));
            assert(record->count <= BT_PAGES - start);
            for (size_t j = 0; j < record->count; j++) {
                assert(bt_owner[start + j] == id + 1);
            }
        }
    }
    /* The sibling guard prevents the arena's largest block from merging out. */
    for (size_t i = BT_PAGES; i < BT_LOAN; i++) {
        struct Page *page = bt_arena + i;
        assert(page->flags == (1ULL << PG_reserved) && page->property == 0);
        assert(page_ref(page) == 0 && page->page_link.next == &page->page_link);
        assert(page->page_link.prev == &page->page_link);
    }
    bt_structure_checks++;
}

static void
bt_expect_block(size_t index, unsigned int order) {
    assert(index < BT_PAGES && order < 7);
    assert(bt_heads[index] == order + 1);
}

static void
bt_expect_histogram(unsigned int a, unsigned int b, unsigned int c,
                    unsigned int d, unsigned int e, unsigned int f,
                    unsigned int g) {
    const unsigned int expected[7] = {a, b, c, d, e, f, g};
    for (size_t i = 0; i < 7; i++) {
        assert(bt_histogram[i] == expected[i]);
    }
}

static void
bt_reset(void) {
    for (size_t i = 0; i < BT_RECORDS; i++) {
        assert(bt_records[i].page == NULL);
    }
    buddy_init();
    memset(bt_delivered, 0, sizeof(bt_delivered));
    memset(bt_owner, 0, sizeof(bt_owner));
    memset(bt_records, 0, sizeof(bt_records));
    for (size_t i = 0; i < BT_LOAN; i++) {
        struct Page *page = bt_arena + i;
        page->flags = 1ULL << PG_reserved;
        page->property = 0;
        set_page_ref(page, 0);
        list_init(&page->page_link);
    }
    bt_verify();
}

static void
bt_deliver(size_t index, size_t count) {
    assert(count > 0 && index < BT_PAGES && count <= BT_PAGES - index);
    for (size_t i = index; i < index + count; i++) {
        assert(!bt_delivered[i]);
    }
    pmm_manager->init_memmap(bt_arena + index, count);
    for (size_t i = index; i < index + count; i++) {
        bt_delivered[i] = 1;
    }
    bt_operations++;
    bt_verify();
}

static unsigned int
bt_allocate(size_t request, size_t expected_count) {
    assert(bt_rounded(request) == expected_count && bt_can_allocate(request));
    unsigned int id = 0;
    while (id < BT_RECORDS && bt_records[id].page != NULL) {
        id++;
    }
    assert(id < BT_RECORDS);
    size_t before = bt_expected_free();
    struct Page *page = alloc_pages(request);
    assert(page != NULL);
    size_t index = bt_index(page);
    assert(expected_count <= BT_PAGES - index);
    assert(page2ppn(page) % expected_count == 0);
    for (size_t i = index; i < index + expected_count; i++) {
        assert(bt_delivered[i] && bt_owner[i] == 0);
        bt_owner[i] = id + 1;
    }
    bt_records[id].page = page;
    bt_records[id].requested = request;
    bt_records[id].count = expected_count;
    assert(nr_free_pages() == before - expected_count);
    bt_operations++;
    bt_verify();
    return id;
}

static void
bt_release(unsigned int id) {
    assert(id < BT_RECORDS && bt_records[id].page != NULL);
    struct buddy_test_allocation record = bt_records[id];
    size_t before = bt_expected_free(), index = bt_index(record.page);
    free_pages(record.page, record.requested);
    for (size_t i = index; i < index + record.count; i++) {
        assert(bt_owner[i] == id + 1);
        bt_owner[i] = 0;
    }
    bt_records[id].page = NULL;
    assert(nr_free_pages() == before + record.count);
    bt_operations++;
    bt_verify();
}

static void
bt_release_all(void) {
    for (unsigned int id = 0; id < BT_RECORDS; id++) {
        if (bt_records[id].page != NULL) {
            bt_release(id);
        }
    }
}

/* Compare every descriptor field and link: allocation failure must be atomic. */
static void
bt_same_pages(struct Page *actual, const struct Page *expected, size_t count) {
    for (size_t i = 0; i < count; i++) {
        assert(actual[i].flags == expected[i].flags);
        assert(actual[i].property == expected[i].property);
        assert(actual[i].ref == expected[i].ref);
        assert(actual[i].page_link.next == expected[i].page_link.next);
        assert(actual[i].page_link.prev == expected[i].page_link.prev);
    }
}

static void
bt_expect_failure(size_t request) {
    assert(!bt_can_allocate(request));
    memcpy(bt_failure_pages, bt_arena, sizeof(bt_failure_pages));
    memcpy(bt_failure_lists, buddy_free_lists, sizeof(bt_failure_lists));
    size_t before = nr_free_pages();
    unsigned int maximum = buddy_max_order;
    assert(alloc_pages(request) == NULL);
    assert(nr_free_pages() == before && buddy_max_order == maximum);
    bt_same_pages(bt_arena, bt_failure_pages, BT_LOAN);
    for (unsigned int i = 0; i < BUDDY_ORDERS; i++) {
        assert(buddy_free_lists[i].next == bt_failure_lists[i].next);
        assert(buddy_free_lists[i].prev == bt_failure_lists[i].prev);
    }
    bt_operations++;
    bt_verify();
}

/* Exact global free partition before borrowing and after returning all loans.
 * One byte per real descriptor plus a coverage bitmap, both statically bounded
 * by this kernel's physical-memory limit; no giant stack snapshot is needed. */
static void
bt_global_audit(bool save) {
    size_t capacity = npage - nbase, total = 0, nodes = 0;
    assert(capacity <= BT_REAL_PAGES);
    memset(bt_real_seen, 0, sizeof(bt_real_seen));
    if (save) {
        memset(bt_real_layout, 0, sizeof(bt_real_layout));
    }
    for (unsigned int order = 0; order < BUDDY_ORDERS; order++) {
        list_entry_t *head = &buddy_free_lists[order], *previous = head;
        list_entry_t *node = head->next;
        while (node != head) {
            assert(++nodes <= capacity);
            struct Page *page = bt_node_page(node, pages, capacity);
            size_t index = page - pages, count = (size_t)1 << order;
            assert(node->prev == previous && page->property == order);
            assert(page->flags == (1ULL << PG_property));
            assert(count <= capacity - index && page2ppn(page) % count == 0);
            for (size_t j = 0; j < count; j++) {
                size_t at = index + j;
                uint8_t bit = 1U << (at % 8);
                assert(!(bt_real_seen[at / 8] & bit));
                bt_real_seen[at / 8] |= bit;
                uint8_t marker = j == 0 ? order + 1 : 255;
                if (save) {
                    bt_real_layout[at] = marker;
                } else {
                    assert(bt_real_layout[at] == marker);
                }
                assert(page[j].ref == 0);
                if (j != 0) {
                    assert(page[j].flags == 0 && page[j].property == 0);
                    assert(page[j].page_link.next == &page[j].page_link);
                    assert(page[j].page_link.prev == &page[j].page_link);
                }
            }
            /* An aligned parent can be checked without XOR or addition overflow. */
            if (order + 1 < BUDDY_ORDERS) {
                size_t ppn = page2ppn(page), parent_size = count * 2;
                size_t parent = ppn / parent_size * parent_size;
                size_t other = ppn == parent ? ppn + count : ppn - count;
                if (other >= nbase && other < npage && count <= npage - other) {
                    struct Page *partner = pages + (other - nbase);
                    assert(!PageProperty(partner) || partner->property != order);
                }
            }
            total += count;
            previous = node;
            node = node->next;
        }
        assert(head->prev == previous);
    }
    assert(total == nr_free_pages());
    if (!save) {
        for (size_t i = 0; i < capacity; i++) {
            assert(!!(bt_real_seen[i / 8] & (1U << (i % 8))) ==
                   (bt_real_layout[i] != 0));
        }
    }
    bt_structure_checks++;
}

static unsigned int
bt_id_at(size_t index) {
    for (unsigned int id = 0; id < BT_RECORDS; id++) {
        if (bt_records[id].page == bt_arena + index) {
            return id;
        }
    }
    panic("buddy test: missing allocation at arena offset %lu", (unsigned long)index);
}

static void __noinline
bt_initialization(void) {
    cprintf("[buddy 1] initialization and region boundaries: BEGIN\n");
    bt_reset();
    bt_deliver(5, 1);
    bt_expect_block(5, 0);
    unsigned int single = bt_allocate(1, 1);
    assert(bt_records[single].page == bt_arena + 5); /* Only legal page. */
    bt_expect_failure(1);
    bt_release(single);

    /* [3,16) = [3,4) + [4,8) + [8,16): exactly 1+4+8 pages. */
    bt_reset();
    bt_deliver(3, 13);
    bt_expect_histogram(1, 0, 1, 1, 0, 0, 0);
    bt_expect_block(3, 0);
    bt_expect_block(4, 2);
    bt_expect_block(8, 3);
    for (size_t i = 0; i < 13; i++) {
        bt_allocate(1, 1);
    }
    assert(nr_free_pages() == 0);
    bt_expect_failure(1);
    bt_release_all();
    bt_expect_histogram(1, 0, 1, 1, 0, 0, 0);

    /* A nonaligned end also leaves a one-page tail: [3,13) = 1+4+4+1. */
    bt_reset();
    bt_deliver(3, 10);
    bt_expect_histogram(2, 0, 2, 0, 0, 0, 0);
    bt_expect_block(3, 0);
    bt_expect_block(4, 2);
    bt_expect_block(8, 2);
    bt_expect_block(12, 0);
    for (size_t i = 0; i < 10; i++) {
        bt_allocate(1, 1);
    }
    bt_expect_failure(1);
    bt_release_all();
    bt_expect_histogram(2, 0, 2, 0, 0, 0, 0);

    /* The undelivered [8,16) hole forbids a 16-page block despite 16 free. */
    bt_reset();
    bt_deliver(0, 8);
    bt_deliver(16, 8);
    bt_expect_histogram(0, 0, 0, 2, 0, 0, 0);
    bt_expect_block(0, 3);
    bt_expect_block(16, 3);
    bt_expect_failure(16);
    bt_allocate(8, 8);
    bt_allocate(8, 8);
    bt_expect_failure(1);
    bt_release_all();

    /* Adjacent handoffs must coalesce, in both address-delivery orders. */
    bt_reset();
    bt_deliver(0, 4);
    bt_deliver(4, 4);
    bt_expect_block(0, 3);
    bt_deliver(8, 8);
    bt_expect_histogram(0, 0, 0, 0, 1, 0, 0);
    bt_expect_block(0, 4);
    bt_reset();
    bt_deliver(8, 8);
    bt_deliver(0, 8);
    bt_expect_histogram(0, 0, 0, 0, 1, 0, 0);
    unsigned int whole = bt_allocate(16, 16);
    assert(bt_records[whole].page == bt_arena);
    bt_release(whole);
    cprintf("[buddy 1] initialization and region boundaries: PASS\n");
}

static void __noinline
bt_rounding(void) {
    cprintf("[buddy 2] powers of two, rounding and non-overlap: BEGIN\n");
    static const size_t requests[] = {1, 2, 4, 8, 3, 5};
    static const size_t occupied[] = {1, 2, 4, 8, 4, 8};
    unsigned int ids[6];
    bt_reset();
    bt_deliver(0, 64);
    for (size_t i = 0; i < 6; i++) {
        ids[i] = bt_allocate(requests[i], occupied[i]);
    }
    assert(nr_free_pages() == 37); /* 64 - (1+2+4+8+4+8). */
    for (size_t i = 6; i > 0; i--) {
        bt_release(ids[i - 1]);
    }
    bt_expect_histogram(0, 0, 0, 0, 0, 0, 1);
    bt_expect_block(0, 6);
    cprintf("[buddy 2] powers of two, rounding and non-overlap: PASS\n");
}

static void __noinline
bt_splitting(void) {
    cprintf("[buddy 3] multilevel splitting and reuse of every half: BEGIN\n");
    bt_reset();
    bt_deliver(0, 32);
    bt_allocate(1, 1);
    /* 32 -> 16+8+4+2+1 free, plus one allocated page, regardless of halves. */
    assert(nr_free_pages() == 31);
    bt_expect_histogram(1, 1, 1, 1, 1, 0, 0);
    bt_allocate(1, 1);
    bt_expect_histogram(0, 1, 1, 1, 1, 0, 0);
    bt_allocate(2, 2);
    bt_expect_histogram(0, 0, 1, 1, 1, 0, 0);
    bt_allocate(4, 4);
    bt_expect_histogram(0, 0, 0, 1, 1, 0, 0);
    bt_allocate(8, 8);
    bt_expect_histogram(0, 0, 0, 0, 1, 0, 0);
    bt_allocate(16, 16);
    bt_expect_histogram(0, 0, 0, 0, 0, 0, 0);
    bt_expect_failure(1);
    bt_release_all();
    bt_expect_histogram(0, 0, 0, 0, 0, 1, 0);
    unsigned int whole = bt_allocate(32, 32);
    assert(bt_records[whole].page == bt_arena);
    bt_release(whole);
    cprintf("[buddy 3] multilevel splitting and reuse of every half: PASS\n");
}

static void __noinline
bt_merging(void) {
    cprintf("[buddy 4] buddy pairs and recursive merging in three orders: BEGIN\n");
    bt_reset();
    bt_deliver(0, 8);
    unsigned int first = bt_allocate(4, 4), second = bt_allocate(4, 4);
    bt_release(first);
    bt_expect_histogram(0, 0, 1, 0, 0, 0, 0);
    bt_release(second);
    bt_expect_histogram(0, 0, 0, 1, 0, 0, 0);
    unsigned int whole = bt_allocate(8, 8);
    assert(bt_records[whole].page == bt_arena);
    bt_release(whole);

    static const unsigned int orders[3][16] = {
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
        {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
        {7, 0, 15, 3, 10, 2, 13, 5, 8, 1, 14, 6, 11, 4, 12, 9},
    };
    for (size_t pass = 0; pass < 3; pass++) {
        bt_reset();
        bt_deliver(0, 16);
        for (size_t i = 0; i < 16; i++) {
            assert(bt_allocate(1, 1) == i); /* Record ID, not physical address. */
        }
        bt_expect_failure(1);
        for (size_t i = 0; i < 16; i++) {
            bt_release(orders[pass][i]);
        }
        bt_expect_histogram(0, 0, 0, 0, 1, 0, 0);
        whole = bt_allocate(16, 16);
        assert(bt_records[whole].page == bt_arena);
        bt_release(whole);
    }
    cprintf("[buddy 4] buddy pairs and recursive merging in three orders: PASS\n");
}

static void __noinline
bt_adjacent_nonbuddies(void) {
    cprintf("[buddy 5] adjacent equal-order blocks that are not buddies: BEGIN\n");
    bt_reset();
    bt_deliver(0, 8);
    for (size_t i = 0; i < 4; i++) {
        bt_allocate(2, 2);
    }
    /* The four allocations must tile [0,8); their return order is irrelevant.
     * [2,4) and [4,6) touch but have different aligned 4-page parents. */
    bt_release(bt_id_at(2));
    bt_release(bt_id_at(4));
    assert(nr_free_pages() == 4);
    bt_expect_histogram(0, 2, 0, 0, 0, 0, 0);
    bt_expect_block(2, 1);
    bt_expect_block(4, 1);
    bt_expect_failure(4);
    bt_release_all();
    unsigned int whole = bt_allocate(8, 8);
    assert(bt_records[whole].page == bt_arena);
    bt_release(whole);
    cprintf("[buddy 5] adjacent equal-order blocks that are not buddies: PASS\n");
}

static void __noinline
bt_boundaries(void) {
    cprintf("[buddy 6] unequal orders, holes and allocated barriers: BEGIN\n");
    bt_reset();
    bt_deliver(0, 4);
    bt_deliver(4, 2);
    bt_expect_histogram(0, 1, 1, 0, 0, 0, 0);
    bt_expect_block(0, 2);
    bt_expect_block(4, 1);
    bt_expect_failure(5); /* n <= 6 free, but round_up(n) == 8 > 6. */
    bt_expect_failure(8);
    bt_allocate(4, 4);
    bt_allocate(2, 2);
    bt_release_all();
    bt_expect_histogram(0, 1, 1, 0, 0, 0, 0);

    bt_reset();
    bt_deliver(0, 4);
    bt_deliver(8, 8);
    bt_expect_histogram(0, 0, 1, 1, 0, 0, 0);
    bt_expect_failure(9);
    bt_allocate(8, 8);
    bt_allocate(4, 4);
    bt_release_all();

    bt_reset();
    bt_deliver(0, 16);
    for (size_t i = 0; i < 4; i++) {
        bt_allocate(4, 4);
    }
    unsigned int barrier = bt_id_at(4);
    bt_release(bt_id_at(0));
    bt_release(bt_id_at(8));
    bt_release(bt_id_at(12));
    assert(nr_free_pages() == 12);
    bt_expect_histogram(0, 0, 1, 1, 0, 0, 0);
    bt_expect_failure(9);
    bt_expect_failure(16);
    bt_release(barrier);
    bt_expect_histogram(0, 0, 0, 0, 1, 0, 0);
    unsigned int whole = bt_allocate(16, 16);
    assert(bt_records[whole].page == bt_arena);
    bt_release(whole);
    cprintf("[buddy 6] unequal orders, holes and allocated barriers: PASS\n");
}

static void __noinline
bt_failures(void) {
    cprintf("[buddy 7] exhaustion, huge requests and failure atomicity: BEGIN\n");
    bt_reset();
    bt_deliver(0, 8);
    bt_expect_failure(9);
    bt_expect_failure(16);
    bt_expect_failure(65);
    bt_expect_failure((size_t)-1);
    size_t high_bit = (size_t)1 << (sizeof(size_t) * 8 - 1);
    assert(bt_rounded(high_bit + 1) == 0);
    bt_expect_failure(high_bit);
    bt_expect_failure(high_bit + 1);
    /* Derive the real maximum block independently, using division. */
    size_t span = npage - nbase, largest = 1;
    while (span > 1) {
        span /= 2;
        largest *= 2;
    }
    assert(largest < (size_t)-1);
    bt_expect_failure(largest);
    bt_expect_failure(largest + 1);
    bt_allocate(8, 8);
    bt_expect_failure(1);
    bt_release_all();

    /* Snapshot live allocations too: a failure must preserve their metadata. */
    bt_reset();
    bt_deliver(0, 16);
    bt_allocate(3, 4);
    bt_allocate(5, 8);
    bt_expect_failure(5);
    bt_expect_failure(17);
    bt_expect_failure((size_t)-1);
    bt_allocate(4, 4);
    bt_expect_failure(1);
    bt_release_all();
    bt_expect_histogram(0, 0, 0, 0, 1, 0, 0);
    cprintf("[buddy 7] exhaustion, huge requests and failure atomicity: PASS\n");
}

static void __noinline
bt_mixed(void) {
    cprintf("[buddy 8] reproducible mixed sequence and complete recovery: BEGIN\n");
    static const size_t requests[8] = {3, 1, 5, 2, 7, 4, 1, 9};
    static const size_t occupied[8] = {4, 1, 8, 2, 8, 4, 1, 16};
    unsigned int ids[8];
    bt_reset();
    bt_deliver(0, 64);
    for (size_t i = 0; i < 8; i++) {
        ids[i] = bt_allocate(requests[i], occupied[i]);
    }
    assert(nr_free_pages() == 20); /* 64 - 44 occupied pages. */
    bt_release(ids[2]);
    bt_release(ids[0]);
    bt_release(ids[6]);
    assert(nr_free_pages() == 33); /* Recovered 8+4+1. */
    unsigned int extra_a = bt_allocate(6, 8), extra_b = bt_allocate(3, 4);
    bt_release(ids[7]);
    bt_release(ids[1]);
    bt_release(ids[4]);
    bt_release(ids[3]);
    bt_release(ids[5]);
    bt_release(extra_b);
    bt_release(extra_a);
    bt_expect_histogram(0, 0, 0, 0, 0, 0, 1);
    bt_expect_block(0, 6);
    unsigned int whole = bt_allocate(64, 64);
    assert(bt_records[whole].page == bt_arena);
    bt_release(whole);
    unsigned int three = bt_allocate(3, 4), five = bt_allocate(5, 8);
    bt_release(three);
    bt_release(five);
    assert(nr_free_pages() == 64);
    bt_expect_histogram(0, 0, 0, 0, 0, 0, 1);
    cprintf("[buddy 8] reproducible mixed sequence and complete recovery: PASS\n");
}

static void
buddy_check(void) {
    assert(pmm_manager == &buddy_pmm_manager);
    bt_operations = bt_structure_checks = 0;
    cprintf("buddy_check: global setup BEGIN\n");
    size_t original_free = nr_free_pages();
    bt_global_audit(1);

    /* Keep an unrelated live allocation with nonzero references throughout. */
    struct Page *canary = alloc_pages(3);
    assert(canary != NULL && page2ppn(canary) % 4 == 0);
    assert(nr_free_pages() == original_free - 4);
    set_page_ref(canary, 7);
    set_page_ref(canary + 3, 9);
    memcpy(bt_canary_save, canary, sizeof(bt_canary_save));
    bt_arena = alloc_pages(BT_LOAN);
    assert(bt_arena != NULL && page2ppn(bt_arena) % BT_LOAN == 0);
    assert(bt_arena + BT_LOAN <= canary || canary + 4 <= bt_arena);
    assert(nr_free_pages() == original_free - BT_LOAN - 4);
    memcpy(bt_loan_save, bt_arena, sizeof(bt_loan_save));
    size_t saved_free = buddy_nr_free;
    unsigned int saved_maximum = buddy_max_order;
    for (unsigned int i = 0; i < BUDDY_ORDERS; i++) {
        list_init(&bt_saved_lists[i]);
        bt_move_list(&buddy_free_lists[i], &bt_saved_lists[i]);
    }
    cprintf("buddy_check: real arena ppn=0x%lx, 64 test + 64 guard pages\n",
            (unsigned long)page2ppn(bt_arena));
    bt_initialization();
    bt_rounding();
    bt_splitting();
    bt_merging();
    bt_adjacent_nonbuddies();
    bt_boundaries();
    bt_failures();
    bt_mixed();

    cprintf("[buddy 9] real allocator restoration and outside allocation: BEGIN\n");
    for (size_t i = 0; i < BT_RECORDS; i++) {
        assert(bt_records[i].page == NULL);
    }
    bt_verify();
    buddy_init(); /* Discard only the isolated arena's list sentinels. */
    memcpy(bt_arena, bt_loan_save, sizeof(bt_loan_save));
    for (unsigned int i = 0; i < BUDDY_ORDERS; i++) {
        bt_move_list(&bt_saved_lists[i], &buddy_free_lists[i]);
        assert(list_empty(&bt_saved_lists[i]));
    }
    buddy_nr_free = saved_free;
    buddy_max_order = saved_maximum;
    bt_same_pages(canary, bt_canary_save, 4);
    free_pages(bt_arena, BT_LOAN);
    assert(nr_free_pages() == original_free - 4);
    bt_same_pages(canary, bt_canary_save, 4);
    set_page_ref(canary, 0);
    set_page_ref(canary + 3, 0);
    free_pages(canary, 3);
    assert(nr_free_pages() == original_free);
    bt_global_audit(0); /* Exact free block heads, orders and coverage restored. */
    struct Page *probe = alloc_pages(3);
    assert(probe != NULL && page2ppn(probe) % 4 == 0);
    assert(nr_free_pages() == original_free - 4);
    free_pages(probe, 3);
    assert(nr_free_pages() == original_free);
    bt_global_audit(0);
    cprintf("[buddy 9] real allocator restoration and outside allocation: PASS "
            "(%lu free pages)\n", (unsigned long)original_free);
    /* Invalid n=0, repeated frees, partial frees and wrong-n frees assert in
     * production. They must run in separate kernels, never abort this suite. */
    cprintf("buddy_check: whole-block free uses original base/n; "
            "precondition violations excluded from normal run\n");
    cprintf("BUDDY CHECK ALL PASSED (9 groups, %lu arena operations, "
            "%lu structure audits)\n", (unsigned long)bt_operations,
            (unsigned long)bt_structure_checks);
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
