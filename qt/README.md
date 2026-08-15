# Qt 6 STM32F4 RAG 诊断工作台

这个界面可以理解为 RAG 引擎的“仪表盘”：Python 继续负责检索和诊断，Qt 只负责发起任务、保持窗口响应，并把 JSON 结果整理成人能快速浏览的界面。

## 工作台结构

主窗口现在是一层稳定的 Workspace Shell，而不是把所有诊断控件直接堆在一个类里：

- 左侧导航注册“诊断工作区”“诊断案例库”“知识源管理”和“评估中心”；协议日志是诊断工作区内的第三种真实模式。
- 顶部 Runtime Center 展示项目目录、Python、索引、Worker 和 Engine 的全局状态，并提供重建索引与运行设置。
- 中间 Diagnosis Workspace Page 保存专家问题、HardFault/协议日志、回答、规则工单、Evidence Table 和报告入口。
- 底部 Runtime Activity Panel 展示全局运行日志和索引摘要；以后切换到其他真实页面时仍可观察同一个 Worker。

专家问答、HardFault 和协议日志是同一诊断页内的三种模式，不是三个独立进程。页面动作先回到 Shell，再由唯一的 `RagProcessBridge` 发送给长驻 Python Worker；结果解析为 `DiagnosisViewModel` 后单向回到页面。切换模式不会清空输入、重建索引或启动第二个 Worker。

## 已实现功能

- 自动从程序目录向上定位 RAG 项目，并检查 `.venv312/Scripts/python.exe` 与 STM32F4 chunks。
- 通过“运行设置”维护项目目录、Python、Embedding/Reranker、默认 top-k 和任务超时，并用 `QSettings` 跨启动保存。
- 启动前执行 Runtime Preflight；设置生效时显式重启空闲 Worker，任务运行中禁止重启。
- 展示 Worker Health Snapshot：Python 版本、索引状态、chunks、更新时间、模型加载状态和 Active Request。
- 支持“专家问答”“HardFault 日志”“协议日志”三种输入模式。
- 应用启动时只创建一个长驻 Python RAG Worker；专家查询、HardFault、协议日志、索引重建和检索评估通过 UTF-8 JSON Lines 复用同一进程、模型与检索器。
- Worker 生命周期与单次任务状态分开显示；单次校验或检索错误不会杀死 Worker，同一时间仍只允许一个重任务。
- 展示 `Idle / Running / Succeeded / Failed`、Grounded/拒答状态、回答、模型信息和检索耗时。
- HardFault 模式展示寄存器快照、置位标志、确定性观察、下一步动作、自动检索问题和 RAG 证据。
- 协议日志模式展示事件、端点、序列、长度、观测周期、CRC 状态、规则异常和独立的 RAG 建议。
- 展示 Rank、Doc ID、Page、Section、Source、Reranker、Hybrid、BM25、Dense、Chunk ID。
- 使用系统默认程序打开 Python 生成的 Markdown 报告。
- 显示索引重建的 documents、chunks、index_count、errors、ignored 摘要。
- 使用左侧 Workspace 导航、全局 Runtime Center 和可折叠比例布局组织诊断页与 Runtime Activity。
- 使用本地 SQLite 自动保存成功/拒答案例，并支持筛选、重开、备注、收藏、复制与单条 JSON 导出。
- 展示 `STM32_SOURCE_MANIFEST` 受控资料、文件状态、索引状态、chunk 数与单文件错误，并复用同一个 Worker 刷新和重建。
- 支持从案例反馈创建 Evaluation Candidate，经人工编辑 query 和 Expected Evidence 后批准为正式样本，再运行 Hit Rate@K、Recall@K、MRR 和 Failed Cases 评估。

## 诊断案例库

诊断完成后，Shell 把完整结果快照保存到当前 Runtime Settings 项目目录下的 `data/runtime/diagnosis_cases.sqlite3`。自动保存范围是专家问答、HardFault、协议日志的成功结果和 `grounded=false` 拒答；输入校验、busy、Worker/JSON 错误不生成案例。数据库不可用时当前诊断仍正常显示，错误会写入状态栏和 Runtime Activity。

