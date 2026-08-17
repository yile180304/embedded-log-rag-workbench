from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
from typing import Any


PROTOCOL_VERSION = 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="真实 RAG Worker 缓存与恢复 smoke")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--max-second-query-ratio", type=float, default=0.8)
    parser.add_argument("--allow-network", action="store_true")
    return parser.parse_args()


class WorkerClient:
    def __init__(self, root: Path, allow_network: bool) -> None:
        environment = os.environ.copy()
        environment.update({"PYTHONUTF8": "1", "PYTHONIOENCODING": "utf-8"})
        if not allow_network:
            environment.update({"HF_HUB_OFFLINE": "1", "TRANSFORMERS_OFFLINE": "1"})
        self.process = subprocess.Popen(
            [
                sys.executable,
                "-m",
                "rag_diagnostic",
                "--root",
                str(root),
                "--embedding",
                "bge",
                "--embedding-model",
                "BAAI/bge-small-zh-v1.5",
                "worker",
            ],
            cwd=root,
            env=environment,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        self.stderr_chunks: list[str] = []
        self.stderr_reader = threading.Thread(target=self._read_stderr, daemon=True)
        self.stderr_reader.start()

    @property
    def pid(self) -> int:
        return self.process.pid

    def _read_stderr(self) -> None:
        assert self.process.stderr is not None
        for chunk in self.process.stderr:
            self.stderr_chunks.append(chunk)

    def stderr_text(self) -> str:
        return "".join(self.stderr_chunks)

    def read_event(self) -> dict[str, Any]:
        assert self.process.stdout is not None
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError(f"worker 提前退出，exit={self.process.poll()}，stderr={self.stderr_text()[-1000:]}")
        event = json.loads(line)
        if event.get("protocol_version") != PROTOCOL_VERSION:
            raise RuntimeError(f"收到不支持的协议事件：{event}")
        return event

    def request(
        self,
        request_id: str,
        operation: str,
        payload: dict[str, Any] | None = None,
        options: dict[str, Any] | None = None,
    ) -> tuple[dict[str, Any], float]:
        assert self.process.stdin is not None
        request = {
            "protocol_version": PROTOCOL_VERSION,
            "request_id": request_id,
            "operation": operation,
            "payload": payload or {},
            "options": options or {},
        }
        started = time.perf_counter()
        self.process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
        self.process.stdin.flush()
        while True:
            event = self.read_event()
            if event.get("request_id") != request_id:
                continue
            if event.get("event") in {"result", "error"}:
                return event, time.perf_counter() - started

    def close(self) -> None:
        if self.process.poll() is not None:
            return
        try:
            event, _ = self.request("smoke-shutdown", "shutdown")
            if event.get("event") != "result":
                raise RuntimeError(f"shutdown 失败：{event}")
            self.process.wait(timeout=10)
        except Exception:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    client = WorkerClient(root, args.allow_network)
    original_pid = client.pid
    try:
        startup = client.read_event()
        require(startup.get("request_id") == "worker-startup", "缺少 worker-startup 事件")
        require(not startup["data"]["models"]["embedding"]["loaded"], "health 不应提前加载模型")

        bad_fault, _ = client.request(
            "bad-hardfault",
            "hardfault",
            {"log": "CFSR=oops"},
            {"top_k": 1},
        )
        require(bad_fault.get("event") == "error", "坏 HardFault 应返回 error")
        require(bad_fault["data"].get("code") == "validation_error", "坏 HardFault 错误码不正确")
        require(client.process.poll() is None and client.pid == original_pid, "校验错误不应终止 worker")

        first_stderr_offset = len(client.stderr_text())
        first, first_seconds = client.request(
            "query-first",
            "query",
            {"query": "CFSR 是什么？"},
            {"top_k": 1},
        )
        require(first.get("event") == "result" and first["data"].get("grounded"), "首次真实查询失败")
        time.sleep(0.1)
        first_stderr = client.stderr_text()[first_stderr_offset:]

        health, _ = client.request("health-after-first", "health")
        require(health["data"]["models"]["embedding"]["loaded"], "首次查询后 cache 应为 loaded")
        second_stderr_offset = len(client.stderr_text())
        second, second_seconds = client.request(
            "query-second",
            "query",
            {"query": "HFSR 的 FORCED 表示什么？"},
            {"top_k": 1},
        )
        require(second.get("event") == "result" and second["data"].get("grounded"), "第二次真实查询失败")
        time.sleep(0.1)
        second_stderr = client.stderr_text()[second_stderr_offset:]

        require(client.process.poll() is None and client.pid == original_pid, "两次查询必须复用同一 PID")
        require("Loading weights" in first_stderr, "首次查询没有观察到模型加载")
        require("Loading weights" not in second_stderr, "第二次查询重复加载了模型")
        require(
            second_seconds < first_seconds * args.max_second_query_ratio,
            f"第二次查询未达到性能门槛：first={first_seconds:.3f}s second={second_seconds:.3f}s",
        )

        hardfault_stderr_offset = len(client.stderr_text())
        hardfault, hardfault_seconds = client.request(
            "hardfault-after-query",
            "hardfault",
            {
                "log": (
                    "CFSR=0x00008200 HFSR=0x40000000 BFAR=0x2003FFF8 "
                    "PC=0x080126AC LR=0xFFFFFFF9"
                )
            },
            {"top_k": 1},
        )
        time.sleep(0.1)
        hardfault_stderr = client.stderr_text()[hardfault_stderr_offset:]
        require(hardfault.get("event") == "result", "query 后的 HardFault 诊断失败")
        require(
            hardfault["data"]["metadata"]["hardfault"]["registers"].get("CFSR") == "0x00008200",
            "HardFault 规则解码结果不正确",
        )
        require(client.process.poll() is None and client.pid == original_pid, "HardFault 必须继续复用同一 PID")
        require("Loading weights" not in hardfault_stderr, "query 后的 HardFault 重复加载了模型")

        print(json.dumps({
            "status": "ok",
            "pid": original_pid,
            "first_query_seconds": round(first_seconds, 3),
            "second_query_seconds": round(second_seconds, 3),
            "second_query_ratio": round(second_seconds / first_seconds, 3),
            "validation_error_recovered": True,
            "second_query_reloaded_models": False,
            "hardfault_seconds": round(hardfault_seconds, 3),
            "hardfault_reused_models": True,
        }, ensure_ascii=False, indent=2))
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
