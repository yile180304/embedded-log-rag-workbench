from __future__ import annotations

import codecs
import ctypes
from datetime import datetime
import io
import json
import os
from pathlib import Path
import queue
import sys
import threading
import time
import traceback
from typing import Any, Callable, TextIO

from rag_diagnostic.evaluation import evaluate_samples
from rag_diagnostic.hardfault import HardFaultValidationError
from rag_diagnostic.protocol import ProtocolLogValidationError
from rag_diagnostic.runtime.stm32 import STM32DiagnosticService


PROTOCOL_VERSION = 1
HEAVY_OPERATIONS = {"query", "hardfault", "protocol_log", "reindex", "evaluate"}
OperationHandler = Callable[[str, dict[str, Any], dict[str, Any]], dict[str, Any]]


class ProtocolWriter:
    def __init__(self, stream: TextIO) -> None:
        self.stream = stream
        self._lock = threading.Lock()

    def emit(self, request_id: str, event: str, data: dict[str, Any]) -> None:
        message = {
            "protocol_version": PROTOCOL_VERSION,
            "request_id": request_id,
            "event": event,
            "timestamp": datetime.now().astimezone().isoformat(timespec="seconds"),
            "data": data,
        }
        with self._lock:
            self.stream.write(json.dumps(message, ensure_ascii=False, separators=(",", ":")) + "\n")
            self.stream.flush()


