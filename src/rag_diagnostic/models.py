from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass
class Document:
    source: str
    content: str
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass
class Chunk:
    chunk_id: str
    text: str
    source: str
    metadata: dict[str, Any] = field(default_factory=dict)

    @property
    def search_text(self) -> str:
        section = str(self.metadata.get("section", ""))
        return f"{self.source}\n{section}\n{self.text}".strip()


@dataclass
class Evidence:
    chunk_id: str
    text: str
    source: str
    metadata: dict[str, Any]
    rank: int
    scores: dict[str, float] = field(default_factory=dict)


@dataclass
class DiagnosisResult:
    query: str
    answer: str
    evidence: list[Evidence] = field(default_factory=list)
    grounded: bool = False
    refusal_reason: str | None = None
    metadata: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "query": self.query,
            "answer": self.answer,
            "grounded": self.grounded,
            "refusal_reason": self.refusal_reason,
            "evidence": [
                {
                    "chunk_id": item.chunk_id,
                    "text": item.text,
                    "source": item.source,
                    "metadata": item.metadata,
                    "rank": item.rank,
                    "scores": item.scores,
                }
                for item in self.evidence
            ],
            "metadata": self.metadata,
        }


@dataclass(frozen=True)
class DecodedFaultFlag:
    register: str
    bit: int
    name: str
    group: str
    meaning: str

    def to_dict(self) -> dict[str, Any]:
        return {
            "register": self.register,
            "bit": self.bit,
            "name": self.name,
            "group": self.group,
            "meaning": self.meaning,
        }


@dataclass
class HardFaultAnalysis:
    raw_log: str
    registers: dict[str, int] = field(default_factory=dict)
    decoded_flags: list[DecodedFaultFlag] = field(default_factory=list)
    observations: list[str] = field(default_factory=list)
    next_actions: list[str] = field(default_factory=list)
    generated_query: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {
            "raw_log": self.raw_log,
            "registers": {name: f"0x{value:08X}" for name, value in self.registers.items()},
            "decoded_flags": [flag.to_dict() for flag in self.decoded_flags],
            "observations": self.observations,
            "next_actions": self.next_actions,
            "generated_query": self.generated_query,
        }
