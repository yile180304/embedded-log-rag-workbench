from __future__ import annotations

import hashlib
import re
import unicodedata
from pathlib import Path

from rag_diagnostic.models import Chunk, Document


def _slug(value: str) -> str:
    value = unicodedata.normalize("NFKC", value).lower()
    value = re.sub(r"[^0-9a-zA-Z\u4e00-\u9fff]+", "-", value).strip("-")
    return value[:48] or "section"


def _sections(text: str) -> list[tuple[str, str]]:
    matches = list(re.finditer(r"(?m)^#{1,6}\s+(.+?)\s*$", text))
    if not matches:
        return [("general", text.strip())]
    sections: list[tuple[str, str]] = []
    prefix = text[: matches[0].start()].strip()
    if prefix:
        sections.append(("intro", prefix))
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        body = text[match.end() : end].strip()
        if body:
            sections.append((match.group(1).strip(), body))
    return sections


def _split_overlap(text: str, max_chars: int, overlap: int) -> list[str]:
    text = re.sub(r"\n{3,}", "\n\n", text).strip()
    if len(text) <= max_chars:
        return [text] if text else []
    paragraphs = [p.strip() for p in re.split(r"\n\s*\n", text) if p.strip()]
    result: list[str] = []
    current = ""
    for paragraph in paragraphs:
        if len(paragraph) > max_chars:
            if current:
                result.append(current)
                current = ""
            start = 0
            while start < len(paragraph):
                result.append(paragraph[start : start + max_chars].strip())
                start += max_chars - overlap
            continue
        candidate = f"{current}\n\n{paragraph}".strip() if current else paragraph
        if len(candidate) <= max_chars:
            current = candidate
        else:
            result.append(current)
            tail = current[-overlap:] if overlap else ""
            current = f"{tail}\n\n{paragraph}".strip()
    if current:
        result.append(current)
    return result


def chunk_documents(documents: list[Document], max_chars: int = 1200, overlap: int = 180) -> list[Chunk]:
    chunks: list[Chunk] = []
    for document in documents:
        section_index = 0
        source_key = hashlib.sha1(document.source.encode("utf-8")).hexdigest()[:10]
        source_label = _slug(Path(document.source).stem)
        for section, body in _sections(document.content):
            for part_index, text in enumerate(_split_overlap(body, max_chars, overlap)):
                section_index += 1
                chunk_id = f"{source_label}-{source_key}-{_slug(section)}-{section_index:03d}"
                metadata = {
                    **document.metadata,
                    "section": section,
                    "chunk_index": part_index,
                    "char_count": len(text),
                }
                chunks.append(
                    Chunk(
                        chunk_id=chunk_id,
                        text=text,
                        source=document.source,
                        metadata=metadata,
                    )
                )
    return chunks