使用步骤：

1. 完成一次专家问答、HardFault 或协议日志诊断。
2. 点击左侧“诊断案例库”，按关键词、模式、证据状态或收藏筛选。
3. 选中记录后，可复制结论、编辑并保存备注、收藏或导出 JSON。
4. 点击“重开到诊断工作区”恢复原输入、回答、Evidence 和 HardFault 工单；该操作只读本地快照，不请求 Worker，任务运行期间会禁用。

直接打开案例库：

```powershell
.\qt\build\RagDiagnosticWorkbench.exe --cases
```

案例和导出 JSON 可能包含用户原始日志、寄存器和备注。Case Store 不保存 API Key、代理、账号、Runtime Settings 或完整 Runtime Activity，但共享文件前仍需做业务脱敏。

## 知识源管理

点击左侧“知识源管理”，可以把它理解成知识库的库存盘点页：它告诉你“哪些资料获准入库、实物是否存在、货架上的索引是否过期”，但不负责读取 PDF、执行 embedding 或修改准入名单。

准入名单的唯一来源仍是 Python `src/rag_diagnostic/profiles.py` 中的 `STM32_SOURCE_MANIFEST`。点击“刷新状态”只发送一次 health 请求，不加载模型，也不创建第二个 Worker；点击“重建 STM32F4 索引”继续走现有 reindex 请求，Running 时会禁用重复操作，完成后自动刷新顶部总 chunks 和来源明细。

状态含义：

- Manifest `allowed`：白名单文件存在；`missing`：白名单文件缺失；`ignored`：目录内文件不在白名单，不参与索引。
- Index `indexed`：已有 chunks 且源文件未更新；`stale`：源文件比 chunks 新；`not_indexed`：当前没有 chunks；`error`：最近一次重建出现单文件解析错误。
- Reindex Phase 只显示 `Idle / Running / Succeeded / Failed`。Worker 没有返回 `sources` 时显示兼容提示，诊断和案例库仍可使用。

第一版不支持任意文件导入、manifest 编辑、文件 watcher、云同步或没有协议依据的百分比进度。

直接打开页面：

```powershell
.\qt\build\RagDiagnosticWorkbench.exe --knowledge
```

## 评估中心

评估中心用于建立可追溯的检索回归集。案例库中的 helpful/unhelpful 只会形成待审核 Candidate，不会自动成为 ground truth。审核者需要校对 query，并勾选、编辑或补充 Source、Doc ID、Page、Section 或 Chunk ID；批准后才生成独立的 Approved Evaluation Sample 快照。

运行评估时，Qt 复制当前 active 样本并发送一次 `evaluate` 请求。它与 query、HardFault、协议日志和 reindex 共用同一个重任务槽，因此不会创建第二个 Worker。页面展示样本数、top-k、Hit Rate@K、Recall@K、MRR、耗时、模型、索引状态和 Failed Cases；失败运行不会伪装成 0% 成功指标。

当前小规模评估集只用于代码回归，不能代表生产环境准确率。

## 构建

在 RAG 项目根目录执行：

```powershell
& 'D:\Qt\Tools\CMake_64\bin\cmake.exe' `
  -S qt `
  -B qt\build `
  -G Ninja `
  -DCMAKE_PREFIX_PATH='D:\Qt\6.8.3\mingw_64'

& 'D:\Qt\Tools\CMake_64\bin\cmake.exe' --build qt\build
```

首次直接运行时，把 Qt 和 MinGW DLL 目录加入当前 PowerShell 的 PATH：

```powershell
$env:Path = 'D:\Qt\6.8.3\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:Path
.\qt\build\RagDiagnosticWorkbench.exe
```

也可以部署运行库后直接双击：

```powershell
& 'D:\Qt\6.8.3\mingw_64\bin\windeployqt.exe' .\qt\build\RagDiagnosticWorkbench.exe
```

## 运行设置与健康自检

点击顶部工具栏的“运行设置”，可以查看和修改六项本地运行参数：

