#!/usr/bin/env python3
"""Regression checks for coverage tool discovery on Linux and macOS."""

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("check-coverage.py")
SPEC = importlib.util.spec_from_file_location("check_coverage", SCRIPT)
coverage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage)


class ToolDiscoveryTests(unittest.TestCase):
    def test_missing_tool_reports_actionable_error(self):
        with tempfile.TemporaryDirectory() as empty_path:
            process = subprocess.run(
                [sys.executable, str(SCRIPT)],
                env={**os.environ, "PATH": empty_path},
                capture_output=True,
                text=True,
            )
        self.assertEqual(process.returncode, 1)
        self.assertIn("required coverage tool is unavailable: clang", process.stderr)
        self.assertIn("install matching Clang and LLVM tools", process.stderr)
        self.assertNotIn("Traceback", process.stderr)

    def test_linux_missing_llvm_tool_does_not_run_xcrun(self):
        with (
            patch.object(coverage.sys, "platform", "linux"),
            patch.object(coverage.shutil, "which", return_value=None),
            patch.object(coverage.subprocess, "run") as run,
        ):
            with self.assertRaisesRegex(RuntimeError, "unavailable: llvm-profdata"):
                coverage.tool("llvm-profdata")
            run.assert_not_called()

    def test_path_tool_is_used(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "llvm-cov"
            executable.write_text("#!/bin/sh\nexit 0\n")
            executable.chmod(0o755)
            with patch.dict(os.environ, {"PATH": directory}):
                self.assertEqual(coverage.tool("llvm-cov"), str(executable))

    def test_macos_xcode_tool_is_used(self):
        with (
            patch.object(coverage.sys, "platform", "darwin"),
            patch.object(
                coverage.shutil, "which",
                side_effect=lambda name: "/usr/bin/xcrun" if name == "xcrun" else None,
            ),
            patch.object(
                coverage.subprocess, "run",
                return_value=subprocess.CompletedProcess([], 0, "/Xcode/llvm-cov\n"),
            ) as run,
        ):
            self.assertEqual(coverage.tool("llvm-cov"), "/Xcode/llvm-cov")
            self.assertEqual(run.call_args.args[0], ["/usr/bin/xcrun", "--find", "llvm-cov"])

    def test_macos_failed_lookup_reports_missing_tool(self):
        for returncode, output in [(1, ""), (0, "\n")]:
            with (
                self.subTest(returncode=returncode),
                patch.object(coverage.sys, "platform", "darwin"),
                patch.object(
                    coverage.shutil, "which",
                    side_effect=lambda name: "/usr/bin/xcrun" if name == "xcrun" else None,
                ),
                patch.object(
                    coverage.subprocess, "run",
                    return_value=subprocess.CompletedProcess([], returncode, output),
                ),
            ):
                with self.assertRaisesRegex(RuntimeError, "unavailable: llvm-cov"):
                    coverage.tool("llvm-cov")


if __name__ == "__main__":
    unittest.main()
