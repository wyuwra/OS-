# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab2：物理内存管理 |
| **小组成员** | 2412832-王雅枫、2413276-胡楚骅、2412737-罗元伟 |
| **完成日期** | 2026-10-22 |
| **本版更新日期** | 2026-10-21 |
| **本版范围** | 模块一及其 Challenge、First-Fit 分析、Best-Fit、Buddy、SLUB 实现与测试已有；本机复核通过，MMU 现场调试及九张截图已补齐；默认 Buddy 的评分选择和部分交付材料仍待收尾 |

### 小组分工

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2412832-王雅枫 | 模块一：`page_init()`，物理内存布局与初始化；对应 Challenge；分支任务：GDB 调试页表查询过程 |
| 2413276-胡楚骅 | 模块二前两项：First-Fit 分析、Best-Fit 实现；实验报告整合 |
| 2412737-罗元伟 | 模块二第三项：Buddy 及对应 Challenge |

王雅枫负责模块一设计、实现、测试、提示词与证据，以及对应 Challenge 和分支任务的报告内容；胡楚骅负责 First-Fit、Best-Fit 对应算法分析、代码说明及测试，并负责实验报告整合；罗元伟负责 Buddy 及对应 Challenge 的实现、提示词、测试与报告说明。


本报告按照仓库 `main` 分支的《实验报告模板.md》组织。报告、提示词和证据统一放在仓库根目录 `report/`，源码位于根目录 `lab2_new/`。2026-10-21 已按小组当前要求合并两处报告；历史提示词与原始日志保留当时路径。

## 一、实验目的

1. 理解物理内存的管理方法，掌握根据 DTB 提供的内存信息初始化页描述符、保留已用内存和登记可用物理页的过程。
2. 理解页表的建立和使用方法，明确虚拟地址、物理地址、物理页号与 `struct Page` 页描述符之间的关系，理解内核启动时建立的页表映射。
3. 理解页面分配算法，分析 First-Fit，实现并接入 Best-Fit 和 Buddy 分配器，掌握连续物理页的分配、拆分、回收与合并，比较不同分配策略及其碎片问题。
4. 通过选做的 SLUB 扩展任务，理解页级分配与内存对象分配的关系，以及在页分配器基础上管理不同大小对象的方法。
5. 使用 GDB 调试 QEMU 的地址翻译过程，观察 TLB 查询与页表遍历，理解 Sv39 下虚拟地址到物理地址的转换机制。
6. 学习根据功能需求与接口约定编写提示词，借助 AI 实现内存管理模块、设计测试并辅助调试，验证生成代码的正确性。

## 二、实验环境

### 2.1 模块一实际验证环境

| 项目 | 实际环境 |
|---|---|
| 主机与终端 | Windows，PowerShell；实验构建与调试在 WSL Ubuntu 内执行 |
| 目标架构 | RISC-V RV64，QEMU `virt` |
| 交叉编译器 | SiFive `riscv64-unknown-elf-gcc` 8.3.0 |
| 模拟器 | QEMU 4.1.1 |
| 固件 | QEMU 配套 OpenSBI v0.4，串口报告 SBI 0.1 |
| 调试器 | GNU gdb-multiarch 15.1 |
| 物理页大小 | 4096 字节 |
| 源码目录 | `lab2_new/` |
| 验证方式 | Linux 临时副本中构建与运行，原源码哈希核对，日志回存本报告目录 |

工具版本分别记录在[自动验证汇总](evidence/module1/results.json)与[GDB 汇总](evidence/module1-gdb/results.json)。成员二、三的环境差异可按实际运行情况补充。

### 2.2 AI 工具

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2412832-王雅枫 | Codex | GPT 6.1 Sol | |
| 2413276-胡楚骅 | ChatGPT、Codex（VS Code 插件） | GPT 6.1 Sol | |
| 2412737-罗元伟 | Codex（VS Code 插件）、Claude（VS Code 插件） | GPT 6.1 Sol、Opus 5.5 | |

工具与模型名称沿用本次远程报告中组员填写的信息；历史提示词中的当时记录保留。

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

Lab1 完成了内核启动和基本输出，Lab2 则围绕物理内存管理展开，解决内核如何确定可用内存范围，以及如何组织、分配和回收这些内存的问题。实验通过页描述符记录物理页的状态，将固件、内核和页描述符占用的内存保留，再把其余可用物理页交给分配器管理。

在此基础上，通过分析 First-Fit、实现 Best-Fit 和 Buddy，理解不同策略如何选择空闲块、拆分和回收内存，以及如何处理碎片问题；选做的 SLUB 则进一步利用页分配器管理不同大小的内存对象。同时，结合启动页表和 QEMU 地址翻译调试，理解内核使用的虚拟地址如何映射到物理内存，明确物理内存分配与地址映射各自的作用及联系。

### 3.2 功能的逐步实现

1. **首先完成物理内存初始化。** 根据 DTB 提供的内存范围建立页描述符，保留固件、内核及描述符占用的内存，将可用物理页交给分配器。只有先明确哪些页可以使用，后续分配和回收才有可靠的基础。

2. **接着分析 First-Fit，再实现 Best-Fit。** 通过已有 First-Fit 代码理解空闲块的组织、分配、拆分、回收与合并，并在相同管理接口下实现 Best-Fit，比较两种策略选择空闲块的区别。

3. **随后实现 Buddy 分配器并设计测试。** 在掌握连续物理页分配后，进一步使用按大小分阶的方式组织空闲块，通过拆分和伙伴合并完成分配与回收，并验证其核心行为及边界情况。

4. **在页分配器基础上完成选做的 SLUB。** 前面的分配器以页为单位提供内存，SLUB 则利用这些页面管理不同大小的内存对象，因此需要先具备可用的页分配与回收接口。

5. **结合启动页表进行地址翻译调试。** 页表映射在进入内核 C 代码之前已经启用，是内核访问内存的基础。通过调试 QEMU 中的 TLB 查询与页表遍历，将地址翻译原理与实际访存过程对应起来。该调试任务可与分配器实现分别开展，不必等到所有分配算法完成后再进行。

## 四、实验内容与实现

### 4.1 功能模块一：物理内存初始化

**负责人：** 2412832-王雅枫

**修改文件：** [kern/mm/pmm.c](../lab2_new/kern/mm/pmm.c)。核心函数：

```c
static void page_init(void);
```

#### 4.1.1 地址和页描述符的含义

| 对象 | 含义 | 当前框架的关系 |
|---|---|---|
| VA | 内核使用的虚拟地址 | 直接映射区内 `VA = PA + va_pa_offset` |
| PA | 物理字节地址 | `PADDR(VA)` 从内核直接映射地址减去偏移 |
| PPN | 绝对物理页号 | `PA >> PGSHIFT` |
| `nbase` | DRAM 起始页号 | `0x80000000 / 4096 = 0x80000` |
| `npage` | 管理结束位置的绝对页号，不包含该页 | 默认 128 MiB 时为 `0x88000` |
| `pages` | 页描述符数组的内核虚拟地址 | 描述符数量是 `npage - nbase` |
| `pages[i]` | 第 i 个描述符 | 对应 PA `(nbase + i) * PGSIZE` |

`npage` 不能直接当作描述符数量。也不能把 `struct Page *` 当作其所描述物理页的起始地址：描述符是管理信息，本身与被管理页位于不同位置。

#### 4.1.2 行为规格

| 类别 | 约束 |
|---|---|
| 前置条件 | 启动分页和栈可用，DTB 已解析，当前分配器已初始化；沿用课程单段 RAM 与固定内核映射布局 |
| 成功后置条件 | 偏移、页号上界、描述符地址正确；所有描述符先初始化为保留；仅将元数据之后的完整空闲页交付分配器 |
| 错误处理 | DTB 信息无效、范围溢出、无法容纳内核或描述符、没有完整空闲页时明确 `panic` |
| 接口约束 | `init_memmap(base, n)` 的 `n > 0`；输入描述符范围合法，交付前每页均为保留状态 |
| 不变量 | 容量检查先于数组写入；不释放内核和元数据占用页；不硬编码描述符结构体大小 |

#### 4.1.3 布局计算与实现顺序

设 DTB 报告起点为 `B`、大小为 `S`，`P = PGSIZE`：

```text
raw_end          = B + S                  （先检查加法溢出）
managed_end      = ROUNDDOWN(min(raw_end, PHYSICAL_MEMORY_END), P)
descriptor_count = PPN(managed_end) - nbase
pages_va         = ROUNDUP(end, P)
pages_pa         = PADDR(pages_va)
descriptor_bytes = descriptor_count * sizeof(struct Page)
metadata_end     = pages_pa + descriptor_bytes
free_begin       = ROUNDUP(metadata_end, P)
free_count       = (managed_end - free_begin) / P
```

所有内存区间采用**左闭右开**表示法，结束地址不属于区间。

实现分为六步：

