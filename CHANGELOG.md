# Changelog

本项目遵循语义化版本号。当前仍处于工程原型阶段，接口可能在后续版本调整。

## [0.1.0] - 2026-08-17

### Added

- 面向 STM32F4 手册、CMSIS 头文件和脱敏技术资料的结构化切片与 Chroma 索引。
- BM25 + BGE + RRF 混合检索与 Cross-Encoder 重排序。
- 专家问答、HardFault 规则诊断和离线协议日志分析。
- Qt 6/C++17 桌面工作台、Runtime Center、案例库、知识源管理和评估中心。
- 长驻 Python Worker 与 Local Worker Protocol v1。
- SQLite 案例、评估候选、人工批准样本和历史运行记录。
- source-assisted Windows 轻量发布脚本与启动器。

### Validation

- Python 61 项回归测试通过。
- Qt 全量构建、页面/Store/Workbench smoke 通过。
- 真实 BGE、Reranker 与 STM32F4 索引 Worker 回归输出 `BRIDGE_SMOKE_OK`。
- Windows 发布目录在不包含 Qt/MinGW PATH 的 PowerShell 中启动验证通过。

### Known limitations

- 不读取 PCAP、ELF/MAP，也不连接实时网卡、串口或调试器。
- 不捆绑 Python、模型、芯片手册和索引。
- 当前索引采用全量重建，任务超时尚未实现强杀和自动恢复。
- 当前评估集规模只适用于代码回归，不代表生产环境准确率。
