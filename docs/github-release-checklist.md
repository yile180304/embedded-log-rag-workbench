# GitHub 发布检查清单

这份清单用于 UI 优化和 Windows 打包完成后，第一次公开上传前的最终核对。

## 代码与数据

- [ ] `git status --short` 中没有 `.venv312`、`storage`、`资料`、`reports`、`tmp`、`qt/build` 或 SQLite。
- [ ] `git check-ignore -v` 能解释所有本地模型、PDF、Chroma 和运行产物为什么被忽略。
- [ ] 代码、README、示例和截图中没有 API Key、代理、账号、内网地址、设备序列号或真实业务日志。
- [ ] 公开资料已确认允许再分发；未授权手册只保留在本地资料目录。
- [ ] 已决定并加入 `LICENSE`。当前仓库尚未选择许可证，不能默认按 MIT 使用。

## 文档

- [ ] README 的安装命令在干净 Python 3.12 环境中可执行。
- [ ] README 没有把 5 条回归样本写成业务准确率。
- [ ] README 明确 CPU/内存、模型下载和授权资料前置条件。
- [ ] 用户指南包含 Python CLI、Qt 工作台、STM32F4 索引和典型 HardFault/协议日志路径。
- [ ] 开发者指南包含 Worker Protocol、目录职责、测试和扩展边界。
- [ ] UI 最终截图来自确认后的版本，不能继续引用旧界面截图。

## 验证

- [ ] Python `unittest discover` 全部通过。
- [ ] Qt 全量构建通过。
- [ ] 页面和 Workbench smoke 全部通过。
- [ ] 真实 `RagProcessBridgeSmoke` 输出 `BRIDGE_SMOKE_OK`。
- [ ] Windows `dist` 目录只包含主程序、运行库、启动检查和必要说明，不包含 Smoke 测试程序。
- [ ] 在没有 Qt/MinGW PATH 的干净 PowerShell 中双击或启动器运行成功。
- [ ] 从干净目录按照 README 能完成首次启动检查。

## GitHub 操作

- [ ] 仓库名、描述、Topics 和默认分支已确认。
- [ ] 首次提交只包含审查后的源代码、示例资料、文档和测试。
- [ ] 首次推送前执行一次 `git diff --cached --stat` 和敏感信息扫描。
- [ ] 上传后从 GitHub 网页重新打开 README，确认 Mermaid、代码块和链接正常。
- [ ] 发布 Windows 压缩包时附带版本号、构建日期、SHA-256 和运行前置条件。
