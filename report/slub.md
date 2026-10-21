# Lab2 简化 SLUB：实现与本轮验证

本记录对应 2026-10-21 的第一份 SLUB 实现任务，源码在 `lab2/lab2_new/`，分支为 `lab2`。
本轮工具为 Codex（GPT-6）；未提供成员姓名、学号或本轮署名，不据此补写成员信息。
实现已在真实 RISC-V QEMU 内核运行；当前验证范围是接入和冒烟检查，完整 SLUB 测试留给第二份任务。

## 工程依据与文件

阅读了工作区 `AGENTS.md`、`docs/lab2.md`、`docs/实验报告模板.md`，以及实际源码中的
`pmm.h`、`pmm.c`、`memlayout.h`、`mmu.h`、`buddy_pmm.c/.h`、`best_fit_pmm.c`、
`entry.S`、`init.c`、`Makefile`、链表库和课程评分脚本。
本轮还通过 curl 成功读取了[课程 Lab2 练习原文](http://oslab.mobisys.top/lab2026/_book/lab2/lab2_2_1_exercise.html)。
参考 [Linux mm/slub.c](https://github.com/torvalds/linux/blob/master/mm/slub.c) 的缓存、slab 内 freelist 和 partial 状态思路，重新编写适合本工程的简化实现，没有移植 Linux 的辅助接口、Page 字段约定或并发设施。

| 文件 | 本轮变更 |
|---|---|
| [`slub.h`](../lab2/lab2_new/kern/mm/slub.h) | 新增三个公共接口及调用契约 |
| [`slub.c`](../lab2/lab2_new/kern/mm/slub.c) | 新增缓存、slab、连续页大对象、分配和回收实现 |
| [`init.c`](../lab2/lab2_new/kern/init/init.c) | `pmm_init()` 后初始化 SLUB 并运行有限冒烟检查 |
| [`prompt.md`](prompt.md) | 本轮实际用户提示词原文、执行情况和迭代说明 |
| [`历史 prompt.md`](../lab2/report/prompt.md)、[`原报告`](../lab2/report/report.md) | 保留既有记录，追加索引并更新 SLUB 阶段状态 |
| `report/evidence/slub-implementation/` | 本轮原始编译、运行、评分日志与源码哈希 |

Makefile 自动收集 `kern/mm` 下的 C 文件，所以未改动构建规则。
`pmm.c`、`struct Page`、Buddy 和 Best-Fit 均保持原样；没有把 SLUB 注册为 `pmm_manager`。
仓库原有 `lab2/report/` 与源码位置未重组，新增材料按本轮指定放在根目录 `report/`。
这不是完整实验的最终交付整理，根目录 `code/`、`report/report.md` 等整组交付规范仍需最终整理时核对。

## 接口与底层语义

```c
void slub_init(void);
void *slub_alloc(size_t size);
void slub_free(void *ptr);
```

没有已有同名接口冲突。`slub_init()` 在页分配器及直接映射可用后调用一次；正数分配和非空释放须在其后调用。
当前单 hart、无中断分配路径的启动环境中顺序调用；与现有页管理器一样，调用方须串行化访问，不提供 SMP 或中断重入保证。

`slub_alloc(0)` 返回 NULL；溢出、内存不足或连续块无法满足时也返回 NULL，已有对象与缓存保持不变。
成功返回至少 size 字节的连续可读写内核虚拟区间，至少 16 字节对齐，不承诺清零。
`slub_free(NULL)` 无操作；其余参数须为尚未释放的分配起始地址，不支持内部指针、非法指针或重复释放。

当前 `alloc_pages(n)` 返回至少 n 个连续物理页的起始 `Page *`；Buddy 实际占用向上取整到二次幂的页数。
`free_pages(base, n)` 要求同一个 base 和**原始请求 n**，由 Buddy 自行恢复整个取整块。
`nr_free_pages()` 返回空闲物理页数量，按实际占用统计。例如 SLUB 请求 8192 字节，加头部后需要 3 页，Buddy 占用 4 页，释放仍传 3，回收 4 页。
空闲页总数充足并不保证存在可满足请求的连续块，最终仍以 `alloc_pages()` 的结果为准。

`npage` 是绝对物理页号上界，描述符数量为 `npage - nbase`。
`page2pa(page)` 提供物理地址；本树的 `KADDR` 已被注释，也没有 `page2kva`。
代码使用 `page2pa(page) + va_pa_offset` 取得直接映射虚拟地址；`entry.S` 已建立该映射，`page_init()` 已限制管理范围并设置偏移。
回收使用 `pa2page(PADDR(header))` 还原首页描述符。
SLUB 的所有状态均在静态缓存或自己申请的物理页内，不读写 `Page` 的 property、flags、ref 或 page_link。

## 数据结构、布局及尺寸类别

静态八个 `slub_cache` 各记录槽位大小，以及 partial、full 两个双向链表哨兵，不通过 SLUB 自举分配。
统一 `slub_header` 记录 cache 指针、原始页请求数、capacity、inuse、freelist 和独立链表节点。
`cache == NULL` 表示大对象，此时只使用 cache 与原始页请求数；其他字段不读取。

在本工程 RV64 布局中，头部结构为 56 字节，对象起点向上对齐到 64 字节：

```text
小对象单页： [0..55 头部][56..63 对齐填充][64.. 对象槽位][尾部余量]
大对象连续页：[0..55 头部][56..63 对齐填充][64..64+size 用户区间][余量]
```

容量统一为 `(PGSIZE - SLUB_HEADER_SIZE) / object_size`，PGSIZE 为 4096；不存在头部与对象区重叠。
每个槽位步长都是 16 的倍数。空闲对象的前 8 字节用于 next，分配时先弹出 next，再把整个槽位交给用户；在用对象不保存分配器仍依赖的字段。

| 请求字节范围 | 槽位字节数 | 单页对象容量 |
|---|---:|---:|
| 1～16 | 16 | 252 |
| 17～32 | 32 | 126 |
| 33～64 | 64 | 63 |
| 65～128 | 128 | 31 |
| 129～256 | 256 | 15 |
| 257～512 | 512 | 7 |
| 513～1024 | 1024 | 3 |
| 1025～2048 | 2048 | 1 |
| >2048 | 连续页路径 | 请求页数为 `ceil((size + 64) / 4096)` |

保留建议的全部尺寸类别。2048 类别因页内头部仅容纳一个对象，接受这个空间代价以保持单页 slab 和统一头部定位的简单性。
大对象返回地址也位于首页，释放只需将合法起始指针按 4096 向下对齐，不需要外部映射表。

## 分配、释放与失败处理

小对象先找到能容纳请求的最小尺寸类别，从 partial 链表取 slab；没有时调用 `alloc_pages(1)`，初始化头部和所有空闲槽位后才挂入缓存。
分配弹出 freelist 并增加 inuse；若达到 capacity，将节点从 partial 移到 full。
没有空页时直接返回 NULL，不修改已有缓存。

释放小对象时，若仍有存活对象，将该槽位压回 freelist；原先满的 slab 从 full 移回 partial。
最后一个对象释放时，**先摘除缓存节点，再立即 free_pages(..., 1)**，不保留空 slab。
即使 capacity 为 1，也会直接从 full 摘除并归还页面，没有悬空缓存引用。

大对象先检查 `size <= SIZE_MAX - SLUB_HEADER_SIZE`，然后计算总字节数。
页数使用除法与余数计算，避免 `bytes + PGSIZE - 1` 溢出；商的上界保证余数导致的加一安全。
在调用底层前检查页数不超过描述符数量或当前空闲页数量；正数请求加固定头部必然产生非零页数。
底层返回 NULL 时不写入任何元数据；成功后在首页保存原始请求页数。
释放按页向下对齐读取头部，以原始请求页数归还整个连续块；Buddy 的取整余量也由底层一起收回。

## 实际验证命令与结果

实际环境：WSL、`riscv64-unknown-elf-gcc` 13.2.0、QEMU 4.1.1、默认 128 MiB RAM。
使用工作区已有的 QEMU 包装器；所有临时构建与评分副本均在工作区 `tmp/` 下。
以下命令从 `workspace/lab2/lab2_new/` 执行：

```sh
make -j2 OBJDIR=/mnt/d/wsl-share/OS/tmp/slub-implementation/obj BINDIR=/mnt/d/wsl-share/OS/tmp/slub-implementation/bin
make qemu OBJDIR=/mnt/d/wsl-share/OS/tmp/slub-implementation/obj BINDIR=/mnt/d/wsl-share/OS/tmp/slub-implementation/bin QEMU=/mnt/d/wsl-share/OS/tmp/buddy-implementation/qemu-system-riscv64-4.1.1
make grade GRADE_TMP=/mnt/d/wsl-share/OS/tmp/slub-implementation/grade-tree QEMU=/mnt/d/wsl-share/OS/tmp/buddy-implementation/qemu-system-riscv64-4.1.1
```

| 检查 | 真实结果与证据 |
|---|---|
| 构建 | 退出码 0，包含 `+ cc kern/mm/slub.c`；[build.log](evidence/slub-implementation/build.log) |
| 正常启动 | Buddy 九组自检、`check_alloc_page()` 和 SLUB 冒烟检查均通过，无 panic；[qemu.log](evidence/slub-implementation/qemu.log) |
| 冒烟内容 | 64 个 64 字节对象跨页、full→partial→复用、2048 字节类、8192 字节跨多页完整写入、16 字节对齐、NULL/零尺寸、两种超大请求失败及已有数据保持、最终页数恢复 |
| 页回收 | 正常内核初始空闲 31912 页，Buddy 自检恢复；SLUB 冒烟结束断言空闲页恢复至调用前值并打印 `SLUB smoke check passed (all pages reclaimed)` |
| 课程 grade | Page Init 为 OK，Best-Fit 为 WRONG，总分 **10/20**，make 退出码 2；[grade.log](evidence/slub-implementation/grade.log)、[评分副本串口](evidence/slub-implementation/grade-qemu.log) |
| 差异检查 | `git diff --check` 通过；未提交、未推送 |

课程评分的 Best-Fit 检查在 `tools/test_lab2.c:test_best_fit()` 中要求
`pmm_manager == &best_fit_pmm_manager`；原分支的 `pmm.c:init_pmm_manager()` 选择 Buddy，且本轮没有修改该选择。
这解释了该项失败，不把 10/20 描述成评分通过，也不为分数改动用户要求保留的管理器。
评分副本中 SLUB 冒烟同样通过。该 grade 本身不覆盖 SLUB 正确性。

正常内核会停留在既有无限循环。采集到成功标记后，由运行脚本对本次创建的进程组发送 SIGTERM；正常启动记录中退出码 -15 来自主动停止采集。
评分脚本结束后也清理了它启动的进程组，没有终止其他人的 QEMU。
构建及运行信息、执行命令和对应源码 SHA-256 另存于 [results.json](evidence/slub-implementation/results.json)。

本轮未增加终端截图，也未宣称整份实验报告已经满足最终提交要求。
未执行完整尺寸边界、实际资源耗尽、碎片下失败、所有类别多 slab 混合随机序列等测试；这些属于第二份任务。
后续可依据上表测试类别边界，尤其关注 2048/2049、总字节数跨页、Buddy 非二次幂页数、partial/full 转移和最后对象立即回收。

## 迭代与认识

本轮只有一份实际用户实现提示词，完整原文见 [prompt.md](prompt.md)。首轮代码构建和运行成功，未发生代码修复迭代；课程 grade 的管理器选择不匹配单独记录。
两层分配使字节对象不必各占一个物理页，但头部、类别向上取整和 Buddy 页数取整分别产生空间开销。
核实释放接口比直接模仿参考代码更关键：此工程不能用 Buddy 取整后的页数替代原始请求数，也不能借用 Page 内部字段。
AI 协作中先限定这些实际接口与布局不变量，再以完整用户区间写入和页数恢复验证接入，能及时发现只验证编译无法暴露的问题。


## 后续第二阶段状态（2026-10-21）

上述“完整测试待第二份任务”的描述保留为第一阶段历史记录。第二份实际提示词已执行，八组 SLUB 内核测试全部通过，见 [第二阶段测试记录](slub-tests.md) 和 [完整实际提示词](prompt.md)。当前源码相对第一阶段新增受宏保护的测试辅助入口及取页包装；第一阶段日志与哈希仍对应原始快照。
