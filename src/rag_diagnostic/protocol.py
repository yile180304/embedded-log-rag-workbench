from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
import re
from typing import Any


PROTOCOL_LOG_MAX_BYTES = 256 * 1024
PROTOCOL_LOG_MAX_LINES = 5000
PROTOCOL_LOG_PROFILES = {"auto", "generic", "udp", "trdp"}
CRC16_ALGORITHMS = {"none", "modbus", "ccitt_false"}

TIMESTAMP_PATTERN = re.compile(
    r"(?<!\d)(\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})?)"
)
KEY_VALUE_PATTERN = re.compile(
    r"\b([A-Za-z_][A-Za-z0-9_.-]*)\s*=\s*([^\s,;]+)",
    re.IGNORECASE,
)
KNOWN_NUMERIC_FIELDS = {
    "seq",
    "sequence",
    "sequence_counter",
    "len",
    "length",
    "dataset_length",
    "com_id",
    "reply_com_id",
}


@dataclass(frozen=True)
class ProtocolLogValidationError(ValueError):
    code: str
    message: str
    field: str | None = None

    def __str__(self) -> str:
        return self.message

    def to_dict(self) -> dict[str, str]:
        payload = {
            "status": "error",
            "type": type(self).__name__,
            "code": self.code,
            "message": self.message,
        }
        if self.field:
            payload["field"] = self.field
        return payload


@dataclass
class ProtocolLogEvent:
    line_no: int
    raw_message: str
    timestamp: str | None = None
    protocol: str = "GENERIC"
    source: str | None = None
    destination: str | None = None
    sequence: int | None = None
    length: int | None = None
    payload_length: int | None = None
    interval_ms: float | None = None
    frame: str | None = None
    provided_crc: str | None = None
    calculated_crc: str | None = None
    crc_valid: bool | None = None
    fields: dict[str, Any] = field(default_factory=dict)
    _timestamp_value: datetime | None = field(default=None, repr=False, compare=False)

    def to_dict(self) -> dict[str, Any]:
        return {
            "line_no": self.line_no,
            "timestamp": self.timestamp,
            "protocol": self.protocol,
            "source": self.source,
            "destination": self.destination,
            "sequence": self.sequence,
            "length": self.length,
            "payload_length": self.payload_length,
            "interval_ms": self.interval_ms,
            "frame": self.frame,
            "provided_crc": self.provided_crc,
            "calculated_crc": self.calculated_crc,
            "crc_valid": self.crc_valid,
            "fields": self.fields,
            "raw_message": self.raw_message,
        }


@dataclass
class ProtocolAnomaly:
    type: str
    severity: str
    message: str
    line_no: int | None = None
    details: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "type": self.type,
            "severity": self.severity,
            "message": self.message,
        }
        if self.line_no is not None:
            payload["line_no"] = self.line_no
        payload.update(self.details)
        return payload


@dataclass
class ProtocolLogAnalysis:
    profile: str
    summary: dict[str, Any]
    events: list[ProtocolLogEvent] = field(default_factory=list)
    anomalies: list[ProtocolAnomaly] = field(default_factory=list)
    config: dict[str, Any] = field(default_factory=dict)
    generated_query: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {
            "profile": self.profile,
            "summary": self.summary,
            "events": [event.to_dict() for event in self.events],
            "anomalies": [anomaly.to_dict() for anomaly in self.anomalies],
            "config": self.config,
            "generated_query": self.generated_query,
        }