1. 项目目录：必须是包含 `pyproject.toml` 的 RAG 根目录。
2. Python 解释器：建议固定为项目内 `.venv312/Scripts/python.exe`。
3. Embedding 模型：默认 `BAAI/bge-small-zh-v1.5`。
4. Reranker 模型：默认 `BAAI/bge-reranker-base`。
5. 默认 top-k：允许 1-20，保存后同步到主界面的证据条数。
6. 任务超时：允许 30-3600 秒；当前版本只保存该值，还不会强杀超时任务或自动重启 Worker。

“应用并重启 Worker”的顺序是：校验字段、保存设置、停止空闲 Worker、用新配置启动 Worker。字段有误时不会写入设置，也不会启动新 Worker；任务运行中“应用并重启”和“恢复默认”会被禁用，但仍可刷新健康状态。恢复默认值需要二次确认。

设置由 Windows 当前用户的原生 `QSettings` 保存，键统一放在 `runtime/v1/*`。它不写入项目目录，也不保存 API Key、代理或账号；模型凭证仍只从环境变量读取。

设置页下半部分的 Health Snapshot 是 Worker 返回的事实状态：

- Worker：`Starting / Ready / Failed` 生命周期状态。
- Python：Worker 实际使用的 Python 版本。
- 索引：ready、chunks 和更新时间。索引不存在是 warning，不阻止 Worker 启动，可回到主界面重建。
- 模型：Embedding/Reranker 名称与当前是否已加载。启动 health 不会为了显示状态而加载模型。
- Active Request：当前重任务的 request ID；为空表示 Worker 空闲。

修改配置后推荐先点击“应用并重启 Worker”，等待顶部状态回到 `Worker · Ready`，再执行查询或重建索引。

## 专家问答模式

1. 确认顶部的项目、Python 和 STM32F4 索引均显示可用。
2. 模式选择“专家问答”，输入 `ETH_DMASR 的 EBS 表示什么？`，保留 top-k=5，点击“开始诊断”。
3. 等待 Worker 显示 Ready 后发起查询。第一次查询需要加载模型，后续查询复用同一个 Retriever Cache，不再重复显示 `Loading weights`。
4. 完成后查看回答与 Evidence Table，可点击“打开 Markdown 诊断报告”。
5. 新增或替换白名单资料后，点击“重建 STM32F4 索引”；当前 5,540 chunks 的重建在本机约需数分钟。

域外问题（例如“草莓蛋糕怎么做”）会显示“无证据”，这是证据门槛生效，不是程序错误。Hugging Face warning 会出现在运行日志中，只要进程 exit code 为 0 且 stdout JSON 有效，任务仍视为成功。

## HardFault 日志模式

1. 模式选择“HardFault 日志”。
2. 打开 `data/examples/hardfault_precise_busfault.log`，把完整内容粘贴到日志框；也可以粘贴调试器或崩溃处理函数输出，其中至少要有 `CFSR` 或 `HFSR`。
3. 点击“解析并诊断”。Engine 先进入 Running；规则校验通过后才加载 BGE 与 Reranker。
4. 在“规则事实”区域核对寄存器、`PRECISERR/BFARVALID/FORCED`、有效地址和下一步检查动作。
5. 在“知识库建议”与 Evidence Table 中核对手册依据。规则事实来自寄存器位，不等于检索分数；RAG 证据不足时规则结果仍会保留。
6. 点击“打开 Markdown 诊断报告”，查看包含原始日志、规则解码、自动问题、回答和来源的完整工单。

输入错误时，界面保留原日志并显示字段级原因。例如冲突的两个 `CFSR`、超过 32 位的数值、非法字符或只有 `PC/LR` 都不会启动模型。任一任务运行期间再次查询、诊断或重建索引，第二个任务会被拒绝，避免多个模型进程抢占内存。

## 协议日志模式

