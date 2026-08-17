from __future__ import annotations

import json
import os
import re
import urllib.error
import urllib.request
from typing import Any

from rag_diagnostic.models import DiagnosisResult, Evidence


STUDY_RERANKER_THRESHOLD = 0.05
STUDY_BM25_THRESHOLD = 8.0
STUDY_DENSE_THRESHOLD = 0.45
STM32_RERANKER_THRESHOLD = 0.05
STM32_BM25_THRESHOLD = 8.0
STM32_DENSE_THRESHOLD = 0.25


def _filter_relevant_evidence(evidence: list[Evidence], domain: str) -> list[Evidence]:
    if domain not in {"study", "stm32f4"} or not evidence:
        return evidence

    if domain == "stm32f4":
        evidence = [
            item
            for item in evidence
            if not item.metadata.get("is_toc")
            and not re.search(r"(?im)^\s*(?:contents|list of (?:figures|tables))\s*$", item.text)
            and not re.search(r"(?:\.\s*){5,}\d+\s*$", str(item.metadata.get("section", "")))
        ]

    has_reranker_scores = any("reranker" in item.scores for item in evidence)
    if has_reranker_scores:
        threshold = STUDY_RERANKER_THRESHOLD if domain == "study" else STM32_RERANKER_THRESHOLD
        relevant = [
            item for item in evidence
            if item.scores.get("reranker", float("-inf")) >= threshold
            or (
                domain == "stm32f4"
                and item.scores.get("bm25", 0.0) >= STM32_BM25_THRESHOLD * 2
                and item.metadata.get("page") not in {None, 1, 2, 3, 4, 5, 6, 7, 8}
            )
        ]
    else:
        bm25_threshold = STUDY_BM25_THRESHOLD if domain == "study" else STM32_BM25_THRESHOLD
        dense_threshold = STUDY_DENSE_THRESHOLD if domain == "study" else STM32_DENSE_THRESHOLD
        relevant = [
            item for item in evidence
            if item.scores.get("bm25", 0.0) >= bm25_threshold
            and item.scores.get("dense", 0.0) >= dense_threshold
        ]

    for rank, item in enumerate(relevant, start=1):
        item.rank = rank
    return relevant


def _refusal_answer(domain: str) -> str:
    if domain == "study":
        return "无法根据当前数学知识库确认。这个问题可能不属于已收录的数学资料，或现有证据相关度不足。"
    if domain == "stm32f4":
        return "无法根据当前 STM32F4 知识库确认。请补充故障寄存器、外设状态、日志或复现条件。"
    return "无法根据当前知识库确认，请补充更完整的日志、错误码或协议上下文。"


def _relevance_metadata(domain: str) -> dict[str, Any]:
    if domain not in {"study", "stm32f4"}:
        return {}
    if domain == "stm32f4":
        return {
            "relevance_policy": {
                "reranker_threshold": STM32_RERANKER_THRESHOLD,
                "fallback_bm25_threshold": STM32_BM25_THRESHOLD,
                "fallback_dense_threshold": STM32_DENSE_THRESHOLD,
            }
        }
    return {
        "relevance_policy": {
            "reranker_threshold": STUDY_RERANKER_THRESHOLD,
            "fallback_bm25_threshold": STUDY_BM25_THRESHOLD,
            "fallback_dense_threshold": STUDY_DENSE_THRESHOLD,
        }
    }


def grounded_prompt(query: str, evidence: list[Evidence], domain: str = "embedded") -> str:
    references = "\n\n".join(
        f"[{item.rank}] source={item.source} section={item.metadata.get('section', 'unknown')}\n{item.text}"
        for item in evidence
    )
    if domain == "study":
        instruction = (
            "你是数学复习助手。只能依据下列笔记证据回答；先直接解释问题，再总结判断信号、方法或易错点，"
            "并用证据编号标注来源。证据不足时明确说无法确认，不补写笔记中没有的公式。"
        )
    else:
        instruction = (
            "你是嵌入式日志诊断助手。只能依据下列证据回答；证据不足时明确说无法确认。"
            "请给出可能原因、下一步检查和证据编号，不要编造寄存器含义。"
        )
    return f"{instruction}\n\n问题：{query}\n\n证据：\n{references}"


def _call_openai_compatible(prompt: str) -> str:
    base_url = os.environ["RAG_LLM_BASE_URL"].rstrip("/")
    api_key = os.environ["RAG_LLM_API_KEY"]
    model = os.environ.get("RAG_LLM_MODEL", "qwen-plus")
    payload = {
        "model": model,
        "temperature": 0,
        "messages": [{"role": "user", "content": prompt}],
    }
    request = urllib.request.Request(
        f"{base_url}/chat/completions",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json", "Authorization": f"Bearer {api_key}"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=45) as response:
        result: dict[str, Any] = json.loads(response.read().decode("utf-8"))
    return result["choices"][0]["message"]["content"]


def _local_answer(evidence: list[Evidence], domain: str = "embedded") -> str:
    top = evidence[0]
    section = top.metadata.get("section", "相关章节")
    if domain == "study":
        excerpt = top.text.strip()
        if len(excerpt) > 360:
            excerpt = excerpt[:360].rstrip() + "……"
        other_sections = "、".join(
            f"《{item.metadata.get('section', '未标注章节')}》" for item in evidence[1:3]
        )
        followup = f"；还可对照 {other_sections}" if other_sections else ""
        return (
            f"优先阅读《{section}》（来源：{top.source}）{followup}。\n\n"
            f"最相关笔记摘要：{excerpt}"
        )
    return (
        f"根据知识库中排名第 1 的证据，建议先检查“{section}”相关条件。"
        f"重点核对来源 {top.source} 的原文，并结合日志中的错误码、地址和时间顺序做进一步确认。"
    )


def diagnose(
    query: str,
    evidence: list[Evidence],
    use_llm: bool = False,
    reranker_name: str = "disabled",
    domain: str = "embedded",
) -> DiagnosisResult:
    evidence = _filter_relevant_evidence(evidence, domain)
    if not evidence:
        return DiagnosisResult(
            query=query,
            answer=_refusal_answer(domain),
            grounded=False,
            refusal_reason="no_evidence",
            metadata={"reranker": reranker_name, **_relevance_metadata(domain)},
        )
    prompt = grounded_prompt(query, evidence, domain)
    answer = _local_answer(evidence, domain)
    generator = "local-template"
    if use_llm and os.getenv("RAG_LLM_BASE_URL") and os.getenv("RAG_LLM_API_KEY"):
        try:
            answer = _call_openai_compatible(prompt)
            generator = os.getenv("RAG_LLM_MODEL", "openai-compatible")
        except (urllib.error.URLError, KeyError, TimeoutError, json.JSONDecodeError) as exc:
            answer = _local_answer(evidence, domain)
            generator = f"local-template-fallback:{type(exc).__name__}"
    return DiagnosisResult(
        query=query,
        answer=answer,
        evidence=evidence,
        grounded=True,
        metadata={
            "generator": generator,
            "reranker": reranker_name,
            "prompt": prompt,
            **_relevance_metadata(domain),
        },
    )
