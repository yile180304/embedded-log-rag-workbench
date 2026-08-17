from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from rag_diagnostic.diagnosis import diagnose
from rag_diagnostic.evaluation import evaluate
from rag_diagnostic.hardfault import HardFaultValidationError
from rag_diagnostic.protocol import ProtocolLogValidationError
from rag_diagnostic.ingestion import chunk_documents, load_documents
from rag_diagnostic.profiles import resolve_stm32_profile
from rag_diagnostic.retrieval import ChromaStore, HybridRetriever, build_embedding_provider
from rag_diagnostic.retrieval.reranker import build_reranker
from rag_diagnostic.retrieval.store import load_chunks, save_chunks
from rag_diagnostic.reporting import write_report
from rag_diagnostic.runtime import STM32DiagnosticService, STM32RuntimeOptions
from rag_diagnostic.runtime.worker import run_worker


def _paths(args: argparse.Namespace, profile: str = "embedded") -> tuple[Path, Path, Path, str]:
    root = Path(args.root).resolve()
    if profile == "math":
        storage = root / "storage" / "math"
        return Path(args.math_source).resolve(), storage, storage / "chunks.jsonl", "math_diagnostics"
    if profile == "stm32f4":
        stm32 = resolve_stm32_profile(root)
        return stm32.document_roots[0], stm32.storage_root, stm32.chunks_path, stm32.collection_name
    return root / "data" / "raw", root / "storage", root / "storage" / "chunks.jsonl", "embedded_diagnostics"


def _build_retriever(args: argparse.Namespace, profile: str = "embedded") -> HybridRetriever:
    _, storage, chunks_path, collection_name = _paths(args, profile)
    chunks = load_chunks(chunks_path)
    if not chunks:
        raise RuntimeError("没有找到 chunks，请先运行 ingest")
    embeddings = build_embedding_provider(args.embedding, args.embedding_model)
    store = ChromaStore(storage / "chroma", collection_name)
    reranker_name = args.reranker
    if reranker_name is None:
        reranker_name = "cross-encoder" if profile in {"math", "stm32f4"} else "none"
    reranker = build_reranker(reranker_name, args.reranker_model)
    return HybridRetriever(chunks, store, embeddings, reranker)


def run_ingest(args: argparse.Namespace) -> int:
    raw_dir, storage, chunks_path, collection_name = _paths(args)
    documents, errors = load_documents(raw_dir)
    chunks = chunk_documents(documents, max_chars=args.max_chars, overlap=args.overlap)
    if not chunks:
        print(json.dumps({"status": "error", "message": "data/raw 中没有可入库文本"}, ensure_ascii=False))
        return 2
    embeddings = build_embedding_provider(args.embedding, args.embedding_model)
    vectors = embeddings.embed([chunk.search_text for chunk in chunks])
    store = ChromaStore(storage / "chroma", collection_name)
    store.reset()
    store.upsert(chunks, vectors)
    save_chunks(chunks_path, chunks)
    print(json.dumps({"status": "ok", "documents": len(documents), "chunks": len(chunks), "index_count": store.count(), "embedding": embeddings.name, "errors": errors}, ensure_ascii=False, indent=2))
    return 0


def run_math_ingest(args: argparse.Namespace) -> int:
    raw_dir, storage, chunks_path, collection_name = _paths(args, "math")
    documents, errors = load_documents(
        raw_dir,
        source_root=raw_dir.parent,
        exclude_dirs={".obsidian", "copilot", "smart-connections", "realclaudian"},
    )
    chunks = chunk_documents(documents, max_chars=args.max_chars, overlap=args.overlap)
    if not chunks:
        print(json.dumps({"status": "error", "message": "数学资料目录中没有可入库 Markdown"}, ensure_ascii=False))
        return 2
    embeddings = build_embedding_provider(args.embedding, args.embedding_model)
    store = ChromaStore(storage / "chroma", collection_name)
    store.reset()
    store.upsert(chunks, embeddings.embed([chunk.search_text for chunk in chunks]))
    save_chunks(chunks_path, chunks)
    print(json.dumps({"status": "ok", "profile": "math", "source_root": str(raw_dir), "documents": len(documents), "chunks": len(chunks), "index_count": store.count(), "embedding": embeddings.name, "errors": errors}, ensure_ascii=False, indent=2))
    return 0


