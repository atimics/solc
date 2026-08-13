#!/usr/bin/env python3
"""Validate the two independent runtime evidence records against fixed budgets."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import tomllib
from typing import Any


def load_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read evidence {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"evidence {path} is not a JSON object")
    return value


def require(record: dict[str, Any], key: str, expected: Any, label: str) -> None:
    actual = record.get(key)
    if actual != expected:
        raise ValueError(f"{label}.{key}: expected {expected!r}, got {actual!r}")


def require_integer(record: dict[str, Any], key: str, label: str) -> int:
    value = record.get(key)
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise ValueError(f"{label}.{key}: expected a non-negative integer, got {value!r}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--budgets", type=pathlib.Path, required=True)
    parser.add_argument("--mollusk", type=pathlib.Path, required=True)
    parser.add_argument("--agave", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    try:
        budget_document = tomllib.loads(arguments.budgets.read_text(encoding="utf-8"))
        pins = budget_document["pins"]
        budgets = budget_document["budgets"]
        mollusk = load_json(arguments.mollusk)
        agave = load_json(arguments.agave)

        require(mollusk, "engine", "mollusk", "mollusk")
        require(mollusk, "engineVersion", pins["mollusk"], "mollusk")
        require(mollusk, "agaveLine", "2.3", "mollusk")
        require(agave, "engine", "agave-validator", "agave")
        require(agave, "engineVersion", pins["agave_release"], "agave")
        for label, record in (("mollusk", mollusk), ("agave", agave)):
            require(record, "programId", pins["program_id"], label)
            require(record, "success", True, label)
            require(record, "returnDataBytes", 0, label)

        if mollusk.get("artifactSha256") != agave.get("artifactSha256"):
            raise ValueError("runtime artifacts have different SHA-256 digests")
        if mollusk.get("artifactBytes") != agave.get("artifactBytes"):
            raise ValueError("runtime artifacts have different sizes")

        artifact_bytes = require_integer(mollusk, "artifactBytes", "mollusk")
        mollusk_units = require_integer(mollusk, "computeUnits", "mollusk")
        agave_units = require_integer(agave, "computeUnits", "agave")
        if artifact_bytes > budgets["artifact_max_bytes"]:
            raise ValueError(
                f"artifact budget exceeded: {artifact_bytes} > "
                f"{budgets['artifact_max_bytes']} bytes"
            )
        if mollusk_units > budgets["mollusk_max_compute_units"]:
            raise ValueError(
                f"Mollusk compute budget exceeded: {mollusk_units} > "
                f"{budgets['mollusk_max_compute_units']}"
            )
        if agave_units > budgets["agave_max_compute_units"]:
            raise ValueError(
                f"Agave compute budget exceeded: {agave_units} > "
                f"{budgets['agave_max_compute_units']}"
            )
    except (KeyError, OSError, TypeError, ValueError, tomllib.TOMLDecodeError) as error:
        print(f"runtime evidence check failed: {error}", file=sys.stderr)
        return 1

    print(
        "runtime evidence matches: "
        f"artifact={artifact_bytes} bytes, "
        f"mollusk={mollusk_units} CU, agave={agave_units} CU"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
