# Lab2 SLUB 第二阶段：内核测试与实际结果

2026-10-21，Codex（GPT-6）针对上一阶段工作区的实际 `slub.c/.h` 实现并运行测试。
开始时位于 `lab2`，上一阶段代码和报告均未提交；既有内容保留，未创建 worktree、提交或推送。
本轮成员姓名和学号未提供，不新增成员署名。

## 实际契约与变更

接口为 `slub_init()`、`slub_alloc(size)`、`slub_free(ptr)`；小对象类别为 16、32、64、128、256、512、1024、2048 字节。
页头结构为 56 字节，对齐后的头部长度为 64 字节。小对象单页容量依次为 252、126、63、31、15、7、3、1。
大于 2048 字节采用连续页路径，头部与用户区间相互独立；返回地址至少 16 字节对齐。
零尺寸分配返回 NULL，NULL 释放无操作，不承诺初始清零。小 slab 最后一个对象释放后立即还页，大对象按原始请求页数释放，Buddy 负责其二次幂取整余量的回收。
阅读结果与上一阶段契约一致，完整测试未发现需要修复的分配器缺陷。

| 文件 | 本轮修改 |
|---|---|
| [`slub_test.c`](../lab2/lab2_new/kern/mm/slub_test.c) | 新增八组测试、独立对象记录、模式生成、区间检查、完整内容复核、页数与失败恢复检查 |
| [`slub_test.h`](../lab2/lab2_new/kern/mm/slub_test.h) | 新增 `void slub_check(void)` 与测试构建专用辅助接口 |
| [`slub.c`](../lab2/lab2_new/kern/mm/slub.c) | 加入统一取页包装；仅测试构建启用只读头部长度查询和一次性取页失败注入 |
| [`init.c`](../lab2/lab2_new/kern/init/init.c) | 测试构建在 `pmm_init(); slub_init();` 后调用 `slub_check()`，普通构建保留冒烟检查 |
| [`Makefile`](../lab2/lab2_new/Makefile) | 增加默认关闭的 `SLUB_TEST` 选项；源码仍由原有规则自动收集 |
| [`prompt.md`](prompt.md)、历史报告与提示词 | 追加第二份实际提示词、执行和测试契约调整；保留原记录 |
| `report/evidence/slub-tests/` | 编译、两种构建的串口、评分、栈帧审计和源码哈希 |

`slub.h`、Page 定义、页管理器及其检查均保持原样。分配器的本轮差异见
[allocator-test-seam.diff](evidence/slub-tests/allocator-test-seam.diff)：只有测试辅助入口与取页包装，没有算法修复。
普通冒烟检查原先断言复用后地址等于指定旧地址，超出了公共接口承诺；本轮改为非空和对齐检查。
新套件使用存活数据、区间隔离和页数变化检验复用，不强制返回特定旧地址。这是测试契约修正，没有降低分配器的有效契约要求。

## 测试记录和判断依据

520 个 `st_record` 放在静态数组中，记录指针、请求字节数和独立标签；占 12480 字节。
另有最多 64 项的去重边界表、固定尺寸表与少量计数器。测试管理结构不使用 SLUB。
成功分配后先检查非空、对齐和返回区间处于内核 RAM 内，再与每个存活请求区间比较，确认不重叠后才写入。
数据模式由对象标签（含分配序号和记录编号）及字节偏移生成；写满并核对完整请求长度，包含首页末尾、跨页位置与最后字节，不读写槽位余量或页头。

每次分配会在写入新对象之前复核旧对象，写入后再次复核所有对象；释放时先核对待释放对象，删除独立记录后释放，再复核所有剩余对象。
因此不会在释放后读取对象。每组开始与结束均要求没有测试对象存活，并核对 `nr_free_pages()` 恢复基线。
最终还检查成功分配和释放总数一致，全部条件满足后才打印最终成功标记。
失败输出包含测试组、阶段、步骤、对象编号、请求大小；数据错误额外包含字节偏移、预期值及实际值，页数错误包含预期和实际页数。

页数判断不读取或篡改 Buddy 的内部链表、Page flags 或 property。大对象页数预期独立使用逐页增加与乘法计算，遇到 Buddy 再乘二取整；也识别现有 First-Fit、Best-Fit 的精确页数语义，未知管理器会明确失败。
只读辅助接口仅返回对齐后头部字节数，容量由测试根据已核实类别独立计算；状态转换是否生效通过对象数、所在页面、数据和页数判断。

## 覆盖表