def run_query(args: argparse.Namespace) -> int:
    started = time.perf_counter()
    retriever = _build_retriever(args)
    evidence = retriever.search(args.query, args.top_k)
    elapsed_ms = (time.perf_counter() - started) * 1000
    result = diagnose(args.query, evidence, args.llm, retriever.reranker_name)
    result.metadata.update(
        {
            "retrieval_ms": round(elapsed_ms, 3),
            "candidate_count": len(evidence),
            "embedding": retriever.embeddings.name,
        }
    )
    print(json.dumps(result.to_dict(), ensure_ascii=False, indent=2))
    return 0


def run_math_query(args: argparse.Namespace) -> int:
    started = time.perf_counter()
    retriever = _build_retriever(args, "math")
    evidence = retriever.search(args.query, args.top_k)
    result = diagnose(args.query, evidence, args.llm, retriever.reranker_name, domain="study")
    result.metadata.update({"retrieval_ms": round((time.perf_counter() - started) * 1000, 3), "candidate_count": len(evidence), "embedding": retriever.embeddings.name, "profile": "math"})
    report_path = write_report(
        result,
        Path(args.root).resolve() / "reports" / "math",
        args.query,
        title="数学知识点复习报告",
    )
    result.metadata["report_path"] = str(report_path)
    print(json.dumps(result.to_dict(), ensure_ascii=False, indent=2))
    return 0


def run_stm32_ingest(args: argparse.Namespace) -> int:
    service = _stm32_service(args)
    exit_code, payload = service.ingest(args.max_chars, args.overlap)
    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return exit_code


def run_stm32_query(args: argparse.Namespace) -> int:
    result = _stm32_service(args).query(args.query, args.top_k, args.llm)
    print(json.dumps(result.to_dict(), ensure_ascii=False, indent=2))
    return 0


def run_stm32_diagnose_log(args: argparse.Namespace) -> int:
    result = _stm32_service(args).diagnose_hardfault(args.log, args.top_k, args.llm)
    print(json.dumps(result.to_dict(), ensure_ascii=False, indent=2))
    return 0


def run_protocol_diagnose_log(args: argparse.Namespace) -> int:
    result = _stm32_service(args).diagnose_protocol_log(
        args.log,
        profile=args.profile,
        expected_cycle_ms=args.expected_cycle_ms,
        jitter_tolerance_ms=args.jitter_tolerance_ms,
        expected_length=args.expected_length,
        crc16_algorithm=args.crc16_algorithm,
        top_k=args.top_k,
        llm=args.llm,
    )
    print(json.dumps(result.to_dict(), ensure_ascii=False, indent=2))
    return 0


def _stm32_service(args: argparse.Namespace) -> STM32DiagnosticService:
    return STM32DiagnosticService(
        STM32RuntimeOptions(
            root=Path(args.root).resolve(),
            embedding=args.embedding,
            embedding_model=args.embedding_model,
            reranker=args.reranker,
            reranker_model=args.reranker_model,
        )
    )


def run_stm32_worker(args: argparse.Namespace) -> int:
    return run_worker(_stm32_service(args))


