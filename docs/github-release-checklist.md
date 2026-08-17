# GitHub 发布检查清单

这份清单用于 UI 优化和 Windows 打包完成后，第一次公开上传前的最终核对。

## 代码与数据

- [x] `git status --short` 中没有 `.venv312`、`storage`、`资料`、`reports`、`tmp`、`qt/build` 或 SQLite。
- [x] `git check-ignore -v` 能解释所有本地模型、PDF、Chroma 和运行产物为什么被忽略，同时不会误忽略 `qt/src/storage/` 源码。
- [x] 代码、README、示例和截图中没有 API Key、代理、账号、设备序列号或真实业务日志。
- [x] 仓库公开数据仅包含自编脱敏样例；未授权手册和协议资料只保留在本地资料目录。
- [x] 已加入标准 MIT `LICENSE`，README 中已同步许可证说明。

## 文档

- [x] README 的安装命令已在当前 Python 3.12 环境中执行并用于回归。
- [x] README 没有把回归样本写成业务准确率。
- [x] README 明确了 Python、模型下载、磁盘/内存和授权资料前置条件。
- [x] 用户指南包含 Python CLI、Qt 工作台、STM32F4 索引和典型 HardFault/协议日志路径。
- [x] 开发者指南包含 Worker Protocol、目录职责、测试和扩展边界。
- [x] UI 最终截图来自当前改版工作台，覆盖 HardFault、协议日志、知识源/案例库和评估中心。

## 验证

- [x] Python `unittest discover` 全部通过（61 项）。
- [x] Qt 全量构建通过。
- [x] 页面和 Workbench smoke 全部通过。
- [x] 真实 `RagProcessBridgeSmoke` 输出 `BRIDGE_SMOKE_OK`。
- [x] Windows `dist` 目录只包含主程序、运行库、启动检查和必要说明，不包含 Smoke 测试程序。
- [x] 在没有 Qt/MinGW PATH 的干净 PowerShell 中通过启动器运行并生成截图。
- [ ] 从干净目录按照 README 能完成首次启动检查。

## GitHub 操作

- [ ] 仓库名、描述、Topics 和默认分支已确认。
- [x] 首次暂存区只包含审查后的源代码、脱敏示例资料、文档、截图和测试。
- [x] 首次推送前已执行 `git diff --cached --stat`、禁止目录检查、大文件检查和敏感信息扫描。
- [ ] 上传后从 GitHub 网页重新打开 README，确认 Mermaid、代码块和链接正常。
- [ ] 发布 Windows 压缩包时附带版本号、构建日期、SHA-256 和运行前置条件。
