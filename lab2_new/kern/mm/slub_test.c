#include <slub_test.h>

#ifdef SLUB_TEST
#include <slub.h>
#include <pmm.h>
#include <buddy_pmm.h>
#include <best_fit_pmm.h>
#include <default_pmm.h>
#include <stdio.h>

/* No test bookkeeping comes from SLUB. 505 objects suffice to fill two
 * 16-byte slabs and start a third with the current 64-byte header.
 * At most 48 objects are used by the mixed/random tests; their largest
 * request is bounded below. No exhaustion of the real allocator is needed.
 */
#define ST_RECORDS 520
#define ST_CASES 64
#define ST_MIXED 48
#define ST_STEPS 512
#define ST_MAX_BYTES (4 * PGSIZE + 37)
#define ST_SEED 0x13579bdfU

struct st_record {
    uint8_t *ptr;
    size_t size;
    uint32_t tag;
};

static struct st_record records[ST_RECORDS];
static size_t cases[ST_CASES], case_count;
static const size_t classes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
static unsigned int group;
static const char *phase;
static size_t step, current_id, current_size, allocations, releases, audits;
static size_t header_bytes, group_baseline, group_allocations;
static uint32_t generation, random_state;

/* Every failure carries its stage and operation/record context. A bad byte
 * additionally reports its exact offset and expected/observed values.
 */
#define ST_CHECK(condition, ...) do {                                      \
    if (!(condition)) {                                                   \
        cprintf("[slub %u] phase=%s step=%lu id=%lu size=%lu\n",           \
                group, phase, (unsigned long)step,                        \
                (unsigned long)current_id, (unsigned long)current_size);   \
        panic(__VA_ARGS__);                                               \
    }                                                                    \
} while (0)

static uint8_t
st_pattern(uint32_t tag, size_t offset) {
    /* Defined unsigned arithmetic: both identity and position affect a byte. */
    uint32_t value = tag * 0x9e3779b9U ^ (uint32_t)offset * 0x045d9f3bU;
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    return (uint8_t)value;
}

static void
st_verify_one(size_t id) {
    struct st_record *record = &records[id];
    current_id = id;
    current_size = record->size;
    ST_CHECK(record->ptr != NULL, "expected a live object");
    for (size_t offset = 0; offset < record->size; offset++) {
        uint8_t expected = st_pattern(record->tag, offset);
        ST_CHECK(record->ptr[offset] == expected,
                 "data mismatch at offset=%lu: expected=%u observed=%u",
                 (unsigned long)offset, (unsigned int)expected,
                 (unsigned int)record->ptr[offset]);
    }
}

static void
st_verify_all(void) {
    for (size_t id = 0; id < ST_RECORDS; id++) {
        if (records[id].ptr != NULL) {
            st_verify_one(id);
        }
    }
    audits++;
}

static void
st_alloc(size_t id, size_t size) {
    current_id = id;
    current_size = size;
    ST_CHECK(id < ST_RECORDS && size > 0 && size <= ST_MAX_BYTES,
             "test allocation exceeds its fixed budget");
    ST_CHECK(records[id].ptr == NULL, "test record already occupied");
    uint8_t *ptr = slub_alloc(size);
    ST_CHECK(ptr != NULL, "allocation unexpectedly returned NULL");
    ST_CHECK(((uintptr_t)ptr % 16) == 0, "allocation is not 16-byte aligned");
    uintptr_t start = (uintptr_t)ptr;
    ST_CHECK(start >= KERNBASE && start < KERNTOP && size <= KERNTOP - start,
             "returned range is outside managed kernel RAM");
    /* Check old contents before writing the new object, and prove that the
     * requested intervals are disjoint before any possible overlap is written.
     * Induction on allocations gives pairwise isolation for all live records.
     */
    st_verify_all();
    current_id = id;
    current_size = size;
    for (size_t other = 0; other < ST_RECORDS; other++) {
        if (records[other].ptr != NULL) {
            uintptr_t other_start = (uintptr_t)records[other].ptr;
            ST_CHECK(start + size <= other_start ||
                     other_start + records[other].size <= start,
                     "requested range overlaps live id=%lu",
                     (unsigned long)other);
        }
    }
    records[id].ptr = ptr;
    records[id].size = size;
    records[id].tag = (++generation << 10) ^ (uint32_t)id;
    for (size_t offset = 0; offset < size; offset++) {
        ptr[offset] = st_pattern(records[id].tag, offset);
    }
    allocations++;
    st_verify_all();
}

static void
st_free(size_t id) {
    current_id = id;
    ST_CHECK(id < ST_RECORDS, "test release exceeds record budget");
    st_verify_one(id);
    void *ptr = records[id].ptr;
    records[id].ptr = NULL;
    /* Remove the record first: no later verifier ever reads a freed object. */
    slub_free(ptr);
    releases++;
    st_verify_all();
}

