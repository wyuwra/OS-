# Lab2 实验材料索引

主实验报告为 [report.md](report.md)，源码位于仓库根目录 `lab2_new/`。我负责Buddy及SLUB模块，姓名与学号为罗元伟、2412737。

| 材料 | 内容 |
|---|---|
| [主实验报告](report.md) | 初始化、First-Fit、Best-Fit、Buddy、SLUB、MMU调试及完整验证结果 |
| [提示词记录](prompt.md) | 各模块实际提示词、实现与测试过程；Buddy原文见[此节](prompt.md#buddy-prompts) |
| [Buddy设计与测试](buddy.md) | 分阶、拆分、伙伴合并、释放约定及九组自检 |
| [SLUB工作总结](slub-summary.md) | 两层设计、缓存、布局与测试方向 |
| [SLUB实现记录](slub.md) | 实现阶段的接口、布局和接入记录 |
| [SLUB测试记录](slub-tests.md) | 八组测试、失败注入及资源上界 |
| [本次完整验证](evidence/final-20261021/results.json) | 两种配置的构建、启动、评分及源码哈希 |
| [截图来源](images/image-sources.json) | 模块一、MMU、Buddy、SLUB及评分截图与日志对应关系 |

Buddy九组自检与SLUB八组测试通过。仓库默认启用Best-Fit，验收配置通过Page Init与Best-Fit检查，得到20/20；Buddy配置的课程评分为10/20。各结果对应的配置与命令见[主报告第5.2节](report.md#best-fit-validation)和[第5.3节](report.md#buddy-slub-validation)。