1. 模式选择“协议日志”，粘贴离线应用日志、Wireshark/tshark 字段文本或显式 `FRAME/CRC` 文本。
2. Profile 可选自动、通用、UDP 或 TRDP 显式字段。周期、容差和期望长度只有勾选后才参与异常判断。
3. CRC 下拉框默认“不校验”；选择 MODBUS 或 CCITT-FALSE 时，每个待校验帧必须同时给出 `FRAME=<hex>` 和 `CRC=<hex>`。
4. 点击“分析协议日志”，先查看 Protocol Rule Facts 中的事件与异常，再看 Evidence-backed Diagnosis。无协议资料时 RAG 可以拒答，但序列、周期、长度和 CRC 规则事实仍保留。
5. 完成结果自动进入 Case Store Schema v2，可按“协议日志”筛选、重开、备注、收藏和导出。

第一版不读取 PCAP、不监听网卡/串口、不连接实时设备、不猜 TRDP 二进制布局，也不会自动修改、编译或烧录代码。

## 长驻 Worker 回归

页面级 smoke 不启动 Python，适合先检查模式切换、按钮 handler、结果渲染和报告入口：

```powershell
$env:Path = 'D:\Qt\6.8.3\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:Path
.\qt\build\DiagnosisWorkspacePageSmoke.exe
```

案例持久化和页面交互 smoke：

```powershell
.\qt\build\CaseStoreSmoke.exe
$env:QT_QPA_PLATFORM='offscreen'
.\qt\build\CaseLibraryPageSmoke.exe
.\qt\build\WorkbenchCaseLibrarySmoke.exe
Remove-Item Env:QT_QPA_PLATFORM
```

知识源页面和 Workbench 级联调 smoke：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
.\qt\build\KnowledgeCenterPageSmoke.exe
.\qt\build\WorkbenchKnowledgeSmoke.exe (Resolve-Path '.').Path
Remove-Item Env:QT_QPA_PLATFORM
```

Qt 桥接 smoke 会验证：坏 HardFault 只让当前任务失败、修正后仍使用相同 PID 完成 HardFault 和协议日志真实诊断，并且任务运行期间查询/诊断/协议分析/重建都会被本地拒绝：

```powershell
$env:Path = 'D:\Qt\6.8.3\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:Path
$env:HF_HUB_OFFLINE='1'
$env:TRANSFORMERS_OFFLINE='1'
.\qt\build\RagProcessBridgeSmoke.exe (Resolve-Path '.').Path
```

运行设置、健康快照和显式重启的回归入口：

```powershell
$env:Path = 'D:\Qt\6.8.3\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:Path
$env:HF_HUB_OFFLINE='1'
$env:TRANSFORMERS_OFFLINE='1'

.\qt\build\RuntimeSettingsSmoke.exe (Resolve-Path '.').Path
.\qt\build\RuntimeSettingsDialogSmoke.exe (Resolve-Path '.').Path
.\qt\build\RagProcessBridgeSmoke.exe (Resolve-Path '.').Path --settings-health
```

生成两种桌面尺寸的设置页截图：

```powershell
$root = (Resolve-Path '.').Path
Start-Process .\qt\build\RagDiagnosticWorkbench.exe `
  -ArgumentList @('--settings', '--viewport', '1280x720', '--screenshot', "$root\qt\build\runtime-settings-1280x720.png") `
  -NoNewWindow -Wait
Start-Process .\qt\build\RagDiagnosticWorkbench.exe `
  -ArgumentList @('--settings', '--viewport', '1920x1200', '--screenshot', "$root\qt\build\runtime-settings-1920x1200.png") `
  -NoNewWindow -Wait
```

生成 Workspace Shell 的 1280×720 和 1920×1200 截图。当前显示器无法原生容纳 1920×1200 时使用 Qt offscreen；`QT_QPA_FONTDIR` 用来保证离屏渲染仍能找到中文字体：

```powershell
$root = (Resolve-Path '.').Path
$env:Path = 'D:\Qt\6.8.3\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:Path

Start-Process .\qt\build\RagDiagnosticWorkbench.exe `
  -ArgumentList @('--viewport', '1280x720', '--screenshot', "$root\qt\build\workspace-shell-1280x720.png") `
  -NoNewWindow -Wait

$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = 'C:\Windows\Fonts'
Start-Process .\qt\build\RagDiagnosticWorkbench.exe `
  -ArgumentList @('--viewport', '1920x1200', '--screenshot', "$root\qt\build\workspace-shell-1920x1200.png") `
  -NoNewWindow -Wait
