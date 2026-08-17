---
doc_type: dev-guide
slug: architecture-and-extension
component: rag-desktop-product
status: current
summary: 介绍 Python RAG、Qt Workspace、Local Worker Protocol、SQLite 数据边界、测试和扩展方式。
tags: [architecture, Python, Qt6, worker-protocol]
last_reviewed: 2026-08-17
---

# 架构与扩展指南

## 概述

项目由 Python 诊断引擎和 Qt 6/C++17 桌面端组成。Python 是检索、规则解析和报告的唯一实现；Qt 负责配置、进程监督、用户交互、状态展示和本地历史。

核心目标是保持跨语言边界清晰：页面不直接启动 Python，Qt 不复制检索算法，Python stdout 不输出协议以外的文本。

## 前置依赖

- Python 3.12。
- `pip install -e ".[ml]"`。
- Qt 6.5+ Widgets/Sql。
- CMake 3.21+、Ninja、MinGW。
- 授权的 STM32F4 资料和 Hugging Face 模型缓存。

## 快速上手

```powershell
.\.venv312\Scripts\python.exe -m unittest discover -s tests -v

& 'D:\Qt\Tools\CMake_64\bin\cmake.exe' `
  -S qt -B qt\build -G Ninja `
  -DCMAKE_PREFIX_PATH='D:\Qt\6.8.3\mingw_64'

& 'D:\Qt\Tools\CMake_64\bin\cmake.exe' --build qt\build
```

## 核心概念

### Python 数据链

```text
Source Manifest
  -> Loader
  -> PDF/Markdown/Header Chunking
  -> Embedding + Chroma
  -> BM25 + Dense Retrieval
  -> RRF Fusion
  -> Cross-Encoder Reranker
  -> Evidence Threshold
  -> DiagnosisResult / Markdown Report
```

主要目录：

- `src/rag_diagnostic/ingestion/`：加载、切片和 metadata。
- `src/rag_diagnostic/retrieval/`：Embedding、Chroma、BM25、Hybrid 和 Reranker。
- `src/rag_diagnostic/hardfault.py`：Cortex-M4 Fault 规则分析。
- `src/rag_diagnostic/protocol.py`：离线协议日志规则分析。
- `src/rag_diagnostic/evaluation.py`：Hit Rate、Recall、MRR 和 Failed Cases。
- `src/rag_diagnostic/runtime/`：STM32 服务、Retriever Cache 和 Worker。

### Qt 页面链

```text
Workspace Page action
  -> DiagnosticWorkbench handler
  -> RagProcessBridge
  -> Local Worker Protocol v1
  -> typed result / task state
  -> Workspace Page rendering
```

页面只能通过 handler 请求 Shell 执行任务。不能在页面类中创建第二个 `QProcess`。

### Local Worker Protocol v1

请求：

```json
{
  "protocol_version": 1,
  "request_id": "uuid",
  "operation": "health | query | hardfault | protocol_log | reindex | evaluate | shutdown",
  "payload": {},
  "options": {"top_k": 5}
}
```

响应事件：

```json
{
  "protocol_version": 1,
  "request_id": "uuid",
  "event": "accepted | progress | result | error",
  "data": {}
}
```

约束：

- query、hardfault、protocol_log、reindex 和 evaluate 共用一个重任务槽。
- health 不加载模型，可以在 Worker 空闲时刷新。
- progress 必须属于当前 request ID。
- stdout 只输出 JSON Lines；模型 warning 和 traceback 写 stderr。
- 单次 validation error 不结束 Worker；意外进程退出才进入 Worker Failed。

### 本地 SQLite

`CaseStore` 和 `EvaluationStore` 共用：

```text
data/runtime/diagnosis_cases.sqlite3
```

`LocalDatabaseSchema` 统一管理 schema v3：

- diagnosis cases
- evaluation candidates
- approved evaluation samples
- evaluation runs

Candidate 和 Approved Sample 物理分离。批准操作使用事务，避免 Candidate 已批准但 Sample 未落盘。

## 接口参考

### Runtime Settings v1

