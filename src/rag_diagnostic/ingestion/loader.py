from __future__ import annotations

from pathlib import Path
from collections import Counter
from collections.abc import Iterable
import re

from rag_diagnostic.models import Document

try:
    from pypdf import PdfReader
except ImportError:  # pragma: no cover - dependency error is reported at runtime
    PdfReader = None


SUPPORTED_SUFFIXES = {".md", ".markdown", ".txt", ".pdf", ".h", ".hpp", ".c", ".cpp"}


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _clean_pdf_text(text: str, repeated_lines: set[str] | None = None) -> str:
    lines = [line.strip() for line in text.replace("\u00a0", " ").splitlines()]
    repeated_lines = repeated_lines or set()
    cleaned = [
        line
        for line in lines
        if line
        and line not in repeated_lines
        and not re.search(r"\b(?:Rev\s+\d+\s+)?\d+/\d+\b", line)
    ]
    return "\n".join(cleaned).strip()


def _load_pdf(path: Path, base_metadata: dict | None = None) -> list[Document]:
    if PdfReader is None:
        raise RuntimeError("读取 PDF 需要安装 pypdf")
    reader = PdfReader(str(path))
    raw_pages = [(page.extract_text() or "").strip() for page in reader.pages]
    line_counts = Counter()
    for text in raw_pages:
        lines = [line.strip() for line in text.splitlines() if line.strip()]
        line_counts.update(lines[:3] + lines[-3:])
    repeated_lines = {
        line for line, count in line_counts.items() if count >= 3
    }
    documents: list[Document] = []
    for page_no, raw_text in enumerate(raw_pages, start=1):
        text = _clean_pdf_text(raw_text, repeated_lines)
        if text:
            documents.append(
                Document(
                    source=path.name,
                    content=text,
                    metadata={**(base_metadata or {}), "page": page_no, "file_type": "pdf"},
                )
            )
    return documents


def load_documents(
    raw_dir: Path,
    source_root: Path | None = None,
    exclude_dirs: set[str] | None = None,
    allowed_paths: Iterable[Path] | None = None,
    metadata_lookup: dict[str, dict] | None = None,
) -> tuple[list[Document], list[dict[str, str]]]:
    """Load supported files and return documents plus per-file errors."""
    documents: list[Document] = []
    errors: list[dict[str, str]] = []
    exclude_dirs = exclude_dirs or set()
    paths = sorted(allowed_paths) if allowed_paths is not None else sorted(raw_dir.rglob("*"))
    metadata_lookup = metadata_lookup or {}
    for path in paths:
        if any(part in exclude_dirs for part in path.parts):
            continue
        if not path.is_file() or path.suffix.lower() not in SUPPORTED_SUFFIXES:
            continue
        source_name = path.name
        if source_root is not None:
            try:
                source_name = path.relative_to(source_root).as_posix()
            except ValueError:
                source_name = path.name
        try:
            if path.suffix.lower() == ".pdf":
                for document in _load_pdf(path, metadata_lookup.get(path.name)):
                    document.source = source_name
                    document.metadata["vault_path"] = source_name
                    documents.append(document)
            else:
                content = _read_text(path).strip()
                if content:
                    documents.append(
                        Document(
                            source=source_name,
                            content=content,
                            metadata={
                                **metadata_lookup.get(path.name, {}),
                                "file_type": path.suffix.lower().lstrip("."),
                                "vault_path": source_name,
                            },
                        )
                    )
                else:
                    errors.append({"source": source_name, "error": "empty document"})
        except Exception as exc:  # one bad file must not block other documents
            errors.append({"source": source_name, "error": str(exc)})
    return documents, errors
