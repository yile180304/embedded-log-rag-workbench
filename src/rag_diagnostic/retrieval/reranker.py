from __future__ import annotations

from typing import Protocol

from rag_diagnostic.models import Evidence


class Reranker(Protocol):
    name: str

    def rerank(self, query: str, evidence: list[Evidence]) -> list[Evidence]: ...


class CrossEncoderReranker:
    name = "cross-encoder"

    def __init__(self, model_name: str = "BAAI/bge-reranker-base") -> None:
        try:
            from sentence_transformers import CrossEncoder
        except ImportError as exc:
            raise RuntimeError("sentence-transformers 未安装，reranker 将无法启用") from exc
        self.model = CrossEncoder(model_name)

    def rerank(self, query: str, evidence: list[Evidence]) -> list[Evidence]:
        if not evidence:
            return evidence
        scores = self.model.predict(
            [
                (
                    query,
                    f"{item.source}\n{item.metadata.get('section', '')}\n{item.text}",
                )
                for item in evidence
            ]
        )
        ordered = sorted(zip(evidence, scores), key=lambda pair: -float(pair[1]))
        result: list[Evidence] = []
        for rank, (item, score) in enumerate(ordered, start=1):
            item.rank = rank
            item.scores["reranker"] = float(score)
            result.append(item)
        return result


class PassthroughReranker:
    def __init__(self, reason: str) -> None:
        self.name = f"disabled:{reason}"

    def rerank(self, query: str, evidence: list[Evidence]) -> list[Evidence]:
        return evidence


def build_reranker(name: str, model_name: str | None = None) -> Reranker | None:
    if name == "none":
        return None
    if name == "cross-encoder":
        try:
            return CrossEncoderReranker(model_name or "BAAI/bge-reranker-base")
        except RuntimeError as exc:
            return PassthroughReranker(type(exc).__name__)
    raise ValueError(f"未知 reranker: {name}")