static void
st_empty(void) {
    for (size_t id = 0; id < ST_RECORDS; id++) {
        current_id = id;
        ST_CHECK(records[id].ptr == NULL, "test group left a live allocation");
    }
}

static void
st_pages(size_t expected) {
    size_t actual = nr_free_pages();
    ST_CHECK(actual == expected, "free pages: expected=%lu observed=%lu",
             (unsigned long)expected, (unsigned long)actual);
}

static void
st_begin(unsigned int number, const char *name) {
    group = number;
    phase = name;
    step = current_id = current_size = 0;
    st_empty();
    group_baseline = nr_free_pages();
    group_allocations = allocations;
    cprintf("[slub %u] %s: BEGIN (baseline=%lu)\n", group, phase,
            (unsigned long)group_baseline);
}

static void
st_end(void) {
    st_empty();
    st_pages(group_baseline);
    ST_CHECK(!slub_test_failure_pending(), "unconsumed page failure injection");
    cprintf("[slub %u] %s: PASS (%lu allocations, baseline restored)\n",
            group, phase, (unsigned long)(allocations - group_allocations));
}

static void
st_basic(void) {
    st_begin(1, "basic contract and NULL");
    ST_CHECK(slub_alloc(0) == NULL, "zero-size allocation must return NULL");
    slub_free(NULL);
    st_pages(group_baseline);
    st_alloc(0, 1);
    st_alloc(1, 3);
    st_alloc(2, 17);
    st_free(1);
    st_free(0);
    st_free(2);
    st_end();
}

static void
st_add_case(size_t size) {
    ST_CHECK(size > 0 && size <= ST_MAX_BYTES, "invalid boundary case");
    for (size_t i = 0; i < case_count; i++) {
        if (cases[i] == size) {
            return;
        }
    }
    ST_CHECK(case_count < ST_CASES, "boundary table exhausted");
    cases[case_count++] = size;
}

static void
st_boundaries(void) {
    st_begin(2, "all size classes and page boundaries");
    case_count = 0;
    for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
        st_add_case(classes[i] - 1);
        st_add_case(classes[i]);
        st_add_case(classes[i] + 1);
    }
    const size_t extra[] = {1, 3, 17, 100, 513, PGSIZE - 1, PGSIZE,
                            PGSIZE + 1, 2 * PGSIZE + 37};
    for (size_t i = 0; i < sizeof(extra) / sizeof(extra[0]); i++) {
        st_add_case(extra[i]);
    }
    /* Large-request thresholds include the actual in-band header. */
    for (size_t pages_count = 1; pages_count <= 2; pages_count++) {
        size_t edge = pages_count * PGSIZE - header_bytes;
        st_add_case(edge - 1);
        st_add_case(edge);
        st_add_case(edge + 1);
    }
    cprintf("[slub 2] %lu distinct boundary sizes\n", (unsigned long)case_count);
    for (size_t i = 0; i < case_count; i++) {
        step = i;
        st_alloc(i, cases[i]);
    }
    for (size_t i = case_count; i > 0; i--) {
        step = i;
        st_free(i - 1);
    }
    st_end();
}

static void
st_isolation(void) {
    st_begin(3, "same-size and mixed-size live isolation");
    for (size_t i = 0; i < ST_MIXED; i++) {
        step = i;
        st_alloc(i, i < ST_MIXED / 2 ? 100 : cases[i % case_count]);
    }
    for (size_t i = 0; i < ST_MIXED; i += 3) {
        step = i;
        st_free(i);
    }
    for (size_t i = 0; i < ST_MIXED; i += 3) {
        step = i;
        st_alloc(i, 1 + (i * 137) % (2 * PGSIZE + 37));
    }
    for (size_t i = ST_MIXED; i > 0; i--) {
        st_free(i - 1);
    }
    st_end();
}

static uintptr_t
st_object_page(size_t id) {
    return (uintptr_t)records[id].ptr / PGSIZE * PGSIZE;
}

/* Release exactly the objects resident on a chosen page. Check after EVERY
 * free that only the last survivor permits returning that slab's page.
 * No allocation is required to reuse a particular old pointer.
 */
static void
st_release_slab(uintptr_t page) {
    size_t live = 0;
    for (size_t id = 0; id < ST_RECORDS; id++) {
        if (records[id].ptr != NULL && st_object_page(id) == page) {
            live++;
        }
    }
    ST_CHECK(live != 0, "chosen slab has no test objects");
    for (size_t id = 0; id < ST_RECORDS; id++) {
        if (records[id].ptr != NULL && st_object_page(id) == page) {
            size_t before = nr_free_pages();
            st_free(id);
            live--;
            st_pages(before + (live == 0));
        }
    }
    ST_CHECK(live == 0, "slab release accounting failed");
}