def analyze_protocol_log(
    raw_log: str,
    *,
    profile: str = "auto",
    expected_cycle_ms: float | int | None = None,
    jitter_tolerance_ms: float | int | None = None,
    expected_length: int | None = None,
    crc16_algorithm: str = "none",
) -> ProtocolLogAnalysis:
    normalized_log = raw_log.strip()
    if not normalized_log:
        raise ProtocolLogValidationError(
            code="empty_log",
            message="协议日志为空，请粘贴离线应用日志、tshark 文本或显式十六进制帧。",
            field="log",
        )
    byte_count = len(raw_log.encode("utf-8"))
    if byte_count > PROTOCOL_LOG_MAX_BYTES:
        raise ProtocolLogValidationError(
            code="log_too_large",
            message=f"协议日志超过 {PROTOCOL_LOG_MAX_BYTES // 1024} KiB 上限。",
            field="log",
        )
    lines = normalized_log.splitlines()
    if len(lines) > PROTOCOL_LOG_MAX_LINES:
        raise ProtocolLogValidationError(
            code="too_many_lines",
            message=f"协议日志超过 {PROTOCOL_LOG_MAX_LINES} 行上限。",
            field="log",
        )

    selected_profile = _validate_choice(profile, PROTOCOL_LOG_PROFILES, "profile")
    selected_crc = _validate_choice(crc16_algorithm, CRC16_ALGORITHMS, "crc16_algorithm")
    cycle_ms = _validate_optional_number(expected_cycle_ms, "expected_cycle_ms", positive=True)
    tolerance_ms = _validate_optional_number(jitter_tolerance_ms, "jitter_tolerance_ms", positive=False)
    length_limit = _validate_expected_length(expected_length)
    if tolerance_ms is not None and cycle_ms is None:
        raise ProtocolLogValidationError(
            code="missing_expected_cycle",
            message="设置 jitter_tolerance_ms 时必须同时设置 expected_cycle_ms。",
            field="expected_cycle_ms",
        )

    events: list[ProtocolLogEvent] = []
    anomalies: list[ProtocolAnomaly] = []
    complete_crc_pairs = 0
    for line_no, raw_line in enumerate(lines, start=1):
        line = raw_line.strip()
        if not line:
            continue
        event, line_anomalies, has_crc_pair = _parse_event(line_no, line, selected_profile, selected_crc)
        events.append(event)
        anomalies.extend(line_anomalies)
        complete_crc_pairs += int(has_crc_pair)

    if selected_crc != "none" and complete_crc_pairs == 0:
        raise ProtocolLogValidationError(
            code="missing_frame_crc",
            message="启用 CRC16 校验后，日志中至少需要一行同时提供 FRAME=<hex> 和 CRC=<hex>。",
            field="log",
        )

    _apply_sequence_and_interval_rules(events, anomalies, cycle_ms, tolerance_ms)
    _apply_length_rules(events, anomalies, length_limit)
    resolved_profile = _resolve_profile(selected_profile, events)
    summary = _build_summary(lines, byte_count, events, anomalies)
    config = {
        "requested_profile": selected_profile,
        "expected_cycle_ms": cycle_ms,
        "jitter_tolerance_ms": tolerance_ms,
        "expected_length": length_limit,
        "crc16_algorithm": selected_crc,
    }
    return ProtocolLogAnalysis(
        profile=resolved_profile,
        summary=summary,
        events=events,
        anomalies=anomalies,
        config=config,
        generated_query=_build_generated_query(resolved_profile, anomalies),
    )


def _validate_choice(value: str, allowed: set[str], field_name: str) -> str:
    if not isinstance(value, str):
        raise ProtocolLogValidationError(
            code=f"invalid_{field_name}",
            message=f"{field_name} 必须是字符串。",
            field=field_name,
        )
    normalized = value.strip().lower()
    if normalized not in allowed:
        choices = ", ".join(sorted(allowed))
        raise ProtocolLogValidationError(
            code=f"invalid_{field_name}",
            message=f"{field_name} 仅支持：{choices}。",
            field=field_name,
        )
    return normalized


def _validate_optional_number(
    value: float | int | None,
    field_name: str,
    *,
    positive: bool,
) -> float | None:
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ProtocolLogValidationError(
            code=f"invalid_{field_name}",
            message=f"{field_name} 必须是数值。",
            field=field_name,
        )
    number = float(value)
    invalid = number <= 0 if positive else number < 0
    if invalid:
        comparator = "大于 0" if positive else "大于等于 0"
        raise ProtocolLogValidationError(
            code=f"invalid_{field_name}",
            message=f"{field_name} 必须{comparator}。",
            field=field_name,
        )
    return number


