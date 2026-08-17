---
doc_type: dev-guide
slug: windows-packaging
component: rag-desktop-product
status: current
summary: 说明如何生成、验证和发布不捆绑 Python 模型与芯片资料的 Windows 轻量包。
tags: [Windows, Qt6, packaging, release]
last_reviewed: 2026-08-17
---

# Windows 打包与发布

## 概述

Windows 发布目录只打包 Qt 桌面端和必要运行库。Python 后端、模型缓存、芯片手册、Chroma 索引和运行数据继续保留在本地项目仓库中。

这种方式适合项目演示和源码仓库配套发布，避免把数 GB 的机器学习环境和未授权资料塞进压缩包。它不是面向普通终端用户的一键安装器。

## 前置依赖

- 已完成的 `qt/build-release/RagDiagnosticWorkbench.exe`。
- Qt 6.8.3 MinGW 运行库和 `windeployqt.exe`。
- 项目内可用的 `.venv312`。
- 已按需准备的模型缓存、资料和索引。

## 快速上手

在项目根目录执行：

```powershell
& 'D:\Qt\Tools\CMake_64\bin\cmake.exe' `
  -S qt -B qt\build-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH='D:\Qt\6.8.3\mingw_64'

& 'D:\Qt\Tools\CMake_64\bin\cmake.exe' --build qt\build-release
.\tools\package-windows.ps1 -CreateArchive
```

默认输出：

```text
dist/RagDiagnosticWorkbench/
├── RagDiagnosticWorkbench.exe
├── Launch-RagWorkbench.cmd
├── Launch-RagWorkbench.ps1
├── README-WINDOWS.md
├── BUILD-INFO.txt
├── Qt6*.dll
├── libgcc_s_seh-1.dll
├── libstdc++-6.dll
├── libwinpthread-1.dll
└── platforms/qwindows.dll
```

启用 `-CreateArchive` 时还会生成 `dist/RagDiagnosticWorkbench-v0.1.0-windows-x64.zip`，供 GitHub Release 上传，并在终端输出压缩包 SHA-256。

## 核心概念

启动器从自身目录向上查找 `pyproject.toml`，然后设置：

```text
RAG_DIAGNOSTIC_PROJECT_ROOT
RAG_DIAGNOSTIC_PYTHON_EXECUTABLE
```

Qt 读取这两个变量时会覆盖旧的本机路径设置，确保从 `dist` 启动时仍连接到当前仓库的 `.venv312`。

## 常见场景

仓库内启动：

```powershell
.\dist\RagDiagnosticWorkbench\Launch-RagWorkbench.ps1
```

发布目录位于仓库外：

```powershell
.\Launch-RagWorkbench.ps1 -ProjectRoot "D:\path\to\RAG"
```

指定不同 Qt 或 MinGW：

```powershell
.\tools\package-windows.ps1 `
  -QtRoot "D:\Qt\6.8.3\mingw_64" `
  -MingwBin "D:\Qt\Tools\mingw1310_64\bin"
```

## 验证发布包

在一个没有添加 Qt/MinGW `PATH` 的新 PowerShell 中运行：

```powershell
$env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
.\dist\RagDiagnosticWorkbench\Launch-RagWorkbench.ps1 `
  -Arguments @('--viewport', '1280x720')
```

至少确认主界面可打开、Worker 进入 Ready、`dist` 中没有 `*Smoke.exe`，并核对 `BUILD-INFO.txt` 中的 SHA-256。

## 已知限制与注意事项

- 不捆绑 Python 解释器和 `site-packages`。
- 不捆绑 Hugging Face 模型缓存。
- 不捆绑 STM32F4 PDF、CMSIS、Chroma 和 SQLite。
- `dist/` 是构建产物，默认不提交 Git；发布时作为 GitHub Release 附件单独压缩上传。
- 对外发布前必须先确认许可证。

## 相关文档

- [项目 README](../../README.md)
- [用户快速上手](../user/getting-started.md)
- [GitHub 发布检查清单](../github-release-checklist.md)