static void
st_slabs(void) {
    st_begin(4, "slab expansion, full/partial and immediate reclaim");
    for (size_t which = 0; which < sizeof(classes) / sizeof(classes[0]); which++) {
        size_t capacity = (PGSIZE - header_bytes) / classes[which];
        size_t count = 2 * capacity + 1;
        step = which;
        current_size = classes[which];
        ST_CHECK(capacity > 0 && count <= ST_RECORDS, "slab test exceeds budget");
        cprintf("[slub 4] class=%lu capacity=%lu objects=%lu\n",
                (unsigned long)classes[which], (unsigned long)capacity,
                (unsigned long)count);
        for (size_t i = 0; i < count - 1; i++) {
            st_alloc(i, classes[which]);
            st_pages(group_baseline - (i / capacity + 1));
        }
        /* Both slabs are full, so no OTHER partial slab can hide a broken
         * full -> partial transition. Reallocation must use an available
         * slot without acquiring a third page (no specific address required).
         * With capacity == 1 the single free immediately returns its page.
         */
        st_free(0);
        st_pages(group_baseline - 2 + (capacity == 1));
        st_alloc(0, classes[which]);
        st_pages(group_baseline - 2);
        st_alloc(count - 1, classes[which]); // force extension to a third slab
        st_pages(group_baseline - 3);
        for (size_t id = 0; id < count; id++) {
            if (records[id].ptr != NULL) {
                st_release_slab(st_object_page(id));
            }
        }
        st_empty();
        st_pages(group_baseline);
    }
    st_end();
}

static void
st_orders(void) {
    st_begin(5, "forward, reverse, alternating release and reuse");
    for (size_t round = 0; round < 3; round++) {
        for (size_t order = 0; order < 3; order++) {
            step = round * 3 + order;
            for (size_t i = 0; i < ST_MIXED; i++) {
                st_alloc(i, i % 2 == 0 ? 64 : cases[(i + round) % case_count]);
            }
            for (size_t i = 0; i < ST_MIXED; i++) {
                size_t id;
                if (order == 0) {
                    id = i;
                } else if (order == 1) {
                    id = ST_MIXED - 1 - i;
                } else {
                    id = i < ST_MIXED / 2 ? 2 * i :
                         2 * (ST_MIXED - 1 - i) + 1;
                }
                st_free(id);
            }
            st_empty();
            st_pages(group_baseline);
        }
    }
    st_end();
}

static uint32_t
st_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void
st_mixed_random(void) {
    st_begin(6, "bounded reproducible mixed sequence");
    random_state = ST_SEED;
    cprintf("[slub 6] seed=0x%08x steps=%u records=%u\n",
            ST_SEED, ST_STEPS, ST_MIXED);
    for (step = 0; step < ST_STEPS; step++) {
        size_t id = st_random() % ST_MIXED;
        if (records[id].ptr != NULL) {
            st_free(id);
        } else {
            st_alloc(id, cases[st_random() % case_count]);
        }
    }
    for (size_t i = 0; i < ST_MIXED; i++) {
        if (records[i].ptr != NULL) {
            st_free(i);
        }
    }
    st_end();
}

static size_t
st_large_page_cost(size_t size) {
    /* Independent oracle: count up pages by multiplication; apply the actual
     * manager's documented rounding, never inspect Page allocation metadata.
     * Only bounded test sizes reach this helper, so multiplication is safe.
     */
    ST_CHECK(size > 2048 && size <= ST_MAX_BYTES, "invalid large-test size");
    size_t requested = 1;
    while (requested * PGSIZE < size + header_bytes) {
        requested++;
    }
    if (pmm_manager == &buddy_pmm_manager) {
        size_t rounded = 1;
        while (rounded < requested) {
            rounded *= 2;
        }
        return rounded;
    }
    ST_CHECK(pmm_manager == &best_fit_pmm_manager ||
             pmm_manager == &default_pmm_manager, "unknown PMM rounding rule");
    return requested;
}

static void
st_large_free(size_t id) {
    size_t before = nr_free_pages();
    size_t expected = st_large_page_cost(records[id].size);
    st_free(id);
    st_pages(before + expected);
}