class RagWorker:
    def __init__(
        self,
        service: STM32DiagnosticService,
        output: TextIO,
        operation_handler: OperationHandler | None = None,
    ) -> None:
        self.service = service
        self.writer = ProtocolWriter(output)
        self.operation_handler = operation_handler
        self._state_lock = threading.Lock()
        self._active_request_id: str | None = None
        self._shutdown_requested = False
        self._input_closed = threading.Event()
        self._heavy_tasks: queue.Queue[
            tuple[str, str, dict[str, Any], dict[str, Any]] | None
        ] = queue.Queue()

    def serve(self, input_stream: TextIO) -> int:
        self.writer.emit("worker-startup", "result", self.health_snapshot())
        reader = threading.Thread(
            target=self._read_input,
            args=(input_stream,),
            name="rag-worker-input",
            daemon=True,
        )
        reader.start()
        while True:
            try:
                task = self._heavy_tasks.get(timeout=0.05)
            except queue.Empty:
                if self._shutdown_requested or (self._input_closed.is_set() and not self._active_request()):
                    break
                continue
            if task is None:
                if self._shutdown_requested or (self._input_closed.is_set() and not self._active_request()):
                    break
                continue
            self._run_heavy_operation(*task)
        return 0

    def _read_input(self, input_stream: TextIO) -> None:
        try:
            try:
                file_descriptor = input_stream.fileno()
            except (AttributeError, io.UnsupportedOperation):
                for raw_line in input_stream:
                    if self._shutdown_requested:
                        break
                    self.handle_line(raw_line)
                return

            decoder = codecs.getincrementaldecoder("utf-8")()
            buffered_text = ""
            while not self._shutdown_requested:
                available = _available_input_bytes(file_descriptor)
                if available < 0:
                    break
                if available == 0:
                    time.sleep(0.02)
                    continue
                incoming = os.read(file_descriptor, min(4096, available))
                if not incoming:
                    break
                buffered_text += decoder.decode(incoming)
                while "\n" in buffered_text:
                    raw_line, buffered_text = buffered_text.split("\n", 1)
                    self.handle_line(raw_line)
            buffered_text += decoder.decode(b"", final=True)
            if buffered_text.strip() and not self._shutdown_requested:
                self.handle_line(buffered_text)
        finally:
            self._input_closed.set()
            self._heavy_tasks.put(None)

    def _active_request(self) -> bool:
        with self._state_lock:
            return self._active_request_id is not None

    def handle_line(self, raw_line: str) -> None:
        request_id = ""
        try:
            request = json.loads(raw_line)
            if not isinstance(request, dict):
                raise WorkerRequestError("invalid_request", "请求必须是 JSON object。")
            raw_request_id = request.get("request_id")
            request_id = raw_request_id if isinstance(raw_request_id, str) else ""
            if not request_id:
                raise WorkerRequestError("invalid_request", "request_id 必须是非空字符串。", "request_id")
            if request.get("protocol_version") != PROTOCOL_VERSION:
                raise WorkerRequestError("unsupported_protocol", "仅支持 Local Worker Protocol v1。", "protocol_version")
            operation = request.get("operation")
            if not isinstance(operation, str) or not operation:
                raise WorkerRequestError("invalid_request", "operation 必须是非空字符串。", "operation")
            payload = request.get("payload", {})
            options = request.get("options", {})
            if not isinstance(payload, dict):
                raise WorkerRequestError("invalid_request", "payload 必须是 JSON object。", "payload")
            if not isinstance(options, dict):
                raise WorkerRequestError("invalid_request", "options 必须是 JSON object。", "options")
            self._route(request_id, operation, payload, options)
        except json.JSONDecodeError as exc:
            self._emit_error(request_id, "invalid_request", f"JSON 解析失败：{exc.msg}。")
        except WorkerRequestError as exc:
            self._emit_error(request_id, exc.code, exc.message, exc.field)

    def health_snapshot(self) -> dict[str, Any]:
        chunks_path = self.service.profile.chunks_path
        index_ready = chunks_path.is_file()
        chunk_count = 0
        if index_ready:
            with chunks_path.open("r", encoding="utf-8") as handle:
                chunk_count = sum(1 for line in handle if line.strip())
        updated_at = None
        if index_ready:
            updated_at = datetime.fromtimestamp(chunks_path.stat().st_mtime).astimezone().isoformat(timespec="seconds")
        with self._state_lock:
            active_request_id = self._active_request_id
        return {
            "worker": "busy" if active_request_id else "ready",
            "python_version": ".".join(str(part) for part in sys.version_info[:3]),
            "project_root": str(self.service.options.root),
            "index": {"ready": index_ready, "chunks": chunk_count, "updated_at": updated_at},
            "models": {
                "embedding": {
                    "name": self.service.options.embedding_model or "BAAI/bge-small-zh-v1.5",
                    "loaded": self.service.cache_loaded,
                },
                "reranker": {
                    "name": self.service.options.reranker_model or "BAAI/bge-reranker-base",
                    "loaded": self.service.cache_loaded,
                },
            },
            "active_request_id": active_request_id,
            "sources": self.service.source_snapshot(),
        }

    def _route(
        self,
        request_id: str,
        operation: str,
        payload: dict[str, Any],
        options: dict[str, Any],
    ) -> None:
        if operation == "health":
            self.writer.emit(request_id, "result", self.health_snapshot())
            return
        if operation == "shutdown":
            with self._state_lock:
                if self._active_request_id:
                    self._emit_error(request_id, "busy", "RAG Worker 正在执行重任务，暂不能关闭。")
                    return
                self._shutdown_requested = True
            self.writer.emit(request_id, "result", {"status": "shutting_down"})
            self._heavy_tasks.put(None)
            return
        if operation not in HEAVY_OPERATIONS:
            raise WorkerRequestError("unsupported_operation", f"不支持的 operation：{operation}。", "operation")
        with self._state_lock:
            if self._active_request_id:
                self._emit_error(request_id, "busy", "RAG Worker 正在执行另一个重任务。")
                return
            self._active_request_id = request_id
        self.writer.emit(request_id, "accepted", {"operation": operation})
        self._heavy_tasks.put((request_id, operation, payload, options))

    def _run_heavy_operation(
        self,
        request_id: str,
        operation: str,
        payload: dict[str, Any],
        options: dict[str, Any],
    ) -> None:
        try:
            if self.operation_handler is None:
                result = self._execute_operation(
                    operation,
                    payload,
                    options,
                    progress_callback=lambda completed, total, current_query: self.writer.emit(
                        request_id,
                        "progress",
                        {
                            "completed": completed,
                            "total": total,
                            "current_query": current_query,
                        },
                    ),
                )
            else:
                result = self.operation_handler(operation, payload, options)
            self.writer.emit(request_id, "result", result)
        except (HardFaultValidationError, ProtocolLogValidationError) as exc:
            self._emit_error(request_id, "validation_error", exc.message, exc.field, {"validation_code": exc.code})
        except WorkerRequestError as exc:
            self._emit_error(request_id, exc.code, exc.message, exc.field, exc.extra)
        except Exception as exc:
            traceback.print_exc(file=sys.stderr)
            code = "index_missing" if "chunks" in str(exc).lower() else "internal_error"
            self._emit_error(request_id, code, str(exc))
        finally:
            with self._state_lock:
                if self._active_request_id == request_id:
                    self._active_request_id = None

    def _execute_operation(
        self,
        operation: str,
        payload: dict[str, Any],
        options: dict[str, Any],
        progress_callback: Callable[[int, int, str], None] | None = None,
    ) -> dict[str, Any]:
        top_k = _bounded_int(options.get("top_k", 5), "top_k", 1, 20)
        llm = bool(options.get("llm", False))
        if operation == "query":
            query = _required_text(payload, "query")
            return self.service.query(query, top_k, llm).to_dict()
        if operation == "hardfault":
            raw_log = _required_text(payload, "log")
            return self.service.diagnose_hardfault(raw_log, top_k, llm).to_dict()
        if operation == "protocol_log":
            raw_log = _required_text(payload, "log")
            return self.service.diagnose_protocol_log(
                raw_log,
                profile=payload.get("profile", "auto"),
                expected_cycle_ms=payload.get("expected_cycle_ms"),
                jitter_tolerance_ms=payload.get("jitter_tolerance_ms"),
                expected_length=payload.get("expected_length"),
                crc16_algorithm=payload.get("crc16_algorithm", "none"),
                top_k=top_k,
                llm=llm,
            ).to_dict()
        if operation == "reindex":
            max_chars = _bounded_int(options.get("max_chars", 1200), "max_chars", 200, 10000)
            overlap = _bounded_int(options.get("overlap", 180), "overlap", 0, max_chars - 1)
            exit_code, result = self.service.ingest(max_chars, overlap)
            if exit_code != 0:
                raise WorkerRequestError(
                    "runtime_unavailable",
                    str(result.get("message") or "STM32F4 索引重建失败。"),
                    extra={
                        key: result[key]
                        for key in ("sources", "errors", "ignored")
                        if key in result
                    },
                )
            return result
        if operation == "evaluate":
            samples = _evaluation_samples(payload)
            result = evaluate_samples(
                samples,
                self.service.build_retriever(),
                top_k,
                progress_callback,
            )
            result["runtime_snapshot"] = self.health_snapshot()
            return result
        raise WorkerRequestError("unsupported_operation", f"不支持的 operation：{operation}。")

    def _emit_error(
        self,
        request_id: str,
        code: str,
        message: str,
        field: str | None = None,
        extra: dict[str, Any] | None = None,
    ) -> None:
        data: dict[str, Any] = {"code": code, "message": message}
        if field:
            data["field"] = field
        if extra:
            data.update(extra)
        self.writer.emit(request_id, "error", data)