def _validate_expected_length(value: int | None) -> int | None:
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ProtocolLogValidationError(
            code="invalid_expected_length",
            message="expected_length 必须是大于等于 0 的整数。",
            field="expected_length",
        )
    return value


def _parse_event(
    line_no: int,
    line: str,
    requested_profile: str,
    crc16_algorithm: str,
) -> tuple[ProtocolLogEvent, list[ProtocolAnomaly], bool]:
    pairs = {key.lower(): value for key, value in KEY_VALUE_PATTERN.findall(line)}
    anomalies: list[ProtocolAnomaly] = []
    timestamp, timestamp_value = _parse_timestamp(line, line_no, anomalies)
    protocol = _detect_protocol(requested_profile, line, pairs)
    event = ProtocolLogEvent(
        line_no=line_no,
        raw_message=line,
        timestamp=timestamp,
        protocol=protocol,
        source=_first_value(pairs, "src", "source"),
        destination=_first_value(pairs, "dst", "destination"),
        _timestamp_value=timestamp_value,
    )

    event.sequence = _parse_first_integer(pairs, ("sequence_counter", "sequence", "seq"), line_no, anomalies)
    event.length = _parse_first_integer(pairs, ("dataset_length", "length", "len"), line_no, anomalies)
    event.fields = _parse_protocol_fields(pairs, line_no, anomalies)
    _record_missing_assignments(line, pairs, line_no, anomalies)

    payload = pairs.get("payload")
    if payload is not None:
        payload_bytes = _parse_hex_bytes(payload, "PAYLOAD", line_no)
        event.payload_length = len(payload_bytes)

    frame_value = pairs.get("frame")
    crc_value = pairs.get("crc")
    if frame_value is not None:
        frame_bytes = _parse_hex_bytes(frame_value, "FRAME", line_no)
        event.frame = frame_bytes.hex().upper()
    else:
        frame_bytes = None
    if crc_value is not None:
        provided_crc = _parse_crc(crc_value, line_no)
        event.provided_crc = f"0x{provided_crc:04X}"
    else:
        provided_crc = None

    has_crc_pair = frame_bytes is not None and provided_crc is not None
    if crc16_algorithm != "none" and (frame_bytes is not None or provided_crc is not None) and not has_crc_pair:
        missing_field = "CRC" if provided_crc is None else "FRAME"
        raise ProtocolLogValidationError(
            code="missing_crc_field",
            message=f"第 {line_no} 行启用了 CRC16 校验，但缺少 {missing_field}=<hex>。",
            field=missing_field.lower(),
        )
    if has_crc_pair and crc16_algorithm != "none":
        calculated = _calculate_crc16(frame_bytes, crc16_algorithm)
        event.calculated_crc = f"0x{calculated:04X}"
        event.crc_valid = calculated == provided_crc
        if not event.crc_valid:
            anomalies.append(
                ProtocolAnomaly(
                    type="crc_mismatch",
                    severity="error",
                    line_no=line_no,
                    message=f"CRC16 校验失败：提供 {event.provided_crc}，计算得到 {event.calculated_crc}。",
                    details={
                        "algorithm": crc16_algorithm,
                        "provided_crc": event.provided_crc,
                        "calculated_crc": event.calculated_crc,
                    },
                )
            )
    return event, anomalies, has_crc_pair


def _parse_timestamp(
    line: str,
    line_no: int,
    anomalies: list[ProtocolAnomaly],
) -> tuple[str | None, datetime | None]:
    match = TIMESTAMP_PATTERN.search(line)
    if match is None:
        return None, None
    raw_timestamp = match.group(1)
    try:
        return raw_timestamp, datetime.fromisoformat(raw_timestamp.replace("Z", "+00:00"))
    except ValueError:
        anomalies.append(
            ProtocolAnomaly(
                type="field_parse_error",
                severity="warning",
                line_no=line_no,
                message=f"时间戳 {raw_timestamp} 无法解析，已保留该行其他字段。",
                details={"field": "timestamp", "value": raw_timestamp},
            )
        )
        return raw_timestamp, None


