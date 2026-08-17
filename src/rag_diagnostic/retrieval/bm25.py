from __future__ import annotations

import math
import re
from collections import Counter

from rag_diagnostic.models import Chunk


ASCII_TOKEN_RE = re.compile(r"0x[0-9a-fA-F]+|[A-Za-z_][A-Za-z_0-9]*|\d+")
CJK_SEQUENCE_RE = re.compile(r"[\u4e00-\u9fff]+")


def tokenize(text: str) -> list[str]:
    """Tokenize mixed embedded terminology and Chinese without single-character noise."""
    tokens = [token.lower() for token in ASCII_TOKEN_RE.findall(text)]
    for sequence in CJK_SEQUENCE_RE.findall(text):
        if len(sequence) == 1:
            tokens.append(sequence)
        else:
            tokens.extend(sequence[index : index + 2] for index in range(len(sequence) - 1))
    return tokens


class BM25Index:
    def __init__(self, chunks: list[Chunk]) -> None:
        self.chunks = chunks
        self.tokens = [tokenize(chunk.search_text) for chunk in chunks]
        self.doc_freq: Counter[str] = Counter()
        for terms in self.tokens:
            self.doc_freq.update(set(terms))
        self.avgdl = sum(map(len, self.tokens)) / max(len(self.tokens), 1)
        self.k1 = 1.5
        self.b = 0.75

    def score(self, query: str, index: int) -> float:
        terms = tokenize(query)
        doc = self.tokens[index]
        counts = Counter(doc)
        total = 0.0
        n_docs = len(self.chunks)
        for term in terms:
            if not counts[term]:
                continue
            df = self.doc_freq[term]
            idf = math.log(1 + (n_docs - df + 0.5) / (df + 0.5))
            denom = counts[term] + self.k1 * (1 - self.b + self.b * len(doc) / max(self.avgdl, 1))
            total += idf * counts[term] * (self.k1 + 1) / denom
        return total

    def search(self, query: str, top_k: int = 5) -> list[tuple[int, float]]:
        scores = [(index, self.score(query, index)) for index in range(len(self.chunks))]
        scores.sort(key=lambda item: (-item[1], item[0]))
        return scores[:top_k]