1. **读取并检查 RAM。** 设置 `va_pa_offset` 后才使用 `PADDR`；拒绝零起点、零大小和范围加法溢出。
2. **确定管理结束位置。** 限制在框架上界 `0x88000000` 内，并向下页对齐。检查 RAM 能覆盖固定内核起点和实际内核末尾。
3. **放置描述符。** 将 `pages` 放在链接符号 `end` 之后的首个页边界，使用实际 `sizeof(struct Page)` 计算容量。
4. **先检查，后写入。** 对齐和乘法可能溢出时提前拒绝；用剩余空间比较避免容量加法溢出。确定存在至少一页空闲内存后再写描述符。
5. **默认全部保留。** 清零引用计数、标志和 `property`，初始化链表节点，再设置保留位。这样尚未确认安全的页不会被提前开放。
6. **登记完整空闲区。** 元数据末尾向上对齐，调用 `init_memmap(pa2page(free_begin), free_count)`。具体分配器清除空闲页保留位，建立空闲块及计数。

内存布局如下；页描述符对应整个管理范围，描述符数组本身也占用其中的 RAM：

```text
0x80000000          0x80200000        pages_pa          free_begin        managed_end
    | 固件预留区域       | 内核、栈、页表等 | 页描述符及对齐尾部 | 可分配完整页       |
    +------------------+----------------+------------------+------------------+
    |<-------------------- 保留 -------------------------->|<---- 空闲 ------>|
```

前 2 MiB 是课程框架的固定预留范围；这不表示 OpenSBI 实际占满 2 MiB。当前运行固件报告自身大小为 112 KiB。

#### 4.1.4 为什么要一处向下对齐、一处向上对齐

RAM 结束位置向下对齐，是因为不足一页的尾部不能分配为完整页。描述符末尾向上对齐，是因为只要某一页仍含元数据，该页就不能交给分配器。两者分别避免分配越界和覆盖管理信息。

描述符初始化属于一次启动过程，时间复杂度为 `O(npage - nbase)`，元数据空间复杂度也为 `O(npage - nbase)`。当前 RV64 构建实际 `sizeof(struct Page) = 40` 字节，128 MiB 对应 32768 个描述符，占用 1310720 字节；实现使用 `sizeof`，没有写死 40。

#### 4.1.5 最终提示词整理版

