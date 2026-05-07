"""Bisect mode: filter cases by changed evidence files (Ultrathink #3).

A case is "selected" by bisect iff its `evidence_paths` overlaps the
set of files changed since `since_ref` (default: merge-base with
origin/main). Glob patterns are supported via fnmatch — the most
useful case is matching directory trees ('codec/src/common/rans/*').

This is intentionally git-only — we shell out to `git`. Running outside
a git checkout falls back to "all cases" with a warning, so devs in a
release tarball still get a usable suite.
"""

from __future__ import annotations

import fnmatch
import shutil
import subprocess
from pathlib import Path

from .schema import Case


def _has_git(repo_root: Path) -> bool:
    return bool(shutil.which("git")) and (repo_root / ".git").exists()


def _resolve_since(repo_root: Path, since: str | None) -> str:
    """If `since` is None, use merge-base with origin/main. Returns the
    commit-ish to diff against."""
    if since:
        return since
    try:
        # Prefer the merge-base with origin/main; fall back to origin/master,
        # then HEAD~1, then return the empty tree (compares all working files).
        for ref in ("origin/main", "origin/master"):
            try:
                out = subprocess.run(
                    ["git", "merge-base", "HEAD", ref],
                    cwd=repo_root, capture_output=True, text=True, check=True,
                )
                return out.stdout.strip()
            except subprocess.CalledProcessError:
                continue
        out = subprocess.run(
            ["git", "rev-parse", "HEAD~1"],
            cwd=repo_root, capture_output=True, text=True, check=True,
        )
        return out.stdout.strip()
    except Exception:
        # Empty tree object hash is well-known across git versions.
        return "4b825dc642cb6eb9a060e54bf8d69288fbee4904"


def changed_files(repo_root: Path, since: str | None = None) -> list[str]:
    if not _has_git(repo_root):
        return []
    base = _resolve_since(repo_root, since)
    diff = subprocess.run(
        ["git", "diff", "--name-only", f"{base}...HEAD"],
        cwd=repo_root, capture_output=True, text=True, check=False,
    )
    if diff.returncode != 0:
        return []
    return [ln.strip() for ln in diff.stdout.splitlines() if ln.strip()]


def filter_cases(cases: list[Case], changed: list[str]) -> list[Case]:
    """Keep cases whose evidence_paths matches any of `changed`."""
    if not changed:
        return []
    out: list[Case] = []
    for c in cases:
        for pat in c.evidence_paths:
            if any(fnmatch.fnmatch(f, pat) or f.startswith(pat.rstrip("/") + "/")
                   for f in changed):
                out.append(c)
                break
    return out
