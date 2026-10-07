#!/usr/bin/env python3
"""Build/run host tests; optionally compile Cortex-M3 objects with real STM32 HAL.

Use a Visual Studio developer shell for --cc cl, or provide GCC/Clang.
No tools, SDKs, board configurations or vendor source files are downloaded.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess


HAL_CONFIG = """#ifndef STM32F1XX_HAL_CONF_H
#define STM32F1XX_HAL_CONF_H
#define HAL_MODULE_ENABLED
#define HSE_VALUE 8000000U
#define HSE_STARTUP_TIMEOUT 100U
#define HSI_VALUE 8000000U
#define LSI_VALUE 40000U
#define LSE_VALUE 32768U
#define LSE_STARTUP_TIMEOUT 5000U
#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 15U
#define USE_RTOS 0U
#define PREFETCH_ENABLE 1U
#define assert_param(expr) ((void)0U)
""" + "".join(f'#define HAL_{m.upper()}_MODULE_ENABLED\n#include "stm32f1xx_hal_{m}.h"\n'
              for m in ("rcc", "gpio", "dma", "cortex", "adc", "flash", "pwr", "spi", "tim", "uart")) + "#endif\n"


def find_tool(name):
    result = shutil.which(name)
    if not result:
        raise RuntimeError(f"Compiler not found: {name}. Use its full path or a developer shell.")
    return str(Path(result).resolve())


def main():
    tests = Path(__file__).resolve().parent
    source = tests.parent
    repo = source.parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cl" if os.name == "nt" else "cc")
    parser.add_argument("--build-dir", type=Path, default=repo / "build/firmware-recovery")
    parser.add_argument("--arm-gcc", help="Optional arm-none-eabi-gcc executable")
    parser.add_argument("--cube-root", type=Path, help="Optional STM32Cube_FW_F1 SDK for real HAL header check")
    args = parser.parse_args()
    if args.cube_root and not args.arm_gcc:
        parser.error("--cube-root requires --arm-gcc")
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    records = []

    def run(name, command):
        environment = os.environ.copy()
        environment.setdefault("VSLANG", "1033")
        completed = subprocess.run(command, cwd=build, capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", env=environment, timeout=120)
        records.append(dict(name=name, command=command, returncode=completed.returncode,
                            stdout=completed.stdout, stderr=completed.stderr))
        (build / f"{name}.log").write_text(completed.stdout + completed.stderr, encoding="utf-8")
        (build / "results.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
        print(f"{'PASS' if completed.returncode == 0 else 'FAIL'}: {name}", flush=True)
        if completed.returncode:
            raise RuntimeError(completed.stdout + completed.stderr)
        return completed.stdout

    core = [source / f"hr_{name}.c" for name in ("control", "sound", "lcd", "stm32f1")]
    checks = [tests / f"test_{name}.c" for name in ("control", "sound", "lcd", "stm32", "main")]
    compiler = find_tool(args.cc)
    executable = build / ("firmware_tests.exe" if os.name == "nt" else "firmware_tests")
    if Path(compiler).name.lower() in ("cl", "cl.exe"):
        command = [compiler, "/nologo", "/std:c11", "/W4", "/WX", f"/I{source}",
                   f"/I{tests / 'hal_mock'}", *(str(p) for p in core + checks), f"/Fe{executable}"]
    else:
        command = [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-I", str(source),
                   "-I", str(tests / "hal_mock"), *(str(p) for p in core + checks), "-lm", "-o", str(executable)]
    run("host_build", command)
    output = run("host_tests", [str(executable)])
    frame = next(line for line in output.splitlines() if line.startswith("FFT:"))
    spec = importlib.util.spec_from_file_location("fft_viewer", repo / "tools/fft_viewer.py")
    viewer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(viewer)
    bins = viewer.parse_fft_frame(frame)
    if len(bins) != 64 or max(range(64), key=bins.__getitem__) != 16:
        raise RuntimeError("C-generated 1000 Hz FFT frame failed viewer interoperability")
    (build / "fft_capture.txt").write_text(frame + "\n", encoding="utf-8")
    print("PASS: C FFT frame -> existing Python viewer (64 bins, 1000 Hz)", flush=True)
    if args.arm_gcc:
        arm = find_tool(args.arm_gcc)
        for path in core[:3]:
            run("arm_" + path.stem, [arm, "-mcpu=cortex-m3", "-mthumb", "-std=c99", "-ffreestanding",
                "-Wall", "-Wextra", "-Werror", "-pedantic", "-I", str(source), "-c", str(path),
                "-o", str(build / (path.stem + ".o"))])
        if args.cube_root:
            drivers = args.cube_root.resolve() / "Drivers"
            includes = [drivers / "STM32F1xx_HAL_Driver/Inc", drivers / "CMSIS/Include",
                        drivers / "CMSIS/Device/ST/STM32F1xx/Include"]
            if not all(path.is_dir() for path in includes):
                raise RuntimeError("--cube-root must be an unpacked STM32Cube_FW_F1 package")
            config = build / "hal-config"
            config.mkdir(exist_ok=True)
            (config / "stm32f1xx_hal_conf.h").write_text(HAL_CONFIG, encoding="utf-8")
            run("arm_real_hal", [arm, "-mcpu=cortex-m3", "-mthumb", "-std=c99", "-ffreestanding",
                "-Wall", "-Wextra", "-Werror", "-pedantic", "-DSTM32F103xB", "-DUSE_HAL_DRIVER",
                "-I", str(source), "-I", str(config), *[item for p in includes for item in ("-I", str(p))],
                "-c", str(core[3]), "-o", str(build / "hr_stm32f1.o")])
            print("STM32F103xB is the header-check target; no linked firmware or board test was performed.")
    print("All requested checks passed. HAL host tests use a model; board timing is not measured.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, StopIteration, subprocess.TimeoutExpired) as error:
        raise SystemExit(f"Firmware test failed: {error}")
