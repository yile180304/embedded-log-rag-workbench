from __future__ import annotations

import hashlib
import math
from typing import Protocol

from rag_diagnostic.retrieval.bm25 import tokenize


class EmbeddingProvider(Protocol):
    name: str

    def embed(self, texts: list[str]) -> list[list[float]]: ...


class HashEmbeddingProvider:
    """Offline deterministic embedding for the MVP; replaceable by BGE later."""

    name = "hash-embedding"

    def __init__(self, dimension: int = 384) -> None:
        self.dimension = dimension

    def _vector(self, text: str) -> list[float]:
        vector = [0.0] * self.dimension
        terms = tokenize(text)
        for term in terms:
            digest = hashlib.blake2b(term.encode("utf-8"), digest_size=8).digest()
            position = int.from_bytes(digest[:4], "big") % self.dimension
            sign = 1.0 if digest[4] % 2 else -1.0
            vector[position] += sign * (1.0 + min(len(term), 12) / 12.0)
        norm = math.sqrt(sum(value * value for value in vector)) or 1.0
        return [value / norm for value in vector]

    def embed(self, texts: list[str]) -> list[list[float]]:
        return [self._vector(text) for text in texts]


class SentenceTransformerEmbeddingProvider:
    def __init__(self, model_name: str) -> None:
        try:
            import sentence_transformers  # noqa: F401
            from sentence_transformers import SentenceTransformer
        except ModuleNotFoundError as exc:
            if exc.name == "sentence_transformers":
                raise RuntimeError("sentence-transformers 未安装，请在当前虚拟环境执行 pip install sentence-transformers") from exc
            raise RuntimeError(f"sentence-transformers 的依赖缺失：{exc}") from exc
        except Exception as exc:
            raise RuntimeError(
                "sentence-transformers 已安装，但导入失败；请检查 Python 与 NumPy/Torch 的二进制版本是否匹配。"
                f" 详细错误：{type(exc).__name__}: {exc}"
            ) from exc
        self.name = f"sentence-transformer:{model_name}"
        self.model = SentenceTransformer(model_name)

    def embed(self, texts: list[str]) -> list[list[float]]:
        vectors = self.model.encode(texts, normalize_embeddings=True, show_progress_bar=False)
        return vectors.tolist()


def build_embedding_provider(name: str = "hash", model_name: str | None = None) -> EmbeddingProvider:
    if name == "hash":
        return HashEmbeddingProvider()
    if name in {"bge", "sentence-transformer"}:
        return SentenceTransformerEmbeddingProvider(model_name or "BAAI/bge-small-zh-v1.5")
    raise ValueError(f"未知 embedding provider: {name}")
