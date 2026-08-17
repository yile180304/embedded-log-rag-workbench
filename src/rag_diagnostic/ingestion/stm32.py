from __future__ import annotations

import hashlib
import re
from pathlib import Path

from rag_diagnostic.models import Chunk, Document
from rag_diagnostic.profiles import STM32_SOURCE_MANIFEST, STM32SourceProfile, discover_stm32_sources
from rag_diagnostic.ingestion.chunker import _slug, _split_overlap
from rag_diagnostic.ingestion.loader import load_documents


def load_stm32_documents(profile: STM32SourceProfile):
    allowed, ignored = discover_stm32_sources(profile)
    metadata_lookup = {
        name: {"doc_id": doc_id, "document_title": title, "domain": "stm32f4"}
        for name, (doc_id, title) in STM32_SOURCE_MANIFEST.items()
    }
    documents, errors = load_documents(
        profile.document_roots[0],
        allowed_paths=allowed,
        metadata_lookup=metadata_lookup,
    )
    for document in documents:
        document.metadata["domain"] = "stm32f4"
        document.metadata.setdefault("section", f"page-{document.metadata['page']}" if "page" in document.metadata else "file")
    ignored_info = [{"source": path.name, "status": "ignored"} for path in ignored]
    return documents, errors, ignored_info


def _pdf_section(document: Document) -> str:
    lines = [line.strip() for line in document.content.splitlines() if line.strip()]
    for line in lines[:30]:
        if re.match(r"^\d+(?:\.\d+){1,4}\s+\S", line):
            return line[:160]
    for phrase in ("Fault Analyzer", "Ethernet DMA status register", "Configurable Fault Status Register"):
        if phrase.lower() in document.content.lower():
            matching = next((line for line in lines if phrase.lower() in line.lower()), phrase)
            return matching[:160]
    for line in lines:
        if re.search(r"\b(?:register|fault|exception|stack overflow)\b", line, re.IGNORECASE):
            return line[:160]
    return f"page-{document.metadata.get('page', 'unknown')}"


def _chunk_id(document: Document, section: str, index: int) -> str:
    source_key = hashlib.sha1(document.source.encode("utf-8")).hexdigest()[:10]
    page = document.metadata.get("page")
    page_label = f"p{int(page):04d}" if isinstance(page, int) else "file"
    return f"{_slug(Path(document.source).stem)}-{source_key}-{page_label}-{_slug(section)}-{index:03d}"


def _code_sections(document: Document, max_chars: int, overlap: int) -> list[tuple[str, str]]:
    sections: list[tuple[str, str]] = []
    structure_ranges: list[tuple[int, int]] = []
    for match in re.finditer(r"typedef\s+struct\s*\{.*?\}\s*([A-Za-z_]\w*)\s*;", document.content, re.DOTALL):
        name = match.group(1)
        sections.extend((name, text) for text in _split_overlap(match.group(0), max_chars, overlap))
        structure_ranges.append(match.span())

    remaining = document.content
    for start, end in reversed(structure_ranges):
        remaining = remaining[:start] + "\n" + remaining[end:]
    for index, text in enumerate(_split_overlap(remaining, max_chars, overlap), start=1):
        macro = re.search(r"(?m)^#define\s+([A-Za-z_]\w*)", text)
        function = re.search(r"\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{", text)
        if macro:
            parts = macro.group(1).split("_")
            section = "_".join(parts[:2]) + " definitions" if len(parts) >= 2 else "macro definitions"
        elif function:
            section = f"{function.group(1)} function"
        else:
            section = f"header-{index}"
        sections.append((section, text))
    return sections


def chunk_stm32_documents(documents: list[Document], max_chars: int = 1200, overlap: int = 180) -> list[Chunk]:
    chunks: list[Chunk] = []
    for document in documents:
        if document.metadata.get("file_type") == "pdf":
            section = _pdf_section(document)
            sections = [(section, text) for text in _split_overlap(document.content, max_chars, overlap)]
        elif document.metadata.get("file_type") in {"h", "hpp", "c", "cpp"}:
            sections = _code_sections(document, max_chars, overlap)
        else:
            section = str(document.metadata.get("section", "file"))
            sections = [(section, text) for text in _split_overlap(document.content, max_chars, overlap)]

        for index, (section, text) in enumerate(sections, start=1):
            metadata = {
                **document.metadata,
                "section": section,
                "chunk_index": index - 1,
                "char_count": len(text),
                "is_toc": bool(re.search(r"\.{5,}\s*\d+", text)),
            }
            chunks.append(
                Chunk(
                    chunk_id=_chunk_id(document, section, index),
                    text=text,
                    source=document.source,
                    metadata=metadata,
                )
            )
    return chunks
