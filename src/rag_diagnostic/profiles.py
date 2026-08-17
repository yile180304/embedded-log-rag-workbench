from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


STM32_SOURCE_MANIFEST = {
    "dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf": (
        "RM0090",
        "STM32F4 Reference Manual",
    ),
    "dm00046982-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf": (
        "PM0214",
        "STM32 Cortex-M4 Programming Manual",
    ),
    "dm00354244-stm32-microcontroller-debug-toolbox-stmicroelectronics.pdf": (
        "AN4989",
        "STM32 Microcontroller Debug Toolbox",
    ),
    "DUI0553.pdf": ("DUI0553", "Cortex-M4 Devices Generic User Guide"),
    "stm32f407vg.pdf": ("STM32F407VG", "STM32F407VG Datasheet"),
    "FreeRTOS 栈溢出资料.txt": ("FREERTOS-STACK", "FreeRTOS 栈溢出资料"),
    "core_cm4.h": ("CMSIS-CORE-CM4", "CMSIS Cortex-M4 Core Peripheral Access Layer"),
    "cmsis_gcc.h": ("CMSIS-GCC", "CMSIS GCC Compiler Header"),
    "cmsis_armclang.h": ("CMSIS-ARMCLANG", "CMSIS ArmClang Compiler Header"),
}


@dataclass(frozen=True)
class STM32SourceProfile:
    document_roots: tuple[Path, Path]
    storage_root: Path
    chunks_path: Path
    collection_name: str
    report_root: Path


def resolve_stm32_profile(root: Path) -> STM32SourceProfile:
    project_root = root.resolve()
    storage_root = project_root / "storage" / "stm32f4" / "index"
    return STM32SourceProfile(
        document_roots=(project_root / "storage" / "stm32f4", project_root / "资料"),
        storage_root=storage_root,
        chunks_path=storage_root / "chunks.jsonl",
        collection_name="stm32f4_diagnostics",
        report_root=project_root / "reports" / "stm32f4",
    )


def discover_stm32_sources(profile: STM32SourceProfile) -> tuple[list[Path], list[Path]]:
    allowed: list[Path] = []
    ignored: list[Path] = []
    for document_root in profile.document_roots:
        if not document_root.exists():
            continue
        for path in sorted(document_root.iterdir()):
            if not path.is_file():
                continue
            if path.name in STM32_SOURCE_MANIFEST:
                allowed.append(path)
            else:
                ignored.append(path)
    return allowed, ignored
