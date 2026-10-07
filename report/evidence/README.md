# 练习一验证证据

验证日期：2026-10-07。全部文本输出来自本实验源码构建的实际工具运行。

## 文件

| 文件 | 内容 |
|---|---|
| `environment-build.txt` | 版本、核验构建输出、源码及产物 SHA-256 |
| `elf-analysis.txt` | 完整符号表、ELF 节表、目标文件重定位和机器指令 |
| `gdb-entry.txt` | 实际 GDB 会话与检查断言结果 |
| `qemu-output.txt` | OpenSBI 与内核打印输出，末尾由脚本主动停止 |
| `exercise1.gdb` | 带断言的 GDB 命令 |
| `verify-exercise1.sh` | 编译核验、静态检查和 QEMU/GDB 验证脚本 |
| `prepare-views.py` | 从完整日志逐字截取便于截图的片段 |
| `view-*.txt` | 完整原始日志的连续片段，没有重写寄存器或输出内容 |

## 复现

在已配置 Lab0 的 Ubuntu WSL 中，从仓库根目录运行：

```bash
bash report/evidence/verify-exercise1.sh
```

默认读取当前已安装的 GCC 8.3.0 与 QEMU 4.1.1。部分断点地址对应当前源码与工具链；修改后需重新检查反汇编并更新 GDB 文件。独立端口为 `127.0.0.1:1235`。每次运行会覆盖同名日志，复现后再截图。

在 Windows PowerShell 或装有 Python 的环境下，从仓库根目录运行：

```powershell
python report/evidence/prepare-views.py
```

这只提取文本片段，不生成截图。

## 待补的真实截图

本次界面截图操作被用户按 Esc 中止，尚未保存 PNG 文件。请本人在 VS Code 打开下列日志，或在 WSL 终端按答辩文档进行实际调试后截图：

| 建议截图文件名 | 观察内容 | 可打开的日志 |
|---|---|---|
| `exercise1-stack-tail.png` | 栈范围、sp 初始化、tail 前后 ra | `view-stack-tail.txt` |
| `exercise1-frame-call.png` | 栈帧、保存 ra、普通调用及零长度返回 | `view-frame-call.txt` |
| `exercise1-loop-bss.png` | 自循环与 edata/end | `view-loop-bss.txt` |
| `exercise1-relocations.png` | 目标文件中的重定位和 tail 指令序列 | `view-relocations.txt` |
| `exercise1-linked-entry.png` | 链接后入口与 memset 分支 | `view-linked-entry.txt` |

保存到 `report/images/`。在 `report.md` 或成员一分析中加入实际图片引用，例如：

```markdown
![入口单步日志截图](images/exercise1-stack-tail.png)
```

如果截图展示的是编辑器中的日志，应注明“实际 GDB 日志的编辑器截图”，不要称为实时终端截图。建议保留文件名、关键数值和 PASS 行；避免包含与实验无关的聊天、账号或其他项目内容。

不要用原始文本绘制一张仿终端图片，再把它当成实际调试界面截图提交。
