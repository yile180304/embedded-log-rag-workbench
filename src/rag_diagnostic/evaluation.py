from __future__ import annotations

import json
from pathlib import Path
import time
from typing import Any, Callable

from rag_diagnostic.models import Evidence
from rag_diagnostic.retrieval.hybrid import HybridRetriever


ProgressCallback = Callable[[int, int, str], None]


def load_evaluation_samples(path: Path) -> list[dict[str, Any]]:
    rows = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(rows, list):
        raise ValueError("evaluation JSON must be an array")
    samples: list[dict[str, Any]] = []
    for index, row in enumerate(rows, start=1):
        if not isinstance(row, dict):
            raise ValueError(f"evaluation row {index} must be an object")
        if "expected_evidence" in row:
            samples.append(row)
            continue
        expected = [
            {"chunk_id": chunk_id}
            for chunk_id in row.get("relevant_chunk_ids", [])
            if isinstance(chunk_id, str) and chunk_id.strip()
        ]
        expected.extend(
            {"source": source}
            for source in row.get("relevant_sources", [])
            if isinstance(source, str) and source.strip()
        )
        samples.append(
            {
                "sample_id": str(row.get("sample_id") or f"legacy-{index}"),
                "query": row.get("query", ""),
                "expected_evidence": expected,
            }
        )
    return samples


def evaluate_samples(
    samples: list[dict[str, Any]],
    retriever: HybridRetriever,
    top_k: int = 3,
    progress_callback: ProgressCallback | None = None,
) -> dict[str, Any]:
    started = time.perf_counter()
    details: list[dict[str, Any]] = []
    hits = 0
    recall_values: list[float] = []
    reciprocal_ranks: list[float] = []
    total = len(samples)

    for completed, sample in enumerate(samples, start=1):
        query = str(sample["query"]).strip()
        expected = _deduplicate_expected(sample["expected_evidence"])
        retrieved = retriever.search(query, top_k)
        matched_expected: set[int] = set()
        hit_ranks: list[int] = []
        for rank, evidence in enumerate(retrieved, start=1):
            matched_here = {
                index
                for index, reference in enumerate(expected)
                if _matches(evidence, reference)
            }
            if matched_here:
                hit_ranks.append(rank)
                matched_expected.update(matched_here)

        hit = bool(hit_ranks)
        recall = len(matched_expected) / len(expected)
        reciprocal_rank = 1 / hit_ranks[0] if hit_ranks else 0.0
        hits += int(hit)
        recall_values.append(recall)
        reciprocal_ranks.append(reciprocal_rank)
        detail = {
            "sample_id": str(sample.get("sample_id", "")),
            "query": query,
            "expected_evidence": expected,
            "retrieved_evidence": [_evidence_snapshot(item, rank) for rank, item in enumerate(retrieved, start=1)],
            "hit": hit,
            "hit_ranks": hit_ranks,
            "first_relevant_rank": hit_ranks[0] if hit_ranks else None,
            "matched_expected_count": len(matched_expected),
            "expected_count": len(expected),
            "recall": recall,
            "reciprocal_rank": reciprocal_rank,
        }
        details.append(detail)
        if progress_callback is not None:
            progress_callback(completed, total, query)

    denominator = total or 1
    return {
        "sample_count": total,
        "queries": total,
        "top_k": top_k,
        f"hit_rate@{top_k}": hits / denominator,
        f"recall@{top_k}": sum(recall_values) / denominator,
        "mrr": sum(reciprocal_ranks) / denominator,
        "duration_ms": round((time.perf_counter() - started) * 1000, 3),
        "details": details,
        "failed_cases": [detail for detail in details if not detail["hit"]],
    }


def evaluate(path: Path, retriever: HybridRetriever, top_k: int = 3) -> dict[str, Any]:
    return evaluate_samples(load_evaluation_samples(path), retriever, top_k)


def _deduplicate_expected(references: list[dict[str, Any]]) -> list[dict[str, Any]]:
    unique: list[dict[str, Any]] = []
    seen: set[tuple[str, ...]] = set()
    for reference in references:
        normalized = {
            field: value.strip() if isinstance(value, str) else value
            for field, value in reference.items()
            if field in {"source", "doc_id", "page", "section", "chunk_id"}
            and value not in (None, "")
        }
        key = _reference_key(normalized)
        if key not in seen:
            seen.add(key)
            unique.append(normalized)
    if not unique:
        raise ValueError("expected_evidence must contain at least one traceable reference")
    return unique


def _reference_key(reference: dict[str, Any]) -> tuple[str, ...]:
    chunk_id = _text(reference.get("chunk_id"))
    if chunk_id:
        return ("chunk_id", chunk_id)
    doc_id = _text(reference.get("doc_id"))
    page = _text(reference.get("page"))
    if doc_id and page:
        return ("doc_page", doc_id, page)
    source = _text(reference.get("source"))
    section = _text(reference.get("section"))
    if source and section:
        return ("source_section", source, section)
    if source:
        return ("source", source)
    raise ValueError("expected evidence requires chunk_id, doc_id + page, source + section, or source")


def _matches(evidence: Evidence, reference: dict[str, Any]) -> bool:
    key = _reference_key(reference)
    if key[0] == "chunk_id":
        return evidence.chunk_id == key[1]
    if key[0] == "doc_page":
        return _text(evidence.metadata.get("doc_id")) == key[1] and _text(evidence.metadata.get("page")) == key[2]
    if key[0] == "source_section":
        return evidence.source == key[1] and _text(evidence.metadata.get("section")) == key[2]
    return evidence.source == key[1]


def _evidence_snapshot(evidence: Evidence, rank: int) -> dict[str, Any]:
    return {
        "rank": rank,
        "chunk_id": evidence.chunk_id,
        "source": evidence.source,
        "doc_id": evidence.metadata.get("doc_id"),
        "page": evidence.metadata.get("page"),
        "section": evidence.metadata.get("section"),
        "scores": evidence.scores,
    }


def _text(value: Any) -> str:
    return str(value).strip() if value is not None else ""
