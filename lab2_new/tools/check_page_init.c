/* Module-one checks, compiled only by verify_page_init.py in a temporary copy. */
#include <default_pmm.h>
#include <dtb.h>
#include <pmm.h>
#include <stdio.h>

static size_t handoff_count;

/* Runs before the allocator can clear flags or overwrite list nodes. */
void check_page_init_handoff(struct Page *base, size_t n) {
    extern char end[];
    cprintf("MODULE1 INIT_MEMMAP ENTERED\n");
    assert(++handoff_count == 1);
    assert(n > 0);
    uintptr_t first = (uintptr_t)pages;
    uintptr_t address = (uintptr_t)base;
    size_t count = npage - nbase;
    assert(address >= first);
    assert((address - first) % sizeof(struct Page) == 0);
    size_t offset = (address - first) / sizeof(struct Page);
    assert(offset < count && n <= count - offset);
    uintptr_t metadata_begin = PADDR(pages);
    uintptr_t metadata_end = metadata_begin + count * sizeof(struct Page);
    uintptr_t free_pa = (nbase + offset) * PGSIZE;
    assert(free_pa >= PADDR(end) && free_pa >= metadata_end);
    assert(free_pa >= get_memory_base());
    assert(free_pa + n * PGSIZE == npage * PGSIZE);

    for (size_t i = 0; i < count; i++) {
        struct Page *p = &pages[i];
        if (p->flags != (1UL << PG_reserved) || page_ref(p) != 0 ||
            p->property != 0 || p->page_link.next != &p->page_link ||
            p->page_link.prev != &p->page_link) {
            panic("MODULE1 HANDOFF: descriptor not initialized");
        }
    }
    cprintf("MODULE1 HANDOFF CONTRACT PASSED\n");
}

void check_page_init_layout(void) {
    extern char end[];
    uintptr_t ram_base = get_memory_base();
    uintptr_t ram_end = ram_base + get_memory_size();
    uintptr_t kernel_end = PADDR(end);
    uintptr_t metadata_begin = PADDR(pages);
    uintptr_t metadata_end = metadata_begin +
        (npage - nbase) * sizeof(struct Page);
    uintptr_t limit = ram_end < PHYSICAL_MEMORY_END ?
        ram_end : PHYSICAL_MEMORY_END;
    size_t available = 0;

    assert(handoff_count == 1);
    assert(pmm_manager == &default_pmm_manager);
    assert(va_pa_offset == PHYSICAL_MEMORY_OFFSET);
    assert(npage == PPN(limit));
    assert(((uintptr_t)pages % PGSIZE) == 0);
    assert((uintptr_t)pages >= ROUNDUP((uintptr_t)end, PGSIZE));
    assert(metadata_begin >= ram_base && metadata_end <= limit);

    /* Classify every page by interval overlap, independently of free_begin. */
    for (size_t i = 0; i < npage - nbase; i++) {
        struct Page *p = &pages[i];
        uintptr_t pa = (nbase + i) * PGSIZE;
        uintptr_t next = pa + PGSIZE;
        bool owns_data = pa < kernel_end ||
            (pa < metadata_end && next > metadata_begin);
        bool usable = pa >= ram_base && next <= ram_end &&
            next <= PHYSICAL_MEMORY_END && !owns_data;

        assert(page2pa(p) == pa);
        assert(pa2page(pa) == p);
        assert(page_ref(p) == 0);
        assert(!!PageReserved(p) == !usable);
        if (usable) {
            available++;
        }
    }
    assert(available > 0 && available == nr_free_pages());

    struct Page *block = alloc_pages(3);
    assert(block != NULL);
    assert(nr_free_pages() == available - 3);
    for (size_t i = 0; i < 3; i++) {
        uintptr_t pa = page2pa(block + i);
        assert(pa >= metadata_end && pa + PGSIZE <= limit);
        assert(!PageReserved(block + i));
    }
    free_pages(block, 3);
    assert(nr_free_pages() == available);
    cprintf("MODULE1 FULL-LAYOUT CHECK PASSED: %lu free pages\n",
            (unsigned long)available);
}