class WorkerRequestError(ValueError):
    def __init__(
        self,
        code: str,
        message: str,
        field: str | None = None,
        extra: dict[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.field = field
        self.extra = extra


def _required_text(payload: dict[str, Any], field: str) -> str:
    value = payload.get(field)
    if not isinstance(value, str) or not value.strip():
        raise WorkerRequestError("validation_error", f"{field} 不能为空。", field)
    return value.strip()


def _evaluation_samples(payload: dict[str, Any]) -> list[dict[str, Any]]:
    value = payload.get("samples")
    if not isinstance(value, list) or not value:
        raise WorkerRequestError("validation_error", "samples 必须是非空数组。", "samples")
    if len(value) > 500:
        raise WorkerRequestError("validation_error", "samples 最多包含 500 个样本。", "samples")
    for index, sample in enumerate(value):
        if not isinstance(sample, dict):
            raise WorkerRequestError("validation_error", f"samples[{index}] 必须是 JSON object。", "samples")
        query = sample.get("query")
        if not isinstance(query, str) or not query.strip() or len(query) > 2000:
            raise WorkerRequestError(
                "validation_error",
                f"samples[{index}].query 必须是 1 到 2000 个字符。",
                "samples",
            )
        sample_id = sample.get("sample_id")
        if not isinstance(sample_id, str) or not sample_id.strip():
            raise WorkerRequestError(
                "validation_error",
                f"samples[{index}].sample_id 必须是非空字符串。",
                "samples",
            )
        expected = sample.get("expected_evidence")
        if not isinstance(expected, list) or not expected or len(expected) > 20:
            raise WorkerRequestError(
                "validation_error",
                f"samples[{index}].expected_evidence 必须包含 1 到 20 项。",
                "samples",
            )
        for evidence_index, reference in enumerate(expected):
            if not isinstance(reference, dict):
                raise WorkerRequestError(
                    "validation_error",
                    f"samples[{index}].expected_evidence[{evidence_index}] 必须是 JSON object。",
                    "samples",
                )
            chunk_id = _optional_text(reference.get("chunk_id"))
            doc_id = _optional_text(reference.get("doc_id"))
            page = _optional_text(reference.get("page"))
            source = _optional_text(reference.get("source"))
            section = _optional_text(reference.get("section"))
            if not (chunk_id or (doc_id and page) or (source and section) or source):
                raise WorkerRequestError(
                    "validation_error",
                    (
                        f"samples[{index}].expected_evidence[{evidence_index}] 必须包含 chunk_id、"
                        "doc_id + page、source + section 或 source。"
                    ),
                    "samples",
                )
    return value


def _optional_text(value: Any) -> str:
    if isinstance(value, str):
        return value.strip()
    return str(value).strip() if value is not None else ""


def _bounded_int(value: Any, field: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise WorkerRequestError(
            "validation_error",
            f"{field} 必须是 {minimum} 到 {maximum} 之间的整数。",
            field,
        )
    return value


def run_worker(service: STM32DiagnosticService) -> int:
    protocol_output = sys.stdout
    sys.stdout = sys.stderr
    return RagWorker(service, protocol_output).serve(sys.stdin)


def _available_input_bytes(file_descriptor: int) -> int:
    if os.name != "nt":
        return 4096
    import msvcrt

    available = ctypes.c_ulong(0)
    handle = msvcrt.get_osfhandle(file_descriptor)
    success = ctypes.windll.kernel32.PeekNamedPipe(
        ctypes.c_void_p(handle),
        None,
        0,
        None,
        ctypes.byref(available),
        None,
    )
    if not success:
        if ctypes.get_last_error() in (109, 232):
            return -1
        return 0
    return int(available.value)
