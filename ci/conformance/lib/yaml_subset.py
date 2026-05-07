"""Constrained YAML parser for conformance case files.

We deliberately do NOT depend on PyYAML to keep the suite stdlib-only
(per ADR-015). The case files conform to a strict subset of YAML:

* Top-level is a single mapping (no document streams).
* Keys are bare identifiers (`[A-Za-z_][A-Za-z0-9_]*`).
* Values are one of:
    - a scalar on the same line as the key
    - a sequence introduced by the next-line `- ITEM` form
* Scalars are: bare strings, single- or double-quoted strings, integers,
  floats, booleans (`true`/`false`/`yes`/`no`), or `null`.
* Indentation is two spaces. Tabs are rejected.
* `#` introduces a comment to end-of-line, except inside quoted strings.
* No anchors / aliases / merge keys / flow style / block scalars.

The parser produces structured Python (dict/list/str/int/float/bool/None)
and raises `YamlError` with a line number on any unsupported construct,
which is what we want — case authors should fix their YAML rather than
hope the parser silently accepts something exotic.
"""

from __future__ import annotations

import re
from dataclasses import dataclass


class YamlError(ValueError):
    """Raised for any unsupported or malformed input."""


_KEY_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_INT_RE = re.compile(r"-?\d+$")
_FLOAT_RE = re.compile(r"-?\d+\.\d+([eE][+-]?\d+)?$")


@dataclass
class _Line:
    n: int          # 1-based line number
    indent: int     # number of leading spaces
    text: str       # the content with leading whitespace stripped, comments removed


def _strip_comment(s: str) -> str:
    """Remove a trailing '# ...' comment, respecting quotes."""
    in_s = False
    in_d = False
    for i, c in enumerate(s):
        if c == "'" and not in_d:
            in_s = not in_s
        elif c == '"' and not in_s:
            in_d = not in_d
        elif c == "#" and not in_s and not in_d:
            return s[:i].rstrip()
    return s.rstrip()


def _scan(src: str) -> list[_Line]:
    out: list[_Line] = []
    for n, raw in enumerate(src.splitlines(), start=1):
        if "\t" in raw:
            raise YamlError(f"line {n}: tabs are not allowed; use two-space indent")
        stripped = _strip_comment(raw)
        if not stripped.strip():
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        out.append(_Line(n=n, indent=indent, text=stripped[indent:]))
    return out


def _parse_scalar(raw: str, line_n: int) -> object:
    """Parse a single scalar token (everything to the right of the colon)."""
    s = raw.strip()
    if s == "" or s == "~" or s.lower() == "null":
        return None
    if s.lower() in ("true", "yes"):
        return True
    if s.lower() in ("false", "no"):
        return False
    # Quoted string.
    if len(s) >= 2 and s[0] == s[-1] and s[0] in ("'", '"'):
        body = s[1:-1]
        if s[0] == '"':
            # Minimal escape support: \\, \", \n, \t.
            return (body.replace(r"\\", "\x00")
                         .replace(r"\"", '"')
                         .replace(r"\n", "\n")
                         .replace(r"\t", "\t")
                         .replace("\x00", "\\"))
        return body
    # Numeric.
    if _INT_RE.match(s):
        return int(s)
    if _FLOAT_RE.match(s):
        return float(s)
    # Reject obvious flow-style attempts.
    if s.startswith("[") or s.startswith("{"):
        raise YamlError(f"line {line_n}: flow-style sequences/mappings are not supported")
    # Bare string. We accept anything else as a literal string -- this
    # is what the test_command field needs.
    return s


def _parse_lines(lines: list[_Line], i: int, indent: int) -> tuple[dict, int]:
    """Recursive descent: parse a mapping at `indent`, starting at lines[i]."""
    out: dict[str, object] = {}
    while i < len(lines):
        ln = lines[i]
        if ln.indent < indent:
            break
        if ln.indent > indent:
            raise YamlError(f"line {ln.n}: unexpected indentation (got {ln.indent}, expected {indent})")
        # Mapping line: KEY: VALUE  or  KEY:  (followed by a sequence)
        match = _KEY_RE.match(ln.text)
        if not match:
            raise YamlError(f"line {ln.n}: expected key, got {ln.text!r}")
        key = match.group(0)
        rest = ln.text[match.end():]
        if not rest.startswith(":"):
            raise YamlError(f"line {ln.n}: expected ':' after key {key!r}")
        rest = rest[1:]   # past the colon
        if rest.strip() == "":
            # Sequence on following lines, indented by 2.
            seq, i_next = _parse_sequence(lines, i + 1, indent + 2)
            out[key] = seq
            i = i_next
            continue
        out[key] = _parse_scalar(rest, ln.n)
        i += 1
    return out, i


def _parse_sequence(lines: list[_Line], i: int, indent: int) -> tuple[list, int]:
    """Parse a `- ITEM` block at `indent`. Returns (list, next-line-index)."""
    out: list[object] = []
    while i < len(lines):
        ln = lines[i]
        if ln.indent < indent:
            break
        if ln.indent > indent:
            raise YamlError(f"line {ln.n}: unexpected indentation in sequence")
        if not ln.text.startswith("-"):
            break
        item_text = ln.text[1:].strip()
        out.append(_parse_scalar(item_text, ln.n))
        i += 1
    if not out:
        raise YamlError(f"line {lines[i-1].n if i else 1}: empty sequence")
    return out, i


def loads(src: str) -> dict:
    """Parse a YAML-subset document. Returns a dict (top-level mapping)."""
    lines = _scan(src)
    if not lines:
        return {}
    if lines[0].indent != 0:
        raise YamlError("top-level mapping must start at column 0")
    result, idx = _parse_lines(lines, 0, 0)
    if idx != len(lines):
        raise YamlError(f"unparsed content starting at line {lines[idx].n}")
    return result


def loadf(path: str | "object") -> dict:
    """Parse a file. Convenience over `loads(open(path).read())`."""
    with open(path, "r", encoding="utf-8") as f:
        return loads(f.read())
