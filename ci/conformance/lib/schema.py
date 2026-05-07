"""Schema for one conformance case.

Field-level validation lives here so the runner can fail loud on malformed
YAML before any subprocess is spawned. Errors carry the case file path
and the offending field name; CI surfaces them as `::error file=...::`.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from . import yaml_subset

_REQ_ID_RE = re.compile(r"^REQ-\d{3}$")
_VALID_TYPES = {"exit_code", "regex", "numeric"}
_VALID_STREAMS = {"stdout", "stderr", "both"}
_VALID_OPS = {">", ">=", "<", "<=", "==", "!="}


class SchemaError(ValueError):
    """Raised when a case file is structurally invalid."""


@dataclass
class PassCriteria:
    """Discriminated by `type`; only the relevant fields are populated."""
    type: str                       # exit_code | regex | numeric
    expected: int | None = None     # exit_code: required
    pattern: str | None = None      # regex / numeric: required
    stream: str = "both"            # regex / numeric: stdout|stderr|both
    comparator: str | None = None   # numeric: required, one of _VALID_OPS
    threshold: float | None = None  # numeric: required


@dataclass
class Case:
    id: str
    path: Path                      # source YAML path
    description: str
    sad_section: str
    evidence: str
    evidence_paths: list[str] = field(default_factory=list)
    test_command: str = ""
    pass_criteria: PassCriteria | None = None
    timeout_seconds: int = 120
    blocking: bool = True
    skip_reason: str | None = None  # if set, runner reports SKIP

    @property
    def numeric_id(self) -> int:
        return int(self.id.split("-", 1)[1])


def _require(d: dict, key: str, where: str, ty: type | tuple[type, ...] | None = None):
    if key not in d:
        raise SchemaError(f"{where}: missing required field {key!r}")
    v = d[key]
    if ty is not None and not isinstance(v, ty):
        raise SchemaError(f"{where}: field {key!r} must be {ty}, got {type(v).__name__}")
    return v


def parse_case(path: Path) -> Case:
    where = str(path)
    raw = yaml_subset.loadf(path)
    if not isinstance(raw, dict):
        raise SchemaError(f"{where}: top-level must be a mapping")

    rid = _require(raw, "id", where, str)
    if not _REQ_ID_RE.match(rid):
        raise SchemaError(f"{where}: 'id' must match REQ-NNN, got {rid!r}")

    description = _require(raw, "description", where, str)
    sad_section = _require(raw, "sad_section", where, str)
    evidence    = _require(raw, "evidence", where, str)
    test_command = _require(raw, "test_command", where, str)
    if not test_command.strip():
        raise SchemaError(f"{where}: 'test_command' is empty")

    blocking = bool(raw.get("blocking", True))
    timeout = int(raw.get("timeout_seconds", 120))
    if timeout <= 0:
        raise SchemaError(f"{where}: 'timeout_seconds' must be positive")

    ev_paths = raw.get("evidence_paths") or []
    if not isinstance(ev_paths, list) or not all(isinstance(p, str) for p in ev_paths):
        raise SchemaError(f"{where}: 'evidence_paths' must be a list of strings")

    # Parse the pass_criteria block.
    pc_type = _require(raw, "pass_criteria_type", where, str)
    if pc_type not in _VALID_TYPES:
        raise SchemaError(f"{where}: 'pass_criteria_type' must be one of {sorted(_VALID_TYPES)}")
    pc = PassCriteria(type=pc_type)

    if pc_type == "exit_code":
        pc.expected = int(_require(raw, "pass_criteria_expected", where, int))
    elif pc_type == "regex":
        pc.pattern = _require(raw, "pass_criteria_pattern", where, str)
        pc.stream = raw.get("pass_criteria_stream", "both")
        if pc.stream not in _VALID_STREAMS:
            raise SchemaError(f"{where}: 'pass_criteria_stream' must be one of {sorted(_VALID_STREAMS)}")
        # Compile-check the regex now so a bad pattern fails at lint time.
        try:
            re.compile(pc.pattern)
        except re.error as e:
            raise SchemaError(f"{where}: invalid regex {pc.pattern!r}: {e}")
    elif pc_type == "numeric":
        pc.pattern = _require(raw, "pass_criteria_pattern", where, str)
        pc.comparator = _require(raw, "pass_criteria_comparator", where, str)
        if pc.comparator not in _VALID_OPS:
            raise SchemaError(f"{where}: 'pass_criteria_comparator' must be one of {sorted(_VALID_OPS)}")
        thr = _require(raw, "pass_criteria_threshold", where, (int, float))
        pc.threshold = float(thr)
        pc.stream = raw.get("pass_criteria_stream", "both")
        if pc.stream not in _VALID_STREAMS:
            raise SchemaError(f"{where}: 'pass_criteria_stream' must be one of {sorted(_VALID_STREAMS)}")
        try:
            re.compile(pc.pattern)
        except re.error as e:
            raise SchemaError(f"{where}: invalid regex {pc.pattern!r}: {e}")

    skip_reason = raw.get("skip_reason")
    if skip_reason is not None and not isinstance(skip_reason, str):
        raise SchemaError(f"{where}: 'skip_reason' must be a string")

    return Case(
        id=rid,
        path=path,
        description=description,
        sad_section=sad_section,
        evidence=evidence,
        evidence_paths=list(ev_paths),
        test_command=test_command,
        pass_criteria=pc,
        timeout_seconds=timeout,
        blocking=blocking,
        skip_reason=skip_reason,
    )


def discover_cases(cases_dir: Path) -> list[Case]:
    """Load every REQ-*.yaml under `cases_dir`, sorted by numeric id."""
    files = sorted(cases_dir.glob("REQ-*.yaml"))
    cases = [parse_case(p) for p in files]
    # Reject duplicate ids.
    seen: dict[str, Path] = {}
    for c in cases:
        if c.id in seen:
            raise SchemaError(f"duplicate case id {c.id} in {c.path} (also in {seen[c.id]})")
        seen[c.id] = c.path
    cases.sort(key=lambda c: c.numeric_id)
    return cases