def _detect_protocol(requested_profile: str, line: str, pairs: dict[str, str]) -> str:
    if requested_profile != "auto":
        return requested_profile.upper()
    upper_line = line.upper()
    if "TRDP" in upper_line or any(
        field in pairs for field in ("com_id", "reply_com_id", "dataset_length", "sequence_counter")
    ):
        return "TRDP"
    if "UDP" in upper_line or any(field in pairs for field in ("src", "dst", "source", "destination")):
        return "UDP"
    return "GENERIC"


def _first_value(pairs: dict[str, str], *keys: str) -> str | None:
    for key in keys:
        if key in pairs:
            return pairs[key]
    return None


def _parse_first_integer(
    pairs: dict[str, str],
    keys: tuple[str, ...],
    line_no: int,
    anomalies: list[ProtocolAnomaly],
) -> int | None:
    for key in keys:
        if key in pairs:
            return _parse_integer_field(key, pairs[key], line_no, anomalies)
    return None


def _parse_protocol_fields(
    pairs: dict[str, str],
    line_no: int,
    anomalies: list[ProtocolAnomaly],
) -> dict[str, Any]:
    fields: dict[str, Any] = {}
    for key in ("com_id", "reply_com_id"):
        if key in pairs:
            fields[key] = _parse_integer_field(key, pairs[key], line_no, anomalies)
    for key in ("msg_type",):
        if key in pairs:
            fields[key] = pairs[key]
    if "dataset_length" in pairs:
        fields["dataset_length"] = _parse_integer_field(
            "dataset_length", pairs["dataset_length"], line_no, anomalies
        )
    if "sequence_counter" in pairs:
        fields["sequence_counter"] = _parse_integer_field(
            "sequence_counter", pairs["sequence_counter"], line_no, anomalies
        )
    return {key: value for key, value in fields.items() if value is not None}


def _parse_integer_field(
    field_name: str,
    raw_value: str,
    line_no: int,
    anomalies: list[ProtocolAnomaly],
) -> int | None:
    try:
        value = int(raw_value, 0)
    except ValueError:
        if raw_value.isdigit():
            value = int(raw_value, 10)
        else:
            anomalies.append(
                ProtocolAnomaly(
                    type="field_parse_error",
                    severity="warning",
                    line_no=line_no,
                    message=f"字段 {field_name}={raw_value} 不是合法的非负整数，已保留该行其他字段。",
                    details={"field": field_name, "value": raw_value},
                )
            )
            return None
    if value < 0:
        anomalies.append(
            ProtocolAnomaly(
                type="field_parse_error",
                severity="warning",
                line_no=line_no,
                message=f"字段 {field_name}={raw_value} 不能为负数，已保留该行其他字段。",
                details={"field": field_name, "value": raw_value},
            )
        )
        return None
    return value


def _record_missing_assignments(
    line: str,
    pairs: dict[str, str],
    line_no: int,
    anomalies: list[ProtocolAnomaly],
) -> None:
    for field_name in KNOWN_NUMERIC_FIELDS:
        if field_name in pairs:
            continue
        if re.search(rf"\b{re.escape(field_name)}\s*=\s*(?:[,;]|$)", line, re.IGNORECASE):
            anomalies.append(
                ProtocolAnomaly(
                    type="missing_field",
                    severity="warning",
                    line_no=line_no,
                    message=f"字段 {field_name} 缺少值，已保留该行其他字段。",
                    details={"field": field_name},
                )
            )


