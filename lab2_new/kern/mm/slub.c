#include <slub.h>
#include <pmm.h>
#include <list.h>
#ifdef SLUB_TEST
#include <slub_test.h>
#endif

#define SLUB_ALIGN 16
#define SLUB_CLASSES 8

struct slub_cache {
    size_t object_size;
    list_entry_t partial;
    list_entry_t full;
};

/* Private, in-band metadata: never borrow any fields from struct Page.
 * cache == NULL denotes a large allocation; requested_pages is the exact n
 * passed to alloc_pages, NOT Buddy's rounded block size. The remaining fields
 * describe a small slab; link belongs to precisely one cache list.
 */
struct slub_header {
    struct slub_cache *cache;
    size_t requested_pages;
    size_t capacity;
    size_t inuse;
    void *freelist;
    list_entry_t link;
};

/* sizeof is a small compile-time constant; user-controlled arithmetic below
 * does not use ROUNDUP (whose addition could overflow).
 */
#define SLUB_HEADER_SIZE \
    ((sizeof(struct slub_header) + SLUB_ALIGN - 1) / SLUB_ALIGN * SLUB_ALIGN)

static const size_t class_sizes[SLUB_CLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};
static struct slub_cache caches[SLUB_CLASSES];
static bool slub_ready;

#ifdef SLUB_TEST
static bool fail_next_page_alloc;

size_t
slub_test_header_bytes(void) {
    return SLUB_HEADER_SIZE;
}

void
slub_test_fail_next_page_alloc(void) {
    assert(!fail_next_page_alloc);
    fail_next_page_alloc = 1;
}

bool
slub_test_failure_pending(void) {
    return fail_next_page_alloc;
}
#endif

static struct Page *
slub_get_pages(size_t count) {
#ifdef SLUB_TEST
    if (fail_next_page_alloc) {
        fail_next_page_alloc = 0;
        return NULL;
    }
#endif
    return alloc_pages(count);
}

void
slub_init(void) {
    assert(!slub_ready);
    assert(pmm_manager != NULL && pages != NULL && npage > nbase);
    assert(va_pa_offset == PHYSICAL_MEMORY_OFFSET);
    assert(SLUB_HEADER_SIZE < PGSIZE);
    assert(PGSIZE % SLUB_ALIGN == 0);
    assert(sizeof(void *) <= class_sizes[0]);
    for (size_t i = 0; i < SLUB_CLASSES; i++) {
        caches[i].object_size = class_sizes[i];
        assert((PGSIZE - SLUB_HEADER_SIZE) / class_sizes[i] > 0);
        list_init(&caches[i].partial);
        list_init(&caches[i].full);
    }
    slub_ready = 1;
}

static struct slub_header *
slub_new_slab(struct slub_cache *cache) {
    struct Page *page = slub_get_pages(1);
    if (page == NULL) {
        return NULL;
    }
    /* KADDR/page2kva are not provided by this tree. entry.S maps RAM with
     * this offset before kern_init; page_init bounds every managed page.
     */
    struct slub_header *slab =
        (struct slub_header *)(page2pa(page) + va_pa_offset);
    slab->cache = cache;
    slab->requested_pages = 1;
    slab->capacity = (PGSIZE - SLUB_HEADER_SIZE) / cache->object_size;
    slab->inuse = 0;
    slab->freelist = NULL;
    list_init(&slab->link);

    /* A next pointer occupies only a FREE slot. Once returned to a caller,
     * the allocator relies exclusively on the separate header and free slots.
     */
    char *objects = (char *)slab + SLUB_HEADER_SIZE;
    for (size_t i = slab->capacity; i > 0; i--) {
        void *object = objects + (i - 1) * cache->object_size;
        *(void **)object = slab->freelist;
        slab->freelist = object;
    }
    list_add(&cache->partial, &slab->link);
    return slab;
}

static void *
slub_alloc_large(size_t size) {
    if (size > (size_t)-1 - SLUB_HEADER_SIZE) {
        return NULL;
    }
    size_t bytes = size + SLUB_HEADER_SIZE;
    /* Division plus remainder avoids overflow in bytes + PGSIZE - 1.
     * The quotient is at most SIZE_MAX / PGSIZE, so this +1 is safe.
     */
    size_t count = bytes / PGSIZE + (bytes % PGSIZE != 0);
    if (count > npage - nbase || count > nr_free_pages()) {
        return NULL;
    }
    struct Page *page = slub_get_pages(count);
    if (page == NULL) {
        return NULL;
    }
    struct slub_header *header =
        (struct slub_header *)(page2pa(page) + va_pa_offset);
    header->cache = NULL;
    header->requested_pages = count;
    /* The starting pointer remains in the first page, even for large sizes. */
    return (char *)header + SLUB_HEADER_SIZE;
}

void *
slub_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    assert(slub_ready);
    struct slub_cache *cache = NULL;
    for (size_t i = 0; i < SLUB_CLASSES; i++) {
        if (size <= caches[i].object_size) {
            cache = &caches[i];
            break;
        }
    }
    if (cache == NULL) {
        return slub_alloc_large(size);
    }
    struct slub_header *slab;
    if (list_empty(&cache->partial)) {
        slab = slub_new_slab(cache);
        if (slab == NULL) {
            return NULL;
        }
    } else {
        slab = to_struct(list_next(&cache->partial), struct slub_header, link);
    }
    void *object = slab->freelist;
    slab->freelist = *(void **)object;
    slab->inuse++;
    if (slab->inuse == slab->capacity) {
        list_del_init(&slab->link);
        list_add(&cache->full, &slab->link);
    }
    return object;
}

void
slub_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    assert(slub_ready);
    /* All valid allocation starts share a page with their own header. */
    struct slub_header *slab =
        (struct slub_header *)ROUNDDOWN((uintptr_t)ptr, PGSIZE);
    if (slab->cache == NULL) {
        size_t count = slab->requested_pages;
        free_pages(pa2page(PADDR(slab)), count);
        return;
    }
    struct slub_cache *cache = slab->cache;
    bool was_full = slab->inuse == slab->capacity;
    slab->inuse--;
    if (slab->inuse == 0) {
        /* Unlink BEFORE releasing the page: no cache references may survive. */
        list_del_init(&slab->link);
        free_pages(pa2page(PADDR(slab)), 1);
        return;
    }
    *(void **)ptr = slab->freelist;
    slab->freelist = ptr;
    if (was_full) {
        list_del_init(&slab->link);
        list_add(&cache->partial, &slab->link);
    }
}
