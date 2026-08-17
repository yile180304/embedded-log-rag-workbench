from __future__ import annotations

from rag_diagnostic.models import Chunk, Evidence
from rag_diagnostic.retrieval.bm25 import BM25Index
from rag_diagnostic.retrieval.embeddings import EmbeddingProvider
from rag_diagnostic.retrieval.reranker import Reranker
from rag_diagnostic.retrieval.store import ChromaStore


class HybridRetriever:
    def __init__(
        self,
        chunks: list[Chunk],
        store: ChromaStore,
        embeddings: EmbeddingProvider,
        reranker: Reranker | None = None,
    ) -> None:
        self.chunks = chunks
        self.by_id = {chunk.chunk_id: chunk for chunk in chunks}
        self.bm25 = BM25Index(chunks)
        self.store = store
        self.embeddings = embeddings
        self.reranker = reranker
        self.reranker_name = reranker.name if reranker else "disabled"

    def search(self, query: str, top_k: int = 5) -> list[Evidence]:
        candidate_k = max(top_k * 8, 20)
        sparse = [(index, score) for index, score in self.bm25.search(query, candidate_k) if score > 0]
        dense = [item for item in self.store.query(self.embeddings.embed([query])[0], candidate_k) if item["dense_score"] >= 0.12]
        merged: dict[str, dict[str, float]] = {}
        max_bm25 = max((score for _, score in sparse), default=1.0)
        max_dense = max((item["dense_score"] for item in dense), default=1.0)
        for rank, (index, score) in enumerate(sparse, start=1):
            chunk = self.chunks[index]
            merged.setdefault(chunk.chunk_id, {})["bm25"] = score
            merged[chunk.chunk_id]["rrf"] = merged[chunk.chunk_id].get("rrf", 0.0) + 1 / (60 + rank)
        for rank, item in enumerate(dense, start=1):
            merged.setdefault(item["chunk_id"], {})["dense"] = item["dense_score"]
            merged[item["chunk_id"]]["rrf"] = merged[item["chunk_id"]].get("rrf", 0.0) + 1 / (60 + rank)
        for scores in merged.values():
            bm25_normalized = scores.get("bm25", 0.0) / max_bm25
            dense_normalized = max(scores.get("dense", 0.0), 0.0) / max_dense
            rrf_normalized = min(scores.get("rrf", 0.0) * 30.5, 1.0)
            scores["hybrid"] = 0.6 * bm25_normalized + 0.3 * dense_normalized + 0.1 * rrf_normalized
        ordered = sorted(merged.items(), key=lambda pair: (-pair[1]["hybrid"], -pair[1]["rrf"], pair[0]))[:candidate_k]
        evidence: list[Evidence] = []
        for rank, (chunk_id, scores) in enumerate(ordered, start=1):
            chunk = self.by_id[chunk_id]
            evidence.append(
                Evidence(
                    chunk_id=chunk.chunk_id,
                    text=chunk.text,
                    source=chunk.source,
                    metadata=chunk.metadata,
                    rank=rank,
                    scores=scores,
                )
            )
        if self.reranker:
            lexical_slots = max(1, top_k // 4)
            lexical = sorted(evidence, key=lambda item: -item.scores.get("bm25", 0.0))[:lexical_slots]
            reranked = self.reranker.rerank(query, evidence)
            selected = reranked[: max(0, top_k - lexical_slots)]
            selected_ids = {item.chunk_id for item in selected}
            selected.extend(item for item in lexical if item.chunk_id not in selected_ids)
            evidence = selected[:top_k]
        else:
            evidence = evidence[:top_k]
        for rank, item in enumerate(evidence, start=1):
            item.rank = rank
        return evidence
