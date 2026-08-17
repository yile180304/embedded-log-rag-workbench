---
doc_type: user-guide
slug: getting-started
component: rag-desktop-product
status: current
summary: 从准备 Python 和 STM32F4 资料到建立索引、运行诊断、管理案例和执行检索评估的完整操作指南。
tags: [RAG, STM32F4, Qt6, user-guide]
last_reviewed: 2026-08-17
---

# 用户快速上手

## 功能简介

这个工具用于辅助排查 STM32F4 技术问题、HardFault 和离线通信日志。它先用规则确定寄存器位、序列、周期、长度和 CRC 等事实，再从本地芯片手册或代码头文件中查找证据。

它更像“自动翻手册并整理排查工单”，不是能凭一段日志唯一确定根因的自动修复器。

## 前置条件

- Windows 10/11。
- Python 3.12。
- 使用 Qt 界面开发构建时需要 Qt 6.5+、MinGW、CMake 和 Ninja。
- 首次使用 BGE 时需要联网下载模型，或提前准备 Hugging Face 缓存。
- 自行取得有权使用的 STM32F4 手册和 CMSIS 头文件。

## 安装 Python 依赖

在项目根目录执行：

```powershell
py -3.12 -m venv .venv312
.\.venv312\Scripts\python.exe -m pip install -U pip
.\.venv312\Scripts\python.exe -m pip install -e ".[ml]"
```

以后所有命令都使用 `.venv312\Scripts\python.exe`。不要把系统 Python、旧 `.venv` 和 `.venv312` 混在一起。

## 准备资料

1. 打开 `src/rag_diagnostic/profiles.py`，查看 `STM32_SOURCE_MANIFEST`。
2. 从芯片厂商、Arm 或 CMSIS 官方渠道取得相应资料。
3. 保持 manifest 要求的文件名，放入 `storage/stm32f4/`。
4. 不在这些目录放无关 PDF；未在 manifest 中的文件会显示为 `ignored`。

仓库不会公开上传本地手册和索引。

## 建立 STM32F4 索引

```powershell
.\.venv312\Scripts\python.exe -m rag_diagnostic `
  --root . `
  --embedding bge `
  --embedding-model BAAI/bge-small-zh-v1.5 `
  --reranker cross-encoder `
  --reranker-model BAAI/bge-reranker-base `
  stm32-ingest
```

成功结果会显示 documents、chunks、index_count、ignored 和单文件 errors。重建失败不会把坏 collection 替换为正式索引。

## 使用 Qt 工作台

开发构建完成后：

```powershell
$env:Path = 'D:\Qt\6.8.3\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:Path
.\qt\build\RagDiagnosticWorkbench.exe
```

首次打开先查看顶部 Runtime Center：

1. 项目路径应指向包含 `pyproject.toml` 的根目录。
2. Python 应指向 `.venv312\Scripts\python.exe`。
3. 索引应显示 ready 和实际 chunks。
4. Worker 应变为 Ready。
5. 第一次真实查询会加载 Embedding/Reranker，后续任务复用缓存。

### 使用 Windows 发布目录

维护者可以在仓库根目录执行：

```powershell
.\tools\package-windows.ps1
```

生成结果位于 `dist/RagDiagnosticWorkbench/`。正式演示使用：

```powershell
.\dist\RagDiagnosticWorkbench\Launch-RagWorkbench.ps1
```

发布目录已经包含 Qt/MinGW 运行库，不要求系统 `PATH` 中安装 Qt。它仍使用仓库内的 `.venv312`、模型缓存、资料和索引；如果把发布目录复制到仓库外，需指定：

```powershell
.\Launch-RagWorkbench.ps1 -ProjectRoot "D:\path\to\RAG"
```

## 专家问答

1. 打开“诊断工作区”。
2. 选择“专家问答”。
3. 输入寄存器、外设或调试工具问题，例如 `ETH_DMASR 的 EBS 表示什么？`。
4. 设置 top-k 后开始诊断。
5. 查看结论、Grounded 状态和 Evidence Table。
6. 需要复核时打开 Markdown 诊断报告。

如果证据不足，系统会拒答。这通常表示问题超出当前资料，而不是程序损坏。

## HardFault 诊断

1. 选择“HardFault 日志”。
2. 粘贴至少包含 `CFSR` 或 `HFSR` 的现场。
3. 点击解析并诊断。
4. 先查看规则事实：寄存器值、置位标志和地址是否有效。
5. 再查看知识库建议和原文 Evidence。

示例：

```text
CFSR=0x00008200 HFSR=0x40000000
BFAR=0x2003FFF8 PC=0x080126AC LR=0xFFFFFFF9
```

`BFAR` 只有在 `BFARVALID` 置位时才会被当作可信地址。

## 协议日志诊断

1. 选择“协议日志”。
2. 粘贴应用日志、tshark 字段文本或显式 `FRAME/CRC` 文本。
3. 选择 auto、generic、udp 或 trdp profile。
4. 只有在已知协议约束时填写周期、容差和期望长度。
5. 需要 CRC 时明确选择算法。
6. 先核对规则异常，再查看 RAG 建议。

系统不会猜测周期、CRC 多项式或 TRDP 二进制布局。

## 管理诊断案例

成功诊断和证据不足拒答会保存到本地 SQLite：

```text
data/runtime/diagnosis_cases.sqlite3
```

在“诊断案例库”中可以：

- 按关键词、模式、Grounded 和收藏筛选。
- 保存备注和收藏状态。
- 复制结论或导出单条 JSON。
- 不经过 Worker 直接重开历史快照。
- 把案例标记为评估候选。

导出前请删除设备序列号、内存内容、内网地址和业务标识。

## 使用评估中心

1. 从案例库创建 Feedback Candidate，或导入评估 JSON。
2. 在评估中心校对 query。
3. 勾选、编辑或补充正确的 Source、Doc ID、Page、Section 或 Chunk ID。
4. 批准为正式样本。
5. 设置 top-k 并运行评估。
6. 查看样本数、Hit Rate@K、Recall@K、MRR 和 Failed Cases。

只有人工批准的 active 样本参与指标。helpful/unhelpful 不会自动成为正确答案。

## 常见问题

Q: Worker Ready，但索引是 warning。

A: Worker 可以在没有索引时启动。进入知识源管理，确认资料存在后执行重建。

Q: 第一次查询很慢，第二次明显更快。

A: 第一次需要加载 BGE 和 Reranker；长驻 Worker 会复用已经加载的模型。

Q: 为什么回答显示“无法根据当前知识库确认”？

A: 当前 Evidence 没有达到证据门槛。应补充资料、调整问题或人工查看手册，不应强制模型生成答案。

Q: `qt/build` 里为什么有很多 exe？

A: 除 `RagDiagnosticWorkbench.exe` 外大部分是 smoke 测试程序。面向演示的目录是 `dist/RagDiagnosticWorkbench/`，其中不会包含这些测试程序。

Q: 可以直接把 `storage` 上传到 GitHub 吗？

A: 不可以。它包含本地手册、Chroma、chunks 和可能受版权限制的资料。

## 相关功能

- [项目 README](../../README.md)
- [架构与扩展指南](../dev/architecture-and-extension.md)
- [Qt 详细说明](../../qt/README.md)
- [Windows 打包与发布](../dev/windows-packaging.md)
