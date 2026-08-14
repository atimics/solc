#!/usr/bin/env python3
"""Enforce the deterministic C-core source, symbol, and ABI policy."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
import textwrap
import tomllib
from pathlib import Path
from typing import Any


FLOAT_LITERAL_PATTERNS = (
    re.compile(r"(?<![\w.])(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?[fFlL]?"),
    re.compile(r"(?<![\w.])\d+[eE][+-]?\d+[fFlL]?"),
    re.compile(
        r"(?<![\w.])0[xX](?:[0-9a-fA-F]+\.[0-9a-fA-F]*|"
        r"\.[0-9a-fA-F]+|[0-9a-fA-F]+)[pP][+-]?\d+[fFlL]?"
    ),
)


def mask_non_code(source: str) -> str:
    """Replace comments and literals with spaces while preserving line offsets."""
    output = list(source)
    state = "code"
    index = 0
    while index < len(source):
        current = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if current == "/" and following == "/":
                output[index] = output[index + 1] = " "
                state = "line-comment"
                index += 2
                continue
            if current == "/" and following == "*":
                output[index] = output[index + 1] = " "
                state = "block-comment"
                index += 2
                continue
            if current == '"':
                output[index] = " "
                state = "string"
            elif current == "'":
                output[index] = " "
                state = "character"
        elif state == "line-comment":
            if current == "\n":
                state = "code"
            else:
                output[index] = " "
        elif state == "block-comment":
            if current == "*" and following == "/":
                output[index] = output[index + 1] = " "
                state = "code"
                index += 2
                continue
            if current != "\n":
                output[index] = " "
        else:
            if current == "\\" and following:
                output[index] = " "
                if following != "\n":
                    output[index + 1] = " "
                index += 2
                continue
            output[index] = "\n" if current == "\n" else " "
            if (state == "string" and current == '"') or (
                state == "character" and current == "'"
            ):
                state = "code"
        index += 1
    return "".join(output)


def line_number(source: str, offset: int) -> int:
    return source.count("\n", 0, offset) + 1


def is_within(path: Path, roots: list[Path]) -> bool:
    resolved = path.resolve()
    return any(resolved == root or root in resolved.parents for root in roots)


def source_violations(
    path: Path, source: str, policy: dict[str, Any], repository: Path
) -> list[str]:
    violations: list[str] = []
    masked = mask_non_code(source)
    allowed_headers = set(policy["allowed_system_headers"])
    allowed_prefixes = tuple(policy["allowed_project_include_prefixes"])

    include_pattern = re.compile(
        r"^\s*#\s*include\s*([<\"])([^>\"]+)[>\"]", re.MULTILINE
    )
    for match in include_pattern.finditer(source):
        delimiter, header = match.groups()
        allowed = header in allowed_headers or header.startswith(allowed_prefixes)
        if delimiter == '"' and not allowed:
            candidate = (path.parent / header).resolve()
            allowed = candidate.is_file() and (
                candidate == repository or repository in candidate.parents
            )
        if not allowed:
            violations.append(
                f"{path}:{line_number(source, match.start())}: forbidden include {header!r}"
            )

    for forbidden_type in policy["forbidden_types"]:
        pattern = re.compile(rf"\b{re.escape(forbidden_type)}\b")
        for match in pattern.finditer(masked):
            violations.append(
                f"{path}:{line_number(source, match.start())}: "
                f"forbidden type {forbidden_type!r}"
            )

    for pattern in FLOAT_LITERAL_PATTERNS:
        for match in pattern.finditer(masked):
            violations.append(
                f"{path}:{line_number(source, match.start())}: "
                f"forbidden floating literal {match.group(0)!r}"
            )

    for token in policy["forbidden_tokens"] + policy["forbidden_macros"]:
        pattern = re.compile(rf"\b{re.escape(token)}\b")
        for match in pattern.finditer(masked):
            violations.append(
                f"{path}:{line_number(source, match.start())}: forbidden token {token!r}"
            )

    for call in policy["forbidden_calls"]:
        pattern = re.compile(rf"\b{re.escape(call)}\s*\(")
        for match in pattern.finditer(masked):
            violations.append(
                f"{path}:{line_number(source, match.start())}: forbidden call {call}()"
            )

    mutable_static = re.compile(
        r"\bstatic\s+(?!(?:const|inline)\b)([^;{}]+);", re.MULTILINE
    )
    for match in mutable_static.finditer(masked):
        declaration = match.group(1)
        if "(" not in declaration or "(*" in declaration:
            violations.append(
                f"{path}:{line_number(source, match.start())}: "
                "mutable static storage is forbidden"
            )
    return violations


def collect_sources(repository: Path, policy: dict[str, Any]) -> list[Path]:
    files: set[Path] = set()
    for root_name in policy["roots"]:
        root = repository / root_name
        for source_glob in policy["source_globs"]:
            files.update(path for path in root.glob(source_glob) if path.is_file())
    return sorted(files)


def clang_ast_violations(
    repository: Path, policy: dict[str, Any], clang: str
) -> list[str]:
    roots = [(repository / root).resolve() for root in policy["roots"]]
    translation_units: set[Path] = set()
    for pattern in policy["translation_unit_globs"]:
        translation_units.update(repository.glob(pattern))
    violations: list[str] = []

    for source in sorted(translation_units):
        command = [
            clang,
            "-std=c11",
            "-I",
            str(repository / "include"),
            "-Xclang",
            "-ast-dump=json",
            "-fsyntax-only",
            str(source),
        ]
        process = subprocess.run(
            command,
            cwd=repository,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if process.returncode != 0:
            detail = process.stderr.strip() or "clang AST generation failed"
            violations.append(f"{source}: {detail}")
            continue
        try:
            tree = json.loads(process.stdout)
        except json.JSONDecodeError as error:
            violations.append(f"{source}: invalid clang AST JSON: {error}")
            continue

        def visit(node: Any, parent_kind: str, inherited_file: Path | None) -> None:
            if not isinstance(node, dict):
                return
            location = node.get("loc", {})
            range_begin = node.get("range", {}).get("begin", {})
            filename = location.get("file") or range_begin.get("file")
            current_file = inherited_file
            if filename:
                current_file = Path(filename)
                if not current_file.is_absolute():
                    current_file = (repository / current_file).resolve()
            in_scope = current_file is not None and is_within(current_file, roots)
            kind = node.get("kind", "")
            line = location.get("line") or range_begin.get("line") or 0
            qualified_type = node.get("type", {}).get("qualType", "")

            if in_scope and kind == "FloatingLiteral":
                violations.append(f"{current_file}:{line}: floating literal in clang AST")
            if in_scope and re.search(r"\b(?:float|double)\b", qualified_type):
                violations.append(
                    f"{current_file}:{line}: floating type in clang AST: {qualified_type}"
                )
            if in_scope and kind == "VarDecl":
                storage = node.get("storageClass")
                has_static_duration = parent_kind == "TranslationUnitDecl" or storage == "static"
                if has_static_duration and "const" not in qualified_type.split():
                    name = node.get("name", "<anonymous>")
                    violations.append(
                        f"{current_file}:{line}: mutable static-duration object {name!r}"
                    )
                if node.get("tlsKind") not in (None, "none"):
                    violations.append(
                        f"{current_file}:{line}: thread-local storage is forbidden"
                    )

            for child in node.get("inner", []):
                visit(child, kind, current_file)

        visit(tree, "", None)
    return violations


def normalize_symbol(symbol: str) -> str:
    if sys.platform == "darwin" and symbol.startswith("_"):
        return symbol[1:]
    return symbol


def parse_nm(output: str) -> tuple[set[str], set[str], list[tuple[str, str]]]:
    defined: set[str] = set()
    undefined: set[str] = set()
    symbols: list[tuple[str, str]] = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 2:
            continue
        symbol_type = fields[-2]
        if len(symbol_type) != 1 or not symbol_type.isalpha():
            continue
        symbol = normalize_symbol(fields[-1])
        symbols.append((symbol_type, symbol))
        if symbol_type.upper() == "U":
            undefined.add(symbol)
        else:
            defined.add(symbol)
    return defined, undefined, symbols


def library_violations(
    library: Path, policy: dict[str, Any], repository: Path
) -> list[str]:
    process = subprocess.run(
        ["nm", "-g", str(library)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if process.returncode != 0:
        return [f"{library}: nm failed: {process.stderr.strip()}"]
    defined, undefined, symbols = parse_nm(process.stdout)
    allowed = set(policy["allowed_undefined_symbols"])
    unresolved = sorted(undefined - defined - allowed)
    violations = [f"{library}: unexpected undefined symbol {name!r}" for name in unresolved]

    public_symbols = sorted(name for kind, name in symbols if kind == "T" and name.startswith("solc_"))
    expected_path = repository / policy["public_api_file"]
    expected = sorted(
        line.strip()
        for line in expected_path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    )
    if public_symbols != expected:
        missing = sorted(set(expected) - set(public_symbols))
        added = sorted(set(public_symbols) - set(expected))
        if missing:
            violations.append(f"{library}: missing public symbols: {', '.join(missing)}")
        if added:
            violations.append(f"{library}: unreviewed public symbols: {', '.join(added)}")

    if sys.platform != "darwin":
        all_symbols_process = subprocess.run(
            ["nm", str(library)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        _, _, all_symbols = parse_nm(all_symbols_process.stdout)
        for kind, name in all_symbols:
            if kind in {"B", "b", "C", "c", "D", "d", "G", "g", "S"}:
                violations.append(f"{library}: writable static storage symbol {name!r}")
    return violations


def run_self_test() -> list[str]:
    policy = {
        "allowed_system_headers": ["stdint.h"],
        "allowed_project_include_prefixes": ["solc/"],
        "forbidden_types": ["float", "double", "time_t", "FILE"],
        "forbidden_tokens": ["_Thread_local", "_Atomic", "asm"],
        "forbidden_macros": ["__DATE__"],
        "forbidden_calls": ["malloc", "clock_gettime"],
    }
    accepted = textwrap.dedent(
        """
        #include <stdint.h>
        #include "solc/wire.h"
        /* float malloc(1) */
        static const uint32_t table[] = {1u, 2u};
        static uint32_t helper(uint32_t value) { return value + table[0]; }
        """
    )
    rejected = {
        "float": "float value;",
        "literal": "uint32_t value = (uint32_t)1.5;",
        "header": "#include <time.h>\n",
        "call": "void *value = malloc(4u);",
        "global": "static uint32_t state = 1u;",
    }
    with tempfile.TemporaryDirectory() as directory:
        repository = Path(directory)
        path = repository / "sample.c"
        if source_violations(path, accepted, policy, repository):
            return ["kernel policy self-test rejected a valid fixture"]
        for name, fixture in rejected.items():
            if not source_violations(path, fixture, policy, repository):
                return [f"kernel policy self-test missed {name} fixture"]
    return []


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--policy", type=Path)
    parser.add_argument("--clang")
    parser.add_argument("--library", type=Path)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    if arguments.self_test:
        violations = run_self_test()
        if violations:
            print("\n".join(violations), file=sys.stderr)
            return 1
        print("kernel policy self-test: ok")
        return 0
    if arguments.policy is None:
        parser.error("--policy is required unless --self-test is used")

    policy_path = arguments.policy.resolve()
    repository = policy_path.parent.parent
    with policy_path.open("rb") as policy_file:
        policy = tomllib.load(policy_file)
    if policy.get("schema") != 1:
        print(f"{policy_path}: unsupported policy schema", file=sys.stderr)
        return 1

    violations: list[str] = []
    sources = collect_sources(repository, policy)
    for path in sources:
        violations.extend(
            source_violations(path, path.read_text(encoding="utf-8"), policy, repository)
        )
    if arguments.clang:
        violations.extend(clang_ast_violations(repository, policy, arguments.clang))
    if arguments.library:
        library = arguments.library
        if not library.is_absolute():
            library = repository / library
        violations.extend(library_violations(library, policy, repository))

    if violations:
        print("\n".join(sorted(set(violations))), file=sys.stderr)
        return 1
    checked = f"{len(sources)} deterministic-core source files"
    if arguments.library:
        checked += " plus link symbols and public ABI"
    print(f"kernel policy: ok ({checked})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
