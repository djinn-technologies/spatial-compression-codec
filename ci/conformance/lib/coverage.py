"""Cross-check between Acceptance_Criteria.md and the case YAMLs.

The SAD's acceptance doc is the source of truth for what REQs exist;
the runner enforces that every REQ has a YAML case. Missing YAML is a
hard failure under `--coverage-strict`, a warning under
`--coverage-warn`, and ignored under `--coverage-skip`.

We deliberately do NOT auto-generate YAML stubs — a stub with no real
test command is worse than an honest gap, because it claims coverage
the suite doesn't actually have.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

from .schema import Case


_REQ_TOKEN_RE = re.compile(r"\bREQ-\d{3}\b")


@dataclass
class Coverage:
    declared:  set[str]      # REQ ids found in Acceptance_Criteria.md
    defined:   set[str]      # REQ ids with a case YAML
    missing:   list[str]     # declared - defined, sorted
    orphaned:  list[str]     # defined - declared, sorted (cases for ghost REQs)


def discover_declared(acceptance_md: Path) -> set[str]:
    text = acceptance_md.read_text(encoding="utf-8")
    return set(_REQ_TOKEN_RE.findall(text))


def compute(acceptance_md: Path, cases: list[Case]) -> Coverage:
    declared = discover_declared(acceptance_md)
    defined = {c.id for c in cases}
    missing = sorted(declared - defined)
    orphaned = sorted(defined - declared)
    return Coverage(declared=declared, defined=defined,
                    missing=missing, orphaned=orphaned)