| 组 | 具体内容 | 可以发现的问题 |
|---|---|---|
| 1 基本接口 | 1、3、17 字节同时存活；非空、16 字节对齐、零尺寸和 NULL 释放 | 接口特殊值处理、错误对齐、简单分配释放错误 |
| 2 类别与边界 | 全部八类的 B−1/B/B+1；1、3、17、100、513；PGSIZE−1/PGSIZE/PGSIZE+1、2×PGSIZE+37；头部导致的单页/双页边界附近，去重后 37 个尺寸同时存活 | 类别选择、头部重叠、容量误算、少分配一页、跨页或末字节不可写 |
| 3 存活隔离 | 48 个同类/异类对象；每第三个释放，再以不同尺寸补回，逐操作校验全部存活区间 | 返回区间别名、误复用存活槽位、释放破坏邻接对象、用户写入覆盖仍依赖的 freelist |
| 4 slab 状态 | 对全部八类先填满两个 slab，释放一个后再分配，此时没有其他 partial slab 可掩盖转移错误；随后增加至 `2×capacity+1` 个对象；逐次检查页消耗；按实际对象所在页释放，每个非最后释放均不得还页，最后一个必须还一页 | 元数据未扣除、扩展不正确、partial/full 转移错误、提前回收、空 slab 未立即回收、摘链遗漏 |
| 5 顺序与复用 | 三轮，每轮顺序、逆序、偶数正序/奇数逆序三种释放次序，每次 48 个对象；每轮恢复基线 | 链表或状态依赖释放顺序、反复创建回收后悬空引用、累计泄漏 |
| 6 固定随机序列 | xorshift32 种子 `0x13579bdf`，512 步，最多 48 个存活记录，每一步分配或释放后完整核验 | 可复现的尺寸交错、生命周期交错导致的数据损坏、偶发状态错误与泄漏 |
| 7 大小对象交错 | 同时保留六个大对象（4095、4096、4097、8229、12325、16421 字节）及两个小对象；交错释放并逐次核对页数 | 大对象彼此/与小对象混淆、跨页不足、原始页数与 Buddy 取整页数混用、错误释放范围 |
| 8 失败与恢复 | 6 个接近 SIZE_MAX 的请求；分别注入新建小 slab、满 slab 扩展、大对象取页失败；检查页数和已有内容，随后正常申请并释放 | 尺寸加法回绕、页数换算错误、将无效页数交给 PMM、失败泄漏或破坏状态、失败条件解除后无法恢复 |

上述各组均执行完整区间检查和最终回收检查。第 4 组中，对象容量为 1 的 2048 类单独按“释放即还页、再次申请新页”规则判断；不会错误要求它释放后仍留在 partial 中。
第 7 组中，8229 字节加 64 字节头部需要 3 页，Buddy 实际占用 4 页；16421 字节需要 5 页，实际占用 8 页。申请和释放两侧分别核对这些实际差值。

## 失败注入与资源上界

`SLUB_TEST=1` 才定义辅助状态和接口。`slub_get_pages()` 在下一次 SLUB 取页时消费一次故障标志并返回 NULL，其余时候直接调用原来的 `alloc_pages()`。
这模拟底层无法提供页面的返回值，不耗尽真实内存，也不修改 PMM 内部状态。
测试会先确认已布置故障，随后要求分配返回 NULL、故障已被消费、页计数保持原值、所有存活数据不变。
测试选择空的尺寸类别，或先填满该类别，确保已有 freelist 无法绕过失败点；之后立即成功分配、写入、释放并继续测试。
宏关闭的正常内核经符号表确认不含 `slub_test_*`、`slub_check` 或故障注入状态。

最大静态记录数为 520；实际最大同时存活批次为 505 个 16 字节对象（3 个 slab）。
混合序列和复用批次最多 48 个对象，其中请求不超过 8229 字节，保守按每对象独占 4 页计算，不超过 192 页（768 KiB）。
边界组 37 个对象、独立大对象组 8 个对象均低于此预算；单个最大请求为 16421 字节。
无无限循环、真实内存耗尽或被测分配器递归依赖。
编译器 `-fstack-usage` 审计中，最终测试函数单个栈帧最大为 `slub_check` 的 256 字节，`st_alloc` 为 112 字节；这是单函数栈帧，不等同于完整调用链峰值。
见 [stack-audit.log](evidence/slub-tests/stack-audit.log)。

## 实际命令

从 `workspace/lab2/lab2_new/` 执行，WSL 下使用 GCC 13.2.0、QEMU 4.1.1、默认单 hart 和 128 MiB RAM。
这些变量展开后就是实际运行参数；结果 JSON 另存完整参数数组。所有临时构建与评分副本均位于工作区 `tmp/`。

