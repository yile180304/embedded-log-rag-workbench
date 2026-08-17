from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from rag_diagnostic.models import Chunk


class ChromaStore:
    def __init__(self, persist_dir: Path, collection_name: str = "embedded_diagnostics") -> None:
        try:
            import chromadb
        except ImportError as exc:
            raise RuntimeError("ChromaDB 未安装，请先执行 pip install -e .") from exc
        persist_dir.mkdir(parents=True, exist_ok=True)
        chroma_path = persist_dir
        try:
            chroma_path = persist_dir.resolve().relative_to(Path.cwd().resolve())
        except ValueError:
            pass
        self.client = chromadb.PersistentClient(path=str(chroma_path))
        self.collection_name = collection_name
        self.collection = self.client.get_or_create_collection(
            name=self.collection_name,
            metadata={"hnsw:space": "cosine"},
        )

    def reset(self) -> None:
        try:
            self.client.delete_collection(self.collection_name)
        except Exception:
            pass
        self.collection = self.client.get_or_create_collection(
            name=self.collection_name,
            metadata={"hnsw:space": "cosine"},
        )

    def delete(self) -> None:
        self.client.delete_collection(self.collection_name)

    def rename(self, collection_name: str) -> None:
        self.collection.modify(name=collection_name)
        self.collection_name = collection_name

    def upsert(self, chunks: list[Chunk], vectors: list[list[float]]) -> None:
        if not chunks:
            return
        batch_size = 5000
        for start in range(0, len(chunks), batch_size):
            batch_chunks = chunks[start : start + batch_size]
            batch_vectors = vectors[start : start + batch_size]
            self.collection.upsert(
                ids=[chunk.chunk_id for chunk in batch_chunks],
                documents=[chunk.text for chunk in batch_chunks],
                metadatas=[{"source": chunk.source, **chunk.metadata} for chunk in batch_chunks],
                embeddings=batch_vectors,
            )

    def query(self, vector: list[float], top_k: int) -> list[dict[str, Any]]:
        if self.collection.count() == 0:
            return []
        result = self.collection.query(
            query_embeddings=[vector],
            n_results=min(top_k, self.collection.count()),
            include=["documents", "metadatas", "distances"],
        )
        rows: list[dict[str, Any]] = []
        for index, chunk_id in enumerate(result["ids"][0]):
            distance = float(result["distances"][0][index])
            rows.append(
                {
                    "chunk_id": chunk_id,
                    "text": result["documents"][0][index],
                    "metadata": result["metadatas"][0][index],
                    "dense_score": 1.0 - distance,
                }
            )
        return rows

    def count(self) -> int:
        return self.collection.count()

    def close(self) -> None:
        self.client.close()


def save_chunks(path: Path, chunks: list[Chunk]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for chunk in chunks:
            handle.write(
                json.dumps(
                    {
                        "chunk_id": chunk.chunk_id,
                        "text": chunk.text,
                        "source": chunk.source,
                        "metadata": chunk.metadata,
                    },
                    ensure_ascii=False,
                )
                + "\n"
            )


def load_chunks(path: Path) -> list[Chunk]:
    if not path.exists():
        return []
    chunks: list[Chunk] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        item = json.loads(line)
        chunks.append(Chunk(**item))
    return chunks
