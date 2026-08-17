from __future__ import annotations

from dataclasses import dataclass
import re

from rag_diagnostic.models import DecodedFaultFlag, HardFaultAnalysis


REGISTER_NAMES = ("CFSR", "HFSR", "BFAR", "MMFAR", "PC", "LR")
REGISTER_PATTERN = re.compile(
    rf"\b({'|'.join(REGISTER_NAMES)})\b\s*[:=]\s*([^\s,;]*)",
    re.IGNORECASE,
)

CFSR_FLAGS = (
    (0, "IACCVIOL", "MemManage", "取指访问违反存储器保护规则"),
    (1, "DACCVIOL", "MemManage", "数据访问违反存储器保护规则"),
    (3, "MUNSTKERR", "MemManage", "异常返回出栈时发生存储器管理错误"),
    (4, "MSTKERR", "MemManage", "异常进入压栈时发生存储器管理错误"),
    (5, "MLSPERR", "MemManage", "浮点惰性状态保存时发生存储器管理错误"),
    (7, "MMARVALID", "MemManage", "MMFAR 保存了有效的故障访问地址"),
    (8, "IBUSERR", "BusFault", "取指总线访问发生错误"),
    (9, "PRECISERR", "BusFault", "发生可精确定位的数据总线访问错误"),
    (10, "IMPRECISERR", "BusFault", "发生无法精确定位的数据总线访问错误"),
    (11, "UNSTKERR", "BusFault", "异常返回出栈时发生总线错误"),
    (12, "STKERR", "BusFault", "异常进入压栈时发生总线错误"),
    (13, "LSPERR", "BusFault", "浮点惰性状态保存时发生总线错误"),
    (15, "BFARVALID", "BusFault", "BFAR 保存了有效的故障访问地址"),
    (16, "UNDEFINSTR", "UsageFault", "执行了未定义指令"),
    (17, "INVSTATE", "UsageFault", "处理器进入了非法状态"),
    (18, "INVPC", "UsageFault", "异常返回时加载了非法 PC"),
    (19, "NOCP", "UsageFault", "尝试使用不可用的协处理器"),
    (24, "UNALIGNED", "UsageFault", "发生未对齐访问"),
    (25, "DIVBYZERO", "UsageFault", "发生除零运算"),
)

HFSR_FLAGS = (
    (1, "VECTTBL", "HardFault", "读取异常向量表时发生总线错误"),
    (30, "FORCED", "HardFault", "可配置 Fault 被升级为 HardFault"),
    (31, "DEBUGEVT", "HardFault", "调试事件触发了 HardFault"),
)


@dataclass(frozen=True)
class HardFaultValidationError(ValueError):
    code: str
    message: str
    field: str | None = None

    def __str__(self) -> str:
        return self.message

    def to_dict(self) -> dict[str, str]:
        payload = {"status": "error", "type": type(self).__name__, "code": self.code, "message": self.message}
        if self.field:
            payload["field"] = self.field
        return payload


def analyze_hardfault_log(raw_log: str) -> HardFaultAnalysis:
    normalized_log = raw_log.strip()
    if not normalized_log:
        raise HardFaultValidationError(
            code="empty_log",
            message="HardFault 日志为空，请粘贴 CFSR 或 HFSR 等故障现场。",
            field="log",
        )

    parsed: dict[str, int] = {}
    for match in REGISTER_PATTERN.finditer(normalized_log):
        name = match.group(1).upper()
        value = _parse_register_value(name, match.group(2))
        if name in parsed and parsed[name] != value:
            raise HardFaultValidationError(
                code="conflicting_register",
                message=f"{name} 在日志中出现了冲突值：0x{parsed[name]:08X} 与 0x{value:08X}。",
                field=name,
            )
        parsed[name] = value

    if not parsed:
        raise HardFaultValidationError(
            code="no_registers",
            message="日志中没有识别到 CFSR/HFSR/BFAR/MMFAR/PC/LR。",
            field="log",
        )
    if "CFSR" not in parsed and "HFSR" not in parsed:
        raise HardFaultValidationError(
            code="missing_fault_status",
            message="已识别到现场寄存器，但缺少 CFSR 或 HFSR，无法判断 Fault 类型。",
            field="CFSR/HFSR",
        )

    registers = {name: parsed[name] for name in REGISTER_NAMES if name in parsed}
    flags = _decode_flags(registers)
    analysis = HardFaultAnalysis(
        raw_log=normalized_log,
        registers=registers,
        decoded_flags=flags,
    )
    analysis.observations = _build_observations(registers, flags)
    analysis.next_actions = _build_next_actions(registers, flags)
    analysis.generated_query = _build_generated_query(registers, flags)
    return analysis


