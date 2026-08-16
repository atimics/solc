#!/usr/bin/env python3
"""Build several compiler/optimization variants and compare test transcripts."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
    process = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        **kwargs,
    )
    if process.returncode != 0:
        print(process.stdout, file=sys.stderr)
        raise RuntimeError(f"command failed: {' '.join(command)}")
    return process


def transcript(executable: Path, repository: Path, noise: str) -> str:
    environment = os.environ.copy()
    environment.update(
        {
            "LANG": "C",
            "LC_ALL": "C",
            "TZ": "Pacific/Kiritimati" if noise == "late" else "UTC",
            "SOLC_DETERMINISM_NOISE": noise,
        }
    )
    cwd = Path("/") if noise == "late" else repository
    return run([str(executable)], cwd=cwd, env=environment).stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", action="append", dest="compilers")
    arguments = parser.parse_args()
    compilers = arguments.compilers or ["gcc", "clang"]
    available = [compiler for compiler in compilers if shutil.which(compiler)]
    if not available:
        print("no requested C compiler is available", file=sys.stderr)
        return 1

    repository = Path(__file__).resolve().parent.parent
    transcripts: dict[str, str] = {}
    with tempfile.TemporaryDirectory(prefix="solc-determinism-") as temporary:
        temporary_path = Path(temporary)
        for compiler in available:
            for optimization in ("-O0", "-O3"):
                label = f"{compiler}-{optimization[1:]}"
                build = temporary_path / label
                environment = os.environ.copy()
                environment["CC"] = compiler
                configure = [
                    "cmake",
                    "-S",
                    str(repository),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    "-DBUILD_TESTING=ON",
                    "-DCMAKE_BUILD_TYPE=Release",
                    f"-DCMAKE_C_FLAGS={optimization}",
                ]
                run(configure, cwd=repository, env=environment)
                run(
                    ["cmake", "--build", str(build), "--target", "solc_determinism_tests"],
                    cwd=repository,
                    env=environment,
                )
                executable = build / "solc_determinism_tests"
                first = transcript(executable, repository, "early")
                second = transcript(executable, repository, "late")
                if first != second:
                    print(f"{label}: host-environment transcript mismatch", file=sys.stderr)
                    return 1
                transcripts[label] = first

    baseline_label, baseline = next(iter(transcripts.items()))
    for label, value in transcripts.items():
        if value != baseline:
            print(
                f"determinism mismatch between {baseline_label} and {label}",
                file=sys.stderr,
            )
            return 1
    print(f"determinism matrix: ok ({', '.join(transcripts)})")
    print(baseline, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
