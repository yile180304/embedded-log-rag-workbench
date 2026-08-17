from __future__ import annotations

from dataclasses import dataclass
from collections import Counter
import hashlib
import os
from pathlib import Path
import time
from typing import Any

from rag_diagnostic.diagnosis import diagnose
from rag_diagnostic.hardfault import analyze_hardfault_log
from rag_diagnostic.ingestion import chunk_stm32_documents, load_stm32_documents
from rag_diagnostic.models import DiagnosisResult
from rag_diagnostic.protocol import analyze_protocol_log
from rag_diagnostic.profiles import STM32_SOURCE_MANIFEST, discover_stm32_sources, resolve_stm32_profile
from rag_diagnostic.retrieval import ChromaStore, HybridRetriever, build_embedding_provider
from rag_diagnostic.retrieval.reranker import build_reranker
from rag_diagnostic.retrieval.store import load_chunks, save_chunks
from rag_diagnostic.reporting import write_hardfault_report, write_protocol_report, write_report


@dataclass(frozen=True)
class STM32RuntimeOptions:
    root: Path
    embedding: str = "hash"
    embedding_model: str | None = None
    reranker: str | None = None
    reranker_model: str | None = None


class STM32DiagnosticService:
    def __init__(self, options: STM32RuntimeOptions) -> None:
        self.options = options
        self.profile = resolve_stm32_profile(options.root)
        self._retriever: HybridRetriever | None = None

    @property
    def cache_loaded(self) -> bool:
        return self._retriever is not None

    def invalidate_cache(self) -> None:
        cached = self._retriever
        self._retriever = None
        if cached is not None:
            cached.store.close()

    def source_snapshot(self, errors: list[dict[str, str]] | None = None) -> list[dict[str, Any]]:
        allowed, ignored = discover_stm32_sources(self.profile)
        allowed_by_name = {path.name: path for path in allowed}
        error_by_name = {
            Path(item.get("source", "")).name: item.get("error", "unknown source error")
            for item in (errors or [])
            if item.get("source")
        }
        chunks = load_chunks(self.profile.chunks_path)
        chunk_counts = Counter(Path(chunk.source).name for chunk in chunks)
        index_mtime = self.profile.chunks_path.stat().st_mtime_ns if self.profile.chunks_path.is_file() else None
        records: list[dict[str, Any]] = []
        for filename, (source_id, display_name) in STM32_SOURCE_MANIFEST.items():
            source_path = allowed_by_name.get(filename)
            exists = source_path is not None
            display_path = source_path or (self.profile.document_roots[0] / filename)
            chunks_for_source = chunk_counts.get(filename, 0)
            source_error = error_by_name.get(filename)
            if source_error:
                index_status = "error"
            elif not exists or chunks_for_source == 0:
                index_status = "not_indexed"
            elif index_mtime is not None and source_path.stat().st_mtime_ns > index_mtime:
                index_status = "stale"
            else:
                index_status = "indexed"
            records.append(
                {
                    "source_id": source_id,
                    "display_name": display_name,
                    "path": str(display_path.resolve()),
                    "source_type": Path(filename).suffix.lower().lstrip("."),
                    "manifest_status": "allowed" if exists else "missing",
                    "index_status": index_status,
                    "chunks": chunks_for_source,
                    "error": source_error,
                }
            )
        for path in ignored:
            records.append(
                {
                    "source_id": "",
                    "display_name": path.name,
                    "path": str(path.resolve()),
                    "source_type": path.suffix.lower().lstrip("."),
                    "manifest_status": "ignored",
                    "index_status": "not_indexed",
                    "chunks": 0,
                    "error": None,
                }
            )
        return records

    def build_retriever(self) -> HybridRetriever:
        if self._retriever is not None:
            return self._retriever
        chunks = load_chunks(self.profile.chunks_path)
        if not chunks:
            raise RuntimeError("没有找到 chunks，请先运行 stm32-ingest")
        embeddings = build_embedding_provider(self.options.embedding, self.options.embedding_model)
        store = ChromaStore(self.profile.storage_root / "chroma", self.profile.collection_name)
        reranker_name = self.options.reranker or "cross-encoder"
        reranker = build_reranker(reranker_name, self.options.reranker_model)
        self._retriever = HybridRetriever(chunks, store, embeddings, reranker)
        return self._retriever

    def ingest(self, max_chars: int = 1200, overlap: int = 180) -> tuple[int, dict[str, Any]]:
        documents, errors, ignored = load_stm32_documents(self.profile)
        chunks = chunk_stm32_documents(documents, max_chars=max_chars, overlap=overlap)
        if not chunks:
            return 2, {
                "status": "error",
                "profile": "stm32f4",
                "documents": len(documents),
                "errors": errors,
                "ignored": ignored,
                "sources": self.source_snapshot(errors),
            }

        embeddings = build_embedding_provider(self.options.embedding, self.options.embedding_model)
        vectors = embeddings.embed([chunk.search_text for chunk in chunks])
        target_dir = self.profile.storage_root / "chroma"
        self.profile.storage_root.mkdir(parents=True, exist_ok=True)
        chroma_path = Path(os.path.relpath(target_dir, Path.cwd()))
        build_collection = f"{self.profile.collection_name}_build"
        build_store = ChromaStore(chroma_path, build_collection)
        build_store.reset()
        build_store.upsert(chunks, vectors)
        index_count = build_store.count()
        build_store.close()

        verification_store = ChromaStore(chroma_path, build_collection)
        verified_count = verification_store.count()
        verification_store.close()
        if verified_count != len(chunks):
            raise RuntimeError(f"STM32 Chroma 校验失败：期望 {len(chunks)}，实际 {verified_count}")

        backup_collection = f"{self.profile.collection_name}_previous"
        stale_backup = ChromaStore(chroma_path, backup_collection)
        stale_backup.delete()
        stale_backup.close()
        current_store = ChromaStore(chroma_path, self.profile.collection_name)
        current_store.rename(backup_collection)
        current_store.close()
        try:
            replacement_store = ChromaStore(chroma_path, build_collection)
            replacement_store.rename(self.profile.collection_name)
            replacement_store.close()
        except Exception:
            rollback_store = ChromaStore(chroma_path, backup_collection)
            rollback_store.rename(self.profile.collection_name)
            rollback_store.close()
            raise
        previous_store = ChromaStore(chroma_path, backup_collection)
        previous_store.delete()
        previous_store.close()
        save_chunks(self.profile.chunks_path, chunks)
        self.invalidate_cache()
        return 0, {
            "status": "ok",
            "profile": "stm32f4",
            "documents": len(documents),
            "chunks": len(chunks),
            "index_count": index_count,
            "embedding": embeddings.name,
            "errors": errors,
            "ignored": ignored,
            "sources": self.source_snapshot(errors),
        }

    def query(self, query: str, top_k: int = 5, llm: bool = False) -> DiagnosisResult:
        result = self._diagnose_query(query, top_k, llm)
        report_path = write_report(result, self.profile.report_root, query, title="STM32F4 智能诊断报告")
        result.metadata["report_path"] = str(report_path)
        return result

    def diagnose_hardfault(self, raw_log: str, top_k: int = 5, llm: bool = False) -> DiagnosisResult:
        analysis = analyze_hardfault_log(raw_log)
        result = self._diagnose_query(analysis.generated_query, top_k, llm)
        analysis_payload = analysis.to_dict()
        result.metadata["hardfault"] = analysis_payload
        report_key = analysis_payload["registers"].get(
            "PC",
            analysis_payload["registers"].get("CFSR", "snapshot"),
        )
        report_path = write_hardfault_report(result, self.profile.report_root, f"HardFault_{report_key}")
        result.metadata["report_path"] = str(report_path)
        return result

    def diagnose_protocol_log(
        self,
        raw_log: str,
        *,
        profile: str = "auto",
        expected_cycle_ms: float | int | None = None,
        jitter_tolerance_ms: float | int | None = None,
        expected_length: int | None = None,
        crc16_algorithm: str = "none",
        top_k: int = 5,
        llm: bool = False,
    ) -> DiagnosisResult:
        analysis = analyze_protocol_log(
            raw_log,
            profile=profile,
            expected_cycle_ms=expected_cycle_ms,
            jitter_tolerance_ms=jitter_tolerance_ms,
            expected_length=expected_length,
            crc16_algorithm=crc16_algorithm,
        )
        result = self._diagnose_query(analysis.generated_query, top_k, llm)
        result.metadata["protocol_log"] = analysis.to_dict()
        report_key = hashlib.sha1(raw_log.encode("utf-8")).hexdigest()[:10]
        report_path = write_protocol_report(
            result,
            self.profile.report_root,
            f"ProtocolLog_{analysis.profile}_{report_key}",
        )
        result.metadata["report_path"] = str(report_path)
        return result

    def _diagnose_query(self, query: str, top_k: int, llm: bool) -> DiagnosisResult:
        started = time.perf_counter()
        retriever = self.build_retriever()
        evidence = retriever.search(query, max(top_k * 3, 15))
        result = diagnose(query, evidence, llm, retriever.reranker_name, domain="stm32f4")
        result.evidence = result.evidence[:top_k]
        for rank, item in enumerate(result.evidence, start=1):
            item.rank = rank
        result.metadata.update(
            {
                "retrieval_ms": round((time.perf_counter() - started) * 1000, 3),
                "candidate_count": len(evidence),
                "embedding": retriever.embeddings.name,
                "profile": "stm32f4",
            }
        )
        return result