static void
st_large(void) {
    st_begin(7, "simultaneous large objects and PMM page rounding");
    const size_t sizes[] = {PGSIZE - 1, PGSIZE, PGSIZE + 1,
                            2 * PGSIZE + 37, 3 * PGSIZE + 37, ST_MAX_BYTES};
    st_alloc(0, 100);
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        step = i;
        size_t before = nr_free_pages();
        size_t cost = st_large_page_cost(sizes[i]);
        st_alloc(i + 1, sizes[i]);
        st_pages(before - cost);
        cprintf("[slub 7] bytes=%lu occupied_pages=%lu\n",
                (unsigned long)sizes[i], (unsigned long)cost);
    }
    st_alloc(7, 17);
    st_large_free(4); // 3 requested pages, 4 occupied with Buddy
    size_t before = nr_free_pages();
    st_free(0);
    st_pages(before + 1);
    st_large_free(2);
    before = nr_free_pages();
    st_free(7);
    st_pages(before + 1);
    st_large_free(6); // 5 requested pages, 8 occupied with Buddy
    st_large_free(1);
    st_large_free(5);
    st_large_free(3);
    st_end();
}

static void
st_failed_request(size_t size, bool injected) {
    current_id = ST_RECORDS;
    current_size = size;
    size_t before = nr_free_pages();
    if (injected) {
        slub_test_fail_next_page_alloc();
        ST_CHECK(slub_test_failure_pending(), "failure injection did not arm");
    }
    void *ptr = slub_alloc(size);
    ST_CHECK(ptr == NULL, "request expected to fail returned a pointer");
    ST_CHECK(!slub_test_failure_pending(), "request bypassed the page failure seam");
    st_pages(before);
    st_verify_all();
}

static void
st_failures(void) {
    st_begin(8, "size overflow, page failure and recovery");
    st_alloc(0, 100);
    st_alloc(1, PGSIZE + 1);
    const size_t maximum = (size_t)-1;
    const size_t huge[] = {maximum, maximum - 1, maximum - 15,
                           maximum - header_bytes + 1,
                           maximum - header_bytes,
                           maximum - header_bytes - (PGSIZE - 1)};
    for (size_t i = 0; i < sizeof(huge) / sizeof(huge[0]); i++) {
        step = i;
        st_failed_request(huge[i], 0);
    }
    /* Class 16 is empty: a freelist cannot bypass this failure point. */
    step = 6;
    st_failed_request(16, 1);
    st_alloc(2, 16);
    st_free(2);
    cprintf("[slub 8] fresh small slab: injected failure consumed, recovery PASS\n");

    /* Repeat on expansion of a fully occupied class, protecting its live
     * objects and both partial/full state across the failed allocation.
     */
    size_t capacity = (PGSIZE - header_bytes) / 1024;
    ST_CHECK(capacity > 0 && capacity + 8 < ST_RECORDS, "bad failure-test capacity");
    for (size_t i = 0; i < capacity; i++) {
        st_alloc(i + 8, 1024);
    }
    step = 7;
    st_failed_request(1024, 1);
    st_alloc(capacity + 8, 1024);
    for (size_t i = 0; i <= capacity; i++) {
        st_free(i + 8);
    }
    cprintf("[slub 8] full-slab expansion: injected failure consumed, recovery PASS\n");

    step = 8;
    st_failed_request(2 * PGSIZE + 37, 1);
    st_alloc(3, 2 * PGSIZE + 37);
    st_large_free(3);
    cprintf("[slub 8] large object: injected failure consumed, recovery PASS\n");
    st_free(0);
    st_large_free(1);
    st_end();
}

void
slub_check(void) {
    group = 0;
    phase = "setup";
    step = current_id = current_size = 0;
    allocations = releases = audits = 0;
    generation = 0;
    st_empty();
    ST_CHECK(!slub_test_failure_pending(), "suite entered with a pending failure");
    header_bytes = slub_test_header_bytes();
    ST_CHECK(header_bytes > 0 && header_bytes < PGSIZE && header_bytes % 16 == 0,
             "invalid reported header size");
    size_t baseline = nr_free_pages();
    ST_CHECK(baseline > 128, "not enough RAM for this bounded test suite");
    cprintf("SLUB CHECK BEGIN (header=%lu, records=%u, max_bytes=%u)\n",
            (unsigned long)header_bytes, ST_RECORDS, ST_MAX_BYTES);
    st_basic();
    st_boundaries();
    st_isolation();
    st_slabs();
    st_orders();
    st_mixed_random();
    st_large();
    st_failures();
    st_empty();
    st_pages(baseline);
    ST_CHECK(allocations == releases, "successful allocation/release count differs");
    cprintf("SLUB CHECK ALL PASSED (8 groups, %lu allocations, %lu releases, "
            "%lu content audits, %lu free pages)\n", (unsigned long)allocations,
            (unsigned long)releases, (unsigned long)audits, (unsigned long)baseline);
}
#endif /* SLUB_TEST */