Remove-Item Env:QT_QPA_PLATFORM
Remove-Item Env:QT_QPA_FONTDIR
```

需要生成一张真实完成诊断后的 HardFault 工作台截图时：

```powershell
$env:HF_HUB_OFFLINE = '1'
$env:TRANSFORMERS_OFFLINE = '1'
.\qt\build\RagDiagnosticWorkbench.exe `
  --hardfault `
  --auto-hardfault `
  --viewport 1280x720 `
  --screenshot "$root\qt\build\workspace-shell-hardfault.png"
```

案例库支持 `--cases` 截图参数；离屏截图需要显式指定中文字体目录：

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = 'C:\Windows\Fonts'
.\qt\build\RagDiagnosticWorkbench.exe `
  --cases `
  --viewport 1280x720 `
  --screenshot "$root\qt\build\case-library-1280x720.png"
```

`--auto-query` 和 `--auto-hardfault` 只用于本地回归自动提交固定输入；与 `--cases --screenshot` 组合时，会等待结果保存后再截取案例库。

知识源管理支持 `--knowledge`。生成两个验收尺寸的截图：

```powershell
$root = (Resolve-Path '.').Path
$shot1280 = Join-Path $root 'qt\build\knowledge-center-1280x720.png'
$shot1920 = Join-Path $root 'qt\build\knowledge-center-1920x1200.png'
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = 'C:\Windows\Fonts'

$args1280 = "--knowledge --viewport 1280x720 --screenshot `"$shot1280`""
Start-Process .\qt\build\RagDiagnosticWorkbench.exe `
  -ArgumentList $args1280 `
  -WindowStyle Hidden -Wait
$args1920 = "--knowledge --viewport 1920x1200 --screenshot `"$shot1920`""
Start-Process .\qt\build\RagDiagnosticWorkbench.exe `
  -ArgumentList $args1920 `
  -WindowStyle Hidden -Wait

Remove-Item Env:QT_QPA_PLATFORM
Remove-Item Env:QT_QPA_FONTDIR
```

Python 真实缓存性能 smoke 见根目录 `tests/persistent_worker_smoke.py`。它连续执行两次查询，检查 PID 不变、第二次没有 `Loading weights`，并要求第二次耗时低于第一次的 80%。

需要直接打开 HardFault 页面做演示时，可以运行：

```powershell
$env:Path = 'D:\Qt\6.8.3\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:Path
.\qt\build\RagDiagnosticWorkbench.exe --hardfault
```

直接打开协议日志模式或自动提交脱敏 UDP 样例：

```powershell
.\qt\build\RagDiagnosticWorkbench.exe --protocol
.\qt\build\RagDiagnosticWorkbench.exe --protocol --auto-protocol
```

生成完成真实协议诊断后的 1280×720 与 1920×1200 截图。该命令复用本地 BGE 模型和 STM32F4 索引；离屏渲染通过字体目录保证中文可见：

```powershell
$root = (Resolve-Path '.').Path
$env:Path = 'D:\Qt\6.8.3\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:Path
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = 'C:\Windows\Fonts'
$env:HF_HUB_OFFLINE = '1'
$env:TRANSFORMERS_OFFLINE = '1'

$shot1280 = Join-Path $root 'qt\build\protocol-log-1280x720.png'
$args1280 = "--protocol --auto-protocol --viewport 1280x720 --screenshot `"$shot1280`""
Start-Process .\qt\build\RagDiagnosticWorkbench.exe `
  -ArgumentList $args1280 -WindowStyle Hidden -Wait

$shot1920 = Join-Path $root 'qt\build\protocol-log-1920x1200.png'
$args1920 = "--protocol --auto-protocol --viewport 1920x1200 --screenshot `"$shot1920`""
Start-Process .\qt\build\RagDiagnosticWorkbench.exe `
  -ArgumentList $args1920 -WindowStyle Hidden -Wait

Remove-Item Env:QT_QPA_PLATFORM
Remove-Item Env:QT_QPA_FONTDIR
```
