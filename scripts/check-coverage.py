#!/usr/bin/env python3
"""Measure deterministic-core line coverage with LLVM's native tooling."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


TEST_EXECUTABLES = (
    "solc_wire_tests",
    "solc_encoding_tests",
    "solc_crypto_tests",
    "solc_sbf_tests",
    "solc_program_tests",
    "solc_migration_tests",
    "solc_determinism_tests",
    "solc_thread_safety_tests",
    "solc_syscall_boundary_tests",
)


def tool(name: str) -> str:
    direct = shutil.which(name)
    if direct:
        return direct
    process = subprocess.run(
        ["xcrun", "--find", name],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if process.returncode == 0:
        return process.stdout.strip()
    raise RuntimeError(f"required coverage tool is unavailable: {name}")


def run(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[bytes]:
    process = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        **kwargs,
    )
    if process.returncode != 0:
        print(process.stdout.decode(errors="replace"), file=sys.stderr)
        raise RuntimeError(f"command failed: {' '.join(command)}")
    return process


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--minimum-lines", type=float, default=77.0)
    arguments = parser.parse_args()
    repository = Path(__file__).resolve().parent.parent
    clang = tool("clang")
    llvm_profdata = tool("llvm-profdata")
    llvm_cov = tool("llvm-cov")

    with tempfile.TemporaryDirectory(prefix="solc-coverage-") as temporary:
        build = Path(temporary) / "build"
        profiles = Path(temporary) / "profiles"
        profiles.mkdir()
        environment = os.environ.copy()
        environment["CC"] = clang
        environment["LLVM_PROFILE_FILE"] = str(profiles / "%p-%m.profraw")
        run(
            [
                "cmake",
                "-S",
                str(repository),
                "-B",
                str(build),
                "-G",
                "Ninja",
                "-DBUILD_TESTING=ON",
                "-DCMAKE_BUILD_TYPE=Debug",
                "-DSOLC_ENABLE_COVERAGE=ON",
            ],
            cwd=repository,
            env=environment,
        )
        run(["cmake", "--build", str(build), "--parallel"], cwd=repository, env=environment)
        run(
            ["ctest", "--test-dir", str(build), "--output-on-failure", "--parallel", "4"],
            cwd=repository,
            env=environment,
        )
        raw_profiles = sorted(profiles.glob("*.profraw"))
        if not raw_profiles:
            raise RuntimeError("coverage tests produced no raw profiles")
        merged = Path(temporary) / "coverage.profdata"
        run(
            [llvm_profdata, "merge", "-sparse", *map(str, raw_profiles), "-o", str(merged)],
            cwd=repository,
        )
        executables = [build / name for name in TEST_EXECUTABLES if (build / name).is_file()]
        command = [
            llvm_cov,
            "export",
            str(executables[0]),
            *[argument for path in executables[1:] for argument in ("-object", str(path))],
            f"-instr-profile={merged}",
            "-format=text",
            "-ignore-filename-regex=(/tests/|/usr/|/Library/)",
        ]
        coverage = json.loads(run(command, cwd=repository).stdout)
        percent = float(coverage["data"][0]["totals"]["lines"]["percent"])

    print(f"deterministic-core line coverage: {percent:.2f}%")
    if percent < arguments.minimum_lines:
        print(
            f"coverage is below the required {arguments.minimum_lines:.2f}%",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
