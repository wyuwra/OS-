#include <console.h>
#include <defs.h>
#include <pmm.h>
#include <slub.h>
#include <slub_test.h>
#include <stdio.h>
#include <string.h>
#include <dtb.h>

int kern_init(void) __attribute__((noreturn));
void grade_backtrace(void);
void print_kerninfo(void);

/* Integration smoke check only; the separate SLUB test task will add the
 * complete size-boundary, exhaustion, and mixed-allocation test suite.
 */
#ifndef SLUB_TEST
static void check_slub_smoke(void) {
    size_t before = nr_free_pages();
    void *objects[64];
    assert(slub_alloc(0) == NULL);
    slub_free(NULL);
    /* 64 slots cross a 64-byte slab boundary (metadata reduces capacity).
     * Writing each complete slot also overwrites its former next pointer.
     */
    for (size_t i = 0; i < 64; i++) {
        objects[i] = slub_alloc(64);
        assert(objects[i] != NULL && ((uintptr_t)objects[i] & 15) == 0);
        memset(objects[i], i + 1, 64);
    }
    void *old = objects[0];
    slub_free(old);  // full -> partial
    objects[0] = slub_alloc(64);
    assert(objects[0] != NULL && ((uintptr_t)objects[0] & 15) == 0);
    memset(objects[0], 1, 64);

    void *small = slub_alloc(2048);
    void *large = slub_alloc(8192);  // 3 requested pages, 4 with Buddy
    assert(small != NULL && large != NULL);
    assert(((uintptr_t)small & 15) == 0 && ((uintptr_t)large & 15) == 0);
    memset(small, 0x5a, 2048);
    memset(large, 0xa5, 8192);
    size_t live_free = nr_free_pages();
    assert(slub_alloc((size_t)-1) == NULL);
    assert(slub_alloc((size_t)-1 - 64) == NULL);
    assert(nr_free_pages() == live_free);
    for (size_t i = 0; i < 64; i++) {
        for (size_t j = 0; j < 64; j++) {
            assert(((uint8_t *)objects[i])[j] == i + 1);
        }
        slub_free(objects[i]);
    }
    for (size_t i = 0; i < 2048; i++) {
        assert(((uint8_t *)small)[i] == 0x5a);
    }
    for (size_t i = 0; i < 8192; i++) {
        assert(((uint8_t *)large)[i] == 0xa5);
    }
    slub_free(small);
    slub_free(large);
    assert(nr_free_pages() == before);
    cprintf("SLUB smoke check passed (all pages reclaimed)\n");
}
#endif

/* *
 * print_kerninfo - print the information about kernel, including the location
 * of kernel entry, the start addresses of data and text segements, the start
 * address of free memory and how many memory that kernel has used.
 * */
void print_kerninfo(void) {
    extern char etext[], edata[], end[];
    cprintf("Special kernel symbols:\n");
    cprintf("  entry  0x%016lx (virtual)\n", (uintptr_t)kern_init);
    cprintf("  etext  0x%016lx (virtual)\n", etext);
    cprintf("  edata  0x%016lx (virtual)\n", edata);
    cprintf("  end    0x%016lx (virtual)\n", end);
    cprintf("Kernel executable memory footprint: %dKB\n",
            (end - (char*)kern_init + 1023) / 1024);
}

int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);
    dtb_init();
    cons_init();  // init the console
    const char *message = "(THU.CST) os is loading ...\0";
    //cprintf("%s\n\n", message);
    cputs(message);

    print_kerninfo();

    // grade_backtrace();
    pmm_init();  // init physical memory management
    slub_init(); // direct map and page allocator are now ready
#ifdef SLUB_TEST
    slub_check();
#else
    check_slub_smoke();
#endif

    /* do nothing */
    while (1)
        ;
}

