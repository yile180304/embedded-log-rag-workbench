from __future__ import annotations

import json
import io
import os
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

from rag_diagnostic.diagnosis import diagnose
from rag_diagnostic.evaluation import evaluate, evaluate_samples
from rag_diagnostic.ingestion import chunk_documents, load_documents
from rag_diagnostic.hardfault import HardFaultValidationError, analyze_hardfault_log
from rag_diagnostic.models import Chunk, Document
from rag_diagnostic.models import DiagnosisResult, Evidence
from rag_diagnostic.profiles import discover_stm32_sources, resolve_stm32_profile
from rag_diagnostic.ingestion import chunk_stm32_documents, load_stm32_documents
from rag_diagnostic.reporting import render_hardfault_markdown, render_markdown
from rag_diagnostic.runtime import STM32DiagnosticService, STM32RuntimeOptions
from rag_diagnostic.runtime.worker import RagWorker
from rag_diagnostic.retrieval import ChromaStore, HybridRetriever, build_embedding_provider
from rag_diagnostic.retrieval.reranker import build_reranker
from rag_diagnostic.retrieval.store import load_chunks, save_chunks


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class PipelineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temp = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        cls.storage = Path(cls.temp.name)
        documents, errors = load_documents(PROJECT_ROOT / "data" / "raw")
        assert not errors
        cls.chunks = chunk_documents(documents)
        cls.embeddings = build_embedding_provider("hash")
        cls.store = ChromaStore(cls.storage / "chroma", "test_diagnostics")
        cls.store.reset()
        cls.store.upsert(cls.chunks, cls.embeddings.embed([chunk.search_text for chunk in cls.chunks]))
        save_chunks(cls.storage / "chunks.jsonl", cls.chunks)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temp.cleanup()

    def test_chunks_keep_source_and_section(self) -> None:
        self.assertTrue(self.chunks)
        self.assertTrue(all(chunk.source and chunk.metadata.get("section") for chunk in self.chunks))

    def test_hardfault_contract_serializes_stable_analysis_shape(self) -> None:
        analysis = analyze_hardfault_log("CFSR=0x00008200")
        payload = analysis.to_dict()
        self.assertEqual(payload["raw_log"], "CFSR=0x00008200")
        self.assertEqual(payload["registers"], {"CFSR": "0x00008200"})
        self.assertEqual(
            [flag["name"] for flag in payload["decoded_flags"]],
            ["PRECISERR", "BFARVALID"],
        )
        self.assertTrue(payload["observations"])
        self.assertTrue(payload["next_actions"])
        self.assertIn("PRECISERR", payload["generated_query"])

    def test_hardfault_contract_rejects_empty_log(self) -> None:
        with self.assertRaises(HardFaultValidationError) as caught:
            analyze_hardfault_log("  \n")
        self.assertEqual(caught.exception.code, "empty_log")
        self.assertEqual(caught.exception.field, "log")

    def test_worker_protocol_handles_health_and_request_errors_without_exiting(self) -> None:
        output = io.StringIO()
        service = STM32DiagnosticService(STM32RuntimeOptions(root=PROJECT_ROOT))
        worker = RagWorker(service, output, operation_handler=lambda operation, payload, options: {"ok": True})
        worker.handle_line("not-json")
        worker.handle_line(json.dumps({"protocol_version": 9, "request_id": "bad-version", "operation": "health"}))
        worker.handle_line(json.dumps({"protocol_version": 1, "operation": "health"}))
        worker.handle_line(json.dumps({"protocol_version": 1, "request_id": "unknown-op", "operation": "unknown"}))
        worker.handle_line(json.dumps({"protocol_version": 1, "request_id": "health-1", "operation": "health"}))
        worker.handle_line(json.dumps({"protocol_version": 1, "request_id": "health-2", "operation": "health"}))
        events = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(
            [event["event"] for event in events],
            ["error", "error", "error", "error", "result", "result"],
        )
        self.assertEqual(events[1]["data"]["code"], "unsupported_protocol")
        self.assertEqual(events[2]["data"]["code"], "invalid_request")
        self.assertEqual(events[2]["data"]["field"], "request_id")
        self.assertEqual(events[3]["data"]["code"], "unsupported_operation")
        self.assertEqual(events[4]["request_id"], "health-1")
        self.assertEqual(events[5]["request_id"], "health-2")
        self.assertFalse(events[4]["data"]["models"]["embedding"]["loaded"])
        self.assertFalse(events[5]["data"]["models"]["embedding"]["loaded"])
        self.assertIsInstance(events[4]["data"]["sources"], list)
        self.assertTrue(events[4]["data"]["sources"])

    def test_worker_protocol_rejects_competing_heavy_tasks_and_stays_alive(self) -> None:
        output = io.StringIO()
        started = threading.Event()
        release = threading.Event()

        def slow_handler(operation, payload, options):
            started.set()
            release.wait(5)
            return {"operation": operation}

        service = STM32DiagnosticService(STM32RuntimeOptions(root=PROJECT_ROOT))
        worker = RagWorker(service, output, operation_handler=slow_handler)
        worker.handle_line(json.dumps({"protocol_version": 1, "request_id": "q1", "operation": "query"}))
        task_runner = threading.Thread(target=lambda: worker._run_heavy_operation(*worker._heavy_tasks.get()))
        task_runner.start()
        self.assertTrue(started.wait(2))
        worker.handle_line(json.dumps({"protocol_version": 1, "request_id": "q2", "operation": "hardfault"}))
        worker.handle_line(json.dumps({"protocol_version": 1, "request_id": "q3", "operation": "protocol_log"}))
        worker.handle_line(json.dumps({"protocol_version": 1, "request_id": "health-2", "operation": "health"}))
        release.set()
        task_runner.join(2)
        self.assertFalse(task_runner.is_alive())
        events = [json.loads(line) for line in output.getvalue().splitlines()]
        busy = next(event for event in events if event["request_id"] == "q2")
        protocol_busy = next(event for event in events if event["request_id"] == "q3")
        health = next(event for event in events if event["request_id"] == "health-2")
        self.assertEqual(busy["data"]["code"], "busy")
        self.assertEqual(protocol_busy["data"]["code"], "busy")
        self.assertEqual(health["data"]["worker"], "busy")
        self.assertTrue(any(event["request_id"] == "q1" and event["event"] == "result" for event in events))

    def test_worker_evaluate_validates_samples_emits_progress_and_returns_metrics(self) -> None:
        output = io.StringIO()
        service = STM32DiagnosticService(STM32RuntimeOptions(root=PROJECT_ROOT))
        worker = RagWorker(service, output)
        worker.handle_line(json.dumps({
            "protocol_version": 1,
            "request_id": "empty-eval",
            "operation": "evaluate",
            "payload": {"samples": []},
        }))
        worker._run_heavy_operation(*worker._heavy_tasks.get())
        retrieved = Evidence(
            chunk_id="core-cfsr",
            text="CFSR definition",
            source="core_cm4.h",
            metadata={"doc_id": "CMSIS", "page": 1, "section": "SCB_Type"},
            rank=1,
        )
        worker.handle_line(json.dumps({
            "protocol_version": 1,
            "request_id": "valid-eval",
            "operation": "evaluate",
            "payload": {
                "samples": [{
                    "sample_id": "sample-1",
                    "query": "CFSR 在哪里定义？",
                    "expected_evidence": [{"source": "core_cm4.h"}],
                }],
            },
            "options": {"top_k": 5},
        }))
        retriever = MagicMock()
        retriever.search.return_value = [retrieved]
        with patch.object(service, "build_retriever", return_value=retriever):
            worker._run_heavy_operation(*worker._heavy_tasks.get())
        events = [json.loads(line) for line in output.getvalue().splitlines()]
        empty_error = next(event for event in events if event["request_id"] == "empty-eval" and event["event"] == "error")
        progress = next(event for event in events if event["request_id"] == "valid-eval" and event["event"] == "progress")
        valid_result = next(event for event in events if event["request_id"] == "valid-eval" and event["event"] == "result")
        self.assertEqual(empty_error["data"]["code"], "validation_error")
        self.assertEqual(empty_error["data"]["field"], "samples")
        self.assertEqual(valid_result["data"]["sample_count"], 1)
        self.assertEqual(valid_result["data"]["top_k"], 5)
        self.assertEqual(valid_result["data"]["hit_rate@5"], 1.0)
        self.assertEqual(valid_result["data"]["recall@5"], 1.0)
        self.assertEqual(valid_result["data"]["mrr"], 1.0)
        self.assertEqual(progress["data"]["completed"], 1)
        self.assertEqual(progress["data"]["total"], 1)
        self.assertEqual(progress["data"]["current_query"], "CFSR 在哪里定义？")
        self.assertIn("runtime_snapshot", valid_result["data"])
        self.assertFalse(service.cache_loaded)

    def test_stm32_service_cache_builds_once_and_invalidates_explicitly(self) -> None:
        service = STM32DiagnosticService(STM32RuntimeOptions(root=PROJECT_ROOT))
        sentinel = MagicMock()
        with patch("rag_diagnostic.runtime.stm32.load_chunks", return_value=[self.chunks[0]]), \
             patch("rag_diagnostic.runtime.stm32.build_embedding_provider") as embedding_builder, \
             patch("rag_diagnostic.runtime.stm32.ChromaStore"), \
             patch("rag_diagnostic.runtime.stm32.build_reranker", return_value=None), \
             patch("rag_diagnostic.runtime.stm32.HybridRetriever", return_value=sentinel):
            embedding_builder.return_value = self.embeddings
            self.assertIs(service.build_retriever(), sentinel)
            self.assertIs(service.build_retriever(), sentinel)
            self.assertEqual(embedding_builder.call_count, 1)
            self.assertTrue(service.cache_loaded)
            service.invalidate_cache()
            self.assertFalse(service.cache_loaded)
            self.assertIs(service.build_retriever(), sentinel)
            self.assertEqual(embedding_builder.call_count, 2)

    def test_stm32_reindex_success_invalidates_cache_and_failure_preserves_it(self) -> None:
        service = STM32DiagnosticService(STM32RuntimeOptions(root=PROJECT_ROOT))
        cached = MagicMock()
        service._retriever = cached
        fake_store = MagicMock()
        fake_store.count.return_value = 1
        with patch("rag_diagnostic.runtime.stm32.load_stm32_documents", return_value=([Document("manual.md", "fault")], [], [])), \
             patch("rag_diagnostic.runtime.stm32.chunk_stm32_documents", return_value=[self.chunks[0]]), \
             patch("rag_diagnostic.runtime.stm32.build_embedding_provider") as embedding_builder, \
             patch("rag_diagnostic.runtime.stm32.ChromaStore", return_value=fake_store), \
             patch("rag_diagnostic.runtime.stm32.save_chunks"):
            embedding_builder.return_value = self.embeddings
            exit_code, payload = service.ingest()
        self.assertEqual(exit_code, 0)
        self.assertEqual(payload["status"], "ok")
        self.assertFalse(service.cache_loaded)
        cached.store.close.assert_called_once()

        preserved = MagicMock()
        service._retriever = preserved
        with patch("rag_diagnostic.runtime.stm32.load_stm32_documents", side_effect=RuntimeError("ingest failed")):
            with self.assertRaisesRegex(RuntimeError, "ingest failed"):
                service.ingest()
        self.assertIs(service._retriever, preserved)
        preserved.store.close.assert_not_called()

    def test_worker_bad_hardfault_does_not_touch_retriever_cache(self) -> None:
        output = io.StringIO()
        service = STM32DiagnosticService(STM32RuntimeOptions(root=PROJECT_ROOT))
        worker = RagWorker(service, output)
        with patch.object(service, "build_retriever", side_effect=AssertionError("cache should not load")):
            worker.handle_line(json.dumps({
                "protocol_version": 1,
                "request_id": "bad-fault",
                "operation": "hardfault",
                "payload": {"log": "CFSR=oops"},
            }))
            worker._run_heavy_operation(*worker._heavy_tasks.get())
        events = [json.loads(line) for line in output.getvalue().splitlines()]
        error = next(event for event in events if event["request_id"] == "bad-fault" and event["event"] == "error")
        self.assertEqual(error["data"]["code"], "validation_error")
        self.assertFalse(service.cache_loaded)

    def test_worker_reindex_failure_preserves_source_error_details(self) -> None:
        output = io.StringIO()
        service = STM32DiagnosticService(STM32RuntimeOptions(root=PROJECT_ROOT))
        worker = RagWorker(service, output)
        failed_sources = [{
            "source_id": "RM0090",
            "display_name": "Reference Manual",
            "path": "E:/docs/RM0090.pdf",
            "source_type": "pdf",
            "manifest_status": "allowed",
            "index_status": "error",
            "chunks": 0,
            "error": "parse failed",
        }]
        with patch.object(service, "ingest", return_value=(2, {
            "status": "error",
            "errors": [{"source": "RM0090.pdf", "error": "parse failed"}],
            "ignored": [],
            "sources": failed_sources,
        })):
            worker.handle_line(json.dumps({
                "protocol_version": 1,
                "request_id": "failed-reindex",
                "operation": "reindex",
            }))
            worker._run_heavy_operation(*worker._heavy_tasks.get())
        events = [json.loads(line) for line in output.getvalue().splitlines()]
        error = next(event for event in events if event["request_id"] == "failed-reindex" and event["event"] == "error")
        self.assertEqual(error["data"]["code"], "runtime_unavailable")
        self.assertEqual(error["data"]["sources"], failed_sources)
        self.assertEqual(error["data"]["errors"][0]["source"], "RM0090.pdf")

    def test_hardfault_decodes_precise_busfault_and_valid_bfar(self) -> None:
        analysis = analyze_hardfault_log(
            "CFSR=0x00008200, HFSR=0x40000000\nBFAR=0x2003FFF8 PC=0x080126AC LR=0xFFFFFFF9"
        )
        flags = {flag.name: flag.group for flag in analysis.decoded_flags}
        self.assertEqual(flags["PRECISERR"], "BusFault")
        self.assertEqual(flags["BFARVALID"], "BusFault")
        self.assertEqual(flags["FORCED"], "HardFault")
        self.assertIn("BFAR 有效", " ".join(analysis.observations))
        self.assertIn("PRECISERR", analysis.generated_query)
        self.assertIn("有效 BFAR=0x2003FFF8", analysis.generated_query)

    def test_hardfault_decodes_usagefault_group(self) -> None:
        analysis = analyze_hardfault_log("CFSR: 0x03000000")
        flags = {flag.name: flag.group for flag in analysis.decoded_flags}
        self.assertEqual(flags["UNALIGNED"], "UsageFault")
        self.assertEqual(flags["DIVBYZERO"], "UsageFault")
        self.assertNotIn("BusFault", flags.values())

    def test_hardfault_accepts_format_variants_and_same_duplicates(self) -> None:
        analysis = analyze_hardfault_log("cfsr : 33280; CFSR=0x00008200\nhfsr = 1073741824")
        self.assertEqual(analysis.registers["CFSR"], 0x00008200)
        self.assertEqual(analysis.registers["HFSR"], 0x40000000)

    def test_hardfault_does_not_treat_invalid_mmfar_as_valid_address(self) -> None:
        analysis = analyze_hardfault_log("CFSR=0x00000000 MMFAR=0xDEADBEEF")
        observations = " ".join(analysis.observations)
        self.assertIn("MMARVALID 未置位", observations)
        self.assertNotIn("有效 MMFAR", analysis.generated_query)

    def test_hardfault_rejects_conflicting_duplicate_register(self) -> None:
        with self.assertRaises(HardFaultValidationError) as caught:
            analyze_hardfault_log("CFSR=0x82 CFSR=0x83")
        self.assertEqual(caught.exception.code, "conflicting_register")
        self.assertEqual(caught.exception.field, "CFSR")

    def test_hardfault_rejects_invalid_or_out_of_range_value(self) -> None:
        for log, code in (("CFSR=oops", "invalid_value"), ("CFSR=0x100000000", "value_out_of_range"), ("CFSR=", "missing_value")):
            with self.subTest(log=log):
                with self.assertRaises(HardFaultValidationError) as caught:
                    analyze_hardfault_log(log)
                self.assertEqual(caught.exception.code, code)

    def test_hardfault_requires_fault_status_register(self) -> None:
        with self.assertRaises(HardFaultValidationError) as caught:
            analyze_hardfault_log("PC=0x08001234 LR=0xFFFFFFF9")
        self.assertEqual(caught.exception.code, "missing_fault_status")

    def test_hardfault_report_separates_rules_from_rag_evidence(self) -> None:
        analysis = analyze_hardfault_log("CFSR=0x00008200 HFSR=0x40000000 BFAR=0x2003FFF8")
        result = DiagnosisResult(
            query=analysis.generated_query,
            answer="根据手册证据检查 BusFault。",
            grounded=False,
            refusal_reason="no_evidence",
            metadata={"embedding": "test", "reranker": "test", "hardfault": analysis.to_dict()},
        )
        report = render_hardfault_markdown(result)
        self.assertIn("## Fault Register Snapshot", report)
        self.assertIn("`CFSR` = `0x00008200`", report)
        self.assertIn("`CFSR.PRECISERR`", report)
        self.assertIn("## 自动检索问题", report)
        self.assertIn("是否有证据支撑：`否`", report)

    def test_duplicate_filenames_in_different_folders_have_unique_chunk_ids(self) -> None:
        chunks = chunk_documents(
            [
                Document(source="概率论/未命名.md", content="# 概率\n条件概率"),
                Document(source="线代/未命名.md", content="# 线代\n线性相关"),
            ]
        )
        self.assertEqual(len({chunk.chunk_id for chunk in chunks}), 2)

    def test_empty_file_is_reported_and_skipped(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            Path(raw, "empty.txt").write_text("", encoding="utf-8")
            documents, errors = load_documents(Path(raw))
        self.assertEqual(documents, [])
        self.assertEqual(errors[0]["error"], "empty document")

    def test_hardfault_query_ranks_hardfault_first(self) -> None:
        retriever = HybridRetriever(self.chunks, self.store, self.embeddings)
        evidence = retriever.search("CFSR=0x82，PC 应该怎样排查？", 3)
        self.assertEqual(evidence[0].source, "hardfault_manual.md")

    def test_hybrid_reranking_keeps_a_lexical_candidate_slot(self) -> None:
        class ReverseReranker:
            name = "reverse-test"

            def rerank(self, query, evidence):
                return list(reversed(evidence))

        retriever = HybridRetriever(self.chunks, self.store, self.embeddings, ReverseReranker())
        evidence = retriever.search("CFSR HardFault", 4)
        self.assertTrue(any(item.scores.get("bm25", 0.0) > 0 for item in evidence))

    def test_unknown_query_refuses(self) -> None:
        retriever = HybridRetriever(self.chunks, self.store, self.embeddings)
        evidence = retriever.search("如何制作草莓蛋糕并调节烤箱温度？", 3)
        result = diagnose("如何制作草莓蛋糕并调节烤箱温度？", evidence)
        self.assertFalse(result.grounded)
        self.assertEqual(result.refusal_reason, "no_evidence")

    def test_missing_cross_encoder_degrades(self) -> None:
        with patch(
            "rag_diagnostic.retrieval.reranker.CrossEncoderReranker",
            side_effect=RuntimeError("model unavailable"),
        ):
            reranker = build_reranker("cross-encoder")
        self.assertIsNotNone(reranker)
        self.assertTrue(reranker.name.startswith("disabled:"))

    def test_external_math_source_keeps_relative_vault_path(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            note = root / "01 数学知识点" / "概率论" / "贝叶斯.md"
            note.parent.mkdir(parents=True)
            note.write_text("# 贝叶斯\n结果已发生后反查原因。", encoding="utf-8")
            documents, errors = load_documents(root / "01 数学知识点", source_root=root)
        self.assertFalse(errors)
        self.assertEqual(documents[0].source, "01 数学知识点/概率论/贝叶斯.md")
        self.assertEqual(documents[0].metadata["vault_path"], documents[0].source)

    def test_markdown_report_contains_obsidian_link_and_evidence(self) -> None:
        evidence = Evidence(
            chunk_id="bayes-001",
            text="结果已发生后反查原因。",
            source="01 数学知识点/概率论/贝叶斯.md",
            metadata={"vault_path": "01 数学知识点/概率论/贝叶斯.md", "section": "贝叶斯"},
            rank=1,
            scores={"hybrid": 0.9},
        )
        report = render_markdown(
            DiagnosisResult(query="贝叶斯什么时候用？", answer="先看结果是否已经发生。", evidence=[evidence], grounded=True),
            title="数学知识点复习报告",
        )
        self.assertIn("数学知识点复习报告", report)
        self.assertIn("[[01 数学知识点/概率论/贝叶斯]]", report)
        self.assertIn("bayes-001", report)

    def test_math_query_accepts_calibrated_reranker_evidence(self) -> None:
        evidence = Evidence(
            chunk_id="bayes-001",
            text="贝叶斯公式用于结果已经发生后反查原因。",
            source="01 数学知识点/概率论/贝叶斯.md",
            metadata={"section": "贝叶斯公式"},
            rank=1,
            scores={"reranker": 0.19, "hybrid": 0.9},
        )
        result = diagnose("贝叶斯公式什么时候用？", [evidence], domain="study")
        self.assertTrue(result.grounded)
        self.assertEqual(result.evidence[0].chunk_id, "bayes-001")

    def test_math_query_refuses_domain_mismatch_after_reranking(self) -> None:
        evidence = Evidence(
            chunk_id="linear-001",
            text="线性相关性可以通过矩阵秩判断。",
            source="01 数学知识点/线代/线性相关性.md",
            metadata={"section": "线性相关性"},
            rank=1,
            scores={"reranker": 0.011, "hybrid": 0.69},
        )
        result = diagnose("STM32 的 CFSR 寄存器怎么分析？", [evidence], domain="study")
        self.assertFalse(result.grounded)
        self.assertEqual(result.refusal_reason, "no_evidence")
        self.assertEqual(result.evidence, [])

    def test_math_query_uses_conservative_fallback_without_reranker(self) -> None:
        weak = Evidence(
            chunk_id="linear-001",
            text="线性相关性可以通过矩阵秩判断。",
            source="01 数学知识点/线代/线性相关性.md",
            metadata={"section": "线性相关性"},
            rank=1,
            scores={"bm25": 5.51, "dense": 0.50, "hybrid": 0.69},
        )
        result = diagnose("STM32 的 CFSR 寄存器怎么分析？", [weak], domain="study")
        self.assertFalse(result.grounded)
        self.assertEqual(result.evidence, [])

    def test_stm32_query_refuses_weak_or_toc_only_evidence(self) -> None:
        evidence = Evidence(
            chunk_id="an4989-toc",
            text="Figure 46. Fault Analyzer ........ 66",
            source="AN4989.pdf",
            metadata={"section": "contents", "is_toc": True, "domain": "stm32f4"},
            rank=1,
            scores={"reranker": 0.8, "hybrid": 0.9},
        )
        result = diagnose("Fault Analyzer 怎么查看 CFSR？", [evidence], domain="stm32f4")
        self.assertFalse(result.grounded)
        self.assertEqual(result.refusal_reason, "no_evidence")

    def test_stm32_query_filters_pdf_list_of_figures(self) -> None:
        evidence = Evidence(
            chunk_id="an4989-list",
            text="List of figures\nFigure 46. Fault Analyzer in STM32CubeIDE",
            source="AN4989.pdf",
            metadata={"section": "Figure 46. Fault Analyzer in STM32CubeIDE . . . . . 66", "domain": "stm32f4"},
            rank=1,
            scores={"reranker": 0.8},
        )
        result = diagnose("Fault Analyzer 怎么查看 CFSR？", [evidence], domain="stm32f4")
        self.assertFalse(result.grounded)

    def test_stm32_query_keeps_strong_bm25_manual_page_after_reranking(self) -> None:
        evidence = Evidence(
            chunk_id="an4989-p65",
            text="open Fault Analyzer in Window -> Show View -> Fault Analyzer",
            source="AN4989.pdf",
            metadata={"page": 65, "section": "Fault Analyzer", "domain": "stm32f4"},
            rank=1,
            scores={"reranker": 0.01, "bm25": 17.3, "hybrid": 0.6},
        )
        result = diagnose("Fault Analyzer 怎么查看 CFSR？", [evidence], domain="stm32f4")
        self.assertTrue(result.grounded)

    def test_stm32_report_contains_page_and_doc_id_without_obsidian_link(self) -> None:
        evidence = Evidence(
            chunk_id="rm0090-p1214",
            text="Ethernet DMA status register (ETH_DMASR)",
            source="RM0090.pdf",
            metadata={"doc_id": "RM0090", "page": 1214, "section": "ETH_DMASR"},
            rank=1,
            scores={"reranker": 0.65},
        )
        report = render_markdown(DiagnosisResult(query="EBS 是什么？", answer="见证据。", evidence=[evidence], grounded=True))
        self.assertIn("文档 ID：`RM0090`", report)
        self.assertIn("页码：`1214`", report)
        self.assertNotIn("Obsidian 链接", report)

    def test_chroma_and_chunk_metadata_survive_reload(self) -> None:
        reloaded = load_chunks(self.storage / "chunks.jsonl")
        new_store = ChromaStore(self.storage / "chroma", "test_diagnostics")
        retriever = HybridRetriever(reloaded, new_store, self.embeddings)
        evidence = retriever.search("串口半包 CRC16", 3)
        self.assertEqual(evidence[0].source, "frame_parser_and_crc.md")

    def test_evaluation_outputs_metrics(self) -> None:
        retriever = HybridRetriever(self.chunks, self.store, self.embeddings)
        report = evaluate(PROJECT_ROOT / "data" / "eval" / "questions.json", retriever, 3)
        self.assertIn("hit_rate@3", report)
        self.assertIn("recall@3", report)
        self.assertIn("mrr", report)

    def test_structured_evaluation_matches_traceable_refs_and_manual_metrics(self) -> None:
        irrelevant = Evidence("noise", "noise", "other.pdf", {}, 1)
        chunk_hit = Evidence("chunk-hit", "chunk", "manual.pdf", {"doc_id": "RM", "page": 3, "section": "A"}, 2)
        section_hit = Evidence("section-hit", "section", "guide.pdf", {"section": "Target"}, 3)
        source_hit = Evidence("source-hit", "source", "legacy.md", {}, 1)
        source_only = Evidence("wrong-chunk", "source", "same-source.pdf", {}, 1)
        retriever = MagicMock()
        retriever.search.side_effect = [
            [irrelevant, chunk_hit, section_hit],
            [source_hit],
            [source_only],
        ]
        progress: list[tuple[int, int, str]] = []
        samples = [
            {
                "sample_id": "sample-a",
                "query": "query a",
                "expected_evidence": [
                    {"chunk_id": "chunk-hit", "source": "ignored-by-priority.pdf"},
                    {"chunk_id": "chunk-hit"},
                    {"source": "guide.pdf", "section": "Target"},
                ],
            },
            {
                "sample_id": "sample-b",
                "query": "query b",
                "expected_evidence": [
                    {"doc_id": "RM", "page": "7"},
                    {"source": "legacy.md"},
                ],
            },
            {
                "sample_id": "sample-c",
                "query": "query c",
                "expected_evidence": [
                    {"chunk_id": "missing", "source": "same-source.pdf"},
                ],
            },
        ]

        report = evaluate_samples(samples, retriever, 3, lambda done, total, query: progress.append((done, total, query)))

        self.assertAlmostEqual(report["hit_rate@3"], 2 / 3)
        self.assertAlmostEqual(report["recall@3"], 0.5)
        self.assertAlmostEqual(report["mrr"], 0.5)
        self.assertEqual(report["details"][0]["expected_count"], 2)
        self.assertEqual(report["details"][0]["hit_ranks"], [2, 3])
        self.assertEqual(report["details"][1]["recall"], 0.5)
        self.assertFalse(report["details"][2]["hit"])
        self.assertEqual([item["sample_id"] for item in report["failed_cases"]], ["sample-c"])
        self.assertEqual(progress, [(1, 3, "query a"), (2, 3, "query b"), (3, 3, "query c")])

    def test_stm32_profile_discovers_manifest_and_ignores_renesas_pdf(self) -> None:
        profile = resolve_stm32_profile(PROJECT_ROOT)
        allowed, ignored = discover_stm32_sources(profile)
        self.assertEqual(len(allowed), 9)
        self.assertIn("REN_TP65B110HRU_DST_20260310.pdf", {path.name for path in ignored})
        self.assertEqual(profile.collection_name, "stm32f4_diagnostics")
        self.assertEqual(profile.chunks_path, PROJECT_ROOT / "storage" / "stm32f4" / "index" / "chunks.jsonl")

    def test_stm32_source_snapshot_reports_manifest_and_index_state(self) -> None:
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            root = Path(directory)
            source_root = root / "storage" / "stm32f4"
            source_root.mkdir(parents=True)
            allowed_path = source_root / "core_cm4.h"
            allowed_path.write_text("typedef struct { int CFSR; } SCB_Type;", encoding="utf-8")
            ignored_path = source_root / "unapproved.pdf"
            ignored_path.write_bytes(b"not imported")
            profile = resolve_stm32_profile(root)
            profile.storage_root.mkdir(parents=True)
            save_chunks(
                profile.chunks_path,
                [Chunk("core-1", "CFSR", "core_cm4.h", {"section": "SCB_Type"})],
            )
            service = STM32DiagnosticService(STM32RuntimeOptions(root=root))

            records = service.source_snapshot()
            by_id = {record["source_id"]: record for record in records if record["source_id"]}
            self.assertEqual(by_id["CMSIS-CORE-CM4"]["manifest_status"], "allowed")
            self.assertEqual(by_id["CMSIS-CORE-CM4"]["index_status"], "indexed")
            self.assertEqual(by_id["CMSIS-CORE-CM4"]["chunks"], 1)
            self.assertEqual(by_id["RM0090"]["manifest_status"], "missing")
            self.assertEqual(by_id["RM0090"]["index_status"], "not_indexed")
            self.assertTrue(any(record["manifest_status"] == "ignored" for record in records))

            newer = profile.chunks_path.stat().st_mtime + 5
            os.utime(allowed_path, (newer, newer))
            stale = {record["source_id"]: record for record in service.source_snapshot() if record["source_id"]}
            self.assertEqual(stale["CMSIS-CORE-CM4"]["index_status"], "stale")
            failed = {
                record["source_id"]: record
                for record in service.source_snapshot([{"source": "core_cm4.h", "error": "bad header"}])
                if record["source_id"]
            }
            self.assertEqual(failed["CMSIS-CORE-CM4"]["index_status"], "error")
            self.assertEqual(failed["CMSIS-CORE-CM4"]["error"], "bad header")

    def test_stm32_loader_keeps_pdf_page_and_cmsis_metadata(self) -> None:
        profile = resolve_stm32_profile(PROJECT_ROOT)
        documents, errors, ignored = load_stm32_documents(profile)
        self.assertFalse(errors)
        self.assertTrue(ignored)
        self.assertTrue(any(doc.source == "core_cm4.h" and doc.metadata["domain"] == "stm32f4" for doc in documents))
        self.assertTrue(any(doc.metadata.get("doc_id") == "PM0214" and doc.metadata.get("page") for doc in documents))

    def test_stm32_chunking_tracks_fault_pages_and_scb_structure(self) -> None:
        profile = resolve_stm32_profile(PROJECT_ROOT)
        documents, _, _ = load_stm32_documents(profile)
        selected = [
            doc
            for doc in documents
            if (doc.metadata.get("doc_id"), doc.metadata.get("page")) in {("PM0214", 45), ("RM0090", 1214)}
            or doc.source == "core_cm4.h"
        ]
        chunks = chunk_stm32_documents(selected)
        self.assertTrue(any(chunk.metadata["doc_id"] == "PM0214" and "Fault types" in chunk.metadata["section"] for chunk in chunks))
        self.assertTrue(any(chunk.metadata["doc_id"] == "RM0090" and "ETH_DMASR" in chunk.metadata["section"] for chunk in chunks))
        self.assertTrue(any(chunk.source == "core_cm4.h" and chunk.metadata["section"] == "SCB_Type" and "CFSR" in chunk.text for chunk in chunks))
        self.assertEqual(len({chunk.chunk_id for chunk in chunks}), len(chunks))

    def test_stm32_index_paths_are_isolated(self) -> None:
        profile = resolve_stm32_profile(PROJECT_ROOT)
        self.assertEqual(profile.storage_root.name, "index")
        self.assertEqual(profile.collection_name, "stm32f4_diagnostics")
        self.assertEqual(profile.report_root.name, "stm32f4")

    def test_stm32_evaluation_set_covers_key_positive_queries(self) -> None:
        rows = json.loads((PROJECT_ROOT / "data" / "eval" / "stm32_questions.json").read_text(encoding="utf-8"))
        self.assertEqual(len(rows), 5)
        self.assertTrue(all(row.get("relevant_sources") or row.get("relevant_chunk_ids") for row in rows))

    def test_chroma_upsert_batches_large_collections(self) -> None:
        chunks = [
            type(self.chunks[0])(
                chunk_id=f"batch-{index}",
                text=f"item {index}",
                source="batch.txt",
                metadata={"section": "batch"},
            )
            for index in range(5001)
        ]
        store = ChromaStore(self.storage / "batch-chroma", "batch_diagnostics")
        store.reset()
        vectors = self.embeddings.embed([chunk.search_text for chunk in chunks])
        store.upsert(chunks, vectors)
        self.assertEqual(store.count(), 5001)
        store.close()
        reopened = ChromaStore(self.storage / "batch-chroma", "batch_diagnostics")
        self.assertEqual(reopened.count(), 5001)
        reopened.close()

    def test_chroma_collection_can_be_replaced_by_validated_build(self) -> None:
        path = self.storage / "swap-chroma"
        old = ChromaStore(path, "swap_target")
        old.upsert(self.chunks[:1], self.embeddings.embed([self.chunks[0].search_text]))
        build = ChromaStore(path, "swap_target_build")
        build.upsert(self.chunks[:2], self.embeddings.embed([chunk.search_text for chunk in self.chunks[:2]]))
        build.close()
        stale = ChromaStore(path, "swap_target_previous")
        stale.delete()
        stale.close()
        old.rename("swap_target_previous")
        old.close()
        replacement = ChromaStore(path, "swap_target_build")
        replacement.rename("swap_target")
        replacement.close()
        previous = ChromaStore(path, "swap_target_previous")
        previous.delete()
        previous.close()
        reopened = ChromaStore(path, "swap_target")
        self.assertEqual(reopened.count(), 2)
        reopened.close()


if __name__ == "__main__":
    unittest.main()