def run_evaluate(args: argparse.Namespace) -> int:
    retriever = _build_retriever(args)
    root = Path(args.root).resolve()
    eval_path = Path(args.eval)
    if not eval_path.is_absolute():
        eval_path = root / eval_path
    result = evaluate(eval_path, retriever, args.top_k)
    output_path = root / "reports" / "evaluation.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    result["report_path"] = str(output_path)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="嵌入式日志诊断 RAG MVP")
    parser.add_argument("--root", default=".", help="RAG 项目根目录")
    parser.add_argument("--embedding", choices=["hash", "bge", "sentence-transformer"], default="hash")
    parser.add_argument("--embedding-model", default=None)
    parser.add_argument(
        "--reranker",
        choices=["none", "cross-encoder"],
        default=None,
        help="重排序器；数学和 STM32F4 查询默认 cross-encoder，其他命令默认 none",
    )
    parser.add_argument("--reranker-model", default=None)
    sub = parser.add_subparsers(dest="command", required=True)

    ingest = sub.add_parser("ingest", help="导入资料并建立 Chroma 索引")
    ingest.add_argument("--max-chars", type=int, default=1200)
    ingest.add_argument("--overlap", type=int, default=180)
    ingest.set_defaults(func=run_ingest)

    math_ingest = sub.add_parser("math-ingest", help="只读导入 Obsidian 数学 Markdown")
    math_ingest.add_argument("--math-source", default="E:/thinking/yile/01 数学知识点")
    math_ingest.add_argument("--max-chars", type=int, default=1200)
    math_ingest.add_argument("--overlap", type=int, default=180)
    math_ingest.set_defaults(func=run_math_ingest)

    query = sub.add_parser("query", help="检索并生成带证据的诊断结果")
    query.add_argument("query")
    query.add_argument("--top-k", type=int, default=3)
    query.add_argument("--llm", action="store_true", help="启用 OpenAI-compatible 生成接口")
    query.set_defaults(func=run_query)

    math_query = sub.add_parser("math-query", help="查询数学知识库并生成 Obsidian Markdown 报告")
    math_query.add_argument("query")
    math_query.add_argument("--math-source", default="E:/thinking/yile/01 数学知识点")
    math_query.add_argument("--top-k", type=int, default=5)
    math_query.add_argument("--llm", action="store_true")
    math_query.set_defaults(func=run_math_query)

    stm32_ingest = sub.add_parser("stm32-ingest", help="只读导入 STM32F4 手册、CMSIS 和 FreeRTOS 资料")
    stm32_ingest.add_argument("--max-chars", type=int, default=1200)
    stm32_ingest.add_argument("--overlap", type=int, default=180)
    stm32_ingest.set_defaults(func=run_stm32_ingest)

    stm32_query = sub.add_parser("stm32-query", help="查询 STM32F4 诊断知识库并生成 Markdown 报告")
    stm32_query.add_argument("query")
    stm32_query.add_argument("--top-k", type=int, default=5)
    stm32_query.add_argument("--llm", action="store_true")
    stm32_query.set_defaults(func=run_stm32_query)

    stm32_diagnose_log = sub.add_parser("stm32-diagnose-log", help="解析 HardFault 日志并生成 STM32F4 诊断结果")
    stm32_diagnose_log.add_argument("log", help="包含 CFSR/HFSR/BFAR/MMFAR/PC/LR 的 HardFault 日志")
    stm32_diagnose_log.add_argument("--top-k", type=int, default=5)
    stm32_diagnose_log.add_argument("--llm", action="store_true")
    stm32_diagnose_log.set_defaults(func=run_stm32_diagnose_log)

    protocol_diagnose_log = sub.add_parser(
        "protocol-diagnose-log",
        help="解析离线 UDP/TRDP/通用嵌入式日志并生成带证据诊断结果",
    )
    protocol_diagnose_log.add_argument("log", help="离线应用日志、tshark 文本或显式 FRAME/CRC 文本")
    protocol_diagnose_log.add_argument("--profile", choices=["auto", "generic", "udp", "trdp"], default="auto")
    protocol_diagnose_log.add_argument("--expected-cycle-ms", type=float, default=None)
    protocol_diagnose_log.add_argument("--jitter-tolerance-ms", type=float, default=None)
    protocol_diagnose_log.add_argument("--expected-length", type=int, default=None)
    protocol_diagnose_log.add_argument(
        "--crc16-algorithm", choices=["none", "modbus", "ccitt_false"], default="none")
    protocol_diagnose_log.add_argument("--top-k", type=int, default=5)
    protocol_diagnose_log.add_argument("--llm", action="store_true")
    protocol_diagnose_log.set_defaults(func=run_protocol_diagnose_log)

    worker = sub.add_parser("worker", help="启动 Local Worker Protocol v1 长驻诊断进程")
    worker.set_defaults(func=run_stm32_worker)

    evaluation = sub.add_parser("evaluate", help="在标注集上计算检索指标")
    evaluation.add_argument("--eval", default="data/eval/questions.json")
    evaluation.add_argument("--top-k", type=int, default=3)
    evaluation.set_defaults(func=run_evaluate)
    return parser


def main(argv: list[str] | None = None) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except (HardFaultValidationError, ProtocolLogValidationError) as exc:
        print(json.dumps(exc.to_dict(), ensure_ascii=False), file=sys.stderr)
        return 2
    except Exception as exc:
        print(json.dumps({"status": "error", "type": type(exc).__name__, "message": str(exc)}, ensure_ascii=False), file=sys.stderr)
        return 1
