from __future__ import annotations

import io
import json
import unittest
from pathlib import Path
from unittest.mock import patch

from rag_diagnostic.models import DiagnosisResult
from rag_diagnostic.protocol import (
    PROTOCOL_LOG_MAX_BYTES,
    PROTOCOL_LOG_MAX_LINES,
    ProtocolLogValidationError,
    analyze_protocol_log,
)
from rag_diagnostic.runtime import STM32DiagnosticService, STM32RuntimeOptions
from rag_diagnostic.runtime.worker import RagWorker
from rag_diagnostic.reporting import render_protocol_markdown


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class ProtocolLogTest(unittest.TestCase):
    def test_udp_gap_and_observed_interval_are_structured(self) -> None:
        analysis = analyze_protocol_log(
            "\n".join(
                [
                    "2026-08-15T10:00:00.000Z UDP src=10.0.0.1:1000 dst=10.0.0.2:2000 seq=40 len=64",
                    "2026-08-15T10:00:00.025Z UDP src=10.0.0.1:1000 dst=10.0.0.2:2000 seq=42 len=64",
                ]
            )
        )

        payload = analysis.to_dict()
        self.assertEqual(payload["profile"], "udp")
        self.assertEqual(payload["summary"]["event_count"], 2)
        self.assertEqual(payload["events"][1]["interval_ms"], 25.0)
        gap = next(item for item in payload["anomalies"] if item["type"] == "sequence_gap")
        self.assertEqual(gap["missing"], [41])
        self.assertEqual(gap["missing_count"], 1)
        self.assertIn("缺失序列 41", payload["generated_query"])

    def test_interval_requires_explicit_threshold(self) -> None:
        log = "\n".join(
            [
                "2026-08-15T10:00:00.000Z UDP seq=1 len=64",
                "2026-08-15T10:00:00.025Z UDP seq=2 len=64",
            ]
        )
        observed_only = analyze_protocol_log(log)
        self.assertFalse(any(item.type == "interval_outlier" for item in observed_only.anomalies))

        checked = analyze_protocol_log(log, expected_cycle_ms=10, jitter_tolerance_ms=2)
        outlier = next(item for item in checked.to_dict()["anomalies"] if item["type"] == "interval_outlier")
        self.assertEqual(outlier["observed_ms"], 25.0)
        self.assertEqual(outlier["expected_ms"], 10.0)

    def test_sequence_reorder_does_not_claim_packet_loss(self) -> None:
        analysis = analyze_protocol_log("UDP seq=9 len=8\nUDP seq=7 len=8")
        anomaly = next(item for item in analysis.anomalies if item.type == "sequence_reorder")
        self.assertIn("乱序、重复或计数器复位", anomaly.message)
        self.assertNotIn("丢包", anomaly.message)

    def test_length_rules_compare_payload_and_explicit_expected_length(self) -> None:
        analysis = analyze_protocol_log(
            "UDP seq=1 len=4 PAYLOAD=010203",
            expected_length=8,
        )
        mismatches = [item.to_dict() for item in analysis.anomalies if item.type == "length_mismatch"]
        self.assertEqual({item["reason"] for item in mismatches}, {"declared_vs_payload", "expected_length"})

    def test_modbus_crc16_known_vector_passes(self) -> None:
        analysis = analyze_protocol_log(
            "FRAME=01030000000A CRC=0xCDC5",
            crc16_algorithm="modbus",
        )
        event = analysis.events[0]
        self.assertTrue(event.crc_valid)
        self.assertEqual(event.calculated_crc, "0xCDC5")
        self.assertFalse(any(item.type == "crc_mismatch" for item in analysis.anomalies))

    def test_crc_fields_are_not_checked_without_explicit_algorithm(self) -> None:
        analysis = analyze_protocol_log("FRAME=01030000000A CRC=0x0000")
        event = analysis.events[0]
        self.assertIsNone(event.calculated_crc)
        self.assertIsNone(event.crc_valid)
        self.assertFalse(any(item.type == "crc_mismatch" for item in analysis.anomalies))

    def test_ccitt_false_crc16_known_vector_and_mismatch(self) -> None:
        valid = analyze_protocol_log(
            "FRAME=313233343536373839 CRC=29B1",
            crc16_algorithm="ccitt_false",
        )
        self.assertTrue(valid.events[0].crc_valid)

        invalid = analyze_protocol_log(
            "FRAME=313233343536373839 CRC=0000",
            crc16_algorithm="ccitt_false",
        )
        mismatch = next(item for item in invalid.anomalies if item.type == "crc_mismatch")
        self.assertEqual(mismatch.severity, "error")
        self.assertEqual(mismatch.details["calculated_crc"], "0x29B1")

    def test_crc_validation_never_guesses_missing_fields_or_bad_hex(self) -> None:
        cases = [
            ("UDP seq=1", "missing_frame_crc", "log"),
            ("FRAME=0102", "missing_crc_field", "crc"),
            ("FRAME=0102 CRC=", "missing_crc_field", "crc"),
            ("FRAME=010 CRC=1234", "invalid_hex", "frame"),
            ("FRAME=0102 CRC=12345", "invalid_crc", "crc"),
        ]
        for log, code, field in cases:
            with self.subTest(log=log):
                with self.assertRaises(ProtocolLogValidationError) as caught:
                    analyze_protocol_log(log, crc16_algorithm="modbus")
                self.assertEqual(caught.exception.code, code)
                self.assertEqual(caught.exception.field, field)

    def test_trdp_profile_only_extracts_explicit_text_fields(self) -> None:
        analysis = analyze_protocol_log(
            "TRDP com_id=1001 msg_type=PD dataset_length=32 sequence_counter=7 reply_com_id=1002"
        )
        event = analysis.events[0].to_dict()
        self.assertEqual(analysis.profile, "trdp")
        self.assertEqual(event["protocol"], "TRDP")
        self.assertEqual(event["sequence"], 7)
        self.assertEqual(event["length"], 32)
        self.assertEqual(
            event["fields"],
            {
                "com_id": 1001,
                "reply_com_id": 1002,
                "msg_type": "PD",
                "dataset_length": 32,
                "sequence_counter": 7,
            },
        )

    def test_bad_field_keeps_other_fields_and_event(self) -> None:
        analysis = analyze_protocol_log("UDP seq=oops len=64\nUDP seq=2 len=")
        self.assertEqual(len(analysis.events), 2)
        self.assertEqual(analysis.events[0].length, 64)
        self.assertTrue(any(item.type == "field_parse_error" for item in analysis.anomalies))
        self.assertTrue(any(item.type == "missing_field" for item in analysis.anomalies))

    def test_input_and_configuration_limits_fail_before_analysis(self) -> None:
        cases = [
            ("", {}, "empty_log", "log"),
            ("x" * (PROTOCOL_LOG_MAX_BYTES + 1), {}, "log_too_large", "log"),
            ("\n".join("x" for _ in range(PROTOCOL_LOG_MAX_LINES + 1)), {}, "too_many_lines", "log"),
            ("UDP seq=1", {"profile": "pcap"}, "invalid_profile", "profile"),
            ("UDP seq=1", {"expected_cycle_ms": 0}, "invalid_expected_cycle_ms", "expected_cycle_ms"),
            ("UDP seq=1", {"jitter_tolerance_ms": 2}, "missing_expected_cycle", "expected_cycle_ms"),
            ("UDP len=1", {"expected_length": 1.5}, "invalid_expected_length", "expected_length"),
            ("FRAME=0102 CRC=1234", {"crc16_algorithm": "auto"}, "invalid_crc16_algorithm", "crc16_algorithm"),
        ]
        for log, kwargs, code, field in cases:
            with self.subTest(code=code):
                with self.assertRaises(ProtocolLogValidationError) as caught:
                    analyze_protocol_log(log, **kwargs)
                self.assertEqual(caught.exception.code, code)
                self.assertEqual(caught.exception.field, field)

    def test_different_udp_flows_are_not_compared(self) -> None:
        analysis = analyze_protocol_log(
            "UDP src=10.0.0.1:1 dst=10.0.0.2:2 seq=1\n"
            "UDP src=10.0.0.3:1 dst=10.0.0.4:2 seq=10"
        )
        self.assertFalse(any(item.type.startswith("sequence_") for item in analysis.anomalies))

    def test_service_attaches_protocol_analysis_to_existing_result_shape(self) -> None:
        service = STM32DiagnosticService(STM32RuntimeOptions(root=PROJECT_ROOT))
        retrieval_result = DiagnosisResult(
            query="placeholder",
            answer="当前知识库无法确认。",
            grounded=False,
            refusal_reason="no_evidence",
        )
        with patch.object(service, "_diagnose_query", return_value=retrieval_result) as diagnose_query, \
             patch("rag_diagnostic.runtime.stm32.write_protocol_report", return_value=PROJECT_ROOT / "reports" / "protocol.md"):
            result = service.diagnose_protocol_log(
                "UDP seq=40 len=64\nUDP seq=42 len=64",
                profile="udp",
                top_k=3,
            )

        generated_query = diagnose_query.call_args.args[0]
        self.assertIn("sequence_gap", generated_query)
        self.assertEqual(diagnose_query.call_args.args[1:], (3, False))
        self.assertFalse(result.grounded)
        self.assertEqual(result.refusal_reason, "no_evidence")
        self.assertEqual(result.metadata["protocol_log"]["anomalies"][0]["missing"], [41])
        self.assertTrue(result.metadata["report_path"].endswith("reports\\protocol.md")
                        or result.metadata["report_path"].endswith("reports/protocol.md"))

    def test_service_validation_happens_before_retriever_load(self) -> None:
        service = STM32DiagnosticService(STM32RuntimeOptions(root=PROJECT_ROOT))
        with patch.object(service, "build_retriever", side_effect=AssertionError("cache should not load")):
            with self.assertRaises(ProtocolLogValidationError):
                service.diagnose_protocol_log("UDP seq=1", expected_cycle_ms=0)
        self.assertFalse(service.cache_loaded)

    def test_protocol_report_separates_rule_facts_from_rag_evidence(self) -> None:
        analysis = analyze_protocol_log(
            "2026-08-15T10:00:00.000Z UDP seq=40 len=64\n"
            "2026-08-15T10:00:00.025Z UDP seq=42 len=64",
            expected_cycle_ms=10,
            jitter_tolerance_ms=2,
        )
        result = DiagnosisResult(
            query=analysis.generated_query,
            answer="当前知识库无法确认协议条款。",
            grounded=False,
            refusal_reason="no_evidence",
            metadata={"protocol_log": analysis.to_dict(), "embedding": "test", "reranker": "test"},
        )
        report = render_protocol_markdown(result)
        self.assertIn("## Protocol Log Events", report)
        self.assertIn("`sequence_gap`", report)
        self.assertIn("## 自动检索问题", report)
        self.assertIn("是否有证据支撑：`否`", report)

    def test_worker_protocol_log_operation_returns_diagnosis_result(self) -> None:
        output = io.StringIO()
        service = STM32DiagnosticService(STM32RuntimeOptions(root=PROJECT_ROOT))
        worker = RagWorker(service, output)
        result = DiagnosisResult(
            query="UDP sequence gap",
            answer="无法根据当前知识库确认。",
            grounded=False,
            refusal_reason="no_evidence",
            metadata={"protocol_log": {"profile": "udp", "events": [], "anomalies": []}},
        )
        with patch.object(service, "diagnose_protocol_log", return_value=result) as diagnose_protocol:
            worker.handle_line(json.dumps({
                "protocol_version": 1,
                "request_id": "protocol-1",
                "operation": "protocol_log",
                "payload": {
                    "log": "UDP seq=1 len=64",
                    "profile": "udp",
                    "expected_cycle_ms": 10,
                    "jitter_tolerance_ms": 2,
                    "expected_length": 64,
                    "crc16_algorithm": "none",
                },
                "options": {"top_k": 3, "llm": False},
            }))
            worker._run_heavy_operation(*worker._heavy_tasks.get())

        events = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual([event["event"] for event in events], ["accepted", "result"])
        self.assertEqual(events[-1]["data"]["metadata"]["protocol_log"]["profile"], "udp")
        diagnose_protocol.assert_called_once_with(
            "UDP seq=1 len=64",
            profile="udp",
            expected_cycle_ms=10,
            jitter_tolerance_ms=2,
            expected_length=64,
            crc16_algorithm="none",
            top_k=3,
            llm=False,
        )

    def test_worker_protocol_validation_error_keeps_process_and_cache_alive(self) -> None:
        output = io.StringIO()
        service = STM32DiagnosticService(STM32RuntimeOptions(root=PROJECT_ROOT))
        worker = RagWorker(service, output)
        with patch.object(service, "build_retriever", side_effect=AssertionError("cache should not load")):
            worker.handle_line(json.dumps({
                "protocol_version": 1,
                "request_id": "bad-protocol",
                "operation": "protocol_log",
                "payload": {"log": "FRAME=0102", "crc16_algorithm": "modbus"},
            }))
            worker._run_heavy_operation(*worker._heavy_tasks.get())

        events = [json.loads(line) for line in output.getvalue().splitlines()]
        error = next(event for event in events if event["event"] == "error")
        self.assertEqual(error["data"]["code"], "validation_error")
        self.assertEqual(error["data"]["field"], "crc")
        self.assertEqual(error["data"]["validation_code"], "missing_crc_field")
        self.assertFalse(service.cache_loaded)


if __name__ == "__main__":
    unittest.main()