def _parse_hex_bytes(raw_value: str, field_name: str, line_no: int) -> bytes:
    normalized = raw_value[2:] if raw_value.lower().startswith("0x") else raw_value
    if not normalized or len(normalized) % 2 != 0 or re.fullmatch(r"[0-9a-fA-F]+", normalized) is None:
        raise ProtocolLogValidationError(
            code="invalid_hex",
            message=f"第 {line_no} 行的 {field_name} 必须是偶数位十六进制字节串。",
            field=field_name.lower(),
        )
    return bytes.fromhex(normalized)


def _parse_crc(raw_value: str, line_no: int) -> int:
    normalized = raw_value[2:] if raw_value.lower().startswith("0x") else raw_value
    if not normalized or len(normalized) > 4 or re.fullmatch(r"[0-9a-fA-F]+", normalized) is None:
        raise ProtocolLogValidationError(
            code="invalid_crc",
            message=f"第 {line_no} 行的 CRC 必须是 1 到 4 位十六进制数。",
            field="crc",
        )
    return int(normalized, 16)


def _calculate_crc16(data: bytes, algorithm: str) -> int:
    if algorithm == "modbus":
        crc = 0xFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
        return crc & 0xFFFF
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _apply_sequence_and_interval_rules(
    events: list[ProtocolLogEvent],
    anomalies: list[ProtocolAnomaly],
    expected_cycle_ms: float | None,
    jitter_tolerance_ms: float | None,
) -> None:
    previous_by_flow: dict[tuple[str, str, str, Any], ProtocolLogEvent] = {}
    tolerance = jitter_tolerance_ms or 0.0
    for event in events:
        flow_key = (
            event.protocol,
            event.source or "",
            event.destination or "",
            event.fields.get("com_id"),
        )
        previous = previous_by_flow.get(flow_key)
        if previous is not None:
            _compare_sequence(previous, event, anomalies)
            _compare_interval(previous, event, anomalies, expected_cycle_ms, tolerance)
        previous_by_flow[flow_key] = event


def _compare_sequence(
    previous: ProtocolLogEvent,
    current: ProtocolLogEvent,
    anomalies: list[ProtocolAnomaly],
) -> None:
    if previous.sequence is None or current.sequence is None:
        return
    if current.sequence > previous.sequence + 1:
        missing_count = current.sequence - previous.sequence - 1
        missing = list(range(previous.sequence + 1, min(current.sequence, previous.sequence + 257)))
        anomalies.append(
            ProtocolAnomaly(
                type="sequence_gap",
                severity="warning",
                line_no=current.line_no,
                message=f"序列从 {previous.sequence} 跳到 {current.sequence}，中间缺少 {missing_count} 个值。",
                details={
                    "previous_line_no": previous.line_no,
                    "previous_sequence": previous.sequence,
                    "current_sequence": current.sequence,
                    "missing": missing,
                    "missing_count": missing_count,
                },
            )
        )
    elif current.sequence <= previous.sequence:
        anomalies.append(
            ProtocolAnomaly(
                type="sequence_reorder",
                severity="warning",
                line_no=current.line_no,
                message=f"序列从 {previous.sequence} 变为 {current.sequence}，可能是乱序、重复或计数器复位。",
                details={
                    "previous_line_no": previous.line_no,
                    "previous_sequence": previous.sequence,
                    "current_sequence": current.sequence,
                },
            )
        )


def _compare_interval(
    previous: ProtocolLogEvent,
    current: ProtocolLogEvent,
    anomalies: list[ProtocolAnomaly],
    expected_cycle_ms: float | None,
    tolerance_ms: float,
) -> None:
    if previous._timestamp_value is None or current._timestamp_value is None:
        return
    try:
        interval_ms = (current._timestamp_value - previous._timestamp_value).total_seconds() * 1000.0
    except TypeError:
        anomalies.append(
            ProtocolAnomaly(
                type="field_parse_error",
                severity="warning",
                line_no=current.line_no,
                message="相邻时间戳的时区格式不一致，无法计算周期。",
                details={"field": "timestamp", "previous_line_no": previous.line_no},
            )
        )
        return
    current.interval_ms = round(interval_ms, 3)
    if expected_cycle_ms is None or abs(interval_ms - expected_cycle_ms) <= tolerance_ms:
        return
    anomalies.append(
        ProtocolAnomaly(
            type="interval_outlier",
            severity="warning",
            line_no=current.line_no,
            message=(
                f"观测周期 {interval_ms:.3f} ms 超出期望 {expected_cycle_ms:.3f} ms "
                f"± {tolerance_ms:.3f} ms。"
            ),
            details={
                "previous_line_no": previous.line_no,
                "observed_ms": round(interval_ms, 3),
                "expected_ms": expected_cycle_ms,
                "tolerance_ms": tolerance_ms,
            },
        )
    )