以下是依据实际请求与后续复核整理的复用版本；实际发送的原文及每轮结果保存在 [prompt.md](prompt.md#raw-inputs)，不把整理版当作历史逐字输入。

```text
[PROMPT]
任务：完成 Lab2 模块一 static void page_init(void)，为后续分配算法建立可靠基础。
功能目标：依据 DTB 计算可管理 RAM，安置并初始化 Page 数组，登记完整空闲页。
修改范围：kern/mm/pmm.c；辅助验证工具放 tools，报告与证据放 lab2/report。
操作要求：先核对地址转换与分配器接口；显式处理边界；保留官方测试与代码标记。
验收方式：正常 RAM、不同容量、非页对齐、容量不足和溢出检查；GDB 核对交付前后状态。

[RELY]
已有 DTB getter、PADDR/PPN/pa2page、链接符号 end、PGSIZE、struct Page 与 pmm_manager。
启动映射已建立，DTB 已解析，分配器 init 已执行；当前采用单段 RAM 与固定内核映射。

[GUARANTEE]
先验证布局容量再写数组；保留固件预留区、内核和元数据页；不向 init_memmap 传零页。
采用实际 sizeof(struct Page)；区分 VA/PA/PPN/描述符指针；保持分配器接口及官方测试。
仅在临时副本隔离未完成的队友代码；结果与日志必须来自真实运行。

[SPECIFICATION]
对象：static void page_init(void)
Pre-Condition：上述启动、DTB、分配器前置条件成立。
Post-Condition：offset、npage、pages 正确，全部描述符先保留，仅开放完整空闲页。
Case 1：有效且足够的 RAM，按框架上限和页边界初始化并打印布局。
Case 2：无效 DTB、溢出、容量不足或没有完整空闲页，明确 panic，禁止错误交付。
Requirements：不覆盖已用页，验证前后核对源文件哈希；未知学号保留占位。
```

#### 4.1.6 实际实现与复核过程

| 阶段 | 实际工作 | 发现与处理 |
|---|---|---|
| 初次实现 | 完成布局、描述符初始化、边界拒绝和输出；建立七组隔离验证 | 验证器读写 Windows 挂载目录中的活动日志出现 `Errno 61`，改为在 Linux 临时目录轮询，进程结束后保存日志；七组通过 |
| 对齐复核 | 增加 `ram64-unaligned`，报告 RAM 大小减 1 字节 | 八组通过；澄清 `npage` 与偏移的注释，未修改函数执行逻辑 |
| 接口与证据复核 | 增加交付前逐页检查、元数据容量不足案例、结果状态及源文件哈希 | 九组通过；故意在副本删除链表初始化，检查器捕获错误，失败结果覆盖预置的旧 PASS |
| 本次补齐交付 | 实际 GDB 核对、完整重跑九组、正式报告和证据图 | GDB 交付前后与原自检通过；验证过程未修改原工程源码 |

后两次质量复核补强了测试，未发现需要修改 `page_init()` 执行逻辑的功能缺陷。故意破坏副本的子验证结果为 FAIL 是检查器自检的预期结果，见[自检记录](evidence/module1/checker-selftest.json)，不是实际实现失败。

<a id="module1-challenge"></a>

#### 4.1.7 模块一 Challenge：硬件可用物理内存范围的获取方法（思考题）

**负责人：** 2412832-王雅枫

**题意：** OS 没有预先写死当前机器的可用物理内存范围时，如何在启动时获得这些范围？本题要求分析方法，不要求在当前 RISC-V 内核中实现所有平台的探测接口。[课程模块一要求](http://oslab.mobisys.top/lab2026/_book/lab2/lab2_2_1_exercise.html)。

**回答：** 应优先使用平台启动协议提供的内存布局，由固件或引导程序把实际 RAM 的起点、长度和保留信息传给内核。具体接口取决于平台：

| 方法 | 取得什么信息 | 适用范围与限制 |
|---|---|---|
| 设备树 DTB | `/memory` 节点的 `reg` 描述 RAM 地址与长度，可以有多段；保留区还需结合保留信息排除 | 本实验采用此路径。应按地址/大小单元编码解析，不能将所有设备树固定视为一组 64 位起点和大小 |
| UEFI 内存映射 | 引导阶段调用 `GetMemoryMap()` 得到带类型的内存描述符，再将其传给内核 | 适用于 UEFI 启动；需要遵守退出启动服务的时序，区分可用内存、运行时保留和不可用区域 |
| 传统 PC BIOS E820 | 引导阶段通过 `INT 15h, E820h` 逐项获取物理地址区间及类型 | 适用于传统 IA-PC 实模式引导环境，本实验的 RISC-V 内核不能直接调用 |
| 平台专用硬件信息 | 在有芯片手册及访问权限时，结合内存控制器配置和 SoC 地址映射获取 RAM 分布 | 依赖具体硬件；容量信息还需结合地址洞、固件预留与当前占用，才能形成可分配范围 |

设备树对 RAM 与保留区的定义见 [Devicetree 节点规范](https://devicetree-specification.readthedocs.io/en/latest/chapter3-devicenodes.html#memory-node)；UEFI 方法见 [UEFI 内存分配服务](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html#memory-allocation-services)；E820 的适用环境与区间枚举见 [ACPI 系统地址映射接口](https://uefi.org/specs/ACPI/6.6/15_System_Address_Map_Interfaces.html#int-15h-e820h-query-system-address-map)。

**本实验的实际路径：** `entry.S` 保存入口参数 `a1` 中的 DTB 物理地址；`dtb_init()` 验证并解析 DTB，保存 RAM 起点与大小；`page_init()` 通过 `get_memory_base()`、`get_memory_size()` 查询这些结果，再计算可管理范围。因此，内核无需把实际 RAM 大小写死；但当前框架仍有固定管理上限，且现有简化解析器只取得首组 RAM 范围，并未实现完整的多段内存与保留区解析。

**基于 `1e9c4da` 版本的核对（2026-10-21）：** 该版本选择 `buddy_pmm_manager`；`dtb.c`、`entry.S` 和 `page_init()` 的主体与模块一原实现一致。因此，切换 Best-Fit/Buddy 不改变“启动协议传入内存描述 → DTB 解析 → 排除占用 → 交付物理页”的发现流程。当前 getter 返回的是 DTB 中实际解析的起点和大小；`PHYSICAL_MEMORY_END` 则是框架管理上限，不能将其当作探测结果。

**题目中的“不预先知道”有两种情况：**

1. **内核未写死容量，但固件/引导程序提供描述。** 内核按约定找到描述并解析即可，本实验属于这种情况；实际布局可随 QEMU 配置变化。
2. **连可信描述都没有。** 需要平台专用信息或引导程序支持；UEFI、E820 是各自启动环境下的替代途径，不能直接搬到当前 OpenSBI/RISC-V 环境中调用。完全没有平台信息时，无法用一段通用试写程序可靠推断任意机器的可分配物理地址范围。

**从“存在 RAM”到“可以分配”还需要处理什么？**

```text
获取带类型的内存区间列表
  → 检查零长度、溢出、重叠及平台管理上限
  → 选取允许使用的 RAM
  → 扣除固件预留、内核、栈、页表、描述符及仍需保留的启动数据
  → 每个剩余区间起点向上、末尾向下页对齐
  → 按合法区间登记空闲页，避免把内存空洞连成可分配块
```

上述是通用处理思路。设备树场景还应处理 `/reserved-memory` 与 DTB 保留表，并确保 DTB 本身在使用结束前不被覆盖，参见 [DTB 保留块规范](https://devicetree-specification.readthedocs.io/en/v0.2/flattened-format.html#memory-reservation-block)。当前课程实现只完成约定的单段 RAM 与固定预留布局，不声称已经加入以上全部通用能力。

**有内存空洞时如何处理？** 以下仅为说明算法的假设示例，不是本次 QEMU 实际探测结果。若 RAM 为 `[0x80000000, 0x88000000)`，扣除前部固件、内核和描述符后，空闲起点为 `free_begin`；另有保留洞 `[0x84000000, 0x84200000)`，且这些边界均已页对齐，那么可登记区间应为：

```text
[free_begin, 0x84000000) ∪ [0x84200000, 0x88000000)
```

应分别计算每个区间的页数、验证对应描述符，再交付给支持这些区间的分配器。不能把起点到最高末尾之间的全部地址作为一个空闲块，也不能仅凭总 RAM 容量确定可分配范围。若扩展当前实现，还需将简化 DTB 解析器与单区间初始化逻辑一起适配，保持空洞描述符为保留状态。

**能否从页表或 `satp` 反推 RAM 大小？** 不能。`satp` 提供分页模式和根页表物理页号；页表项描述地址转换。当前 `entry.S` 用一个根级叶子项描述 1 GiB 映射，但默认实际 RAM 是 128 MiB，PMM 仍应依据 DTB 及保留信息管理内存。映射覆盖某个地址，并不证明那里存在 RAM，更不证明它可以分配。

**能否直接从低地址往高地址读写来探测？** 在已知候选范围、具备异常恢复和平台保障的条件下，可以讨论保存原值、写入模式、读回比较和恢复的受控探测。但将这种方法推广到未知整个地址空间并不可靠：访问可能触及设备 MMIO、覆盖固件或内核、发生总线异常或地址别名；即使读写成功，也不能说明该内存未被其他组件占用。因此推荐采用固件/引导程序提供的描述，再做区间校验；完全缺少可信平台信息时，应停止或使用明确的板级配置，而不能凭访问成功就认定可用。

**结论：** 内存发现提供资源描述，物理内存初始化据此扣除保留和占用区域，最终得到可以交给分配器的完整页。本题以思考题形式完成，未新增 UEFI/E820/硬件试写代码，也不需要额外运行截图。

### 4.2 功能模块二：物理内存分配

#### 4.2.1 任务一：First-Fit 分析

**负责人：** 2413276-胡楚骅

**（1）空闲页初始化**

First-Fit 分配器首先通过 `default_init()` 初始化双向循环链表，并将空闲页总数置为零。

随后，`default_init_memmap()` 检查传入的页数 `n` 是否大于零，并初始化这段连续物理页对应的描述符，清除标志和 `property` 字段，将引用计数置零。之后，将这段空闲区的首页标记为空闲块首页，用其 `property` 记录块内页数，并增加空闲页总数。

最后，将该空闲块加入链表。如果链表为空，直接插入首页的链表节点；否则遍历已有空闲块，按照地址递增顺序找到位置并插入。

**（2）分配连续物理页**

`default_alloc_pages()` 首先检查申请页数 `n` 是否大于零；如果申请量超过空闲页总数，则返回 `NULL`。随后，从空闲链表头开始遍历，找到第一个页数不少于 `n` 的空闲块；如果没有满足条件的连续块，同样返回 `NULL`。

找到后，将该块的首页节点从链表中摘下，把前 `n` 页分配给调用者。如果仍有剩余页，则以剩余区域的第一页作为新空闲块首页，记录剩余页数、设置首页标志，并将其插回原链表位置。

最后，将空闲页总数减少 `n`，清除已分配区域首页的空闲块首页标志，并返回其 `Page` 描述符指针。

**（3）回收页面与合并相邻空闲块**

`default_free_pages()` 首先检查归还页数 `n` 是否大于零，并逐页检查其不是保留页或已登记的空闲块首页。随后清除相关状态标志，将引用计数置零，以传入区域的第一页作为空闲块首页，记录块内页数、设置首页标志，并增加空闲页总数。之后按照地址递增顺序，将该块的首页节点插入空闲链表。

插入后，先检查能否与前一个空闲块合并。如果前块的起始描述符加上块内页数，恰好等于本块的起始描述符，说明两个块在物理位置上连续。此时将本块长度累加到前块，清除本块的首页标志并摘下其节点，再将当前块指针更新为合并后的首页。

随后，以更新后的当前块检查后一个空闲块。如果当前块的起始描述符加上块内页数，恰好等于后块的起始描述符，则累加后块长度，并清除后块的首页标志、摘下其节点。合并只改变空闲块的组织方式，不再增加空闲页总数。

**（4）First-Fit 算法的改进空间**

First-Fit 在找到第一个满足申请量的空闲块后就进行分配，拆分后可能留下较小的碎片。可以考虑改为选择最大的空闲块，使分配后的剩余块仍能满足多种大小的申请，即 Worst-Fit 策略。但这种方式也可能提前拆散大块，影响后续较大连续空间的申请，因此效果取决于实际申请序列。

查找开销也有改进空间。当前实现每次从链表头开始遍历，可以记录上次查找的位置，采用 Next-Fit 减少重复扫描，但最坏情况下仍需遍历全部空闲块。也可以按块大小分组，跳过明显不足的块，不过需要在拆分、释放和合并时维护分组，并可能改变原有的首次适配选择顺序。

#### 4.2.2 任务二：Best-Fit 实现

**负责人：** 2413276-胡楚骅

**（1）实现与提示词设计**

本模块通过双向循环链表管理空闲物理页，链表按照地址递增排列，每个节点代表一个连续空闲块的首页。首页的 `property` 记录块内页数，`nr_free` 记录全部空闲页总数。初始化时先建立空链表，再接收 `page_init()` 交付的可用物理页，将其登记为空闲块。

与 First-Fit 找到第一个足够大的块就分配不同，`best_alloc_pages()` 需要比较满足申请量的候选块，选择其中页数最少的一块，大小相同时选择地址较低的块。遇到恰好满足申请量的块时，可以提前结束查找。选中后分配前 `n` 页，若有剩余，则重新设置剩余块的首页和长度，放回空闲链表；若没有合适的连续块，则返回 `NULL`，不改变原有管理状态。

释放时，将归还区域按地址顺序插入链表，并检查它与前后空闲块在物理位置上是否连续，能合并时扩大块长度并移除多余节点。保持链表按地址排列，既方便判断相邻关系，也保证同大小候选块按低地址优先选择。最后通过 `best_fit_pmm_manager` 注册各管理接口，并在 `pmm.c` 中切换分配器，使上层仍通过原有接口完成申请和释放。

提示词采用 `[PROMPT]`、`[RELY]`、`[GUARANTEE]` 和 `[SPECIFICATION]` 四段式结构，分别说明任务范围、已有依赖、交付接口和行为规格。规格重点规定了最小适配规则、拆分后剩余块的状态、释放后的相邻合并，以及分配失败时链表和计数保持不变，未逐行限定具体实现步骤。

修改范围限定为 Best-Fit 实现区和分配器选择位置，要求保留两个已有测试函数、公共接口和队友完成的 `page_init()`。验收要求包括正常编译运行、确认实际使用 Best-Fit，并通过已有自检和评分测试。

<details>
<summary>本任务使用的完整提示词</summary>

````text
[PROMPT]

任务：
在操作系统 Lab2 工程中，实现完整的 Best-Fit 连续物理页分配模块，并接入内核。

工程目录为：
`/home/sven1/course-project/os/OS-/lab2/lab2_new`

以下文件路径均相对于该目录：
- 在 `kern/mm/best_fit_pmm.c` 的 `//lab2 function2-2 YOUR CODE` 标记处补全实现。
- 定义 `best_fit_pmm_manager`，并在 `kern/mm/pmm.c` 的 `init_pmm_manager()` 中切换使用它。

Best-Fit 的选块规则是：在所有页数不少于申请量的空闲块中，选择页数最少的一块；大小相同时选择地址较低的块。成功时分配恰好 `n` 个连续物理页，剩余部分继续作为空闲资源管理。

操作要求：
- 必须修改项目中的实际文件，不能只在对话中展示代码。
- 开始前检查当前分支、工作区和相关源码，保留已有未提交修改。
- 修改范围限于 `best_fit_pmm.c` 的待实现区域，以及 `pmm.c` 中 `init_pmm_manager()` 的分配器选择。
- 不得修改已有的 `basic_check()`、`best_fit_check()`，不得修改公共头文件中的接口、数据结构和宏定义。
- 不修改队友已完成的 `page_init()`、First-Fit 实现、构建与测试脚本、报告和提示词记录。
- 保留任务注释，将本任务标记中的 `YOUR CODE` 替换为学号 `2413276`。
- 可以添加本模块内部需要的静态辅助函数和管理状态，不引入无关功能、依赖或跨文件接口。
- 暂不执行 Git 提交或推送。

输出与验收要求：
1. 交付完整实现，满足 `basic_check()`、`best_fit_check()` 使用的状态约定，并通过两个测试，不得修改测试以获得通过。
2. 必须将 `pmm_manager` 设置为 `&best_fit_pmm_manager`，保证实际运行和检查的是 Best-Fit。
3. 完成编译和运行验证，终端输出应能明确显示当前策略为 `best_fit_pmm_manager`，并观察分配器自检结果。
4. 使用现有 `make grade` 入口进行验收，核对 Page Init 和 Best-Fit 的实际结果；不另建测试框架。
5. 最后说明修改文件、主要内容、验证命令和实际结果。失败或无法执行的检查须说明原因，不得把未验证或单纯超时写成通过。

环境：
- 编译与运行在 WSL Ubuntu 中进行。
- 已有 RISC-V 工具链目录：
  `/home/sven1/course-tools/riscv-elf-toolchains/bin`
- 本实验使用的 QEMU：
  `/home/sven1/course-tools/qemu-4.1.1-lab1/install/bin/qemu-system-riscv64`
- 运行前确认实际使用的工具路径及 QEMU 版本。需要调整时，只修改当前终端环境，不修改全局配置。
- 可使用现有 `make`、`make qemu`、`make grade`。内核正常运行后会进入循环，应根据实际输出判断结果，并只结束自己启动的测试进程。

[RELY]

以当前工程真实定义为准，使用以下已有接口、数据结构和工具，不重复定义或修改它们。

1. 分配器接口
- `kern/mm/pmm.h` 中的 `struct pmm_manager` 提供 `name`、`init`、`init_memmap`、`alloc_pages`、`free_pages`、`nr_free_pages`、`check` 成员。
- `kern/mm/best_fit_pmm.h` 已声明 `best_fit_pmm_manager`，其定义需要在本模块中补全。
- `kern/mm/pmm.c` 中的上层分配、释放和计数接口，通过当前 `pmm_manager` 调用具体分配器。

2. 页描述符与空闲区结构
- `kern/mm/memlayout.h` 提供 `struct Page`，包含 `ref`、`flags`、`property`、`page_link`。
- `Page*` 指向描述符，不是物理页的起始地址；描述符在 `pages` 数组中的顺序对应物理页顺序。
- 空闲块首页通过 `PG_property` 标识，其 `property` 记录块内连续页数；非首页不能仅凭该字段判断块大小。
- `free_area_t` 提供空闲链表头 `free_list` 和空闲页总数 `nr_free`。Best-Fit 的具体管理状态需要在本模块内定义。

3. 已有工具
- `libs/list.h` 提供链表初始化、插入、删除、判空和前后遍历操作。
- `kern/mm/memlayout.h` 提供保留位、空闲块首页标志的检查与修改宏，以及 `le2page`。
- `kern/mm/pmm.h` 提供 `page_ref`、`set_page_ref`、`page2pa`，以及测试使用的分配与释放接口。
- `pages`、`npage`、`nbase` 等由框架维护，不在 Best-Fit 中重复定义或改写。`npage` 是物理页号上界，不是空闲页数。

4. 初始化交接约定
- 分配器的 `init()` 先执行，随后已有 `page_init()` 通过 `init_memmap(base, n)` 交付可用的连续物理页。
- 交付时 `n > 0`，描述符范围有效，各页处于保留状态。本模块负责登记空闲资源，不重新实现 DTB 解析或内存布局计算。

5. 参考实现与测试
- `kern/mm/default_pmm.c` 提供完整 First-Fit，可参考其接口接入和状态维护方式，但应检查其逻辑，不能机械复制。
- `best_fit_pmm.c` 已提供 `basic_check()` 和 `best_fit_check()`。测试直接引用 `free_list`、`nr_free`，实现必须提供符合其预期的状态，且不得修改测试。
- `tools/` 中已有本实验的验收入口和测试文件。

[GUARANTEE]

在 `best_fit_pmm.c` 中实现以下模块内部函数：

```c
static void best_init(void);
static void best_init_memmap(struct Page *base, size_t n);
static struct Page *best_alloc_pages(size_t n);
static void best_free_pages(struct Page *base, size_t n);
static size_t best_nr_free_pages(void);
```

定义 `const struct pmm_manager best_fit_pmm_manager`：
- `.name` 为 `"best_fit_pmm_manager"`。
- 管理接口绑定到上述对应函数。
- `.check` 绑定已有的 `best_fit_check`，不重新编写检查函数。

提供测试所需的 `free_list`、`nr_free` 内部状态，可使用模块内的 `static free_area_t free_area` 及相应宏。

在 `pmm.c` 的 `init_pmm_manager()` 中，将当前分配器切换为 `&best_fit_pmm_manager`。

[SPECIFICATION]

函数：`best_init(void)`

Pre-Condition：
- 在登记可用物理页之前调用，本分配器尚未管理已交付的空闲块。
- 不要求空闲链表预先初始化。

Post-Condition：
- `free_list` 为合法的空双向循环链表，前后指针均指向链表头。
- `nr_free` 为 0。

函数：`best_init_memmap(struct Page *base, size_t n)`

Pre-Condition：
- `best_init()` 已完成，当前空闲链表及计数有效。
- `n > 0`；`base` 指向有效描述符数组中连续 `n` 个描述符，对应已确认可用的连续物理页。
- 这些页均处于保留状态，不与已登记的空闲区域重叠。

Post-Condition：
- 交付页的描述符标志、`property` 和引用计数完成初始化，不再处于保留状态。
- 以 `base` 为该空闲块首页，设置首页标志，`base->property = n`；其余页不标记为块首页。
- 首页节点加入空闲链表，保持地址递增顺序。
- `nr_free` 比调用前增加 `n`，已有空闲资源不丢失、不重复登记。

Requirements：
- `n == 0` 或交付页不处于保留状态时，按现有框架约定断言失败。
- 不清零物理页内容，不修改交付范围之外的描述符状态；必要的链表连接更新除外。

函数：`best_alloc_pages(size_t n)`

Pre-Condition：
- 分配器已初始化，`n > 0`。
- 空闲链表按地址递增排列，各块不重叠，首页标志和块长度有效。
- `nr_free` 等于全部空闲块的页数之和。

Post-Condition：

Case 1：存在满足申请量的空闲块
- 在所有 `property >= n` 的块中，选择 `property` 最小的一块；大小相同时选择地址较低的块。
- 从选中块起始位置分配恰好 `n` 个连续物理页，返回首页的 `Page*`。
- 已分配区域不再属于空闲链表，原首页的空闲块首页标志被清除。
- 若原块恰好为 `n` 页，不产生剩余块；否则剩余部分以 `page + n` 为首页，正确设置块长度和首页标志，并登记回空闲链表。
- `nr_free` 比调用前减少 `n`，其余空闲资源保持有效。

Case 2：无法满足申请
- `n > nr_free`，或总量足够但不存在至少 `n` 页的连续块时，返回 `NULL`。
- 不改变空闲链表、计数和页描述符状态。

Case 3：`n == 0`
- 断言失败，不作为正常分配请求处理。

Requirements：
- 返回描述符指针，不返回物理地址。
- 不清零物理页内容，不建立页表映射，不增加引用计数。

函数：`best_free_pages(struct Page *base, size_t n)`

Pre-Condition：
- 分配器已初始化，空闲链表及计数有效。
- `n > 0`；`base` 指向有效描述符数组中连续 `n` 页的起始描述符。
- 调用者保证该区域已分配、可以归还，且不与已有空闲块重叠。

Post-Condition：
- 归还页的相关标志和引用计数完成初始化，以空闲块形式重新登记。
- 空闲链表保持地址递增顺序。
- 与归还区域在物理位置上连续的前后空闲块均完成合并，不跨过占用页合并。
- 合并后的首页正确记录总页数并设置首页标志，被并入块的多余首页标志和链表节点被移除。
- `nr_free` 比调用前增加 `n`，合并不重复增加计数。

Requirements：
- 按现有框架约定，检查 `n > 0`，并逐页断言其不是保留页或已登记的空闲块首页。
- 上述断言不替代调用者对归还区域合法性的保证。
- 不清零物理页内容，不修改页表映射。

函数：`best_nr_free_pages(void)`

Pre-Condition：
- 分配器已初始化，`nr_free` 正确维护。

Post-Condition：
- 返回当前可分配的空闲物理页总数，不是空闲块数量。
- 不改变空闲链表、页描述符或计数。
````

</details>

完整过程记录见 [prompt.md 中成员二第 1 轮](prompt.md#member2-best-fit-round1)。

**（2）实现结果与验证**

将首轮提示词交给 VS Code 中的 Codex 后，完成了 `best_fit_pmm.c` 的分配器实现，并在 `pmm.c` 中切换为 `best_fit_pmm_manager`。原有的 `basic_check()`、`best_fit_check()` 和队友完成的 `page_init()` 保持不变。本次首轮实现通过检查，未进行追加修复。

随后在 WSL 中进行编译和运行验证，结果如下：

| 命令 | 实际结果 |
|---|---|
| `make -j2` | 编译、链接并生成内核镜像成功 |
| `make qemu` | 输出 `memory management: best_fit_pmm_manager`，确认使用 Best-Fit；随后输出 `check_alloc_page() succeeded!`，表明分配器自检完成 |
| `make grade` | Page Init、Best-Fit 均为 `OK`，总分为 `20/20` |
| `echo $?` | 紧接评分命令执行，输出 `0`，确认正常退出 |

正常启动与自检、评分结果分别保存在以下两张截图中。评分脚本通过说明当前实现满足其检查条件，不代表已经覆盖所有可能的申请和释放情况。

![Best-Fit 正常启动、内存布局与分配器自检](images/best-fit-boot.png)

![Page Init 与 Best-Fit 评分通过，总分 20/20，退出码为 0](images/best-fit-grade.png)

**（3）Best-Fit 算法的改进空间**

当前实现按照地址顺序维护空闲链表，分配时需要遍历各个空闲块，才能确定满足申请量的最小块，最坏查找复杂度为 O(k)，其中 k 为空闲块数量。可以增加按块大小组织的索引，例如使用按块大小和地址排序的平衡树，加快最小合适块的查找。但在拆分、释放和合并后，需要同步更新索引，会增加管理结构和维护开销。

此外，Best-Fit 只保证本次选择的块最合适，仍可能在拆分后留下较小的碎片，不能保证后续申请中的整体效果最优。可以针对单页等常见申请大小维护专门的空闲块索引，在保持最小适配规则的前提下减少查找开销。

#### 4.2.3 任务三：Buddy System 实现

**负责人：** 2412737-罗元伟

2026-10-21 已同步 `1e9c4da` 中的 `buddy_pmm.c`、`buddy_pmm.h`，当前 `pmm.c` 选择 `buddy_pmm_manager`。本机在该版本未修改的源码临时副本中编译和启动成功，Buddy 内置九组自检通过，输出 `BUDDY CHECK ALL PASSED` 以及 `check_alloc_page() succeeded!`。原源码验证前后哈希一致，见[本次结果](evidence/sync-20261021-buddy/results.json)和[串口日志](evidence/sync-20261021-buddy/qemu.log)。

**实现设计：** 使用按阶划分的空闲链表，order 阶对应 `2^order` 页。登记可用区间时按物理页号对齐与剩余长度逐段拆分，保留尾部可用页；申请时将页数向上取整到二次幂，必要时逐级拆分较大块；释放时通过绝对物理页号 XOR 定位伙伴，仅合并同阶、空闲且范围合法的块。相邻块不一定是伙伴，不能只按地址连续合并。

**接口约定：** 返回连续物理页的首页描述符，`nr_free_pages()` 按实际取整后的页数计数。释放须传原始首页和原始请求页数，例如申请 3 页实际占 4 页，释放仍传 3。零页、部分释放、重复释放或错误请求数属于断言拒绝的前置条件违反。

**测试与文档：** 已补充 [Buddy 独立设计与测试说明](buddy.md)，覆盖分阶、拆分、伙伴定位与合并、描述符状态、九组自检及证据索引。后续 SLUB 测试镜像也保留 Buddy 九组自检，日志显示 303 次测试区操作和 326 次结构审计通过；归档哈希与当前代码的核对需先统一 LF 换行；pmm.c 另有已记录的学号注释替换，执行逻辑不变，详见独立说明。本轮为文档补充，没有重新执行内核测试。Buddy 原始实现和测试两份完整实际提示词、最终运行截图仍需补齐，不以现有说明或日志冒充这些材料。

上述状态只描述当前已取得的文件，不代表队友独立工作区的实际进度。**Buddy 是模块二的必做任务。**

### 4.3 模块二 Challenge：SLUB（选做）

**负责人：** 2412737-罗元伟

2026-10-21 已按第一份 SLUB 实现提示词新增 `slub.c`、`slub.h`，在 `pmm_init()` 后接入八个尺寸类别的对象缓存与连续页大对象分配。随后按第二份实际提示词新增独立 `slub_test.c/.h`，`SLUB_TEST=1` 构建在初始化后执行 `slub_check()`：八组测试全部通过，完成 1831 次成功分配和对应释放、5502 次完整内容复核，空闲页恢复到测试镜像基线 31905。覆盖全部类别边界、完整区间读写与隔离、slab 扩展和状态变化、释放顺序、固定随机序列、Buddy 大对象页数取整、溢出和三次取页失败恢复。

保留默认 Buddy 及原有九组自检，未改动 `Page` 字段或页分配算法；本轮只增加受宏保护的测试辅助入口与取页包装，没有发现需要修复的分配器算法缺陷。普通构建关闭辅助入口后也编译启动通过。课程 `make grade SLUB_TEST=1` 为 10/20，其中 Page Init 通过，Best-Fit 项因原分支默认选择 Buddy 而未通过；评分副本同样到达 SLUB 最终成功标记。课程评分结果与新增 SLUB 测试结果分别记录。

简明设计与测试方向见[SLUB 工作总结](slub-summary.md)。接口、数据结构、内存布局和分配回收流程见[第一阶段实现记录](slub.md)，第二阶段的覆盖表、资源上界、真实命令和原始日志见[SLUB 内核测试记录](slub-tests.md)，两份完整实际提示词见[SLUB 完整提示词记录](prompt.md#slub-prompts)。实际截图与整组最终提交事项仍需补齐，不据测试通过宣称整组报告已完成。模块一的内存范围获取思考题见第 4.1.7 节。

### 4.4 分支任务：GDB 调试页表查询过程

**负责人：** 2412832-王雅枫

2026-10-21 已按实验书的三终端流程完成现场交互观察：终端一 `make debug` 启动 QEMU；终端二使用宿主 GDB 附加 QEMU；终端三通过 `make gdb` 控制 ucore。完成 Sv39 页表单步、取指和数据写入地址转换、软件 TLB 填充与命中，以及重新启动后的 S 模式 Bare 对比。九张图均为成员一自行操作并保存的终端截图，未裁剪或重绘；[观察结果与截图哈希](evidence/mmu-interactive-20261021/results.json)记录具体数值，不作为完整分配器回归测试的替代。

#### 4.4.1 环境与复现入口

宿主系统为 WSL Ubuntu；宿主调试使用普通 `gdb`，内核使用 `make gdb` 调用的 SiFive RISC-V GDB 8.3。QEMU 4.1.1 调试版位于：

```text
/home/wyuwra/.local/src/lab2-mmu/qemu-4.1.1/build-mmu-debug/riscv64-softmmu/qemu-system-riscv64
```

配置开启 `--enable-debug`，`file` 确认包含 `with debug_info, not stripped`；保留原安装版本，未执行安装。最初只构建 RV64；随后按实验书给出清理、同时配置 RV32/RV64、重新编译的手动步骤，成员一返回了调试信息检查结果，但未额外提供完整手动构建日志，因此不据此补造该轮日志。最初自动准备阶段的配置、构建日志和宿主预检保留于[准备记录](evidence/mmu-preparation-20261021/results.json)，其“仅准备”状态描述的是当时阶段。

终端一的启动方法如下；通过环境变量选择 QEMU，未修改 Makefile：

```bash
cd /mnt/c/Users/ASUS/Desktop/操作系统/实验/lab2_new
export PATH="$HOME/.local/opt/riscv/bin:$PATH"
export QEMU="$HOME/.local/src/lab2-mmu/qemu-4.1.1/build-mmu-debug/riscv64-softmmu/qemu-system-riscv64 -L $HOME/.local/opt/qemu-4.1.1/share/qemu"
make debug
```

终端二用 `pgrep -af '[q]emu-system-riscv64'` 查询本次 PID，再运行 `sudo gdb -q` 和 `attach 实际PID`。使用 `set pagination off`、`handle SIGPIPE nostop noprint`，设置条件断点后 `continue`。终端三在源码目录配置工具链 PATH 后 `make gdb`，连接成功再 `continue`。PID 每轮可能变化，不能把首次的 7708 或重新启动后的 8562 写死。

两个 GDB 的控制存在先后关系：宿主 GDB 停住 QEMU 时，内核 GDB 等待响应是正常的；要先让宿主 GDB `continue`，再在内核 GDB 发出控制命令。GDB 的分页提示中小写 `c` 表示继续显示且关闭分页，与 `(gdb)` 下执行内核的 `continue` 不同。

#### 4.4.2 取指调用链与条件断点

本轮 `kern_init` 地址为 `0xffffffffc0200404`。宿主条件断点如下：

```gdb
break riscv_cpu_tlb_fill if address == 0xffffffffc0200404
```

命中时类型为 `MMU_INST_FETCH`。`bt 6` 观察到取指/代码页查询慢路径：

```text
get_page_addr_code
  → tlb_fill
  → riscv_cpu_tlb_fill
  → get_physical_address
  → tlb_set_page
  → tlb_set_page_with_attrs
```

该链路属于取指/代码页查询；不能说所有数据加载、存储也必经 `get_page_addr_code`。数据写入另设条件断点观察，见 4.4.6。

![目标取指地址与 QEMU 调用栈](images/mmu-01-call-chain.png)

#### 4.4.3 satp、根页表与叶子项

在 `get_physical_address` 内检查 `env->satp=0x800000000008020a`：最高四位 MODE=8，为 Sv39；低 44 位 PPN 左移 12 位，得到根页表物理地址 `0x8020a000`。本轮 S 模式未进入第 171 行 M 模式绕过分支，也未进入第 196—199 行 Bare 分支，而是按 Sv39 设置 `levels=3`、`ptidxbits=9`、`ptesize=8`。

![Sv39 模式与根页表物理地址](images/mmu-02-satp-root.png)

使用 `until cpu_helper.c:252` 到读取 PTE 之前，`next` 实际读取 8 字节，再 `next` 提取 PPN，得到：

| 变量 | 实际值 | 含义 |
|---|---|---|
| `i` | 0 | 当前为根页表这一轮 |
| `idx` | `0x1ff` | 虚拟地址位 38:30 选择第 511 项 |
| `pte_addr` | `0x8020aff8` | 根页表基址加 `511×8` |
| `pte` | `0x200000cf` | V/R/W/X/A/D 置位，U 未置位 |
| `ppn` | `0x80000` | `pte >> 10` |

关键分支包括：第 256 行检查 V；第 259 行检查 R/W/X 是否全零，全零时才沿下一级页表继续；第 262—287 行检查保留组合、U/S 权限、大页对齐和本次访问权限。本项的 V/R/W/X 都为 1，单步从 256 到 259、262，跳过了“下一级页表”的赋值分支。根级叶子项覆盖 1 GiB，因此 Sv39 支持三级，并不表示每次转换都必须读三张表。

![实际读取根页表第 511 项与提取物理页号](images/mmu-03-page-table-entry.png)

#### 4.4.4 大页地址计算与权限

通过权限检查后，`updated_pte` 逻辑按需设置 A/D；本项 A/D 已预置，当前取指不需要修改 PTE，也无需走原子更新及重试路径。第 333—334 行执行：

```c
target_ulong vpn = addr >> PGSHIFT;
*physical = (ppn | (vpn & ((1L << ptshift) - 1))) << PGSHIFT;
```

观察到 `ptshift=18`、`vpn=0xffffffffc0200`。对 1 GiB 大页，使用虚拟页号低 18 位补足页号，得到 **4 KiB 页基址** `0x80200000`。再组合原地址低 12 位 `0x404`，完整物理地址为 `0x80200404`。不能把函数此处返回的页基址直接写成完整访存地址。

![大页映射下的页基址与完整物理地址](images/mmu-04-physical-address.png)

`finish` 返回 `riscv_cpu_tlb_fill` 后，运行到第 472 行，`ret=0` 表示成功，`prot=7` 表示软件 TLB 条目允许读、写、执行。第 463—471 行还可能因 PMP 检查失败转入异常；本次到达成功填充路径，没有观测到权限失败异常。

#### 4.4.5 软件 TLB 的填充、重新查找与命中

`step` 进入 `tlb_set_page`，参数为虚拟页 `0xffffffffc0200000`、物理页 `0x80200000`、`size=0x1000`。虽然页表叶子映射为 1 GiB，这次 QEMU 软件 TLB 按 4 KiB 缓存。其条目含读/写/取指地址标签及用于映射到宿主地址的 `addend`；`addend` 不能误认为客体物理地址。

![软件 TLB 的映射页与 4096 字节填充大小](images/mmu-05-tlb-fill.png)

返回取指查询后，第 1033—1034 行重新获取索引和条目指针，因为 `tlb_fill` 可能导致表扩容，旧指针不能继续假定有效。之后第 1036 行检查填充后的命中。截图中把 `assert(...)` 源码误输入为 GDB 命令，出现 `Undefined command`；这一输入没有执行 C 断言，随后 `next` 正常经过源码断言并到达 1039 行，结果不受该误输入改变。

![重新查找 TLB 条目及经过命中断言](images/mmu-06-tlb-after-fill.png)

随后在第 1030 行设置“目标页且标签已匹配”的条件断点。真实再次调用时 `addr=0xffffffffc0200404`，标签为 `0xffffffffc0200000`；`next` 从 1030 直接到 1039，跳过主表未命中后的 victim TLB 查询、`tlb_fill` 和页表遍历。之前的未命中路径则实际经过 1032 行 `tlb_fill`；只主表未命中不代表一定遍历页表，因为 victim TLB 仍可能命中。

![再次查询时 TLB 命中并跳过慢路径](images/mmu-07-tlb-hit.png)

#### 4.4.6 实际数据写入

禁用先前断点后，设置：

```gdb
break riscv_cpu_tlb_fill if access_type == MMU_DATA_STORE && mmu_idx == 1 && address >= 0xffffffffc0000000ULL
```

捕获到 8 字节写入，虚拟地址 `0xffffffffc0209ff8`，`ret=0`；返回页基址 `0x80209000`，组合偏移 `0xff8` 得到物理地址 `0x80209ff8`。这是数据写入证据，与前面的取指访问分别记录。

静态交叉核对当前内核反汇编：`kern_init` 中 `0xffffffffc0200414` 为 `addi sp,sp,-16`，`0xffffffffc020041a` 为 `sd ra,8(sp)`；入口设置的 `bootstacktop=0xffffffffc020a000`。有效地址为 `bootstacktop-16+8=0xffffffffc0209ff8`，与本次实际写入匹配。这一指令定位依据反汇编和入口栈布局，不把宿主转换函数里可能滞后的 `env->pc` 当作精确的访存指令 PC。

![实际 8 字节数据写入及转换后的物理地址](images/mmu-08-data-access.png)

#### 4.4.7 Bare 模式与软件、硬件 TLB 的区别

尝试在内核 GDB 执行 `monitor system_reset` 后，没有命中目标 S 模式 Bare 分支，反复中断时客体 PC 为 `0x80000474`。核对固件反汇编，该处位于 OpenSBI 的异常入口；本轮没有确认热复位问题的完整根因，不能断言是某个特定固件变量未清理。最终通过 `monitor quit` 退出模拟器，重新 `make debug`、附加新 PID、`make gdb` 从全新启动观察。此失败过程作为调试经验保留，不将热复位写成成功复现方法。

新的宿主条件断点为：

```gdb
break cpu_helper.c:197 if mmu_idx == 1 && (env->satp >> 60) == 0
```

命中时 `satp=0`、`mmu_idx=1`，即当前 S 模式选择 Bare。执行第 197 行 `*physical=addr` 后，二者均为 `0x80200000`。此时无需页表遍历，但调用栈仍有 `get_page_addr_code → tlb_fill → riscv_cpu_tlb_fill → get_physical_address`，说明软件 TLB 慢路径也会处理 Bare 下的访问。

![全新启动后的 S 模式 Bare 直接映射与调用栈](images/mmu-09-bare-mode.png)

真实 RISC-V 的 Bare 模式在地址转换语义上直接使用地址，不要求基于 `satp` 页表做翻译。QEMU 的软件 TLB 还缓存访存权限、RAM/MMIO 分类和客体地址到宿主内存的映射，所以 Bare 下仍可能进入填充路径。不能仅凭模拟器调用栈推断真实 CPU 的内部缓存实现，也不能说“进入 tlb_fill 就一定查了页表”。

#### 4.4.8 调试经验与 AI 交互

本次通过 AI 协助完成：匹配符号的 QEMU 构建、三终端调度、条件断点选择、源码行号核对、页表项解码、页基址与偏移区分、软件 TLB 路径定位，以及 Bare 复现失败后的重新启动。实际请求、截图上下文和执行反馈见 [MMU 交互记录](prompt.md#stage-13)。

关键问题是区分 GDB 命令与 C 源码、区分分页提示和执行命令、区分两个 GDB 的目标。误在宿主 GDB 按 Ctrl+C 会停到 QEMU 主线程 `poll()`，此时没有 `env/addr` 等转换函数局部变量；应让宿主继续运行，在内核 GDB 控制客体，再等待目标条件断点。截图中的错误保留原样，用文字说明原因与恢复过程。

本轮覆盖的是正常映射的实际路径和关键条件检查，未伪造无效 PTE、权限失败、victim 命中等未实测异常分支的结果。MMU 分支调试及其截图材料已补齐；成员信息已合并远程组内填写内容，整组分配器验收与最终提交仍按第 6.3 节清单收尾。

## 五、测试与验证

### 5.1 模块一历史验证为何使用临时副本

模块一最初验证时，Best-Fit 尚未实现完毕，直接编译全工程会受其影响。当时验证工具在 Linux 临时目录复制源码，仅在副本 Makefile 的源文件列表中排除 `best_fit_pmm.c`，保留当时选择的 `default_pmm_manager`。下面的九组结果与 GDB 截图对应该历史 First-Fit 源码快照。

九组检查还仅在临时副本加入交付检查、逐页布局检查及官方模块一测试调用。输入故障只注入临时 DTB getter。GDB 构建不注入这些 C 检查代码，仅隔离未完成的 Best-Fit。原始工程文件均进行验证前后 SHA-256 比较，结果记录在汇总 JSON。

因此，这些证据证明模块一在当时 First-Fit 接口下的独立行为。成员二的 Best-Fit 运行与 `make grade` 截图见第 4.2.2 节；`1e9c4da` 版本的 Buddy 启动核验见第 5.7 节。三者对应不同源码快照，不将旧结果直接当作最新整组完整验收。

### 5.2 原 First-Fit 快照的验证命令

以下命令对应原 First-Fit 版本，现有辅助脚本仍假定该管理器。当前默认 Buddy 版本不直接使用这些命令覆盖原证据；如需重新运行模块一专用检查，应先在临时副本中适配管理器选择并使用新的证据目录。`1e9c4da` 版本的启动结果见第 5.7 节。

以下保留原验证环境的命令，其中 `/mnt/c/Users/ASUS/Desktop/操作系统/实验` 是当时的仓库根目录，在本机仍为仓库根目录。复用时需先进入自己的仓库根目录，并确认工具路径；下列源码目录已按当前整理结果更新：

```bash
cd /mnt/c/Users/ASUS/Desktop/操作系统/实验

# 九组真实 QEMU 检查；日志写入报告目录
python3 lab2_new/tools/verify_page_init.py \
  --log-dir report/evidence/module1

# 实际 GDB + QEMU；构建、启动、断点观察及清理均由脚本完成
python3 lab2_new/tools/debug_page_init.py \
  --log-dir report/evidence/module1-gdb
```

脚本使用已有工具路径 `~/.local/opt/riscv/bin`、`~/.local/opt/qemu-4.1.1/bin`，同时支持 PATH 中的工具。只终止各自启动的 QEMU；日志末尾 signal 15 来自完成采集后的主动结束。

工具：[自动验证入口](../lab2_new/tools/verify_page_init.py)、[交付与逐页检查](../lab2_new/tools/check_page_init.c)、[GDB 验证入口](../lab2_new/tools/debug_page_init.py)。

### 5.3 九组结果

本次完整重跑的顶层状态为 **PASS**，九组均通过，原源码未被验证过程修改。判定要求是命中明确结果标记；无标记的超时按失败处理。

| 案例 | 输入/变化 | 判定结果 |
|---|---|---|
| `ram128` | 128 MiB | 原 First-Fit 自检、官方模块一测试、交付前与逐页检查通过 |
| `ram64` | 64 MiB | 随容量调整，以上检查通过 |
| `ram64-unaligned` | 64 MiB 的 DTB 报告大小减 1 字节 | RAM 末尾向下对齐，元数据尾页继续保留；以上检查通过 |
| `ram256-capped` | 256 MiB | 管理范围限制到 `0x88000000`；交付、逐页和 First-Fit 自检通过 |
| `invalid-zero` | DTB 报告大小为零 | 明确拒绝，未进入 `init_memmap` |
| `invalid-overflow` | 起点与大小相加溢出 | 明确拒绝，未进入 `init_memmap` |
| `insufficient-ram` | 报告范围无法容纳内核 | 明确拒绝，未进入 `init_memmap` |
| `insufficient-metadata` | 内核可放下，描述符数组放不下 | 明确拒绝，未进入 `init_memmap` |
| `no-free-pages` | 能容纳元数据，但无完整空闲页 | 明确拒绝，未进入 `init_memmap` |

负向案例 PASS 的含义是**正确拒绝错误输入**。256 MiB 场景中，官方模块一测试假设 `npage` 等于未经截断的 DTB 结束页号，与框架固定上界不一致，因此明确输出该官方检查的 SKIP，使用新增逐页检查核验截断布局；没有修改官方测试函数。其余三个正常容量案例仍执行官方模块一测试。

![模块一九组检查全部通过的实际终端截图](images/module1-terminal-tests.png)

图为王雅枫在 WSL 重新运行验证脚本后自行截取的终端结果，包含九行 PASS 与最终成功总结。[完整结果与源码哈希](evidence/module1/results.json)；[元数据不足时的实际 panic](evidence/module1/insufficient-metadata-qemu.log)。

### 5.4 串口输出与布局数值

![默认 128 MiB 检查内核串口日志的实际终端截图](images/module1-terminal-boot.png)

图为本次验证运行完成后，在终端回显串口日志并自行截取的图片，包含 DTB、内存布局、原自检、官方模块一测试及完整验证结束标记。[原始 QEMU 日志](evidence/module1/ram128-qemu.log)、[原始编译日志](evidence/module1/ram128-build.log)。

| 数值 | 含额外检查的九组验证构建 | 不注入 C 检查的 GDB 构建 |
|---|---|---|
| `nbase` / `npage` | `0x80000` / `0x88000` | 相同 |
| 描述符数量 / 字节数 | 32768 / 1310720 | 相同 |
| 描述符数组 PA | `0x80208000` | `0x80207000` |
| 首个空闲页 PA | `0x80348000` | `0x80347000` |
| 空闲页数 | 31928 | 31929 |

两份构建包含的检查代码不同，内核 `end` 因而不同；布局结果随之变化一页是正常现象。**应验证计算关系，不应把某一构建的地址写死为预期常量。**

未对齐案例的管理末尾为 `0x83fff000`，描述符占 655320 字节，空闲起点 `0x802a8000`，初始空闲页数 15703，见[实际日志](evidence/module1/ram64-unaligned-qemu.log)。

### 5.5 实际 GDB 观察

本次使用 QEMU `-S` 暂停 CPU，并开放绑定在 `127.0.0.1` 的独立 GDB 端口。调试器加载本次临时构建的 `bin/kernel`，随后连接该 QEMU。完整执行脚本见 [commands.gdb](evidence/module1-gdb/commands.gdb)。其中端口由本次运行分配，不能直接假定始终为 1234。

核心指令及其作用：

```gdb
set pagination off
set architecture riscv:rv64
file bin/kernel
target remote 127.0.0.1:<本次端口>
break page_init
continue
bt 3
break *default_init_memmap
continue
set $base = (struct Page *)$a0
set $count = (unsigned long)$a1
p/x nbase
p/x npage
p/x pages
p *$base
delete breakpoints
tbreak *default_check
continue
p free_area.nr_free
p *$base
finish
p free_area.nr_free
detach
quit
```

1. **在 `page_init` 停住。** 回溯确认调用来自 `pmm_init → kern_init`。
2. **在 `default_init_memmap` 的首条指令停住。** RV64 ABI 的 `a0`、`a1` 分别保存交付描述符与数量，尚未被分配器修改。抽查起始保留页、元数据末页、首个空闲页及管理范围末页，均为 `flags=1`、`ref=0`、`property=0`，链表节点自链接。同时验证空闲区起点、长度、偏移和数组边界。
3. **在 `default_check` 入口停住。** 此时空闲区已登记，`nr_free=31929`；空闲块头 `flags=2`、`property=31929`，保留前缀仍为 `flags=1`。这些位值来自当前框架的 Reserved/Property 定义。
4. **`finish` 执行完原 First-Fit 自检。** 空闲页计数恢复为 31929；断开 GDB 后串口输出 `check_alloc_page() succeeded!`，并继续输出页表地址。

![GDB 交付前、登记后与原自检返回日志的实际终端截图](images/module1-terminal-gdb.png)

GDB 的自动断言均通过。这里的 GDB 抽查与九组验证中的**全描述符扫描**相互补充。[原始 GDB 日志](evidence/module1-gdb/gdb.log)、[GDB 结果与数值](evidence/module1-gdb/results.json)、[断开调试后的串口日志](evidence/module1-gdb/qemu.log)、[编译日志](evidence/module1-gdb/build.log)。优化构建下入口断点可能显示内联头文件位置，应结合实际函数名、指令地址和调用栈理解，不据此误判为跳错函数。

### 5.6 图片性质与整组验收待办

本节涉及的三张模块一 PNG 为王雅枫于 2026-10-18 自行保存的实际终端截图。第一张显示验证脚本的直接输出，第二、三张分别显示本次串口日志与 GDB 批处理日志的终端回显；GDB 图不是现场逐条输入指令的交互界面。已检查图片中的通过标记及布局数值，并替换此前生成的日志阅读图。文件及对应日志的 SHA-256 清单见 [image-sources.json](images/image-sources.json)。

First-Fit、Best-Fit、Buddy 和 SLUB 的已有验证分别对应各自的源码版本，不能互相替代。MMU 现场调试和九张截图已在第 4.4 节补齐；全组仍需按最终配置完成正常构建、所要求的完整测试，并补充相应的实际终端截图。报告模板要求的整组 `make qemu` / `make grade` 截图不以本版模块一图片替代；最终具体命令以本实验 Makefile 的真实目标为准。

### 5.7 `1e9c4da` 版本的 Buddy 启动核验

2026-10-21 将本地从 `89baea2` 快进更新到 GitHub `lab2` 的 `1e9c4da`，保留原提交及四个远端新增提交。本地未提交的报告与提示词已备份并恢复；文档冲突按保留双方内容的方式合并。

在 Linux 临时副本中，对 `1e9c4da` 版本源码执行 `make -j2` 并启动 128 MiB QEMU。该版本默认管理器为 Buddy，尚未接入后续 SLUB 实现，实际结果为：

```text
memory management: buddy_pmm_manager
BUDDY CHECK ALL PASSED (9 groups, 303 arena operations, 326 structure audits)
check_alloc_page() succeeded!
```

自检后恢复 31913 个空闲页；该数值对应该版本构建，新增 Buddy 代码及测试静态数据改变了内核大小，不能与原 First-Fit 构建的页数直接等同。此轮未执行完整 `make grade`，也未新增终端截图。

证据：[编译日志](evidence/sync-20261021-buddy/build.log)、[运行日志](evidence/sync-20261021-buddy/qemu.log)、[源码哈希及结果](evidence/sync-20261021-buddy/results.json)。模块一 Challenge 的本次完善为书面分析，未改动内核源码或新增跨平台探测接口。

### 5.8 目录整理后的当前源码复核（2026-10-21）

本次只审查和验证，未修改工作区的内核实现与管理器选择。48 个源文件在验证前后哈希一致；Best-Fit 原有 `basic_check()`、`best_fit_check()` 与基础提交保持逐字一致。

在互相独立的 Linux 临时副本中使用本机 GCC 8.3.0、QEMU 4.1.1，得到以下实际结果：

| 临时副本 | 改动与验证入口 | 结果 |
|---|---|---|
| 默认 Buddy，普通构建 | 原源码，`make -j2` 后启动 QEMU | 编译成功；Buddy 九组自检和 SLUB 冒烟检查通过 |
| 默认 Buddy，`SLUB_TEST=1` | 原样使用官方 `tools/inject.py` 与 `tools/grade.sh` | 官方 Page Init 通过，Best-Fit 因当前选择 Buddy 失败，评分 **10/20**；Buddy 九组及 SLUB 八组测试均通过 |
| Best-Fit 对照，`SLUB_TEST=1` | 只在副本将管理器选择改为 Best-Fit；相同官方注入器及评分脚本 | 官方评分 **20/20**，退出码 0；SLUB 八组、1831 次分配与释放、5502 次内容核验均通过 |

本次执行了官方注入和评分脚本，未调用会创建并清理固定评分副本的顶层 `make grade`；未修改评分测试以获得通过。Best-Fit 对照结果说明当前实现可以在该配置下通过检查，不代表工作区默认 Buddy 已获得 20/20。最终应按课程 Best-Fit 验收要求选定配置，保留 Buddy 的独立运行与测试方式，并对正式提交版本重新保存评分截图。

证据：[本次汇总与源码哈希](evidence/current-review-20261021/results.json)、[Buddy 评分](evidence/current-review-20261021/buddy-slub-grade/grade.log)、[Buddy 与 SLUB 运行](evidence/current-review-20261021/buddy-slub-grade/qemu.log)、[Best-Fit 评分](evidence/current-review-20261021/best-fit-slub-grade/grade.log)、[Best-Fit 与 SLUB 运行](evidence/current-review-20261021/best-fit-slub-grade/qemu.log)。这些原始日志为新增复核证据，本次未截图。

2026-10-21 随后核对基础代码包的两处 `YOUR CODE` 标记：Best-Fit 已由成员二填写 `2413276`；本轮将模块一标记填写为 `2412832`。原 `lab2 function1` 与 `lab2 function2-2` 均保留。逐字节核对确认只替换了模块一的一处注释，未改执行代码，未为这一注释变动重新构建；因此第 5.8 节源码哈希属于填写学号前的实际验证快照，未覆盖修改旧证据。见[标记核对记录](evidence/comment-marker-check-20261021/results.json)。

## 六、实验总结与收获

### 6.1 对操作系统的理解

| 原理知识 | 模块一中的体现 | 范围与区别 |
|---|---|---|
| 物理页帧管理 | 以 4 KiB 为单位建立描述符与空闲区 | 初始化只确定可分配资源，选择哪一块由分配器负责 |
| 元数据与被管理资源 | `Page` 记录物理页状态，但数组自身也消耗 RAM | 元数据占用页必须保留，不能把数组存放区再次分配 |
| 地址转换 | 直接映射偏移、页号、数组下标相互转换 | 当前固定映射公式不能推广到任意进程地址空间 |
| 安全初始化顺序 | 先全部保留，再只开放已确认安全的区间 | 先做容量检查，防止在发现错误之前已经覆盖内存 |
| 页边界与内部碎片 | 可用末尾向下对齐，元数据末尾向上对齐 | 未满一页的空间不能作为完整物理页分配 |
| 分配状态与映射状态 | PMM 返回页，启动页表完成地址映射 | 4 KiB 分配粒度与启动页表的 1 GiB 叶子映射粒度不同 |

本模块尚未覆盖按需分页、换入换出、进程地址空间隔离、多段 RAM、完整 DTB 保留区处理、多核并发分配和内存热插拔。当前实现沿用课程单段内存及固定预留布局，不能直接宣称为通用操作系统的完整内存发现与管理机制。

### 6.2 AI 协作开发的经验

1. 先把 `npage`、`pages` 与地址单位讲清楚，再写函数，能减少“可编译但范围算错”的问题。
2. 仅检查分配器处理后的结果可能掩盖初始描述符缺陷。把观察点前移到 `init_memmap` 之前，才能验证模块之间真实的交付约定。
3. 测试工具本身也会出错。实际遇到的活动日志读取问题和旧 PASS 风险，需要通过日志位置、状态更新和故意破坏副本的自检解决。
4. 成功输出必须对应具体源码版本；临时隔离检查应记录排除了什么、加入了什么，不能直接当作整组验收。
5. 提示词润色有助于表达目标、依赖和验收条件，但应保留实际输入，避免混淆真实协作过程。

### 6.3 提交前核对

- [x] 模块一实现、九组检查、实际 GDB 核验与原始证据。
- [x] 模块一 Challenge：内存范围获取思考题，说明方法、限制及本实验实际路径。
- [x] 本报告、四段式提示词记录、三张实际终端截图和来源说明，均位于 `report/`。
- [x] 当前 Buddy 普通构建、SLUB 完整测试及临时 Best-Fit 评分对照复核，原内核源码未改。
- [x] 完成 QEMU MMU 分支调试，补齐关键路径、正常映射分支、页表单步、TLB 命中/Bare 对比、九张实际截图及 AI 交互记录。
- [ ] 按课程要求确定最终 Best-Fit 验收配置，并保留 Buddy 的运行/自检方式，保存正式版本评分截图。
- [x] 补充 Buddy 设计与测试覆盖说明，整合 SLUB 独立材料及工作总结，见[模块文档索引](README.md)。
- [ ] 补齐 Buddy 实现/测试两份原始实际提示词及运行截图；补齐 SLUB 实际截图。
- [x] 基础代码的两处 `lab2` 任务标记保留，分别填写成员一、二实际学号，源码中无 `YOUR CODE` 占位。
- [x] 合并远程组内填写的三位成员姓名、学号、分工及 AI 工具、模型信息。
- [ ] 整合成员二、三内容，补齐全组报告及最终完整测试截图；MMU 调试截图已齐。
- [ ] 全组最终核对分支、源码目录和提交材料，再按助教要求提交。
