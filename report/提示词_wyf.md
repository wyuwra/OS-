[PROMPT]
任务：将 Lab2 中已实现的物理页初始化功能迁移到 Lab3。

功能目标：复用 Lab2 的 page_init()，根据设备树提供的物理内存信息，
建立 Page 描述符数组，保留固件、内核和描述符数组占用的页面，
并将剩余完整页面注册到当前物理内存管理器。

修改范围：
- 在 lab3_new/kern/mm/pmm.c 的 Lab2 预留位置补入 page_init()。
- 允许在 lab3_new/kern/mm/memlayout.h 中补充必要的地址宏。
- 保留 Lab3 的中断保护、原子位操作、现有接口和管理器选择。
- Best-Fit 由其他成员负责，本次不迁移该分配器。
- 保留任务注释和学号 2412832。

操作要求：先对比两个实验的接口和内存布局，再迁移实际代码。
完成后检查差异并编译相关文件，说明验证结果和未验证部分。


[RELY]
依据当前工程中的真实定义：

1. 迁移来源：
   lab2 分支中的 lab2_new/kern/mm/pmm.c。

2. 设备树接口：
   uint64_t get_memory_base(void);
   uint64_t get_memory_size(void);
   kern_init() 已在 pmm_init() 之前调用 dtb_init()。

3. 地址及页面相关定义：
   KERNBASE、KERNTOP、PHYSICAL_MEMORY_OFFSET、
   DRAM_BASE、PGSIZE、PPN、ROUNDUP、ROUNDDOWN 和 PADDR。

   Lab3 尚未定义旧实现依赖的：
   KERNEL_BEGIN_PADDR、PHYSICAL_MEMORY_END。
   应根据当前虚拟地址布局和虚实偏移补齐。

4. 数据结构和接口：
   struct Page 包含 ref、flags、property 和 page_link。
   可使用 set_page_ref()、SetPageReserved()、list_init()、
   pa2page()、init_memmap()、panic() 和 cprintf()。

5. 全局变量含义：
   va_pa_offset：内核直接映射的虚拟地址减物理地址。
   pages：Page 描述符数组的虚拟地址。
   nbase：DRAM 起始物理页号。
   npage：可管理物理页号的排他上界，
          描述符数量为 npage - nbase。

6. Lab3 的 alloc_pages()、free_pages() 和 nr_free_pages()
   已包含 local_intr_save()/local_intr_restore()，需要保留。


[GUARANTEE]
必须迁移的函数：
static void page_init(void);

必要的配套修改：
补齐 KERNEL_BEGIN_PADDR 和 PHYSICAL_MEMORY_END 的定义，
使其与当前 KERNBASE、KERNTOP 和 PHYSICAL_MEMORY_OFFSET 一致。

辅助函数要求：
优先复用已有接口，本任务不需要新增对外接口。


[SPECIFICATION]
函数：page_init()

Pre-Condition（前置条件）：
- 设备树初始化流程已经执行，但仍需校验读取到的内存信息。
- 当前物理内存管理器已完成 init()。
- 启动页表已建立内核直接映射。
- 链接符号 end 表示当前内核镜像的结束位置。

Post-Condition（后置条件）：
- 正确设置 va_pa_offset、npage 和 pages。
- 描述符数组位于内核镜像之后，且完全落在可管理内存内。
- 描述符初始化时具有确定的引用计数、标志、property 和链表状态。
- 固件、内核、描述符数组及不足一页的边界空间不进入空闲区。
- 合法的完整空闲页通过 init_memmap() 注册给当前管理器。

Case 1（正常情况）：
- 内存信息合法且空间充足。
- 根据当前内核 end 动态计算描述符位置和空闲页范围。
- 输出探测范围、管理上界、描述符位置和空闲页数量。

Case 2（异常与边界情况）：
- 内存信息为空、地址运算溢出、内存无法覆盖内核、
  描述符空间不足或没有完整空闲页时，通过 panic() 终止。
- 所有容量和溢出检查必须先于描述符数组写入。
- 不得以零页面数量调用 init_memmap()。

Requirements（其他约束）：
- 在首次使用 PADDR() 前设置 va_pa_offset。
- 正确区分虚拟地址、物理地址、物理页号和描述符地址。
- 管理上界向下按页对齐，空闲起点向上按页对齐。
- 保留 Lab2 实现已有的安全检查。
- 使用 Lab3 编译参数验证 pmm.c。
- 若其他未完成模块妨碍整体编译或运行，应明确说明，
  不得把单文件编译成功描述为整个 Lab3 测试通过。