def _apply_length_rules(
    events: list[ProtocolLogEvent],
    anomalies: list[ProtocolAnomaly],
    expected_length: int | None,
) -> None:
    for event in events:
        if event.length is not None and event.payload_length is not None and event.length != event.payload_length:
            anomalies.append(
                ProtocolAnomaly(
                    type="length_mismatch",
                    severity="warning",
                    line_no=event.line_no,
                    message=f"声明长度 {event.length} 与 PAYLOAD 字节数 {event.payload_length} 不一致。",
                    details={
                        "reason": "declared_vs_payload",
                        "declared_length": event.length,
                        "payload_length": event.payload_length,
                    },
                )
            )
        if expected_length is not None and event.length is not None and event.length != expected_length:
            anomalies.append(
                ProtocolAnomaly(
                    type="length_mismatch",
                    severity="warning",
                    line_no=event.line_no,
                    message=f"报文长度 {event.length} 与显式期望长度 {expected_length} 不一致。",
                    details={
                        "reason": "expected_length",
                        "observed_length": event.length,
                        "expected_length": expected_length,
                    },
                )
            )


def _resolve_profile(requested_profile: str, events: list[ProtocolLogEvent]) -> str:
    if requested_profile != "auto":
        return requested_profile
    protocols = {event.protocol for event in events}
    if "TRDP" in protocols:
        return "trdp"
    if "UDP" in protocols:
        return "udp"
    return "generic"


def _build_summary(
    lines: list[str],
    byte_count: int,
    events: list[ProtocolLogEvent],
    anomalies: list[ProtocolAnomaly],
) -> dict[str, Any]:
    observed_intervals = [event.interval_ms for event in events if event.interval_ms is not None]
    summary: dict[str, Any] = {
        "line_count": len(lines),
        "byte_count": byte_count,
        "event_count": len(events),
        "error_count": sum(anomaly.severity == "error" for anomaly in anomalies),
        "warning_count": sum(anomaly.severity == "warning" for anomaly in anomalies),
    }
    if observed_intervals:
        summary["observed_interval_ms"] = {
            "min": round(min(observed_intervals), 3),
            "max": round(max(observed_intervals), 3),
            "average": round(sum(observed_intervals) / len(observed_intervals), 3),
        }
    return summary


def _build_generated_query(profile: str, anomalies: list[ProtocolAnomaly]) -> str:
    if not anomalies:
        return f"{profile.upper()} 协议日志中序列号、周期、长度和 CRC16 应如何进行防御性排查？"
    anomaly_types = list(dict.fromkeys(anomaly.type for anomaly in anomalies))
    details: list[str] = []
    for anomaly in anomalies:
        if anomaly.type == "sequence_gap":
            missing = anomaly.details.get("missing", [])
            if missing:
                details.append("缺失序列 " + ",".join(str(value) for value in missing[:8]))
        elif anomaly.type == "crc_mismatch":
            details.append(
                f"CRC 提供值 {anomaly.details.get('provided_crc')}、计算值 {anomaly.details.get('calculated_crc')}"
            )
    suffix = "；".join(details)
    if suffix:
        suffix = "；" + suffix
    return f"{profile.upper()} 协议日志检测到 {', '.join(anomaly_types)}{suffix}，应如何定位原因并给出防御性排查步骤？"