def _parse_register_value(name: str, raw_value: str) -> int:
    if not raw_value:
        raise HardFaultValidationError(
            code="missing_value",
            message=f"{name} 缺少寄存器值。",
            field=name,
        )
    if re.fullmatch(r"0[xX][0-9a-fA-F]+", raw_value):
        value = int(raw_value, 16)
    elif re.fullmatch(r"[0-9]+", raw_value):
        value = int(raw_value, 10)
    else:
        raise HardFaultValidationError(
            code="invalid_value",
            message=f"{name} 的值“{raw_value}”不是合法的十六进制或十进制无符号整数。",
            field=name,
        )
    if value > 0xFFFFFFFF:
        raise HardFaultValidationError(
            code="value_out_of_range",
            message=f"{name} 的值超过 32 位无符号整数范围。",
            field=name,
        )
    return value


def _decode_flags(registers: dict[str, int]) -> list[DecodedFaultFlag]:
    decoded: list[DecodedFaultFlag] = []
    for register, definitions in (("CFSR", CFSR_FLAGS), ("HFSR", HFSR_FLAGS)):
        value = registers.get(register)
        if value is None:
            continue
        for bit, name, group, meaning in definitions:
            if value & (1 << bit):
                decoded.append(
                    DecodedFaultFlag(
                        register=register,
                        bit=bit,
                        name=name,
                        group=group,
                        meaning=meaning,
                    )
                )
    return decoded


def _build_observations(registers: dict[str, int], flags: list[DecodedFaultFlag]) -> list[str]:
    flag_names = {flag.name for flag in flags}
    observations: list[str] = []
    observations.extend(
        f"{flag.register}.{flag.name} 已置位：{flag.meaning}。"
        for flag in flags
    )
    observations.extend(_address_observations(registers, flag_names, "BFAR", "BFARVALID"))
    observations.extend(_address_observations(registers, flag_names, "MMFAR", "MMARVALID"))
    if not flags:
        observations.append("CFSR/HFSR 中没有识别到已定义的 Fault 状态位。")
    return observations


def _address_observations(
    registers: dict[str, int],
    flag_names: set[str],
    address_register: str,
    valid_flag: str,
) -> list[str]:
    address = registers.get(address_register)
    is_valid = valid_flag in flag_names
    if is_valid and address is not None:
        return [f"{address_register} 有效，可结合地址 0x{address:08X} 定位异常访问。"]
    if is_valid:
        return [f"{valid_flag} 已置位，但日志没有提供 {address_register}。"]
    if address is not None:
        return [f"收到 {address_register}=0x{address:08X}，但 {valid_flag} 未置位，不能确认该地址有效。"]
    return []


def _build_next_actions(registers: dict[str, int], flags: list[DecodedFaultFlag]) -> list[str]:
    flag_names = {flag.name for flag in flags}
    actions: list[str] = []
    if "FORCED" in flag_names:
        actions.append("优先查看 CFSR 中的 MemManage、BusFault 和 UsageFault 标志，确认被升级的原始异常。")
    if "PRECISERR" in flag_names:
        actions.append("结合 PC 和有效 BFAR 定位触发精确总线错误的访问指令与目标地址。")
    if "IMPRECISERR" in flag_names:
        actions.append("回查异常前的写操作、总线事务和编译优化，因为 IMPRECISERR 可能无法由当前 PC 精确定位。")
    if flag_names & {"MSTKERR", "MUNSTKERR", "STKERR", "UNSTKERR"}:
        actions.append("检查 MSP/PSP、任务栈边界和异常入栈/出栈期间的内存可访问性。")
    if flag_names & {"UNDEFINSTR", "INVSTATE", "INVPC", "NOCP"}:
        actions.append("检查跳转地址、函数指针、异常返回值和目标指令集/协处理器配置。")
    if "UNALIGNED" in flag_names:
        actions.append("检查结构体对齐、强制类型转换和未对齐指针访问。")
    if "DIVBYZERO" in flag_names:
        actions.append("定位除法运算并检查除数来源，同时确认 DIV_0_TRP 配置是否符合预期。")
    if "PC" in registers:
        actions.append(f"使用 ELF/MAP 或调试器人工核对 PC=0x{registers['PC']:08X} 对应的指令和源码位置。")
    if not actions:
        actions.append("结合 CFSR/HFSR 原始值与异常现场，继续核对 Cortex-M4 Fault 状态寄存器说明。")
    return actions


def _build_generated_query(registers: dict[str, int], flags: list[DecodedFaultFlag]) -> str:
    status_parts = [
        f"{name}=0x{registers[name]:08X}"
        for name in ("CFSR", "HFSR")
        if name in registers
    ]
    flag_names = [flag.name for flag in flags]
    context_parts: list[str] = []
    if flag_names:
        context_parts.append("置位标志：" + "、".join(flag_names))
    valid_flag_names = set(flag_names)
    if "BFARVALID" in valid_flag_names and "BFAR" in registers:
        context_parts.append(f"有效 BFAR=0x{registers['BFAR']:08X}")
    if "MMARVALID" in valid_flag_names and "MMFAR" in registers:
        context_parts.append(f"有效 MMFAR=0x{registers['MMFAR']:08X}")
    for name in ("PC", "LR"):
        if name in registers:
            context_parts.append(f"{name}=0x{registers[name]:08X}")
    context = "；".join(status_parts + context_parts)
    return f"STM32F4 HardFault 现场：{context}。这些状态位表示什么，应该按什么顺序排查？"
