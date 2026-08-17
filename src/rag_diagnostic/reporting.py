from __future__ import annotations

from datetime import datetime
import hashlib
from pathlib import Path

from rag_diagnostic.models import DiagnosisResult


def render_markdown(result: DiagnosisResult, title: str = "RAG 检索报告") -> str:
    lines = [
        f"# {title}",
        "",
        f"> 生成时间：{datetime.now().astimezone().isoformat(timespec='seconds')}",
        "",
        "## 问题",
        "",
        result.query,
        "",
        "## 检索结论",
        "",
        result.answer,
        "",
        f"- 是否有证据支撑：`{'是' if result.grounded else '否'}`",
        f"- Embedding：`{result.metadata.get('embedding', 'unknown')}`",
        f"- Reranker：`{result.metadata.get('reranker', 'unknown')}`",
        "",
        "## 证据卡片",
        "",
    ]
    if not result.evidence:
        lines.extend(["当前知识库没有召回可引用证据。", ""])
    else:
        for item in result.evidence:
            vault_path = str(item.metadata.get("vault_path", item.source))
            obsidian_target = vault_path[:-3] if vault_path.lower().endswith(".md") else vault_path
            lines.extend([
                f"### {item.rank}. {item.metadata.get('section', '未标注章节')}",
                "",
                f"- 来源：`{item.source}`",
                f"- 文档 ID：`{item.metadata.get('doc_id', 'unknown')}`",
                f"- 页码：`{item.metadata.get('page', '不适用')}`",
                f"- Chunk ID：`{item.chunk_id}`",
                f"- 分数：`{item.scores}`",
            ])
            if item.source.lower().endswith((".md", ".markdown")):
                lines.extend([f"- Vault 路径：`{vault_path}`", f"- Obsidian 链接：[[{obsidian_target}]]"])
            lines.extend(["", "> " + item.text.replace("\n", "\n> "), ""])
    return "\n".join(lines)


def render_hardfault_markdown(result: DiagnosisResult) -> str:
    hardfault = result.metadata.get("hardfault", {})
    registers = hardfault.get("registers", {})
    flags = hardfault.get("decoded_flags", [])
    observations = hardfault.get("observations", [])
    next_actions = hardfault.get("next_actions", [])
    lines = [
        "# STM32F4 HardFault 诊断工单",
        "",
        f"> 生成时间：{datetime.now().astimezone().isoformat(timespec='seconds')}",
        "",
        "## 原始 HardFault 日志",
        "",
        "```text",
        str(hardfault.get("raw_log", "")),
        "```",
        "",
        "## Fault Register Snapshot",
        "",
    ]
    if registers:
        lines.extend(f"- `{name}` = `{value}`" for name, value in registers.items())
    else:
        lines.append("没有识别到寄存器快照。")
    lines.extend(["", "## Decoded Fault Flags", ""])
    if flags:
        lines.extend(
            f"- `{flag.get('register')}.{flag.get('name')}`（bit {flag.get('bit')}，{flag.get('group')}）：{flag.get('meaning')}"
            for flag in flags
        )
    else:
        lines.append("CFSR/HFSR 中没有识别到已定义的置位标志。")
    lines.extend(["", "## 确定性观察", ""])
    lines.extend(f"- {item}" for item in observations)
    lines.extend(["", "## 下一步检查", ""])
    lines.extend(f"{index}. {item}" for index, item in enumerate(next_actions, start=1))
    lines.extend(["", "## 自动检索问题", "", result.query, "", "## RAG 检索结果", ""])
    evidence_report = render_markdown(result, title="STM32F4 手册证据").split("## 检索结论", maxsplit=1)
    if len(evidence_report) == 2:
        lines.append("## 检索结论" + evidence_report[1])
    return "\n".join(lines)