```sh
SLUB_VERIFY=/mnt/d/wsl-share/OS/tmp/slub-tests
SLUB_QEMU=/mnt/d/wsl-share/OS/tmp/buddy-implementation/qemu-system-riscv64-4.1.1

make -j2 SLUB_TEST=1 OBJDIR="$SLUB_VERIFY/test-obj" BINDIR="$SLUB_VERIFY/test-bin"
make qemu SLUB_TEST=1 OBJDIR="$SLUB_VERIFY/test-obj" BINDIR="$SLUB_VERIFY/test-bin" QEMU="$SLUB_QEMU"

make -j2 SLUB_TEST=0 OBJDIR="$SLUB_VERIFY/normal-obj" BINDIR="$SLUB_VERIFY/normal-bin"
make qemu SLUB_TEST=0 OBJDIR="$SLUB_VERIFY/normal-obj" BINDIR="$SLUB_VERIFY/normal-bin" QEMU="$SLUB_QEMU"

make grade SLUB_TEST=1 GRADE_TMP="$SLUB_VERIFY/grade-tree" QEMU="$SLUB_QEMU"
```

两种构建使用独立的 OBJDIR/BINDIR；切换选项时应采用独立输出目录或先清理构建，避免 Make 复用不同宏配置的旧目标文件。
QEMU 会继续进入原有内核无限循环；采集到标记后，运行脚本仅对自己创建的进程组发送 SIGTERM。因此正常采集记录中 -15 是主动停止，不是内核失败。

## 真实结果

| 检查 | 结果与原始证据 |
|---|---|
| 测试构建 | 首次完整编译与加强后的重编译均退出码 0；[首轮完整构建](evidence/slub-tests/initial-test-build.log)、[最终重编译](evidence/slub-tests/test-build.log) |
| 测试内核 QEMU | Buddy 九组自检与 `check_alloc_page()` 通过，SLUB 八组全部通过，无 panic；[test-qemu.log](evidence/slub-tests/test-qemu.log) |
| 对象与内容统计 | 1831 次成功分配、1831 次释放、5502 次完整存活内容复核；结束恢复 31905 个空闲页 |
| 三个失败注入点 | 日志分别显示故障被消费并恢复成功；没有真实耗尽内核内存 |
| 普通构建与启动 | 编译退出码 0、Buddy 自检和原冒烟检查通过；测试辅助符号缺席；[normal-build.log](evidence/slub-tests/normal-build.log)、[normal-qemu.log](evidence/slub-tests/normal-qemu.log)、[normal-result.json](evidence/slub-tests/normal-result.json) |
| 课程 make grade | **10/20**，Page Init OK、Best-Fit WRONG，make 退出码 2；[grade.log](evidence/slub-tests/grade.log) |
| 评分副本内核 | Page Init PASS，Best-Fit 明确打印 `pmm_manager=buddy_pmm_manager`；同一串口到达 SLUB 最终成功标记；[grade-qemu.log](evidence/slub-tests/grade-qemu.log) |
| 差异检查 | `git diff --check` 通过，上一阶段历史提示词前缀完整保留 |

最终成功输出为：

```text
SLUB CHECK ALL PASSED (8 groups, 1831 allocations, 1831 releases, 5502 content audits, 31905 free pages)
```

课程 `tools/test_lab2.c:test_best_fit()` 要求当前管理器为 Best-Fit，原分支实际选择 Buddy；本轮未改动该选择或评分脚本。
因此 10/20 不能描述为课程 grade 通过，SLUB 的测试通过结论则来自独立的八组检查。
测试构建静态数据及代码增大了内核，空闲页基线与普通构建的 31912 不同，不能将这项差异当作泄漏；同一镜像内各组均恢复自己的基线。
完整执行参数、环境及源码 SHA-256 见 [results.json](evidence/slub-tests/results.json)。

本轮完成全部新增测试和当前可用的必要检查，没有未能执行的构建或运行项。
本轮没有新增终端截图；原实验整组报告中的身份、截图及最终目录整理仍应在最终提交前核对，不据本测试记录宣称整组报告已完成。
既有第一阶段日志保留其原始源码快照含义，不重写旧证据来匹配本轮修改。

## 实际迭代与收获

本轮实际提示词全文追加在 [prompt.md](prompt.md)。首版测试编译运行通过；复核后加强第 4 组，改为先填满两个 slab 再释放与重新分配，排除其他 partial slab 掩盖状态转移错误的可能。随后重新编译运行测试与普通构建并重新执行 grade，正式证据对应最终版本。没有分配器算法修复，也没有删除失败检查换取通过。
对上一阶段普通冒烟检查的地址相等断言作了契约修正；本轮对象复用测试通过页数、数据和区间关系验证实际行为。
读辅助接口只提供布局常量，页数预期、模式和存活记录均由测试独立维护，避免把被测实现的状态直接当作正确答案。
核实 Buddy 的原始请求数释放约定，并用 3→4 页、5→8 页的案例同时验证分配和回收，体现了对象层与页层之间的职责边界。
AI 协作时把失败入口是否真正触发作为断言，是避免“失败测试实际走了缓存成功路径”的关键步骤。
