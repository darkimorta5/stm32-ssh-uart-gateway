#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and run the portable C core. No downloads, network, or hardware access."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(command, cwd):
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    print(result.stdout, end="")
    print(result.stderr, end="")
    if result.returncode:
        raise SystemExit(result.returncode)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--sanitize", action="store_true",
                        help="Enable address/undefined sanitizers with GCC or Clang")
    args = parser.parse_args()
    compiler = shutil.which(args.cc)
    if not compiler:
        parser.error("Compiler not found. On Windows use a Visual Studio developer shell and --cc cl.")
    msvc = Path(compiler).name.lower() in {"cl", "cl.exe"}
    zig = Path(compiler).name.lower() in {"zig", "zig.exe"}
    if msvc and args.sanitize:
        parser.error("--sanitize requires a GCC/Clang-compatible compiler in this script")
    output = ROOT / "build"
    output.mkdir(exist_ok=True)
    logs = {}
    for name, source in (("gateway_tests", "tests/test_gateway.c"),
                         ("host_demo", "examples/host_demo.c")):
        directory = output / name
        directory.mkdir(exist_ok=True)
        executable = directory / (name + (".exe" if os.name == "nt" else ""))
        if msvc:
            command = [compiler, "/nologo", "/std:c11", "/W4", "/WX", "/Od",
                       "/I" + str(ROOT / "include"), str(ROOT / "src/gateway.c"),
                       str(ROOT / source), "/Fe:" + str(executable)]
        else:
            command = [compiler] + (["cc"] if zig else []) + ["-std=c99", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                       "-O0", "-g", "-I", str(ROOT / "include"),
                       str(ROOT / "src/gateway.c"), str(ROOT / source), "-o", str(executable)]
            if args.sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        run(command, directory)
        logs[name] = run([str(executable)], directory)
    match = re.search(r"RESULT cases=(\d+) checks=(\d+) failures=0", logs["gateway_tests"])
    size = re.search(r"CORE_SIZE bytes=(\d+)", logs["gateway_tests"])
    if not match or not size:
        raise SystemExit("Test result was not produced")
    report = {
        "scope": "Host C core with synthetic adapters, not firmware or hardware validation",
        "compiler": Path(compiler).name,
        "cases": int(match[1]), "checks": int(match[2]), "failures": 0,
        "core_bytes_on_this_host": int(size[1]), "demo_passed": True,
        "sanitizers": args.sanitize, "hardware_tested": False,
    }
    (output / "host-verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Host verification passed. Results: build/host-verification.json")


if __name__ == "__main__":
    main()