def render_protocol_markdown(result: DiagnosisResult) -> str:
    analysis = result.metadata.get("protocol_log", {})
    summary = analysis.get("summary", {})
    config = analysis.get("config", {})
    events = analysis.get("events", [])
    anomalies = analysis.get("anomalies", [])
    lines = [
        "# 嵌入式协议日志诊断工单",
        "",
        f"> 生成时间：{datetime.now().astimezone().isoformat(timespec='seconds')}",
        "",
        "## 解析配置与摘要",
        "",
        f"- Profile：`{analysis.get('profile', 'unknown')}`",
        f"- 输入行数：`{summary.get('line_count', 0)}`",
        f"- 事件数：`{summary.get('event_count', 0)}`",
        f"- Warning / Error：`{summary.get('warning_count', 0)} / {summary.get('error_count', 0)}`",
        f"- 期望周期 / 容差：`{config.get('expected_cycle_ms')} / {config.get('jitter_tolerance_ms')} ms`",
        f"- 期望长度：`{config.get('expected_length')}`",
        f"- CRC16：`{config.get('crc16_algorithm', 'none')}`",
        "",
        "## Protocol Log Events",
        "",
        "| Line | Time | Protocol | Source | Destination | Seq | Length | Interval ms | CRC |",
        "|---:|---|---|---|---|---:|---:|---:|---|",
    ]
    if not events:
        lines.append("| - | - | - | - | - | - | - | - | - |")
    else:
        for event in events:
            crc_state = "未校验"
            if event.get("crc_valid") is True:
                crc_state = f"通过 {event.get('calculated_crc', '')}".strip()
            elif event.get("crc_valid") is False:
                crc_state = f"失败 {event.get('calculated_crc', '')}".strip()
            values = [
                event.get("line_no", ""), event.get("timestamp") or "", event.get("protocol") or "",
                event.get("source") or "", event.get("destination") or "", event.get("sequence"),
                event.get("length"), event.get("interval_ms"), crc_state,
            ]
            lines.append("| " + " | ".join(str(value if value is not None else "") for value in values) + " |")
    lines.extend(["", "## Protocol Anomalies", ""])
    if not anomalies:
        lines.append("没有检测到基于当前显式字段和阈值的异常。")
    else:
        lines.extend(
            f"- **{item.get('severity', 'warning')} · `{item.get('type', 'unknown')}` · line {item.get('line_no', '-')}**：{item.get('message', '')}"
            for item in anomalies
        )
    lines.extend([
        "",
        "## 原始日志行",
        "",
        "```text",
        *[str(event.get("raw_message", "")) for event in events],
        "```",
        "",
        "## 自动检索问题",
        "",
        result.query,
        "",
        "## RAG 检索结果",
        "",
    ])
    evidence_report = render_markdown(result, title="协议诊断知识库证据").split("## 检索结论", maxsplit=1)
    if len(evidence_report) == 2:
        lines.append("## 检索结论" + evidence_report[1])
    return "\n".join(lines)


def write_report(result: DiagnosisResult, output_dir: Path, slug: str, title: str = "RAG 检索报告") -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    safe_slug = "".join(char if char.isalnum() or char in "-_" else "_" for char in slug).strip("_") or "query"
    if safe_slug == "query" and slug != "query":
        safe_slug = f"query-{hashlib.sha1(slug.encode('utf-8')).hexdigest()[:8]}"
    path = output_dir / f"{safe_slug}.md"
    path.write_text(render_markdown(result, title), encoding="utf-8")
    return path


def write_hardfault_report(result: DiagnosisResult, output_dir: Path, slug: str) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    safe_slug = "".join(char if char.isalnum() or char in "-_" else "_" for char in slug).strip("_") or "HardFault"
    path = output_dir / f"{safe_slug}.md"
    path.write_text(render_hardfault_markdown(result), encoding="utf-8")
    return path


def write_protocol_report(result: DiagnosisResult, output_dir: Path, slug: str) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    safe_slug = "".join(char if char.isalnum() or char in "-_" else "_" for char in slug).strip("_") or "ProtocolLog"
    path = output_dir / f"{safe_slug}.md"
    path.write_text(render_protocol_markdown(result), encoding="utf-8")
    return path
