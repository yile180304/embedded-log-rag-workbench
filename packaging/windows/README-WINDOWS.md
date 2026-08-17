# Windows 运行说明

这个目录是 Qt 桌面端的轻量发布包，包含主程序和所需 Qt/MinGW 运行库。

它不包含 Python、Embedding/Reranker 模型、STM32F4 手册、Chroma 索引或真实日志。这样可以避免发布包体积失控，也避免重新分发未授权资料。

## 启动

1. 确认完整项目仓库已经准备好 `.venv312`，并完成 `pip install -e ".[ml]"`。
2. 如果本目录位于仓库的 `dist/RagDiagnosticWorkbench/`，双击 `Launch-RagWorkbench.cmd`。
3. 如果把本目录复制到了其他位置，在 PowerShell 中显式指定仓库根目录：

```powershell
.\Launch-RagWorkbench.ps1 -ProjectRoot "D:\path\to\RAG"
```

启动器会设置运行时目录覆盖，再启动 `RagDiagnosticWorkbench.exe`。不需要把 Qt 或 MinGW 添加到系统 `PATH`。

需要传递工作台参数时使用数组，例如：

```powershell
.\Launch-RagWorkbench.ps1 -Arguments @('--knowledge', '--viewport', '1280x720')
```

## 首次运行

- 首次使用模型需要联网下载，或提前准备 Hugging Face 缓存。
- 芯片手册和 CMSIS 资料需要自行放入 `storage/stm32f4/` 并建立索引。
- 打开后先确认 Runtime Center 中 Project、Python 和 Worker 状态。

## 常见问题

直接双击 `RagDiagnosticWorkbench.exe` 也能打开界面，但程序可能无法自动定位仓库后端。正式演示请使用启动器。