| 字段 | 含义 |
|---|---|
| `project_root` | 包含 `pyproject.toml` 的项目目录 |
| `python_executable` | Worker 使用的 Python |
| `embedding_model` | Embedding 模型名 |
| `reranker_model` | Cross-Encoder 模型名 |
| `default_top_k` | 页面默认证据数量 |
| `task_timeout_seconds` | 超时配置契约，当前尚未执行强杀 |

设置使用当前 Windows 用户的 `QSettings`，不写入 API Key。

发布启动器可以设置以下环境变量，它们的优先级高于旧的 `QSettings` 路径：

| 环境变量 | 含义 |
|---|---|
| `RAG_DIAGNOSTIC_PROJECT_ROOT` | 包含 `pyproject.toml` 的仓库根目录 |
| `RAG_DIAGNOSTIC_PYTHON_EXECUTABLE` | Worker 使用的 Python 解释器 |

这两个变量只覆盖项目和 Python 路径，不覆盖模型、top-k 或超时设置。

### Expected Evidence 匹配

匹配优先级：

1. `chunk_id`
2. `doc_id + page`
3. `source + section`
4. `source`

新增匹配粒度时必须保持历史 Evaluation Run 可解释，不应修改既有指标含义。

## 常见扩展

### 增加新的 STM32 资料

1. 确认资料允许本地使用。
2. 在 `STM32_SOURCE_MANIFEST` 注册准确文件名、source ID 和展示名。
3. 在 loader/chunker 中保留 doc_id、page、section 和 domain。
4. 增加切片与 source snapshot 测试。
5. 重建索引并扩充人工评估样本。

不要通过 Qt UI 绕过 manifest。

### 增加新的诊断规则

1. 先定义可从输入确定的字段和错误边界。
2. 在 Retriever 加载前完成输入校验。
3. 把规则事实放入 `metadata` 的独立对象。
4. generated query 只用于检索，不反写为规则事实。
5. 添加正常、边界、冲突和错误恢复测试。

### 增加新的 Workspace 页面

1. 页面提供输入、显示和 handler，不持有 Worker。
2. 在 Shell 静态注册页面和导航。
3. 接收统一 `WorkspaceTaskState`。
4. 添加 page smoke 和 Workbench smoke。
5. 保持既有 `objectName`/`testId` 稳定。

## 测试

Python 全量回归：

```powershell
.\.venv312\Scripts\python.exe -m unittest discover -s tests -v
```

Qt 页面与 Store：

```powershell
$env:Path = 'D:\Qt\6.8.3\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:Path
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = 'C:\Windows\Fonts'

.\qt\build\DiagnosisWorkspacePageSmoke.exe
.\qt\build\CaseStoreSmoke.exe
.\qt\build\CaseLibraryPageSmoke.exe
.\qt\build\KnowledgeCenterPageSmoke.exe
.\qt\build\EvaluationStoreSmoke.exe
.\qt\build\EvaluationCenterPageSmoke.exe
.\qt\build\WorkbenchCaseLibrarySmoke.exe
.\qt\build\WorkbenchKnowledgeSmoke.exe (Resolve-Path '.').Path
.\qt\build\WorkbenchEvaluationSmoke.exe (Resolve-Path '.').Path
```

真实模型与 Worker：

```powershell
$env:HF_HUB_OFFLINE = '1'
$env:TRANSFORMERS_OFFLINE = '1'
.\qt\build\RagProcessBridgeSmoke.exe (Resolve-Path '.').Path
```

## 已知限制与注意事项

- `DiagnosticWorkbench` 仍承担较多页面协调，后续重构不能改变页面 handler 和唯一 Worker 契约。
- `task_timeout_seconds` 当前只保存设置，运行时恢复功能尚未完成。
- STM32F4 索引目前是全量重建，尚未实现文档指纹、增量更新和回滚记录。
- 当前评估集较小，只适合代码回归。
- Windows `dist` 是 source-assisted 轻量包，不包含 Python、模型、手册和索引；完整自包含安装器仍未实现。

## 相关文档

- [项目 README](../../README.md)
- [用户快速上手](../user/getting-started.md)
- [Qt 详细运行说明](../../qt/README.md)
- [GitHub 发布检查清单](../github-release-checklist.md)
- [Windows 打包与发布](windows-packaging.md)
