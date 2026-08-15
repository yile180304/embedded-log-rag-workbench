# Contributing

感谢关注这个项目。它是一个本地、证据优先的嵌入式日志诊断 RAG 原型，贡献应优先保持可复现、可解释和不泄露资料。

## 开始前

- 使用 Python 3.12 和项目内 `.venv312`，不要混用系统 Python 或 `.venv`。
- 不要提交未授权芯片手册、协议规范、模型缓存、Chroma 索引、设备日志、SQLite 运行库或 API Key。
- 新的检索指标必须来自可追溯的人工标注集，并同时报告样本数。
- 规则事实和 RAG 证据必须分层保存，不能用生成回答替代寄存器/协议规则分析。

## 本地验证

```powershell
.\.venv312\Scripts\python.exe -m unittest discover -s tests -v
& 'D:\Qt\Tools\CMake_64\bin\cmake.exe' --build 'qt\build'
```

Qt smoke 和真实 Worker 回归命令见 [开发者指南](docs/dev/architecture-and-extension.md)。

## 提交改动

- 保持改动聚焦，避免把 UI、检索算法和发布脚本混在同一提交中。
- 为协议字段、评估指标、数据库迁移和 Worker 消息补测试。
- Markdown 示例使用脱敏数据，路径不要写成个人机器的绝对路径。
- Pull Request 应说明行为变化、测试命令和已知限制。
