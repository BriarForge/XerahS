#!/usr/bin/env python3
"""Shared conformance runner (D-CON-001, CONF-RUNNER-001).

Executes Product Contract test vectors against a thin platform adapter as
defined in product-contract/VECTORS.md. Expected results come only from the
contract (CONF-CONTRACT-001). Standard library only; Python 3.12 or later.
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

MIN_PYTHON = (3, 12)
INT64_MIN = -(2**63)
INT64_MAX = 2**63 - 1
REPO_ROOT = Path(__file__).resolve().parents[2]
CAPABILITIES_DIR = REPO_ROOT / "product-contract" / "capabilities"


def deep_merge(defaults: Any, override: Any) -> Any:
    """VECTORS.md step 1: objects merge key by key and the vector's value wins."""
    if isinstance(defaults, dict) and isinstance(override, dict):
        merged = dict(defaults)
        for key, value in override.items():
            merged[key] = deep_merge(defaults[key], value) if key in defaults else value
        return merged
    return override


def _kind(value: Any) -> str:
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "bool"
    if isinstance(value, int):
        return "int"
    if isinstance(value, float):
        return "float"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return "array"
    if isinstance(value, dict):
        return "object"
    raise TypeError(f"unsupported JSON value {value!r}")


def compare(expected: Any, actual: Any, mode: str, path: str = "$") -> list[str]:
    """Returns mismatch descriptions; an empty list means the values match."""
    expected_kind = _kind(expected)
    actual_kind = _kind(actual)
    if expected_kind != actual_kind:
        return [f"{path}: expected {expected_kind} {expected!r}, got {actual_kind} {actual!r}"]
    if expected_kind == "object":
        problems: list[str] = []
        for key, value in expected.items():
            if key not in actual:
                problems.append(f"{path}.{key}: missing")
            else:
                problems.extend(compare(value, actual[key], mode, f"{path}.{key}"))
        if mode == "exact":
            problems.extend(f"{path}.{key}: unexpected key" for key in actual if key not in expected)
        return problems
    if expected_kind == "array":
        if len(expected) != len(actual):
            return [f"{path}: expected {len(expected)} elements, got {len(actual)}"]
        problems = []
        for index, (left, right) in enumerate(zip(expected, actual)):
            problems.extend(compare(left, right, mode, f"{path}[{index}]"))
        return problems
    if expected_kind == "int":
        if not INT64_MIN <= actual <= INT64_MAX:
            return [f"{path}: {actual} is outside signed 64-bit range"]
        return [] if expected == actual else [f"{path}: expected {expected}, got {actual}"]
    if expected_kind == "float":
        same = expected == actual or (math.isnan(expected) and math.isnan(actual))
        return [] if same else [f"{path}: expected {expected!r}, got {actual!r}"]
    if expected_kind == "string":
        same = expected.encode("utf-8") == actual.encode("utf-8")
        return [] if same else [f"{path}: expected {expected!r}, got {actual!r}"]
    return [] if expected == actual else [f"{path}: expected {expected!r}, got {actual!r}"]


@dataclass
class VectorResult:
    capability: str
    vector_id: str
    operation: str
    requirements: list[str]
    passed: bool
    problems: list[str] = field(default_factory=list)


def run_adapter(adapter: list[str], request: dict[str, Any], timeout: float) -> tuple[Any, str | None]:
    try:
        completed = subprocess.run(
            adapter,
            input=json.dumps(request, ensure_ascii=False).encode("utf-8"),
            capture_output=True,
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return None, f"adapter failed to run: {exc}"
    if completed.returncode != 0:
        stderr = completed.stderr.decode("utf-8", "replace").strip()
        return None, f"adapter exited {completed.returncode}: {stderr}"
    try:
        return json.loads(completed.stdout.decode("utf-8")), None
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        return None, f"adapter returned invalid JSON: {exc}"


def run_capability(vectors_file: Path, adapter: list[str], timeout: float) -> list[VectorResult]:
    document = json.loads(vectors_file.read_text(encoding="utf-8"))
    capability = document["capability"]
    mode = document["comparison"]
    defaults = document.get("input_defaults", {})
    results = []
    for vector in document["vectors"]:
        operation = vector.get("operation", document.get("operation"))
        request = {
            "capability": capability,
            "operation": operation,
            "input": deep_merge(defaults, vector["input"]),
        }
        actual, failure = run_adapter(adapter, request, timeout)
        if failure is None and isinstance(actual, dict) and "adapter_error" in actual:
            failure = f"adapter error: {actual['adapter_error']}"
        problems = [failure] if failure else compare(vector["expected"], actual, mode)
        results.append(
            VectorResult(capability, vector["id"], operation, vector.get("requirements", []),
                         not problems, problems)
        )
    return results


def main(argv: list[str] | None = None) -> int:
    if sys.version_info < MIN_PYTHON:
        print("xerahs_conformance requires Python 3.12 or later", file=sys.stderr)
        return 2
    parser = argparse.ArgumentParser(description="Run Product Contract vectors against a platform adapter.")
    parser.add_argument("--adapter", required=True, help="adapter executable")
    parser.add_argument("--platform", required=True, help="windows, macos, linux, android, or ios")
    parser.add_argument("--edition", help="Linux edition, for example linux-qt")
    parser.add_argument("--capability", action="append",
                        help="capability ID to run; repeatable; default runs every capability with vectors")
    parser.add_argument("--report", type=Path, help="write a JSON report to this path")
    parser.add_argument("--timeout", type=float, default=30.0, help="seconds per vector")
    args = parser.parse_args(argv)

    available = {path.parent.name: path for path in sorted(CAPABILITIES_DIR.glob("*/test-vectors.json"))}
    selected = args.capability or list(available)
    unknown = [capability for capability in selected if capability not in available]
    if unknown:
        print(f"no test-vectors.json for: {', '.join(unknown)}", file=sys.stderr)
        return 2

    results: list[VectorResult] = []
    for capability in selected:
        results.extend(run_capability(available[capability], [args.adapter], args.timeout))

    failed = [result for result in results if not result.passed]
    for result in results:
        status = "PASS" if result.passed else "FAIL"
        print(f"{status} {result.capability} {result.vector_id}")
        for problem in result.problems:
            print(f"     {problem}")
    print(f"\n{len(results) - len(failed)} passed, {len(failed)} failed "
          f"({args.platform}{'/' + args.edition if args.edition else ''})")

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        report = {
            "platform": args.platform,
            "edition": args.edition,
            "capabilities": selected,
            "passed": len(results) - len(failed),
            "failed": len(failed),
            "vectors": [result.__dict__ for result in results],
        }
        args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
