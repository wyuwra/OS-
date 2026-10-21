#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// exclusive physical page-number limit; descriptor count is npage - nbase
size_t npage = 0;
// kernel direct-map offset: virtual address minus physical address
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &buddy_pmm_manager;
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
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 2412832
static void page_init(void) {
    extern char end[];
    const uintptr_t max_addr = (uintptr_t)-1;

    // PADDR() 依赖此偏移；必须在转换内核地址之前设置。
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    if (mem_base == 0 || mem_size == 0) {
        panic("page_init: invalid memory information from DTB");
    }
    if (mem_size > max_addr - mem_base) {
        panic("page_init: physical memory range overflows");
    }
    uintptr_t mem_end = mem_base + mem_size;

    // 结束边界不包含在区间中；上限以外和不足一页的尾部不管理。
    uintptr_t managed_end = mem_end;
    if (managed_end > PHYSICAL_MEMORY_END) {
        managed_end = PHYSICAL_MEMORY_END;
    }
    managed_end = ROUNDDOWN(managed_end, PGSIZE);
    uintptr_t kernel_end_pa = PADDR(end);
    if (mem_base > KERNEL_BEGIN_PADDR ||
        kernel_end_pa < KERNEL_BEGIN_PADDR ||
        managed_end <= kernel_end_pa || PPN(managed_end) <= nbase) {
        panic("page_init: RAM cannot contain the kernel and page descriptors");
    }

    // npage 是绝对物理页号上界，描述符数量要减去 DRAM 起始页号。
    size_t page_count = PPN(managed_end) - nbase;
    if ((uintptr_t)end > max_addr - (PGSIZE - 1) ||
        page_count > max_addr / sizeof(struct Page)) {
        panic("page_init: page descriptor layout overflows");
    }
    uintptr_t pages_va = ROUNDUP((uintptr_t)end, PGSIZE);
    uintptr_t pages_pa = PADDR(pages_va);
    size_t descriptor_bytes = page_count * sizeof(struct Page);

    // 先检查容量再做加法，更不能在检查完成前写入描述符数组。
    if (pages_pa < mem_base || pages_pa >= managed_end ||
        descriptor_bytes > managed_end - pages_pa) {
        panic("page_init: insufficient RAM for page descriptors");
    }
    uintptr_t metadata_end = pages_pa + descriptor_bytes;
    if (metadata_end > max_addr - (PGSIZE - 1)) {
        panic("page_init: free memory alignment overflows");
    }
    uintptr_t free_begin = ROUNDUP(metadata_end, PGSIZE);
    if (free_begin >= managed_end) {
        panic("page_init: no free physical pages");
    }
    size_t free_count = (managed_end - free_begin) / PGSIZE;

    npage = PPN(managed_end);
    pages = (struct Page *)pages_va;
    for (size_t i = 0; i < page_count; i++) {
        struct Page *p = &pages[i];
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
        SetPageReserved(p);
    }

    // 仅开放元数据之后的完整页，其余页面继续保留。
    // 当前分配器的 init_memmap() 负责清除空闲页的保留位并建立空闲块。
    assert(PPN(free_begin) >= nbase && PPN(free_begin) < npage);
    init_memmap(pa2page(free_begin), free_count);

    cprintf("physical memory map:\n");
    cprintf("  memory: 0x%016lx, [0x%016lx, 0x%016lx).\n",
            (unsigned long)mem_size, (unsigned long)mem_base,
            (unsigned long)mem_end);
    cprintf("  managed end: 0x%016lx, nbase: 0x%lx, npage: 0x%lx\n",
            (unsigned long)managed_end, (unsigned long)nbase,
            (unsigned long)npage);
    cprintf("  page descriptors: VA 0x%016lx, PA 0x%016lx, %lu bytes\n",
            (unsigned long)pages_va, (unsigned long)pages_pa,
            (unsigned long)descriptor_bytes);
    cprintf("  free memory: [0x%016lx, 0x%016lx), %lu pages\n",
            (unsigned long)free_begin, (unsigned long)managed_end,
            (unsigned long)free_count);
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
