"""Self-tests for the conformance suite.

Run with `python -m unittest ci/conformance/tests/test_runner.py` from
the repo root. These tests have ZERO dependencies beyond Python 3.11+
stdlib and never call out to git or to any system codec library.
"""

from __future__ import annotations

import os
import sys
import textwrap
import tempfile
import unittest
from pathlib import Path

# Make the conformance package importable.
_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parents[1]))

from conformance.lib import yaml_subset, schema, runner, diagnose, coverage, report


# --- yaml_subset --------------------------------------------------------

class YamlSubsetTests(unittest.TestCase):
    def test_basic_mapping(self):
        d = yaml_subset.loads("a: 1\nb: hello\nc: true\n")
        self.assertEqual(d, {"a": 1, "b": "hello", "c": True})

    def test_quoted_strings(self):
        d = yaml_subset.loads('greeting: "hello: world"\n')
        self.assertEqual(d, {"greeting": "hello: world"})

    def test_sequence(self):
        d = yaml_subset.loads("paths:\n  - a/b\n  - c/d\n")
        self.assertEqual(d, {"paths": ["a/b", "c/d"]})

    def test_comment_handling(self):
        d = yaml_subset.loads("# top comment\nkey: value  # trailing\n")
        self.assertEqual(d, {"key": "value"})

    def test_rejects_tabs(self):
        with self.assertRaises(yaml_subset.YamlError):
            yaml_subset.loads("a:\n\t- b\n")

    def test_rejects_flow_style(self):
        with self.assertRaises(yaml_subset.YamlError):
            yaml_subset.loads("a: [1, 2, 3]\n")


# --- schema -------------------------------------------------------------

# Use double quotes around the Python script so the test works under
# both /bin/sh (Unix) and cmd.exe (Windows). cmd.exe treats single
# quotes as literal characters and would mis-parse `-c 'foo'`.
_VALID_YAML = textwrap.dedent("""\
    id: REQ-099
    description: synthetic case for unit-tests
    sad_section: "§Z"
    evidence: synthetic
    test_command: "python -c \\"import sys; sys.exit(0)\\""
    pass_criteria_type: exit_code
    pass_criteria_expected: 0
    timeout_seconds: 5
    blocking: true
    """)


class SchemaTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.cases_dir = Path(self.tmp.name)
        (self.cases_dir / "REQ-099.yaml").write_text(_VALID_YAML, encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def test_loads_valid(self):
        cases = schema.discover_cases(self.cases_dir)
        self.assertEqual(len(cases), 1)
        c = cases[0]
        self.assertEqual(c.id, "REQ-099")
        self.assertEqual(c.pass_criteria.type, "exit_code")
        self.assertEqual(c.pass_criteria.expected, 0)

    def test_rejects_bad_id(self):
        bad = _VALID_YAML.replace("REQ-099", "REQ-99")
        (self.cases_dir / "REQ-099.yaml").write_text(bad, encoding="utf-8")
        with self.assertRaises(schema.SchemaError):
            schema.discover_cases(self.cases_dir)

    def test_rejects_bad_regex(self):
        bad = textwrap.dedent("""\
            id: REQ-100
            description: bad regex
            sad_section: "§Z"
            evidence: synthetic
            test_command: "true"
            pass_criteria_type: regex
            pass_criteria_pattern: "(unclosed"
            pass_criteria_stream: stdout
            timeout_seconds: 5
            blocking: false
            """)
        (self.cases_dir / "REQ-100.yaml").write_text(bad, encoding="utf-8")
        with self.assertRaises(schema.SchemaError):
            schema.discover_cases(self.cases_dir)


# --- runner -------------------------------------------------------------

class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        self.cases_dir = self.repo / "cases"
        self.cases_dir.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def _write(self, fname: str, content: str):
        (self.cases_dir / fname).write_text(content, encoding="utf-8")

    def _run(self, budget=30):
        cases = schema.discover_cases(self.cases_dir)
        return runner.run_all(cases, repo_root=self.repo,
                              global_budget_seconds=budget, max_workers=2)

    def test_pass_via_exit_code(self):
        self._write("REQ-099.yaml", _VALID_YAML)
        results = self._run()
        self.assertEqual(results[0].status, runner.PASS)

    def test_fail_via_wrong_exit(self):
        body = _VALID_YAML.replace(
            "pass_criteria_expected: 0",
            "pass_criteria_expected: 7",
        )
        self._write("REQ-099.yaml", body)
        results = self._run()
        self.assertEqual(results[0].status, runner.FAIL)

    def test_regex_pass(self):
        body = textwrap.dedent("""\
            id: REQ-100
            description: regex pass
            sad_section: "§Z"
            evidence: synthetic
            test_command: "python -c \\"print('the magic value')\\""
            pass_criteria_type: regex
            pass_criteria_pattern: "magic"
            pass_criteria_stream: stdout
            timeout_seconds: 5
            blocking: false
            """)
        self._write("REQ-100.yaml", body)
        results = self._run()
        self.assertEqual(results[0].status, runner.PASS)

    def test_numeric_threshold(self):
        body = textwrap.dedent("""\
            id: REQ-101
            description: numeric threshold
            sad_section: "§Z"
            evidence: synthetic
            test_command: "python -c \\"print('rate: 250.5 mb/s')\\""
            pass_criteria_type: numeric
            pass_criteria_pattern: "rate: ([0-9.]+)"
            pass_criteria_stream: stdout
            pass_criteria_comparator: ">"
            pass_criteria_threshold: 200.0
            timeout_seconds: 5
            blocking: false
            """)
        self._write("REQ-101.yaml", body)
        results = self._run()
        self.assertEqual(results[0].status, runner.PASS)

    def test_timeout(self):
        body = textwrap.dedent("""\
            id: REQ-102
            description: deliberate timeout
            sad_section: "§Z"
            evidence: synthetic
            test_command: "python -c \\"import time; time.sleep(10)\\""
            pass_criteria_type: exit_code
            pass_criteria_expected: 0
            timeout_seconds: 1
            blocking: false
            """)
        self._write("REQ-102.yaml", body)
        results = self._run(budget=20)
        self.assertEqual(results[0].status, runner.TIMEOUT)


# --- diagnose -----------------------------------------------------------

class DiagnoseTests(unittest.TestCase):
    def test_diagnose_fail_returns_focused_text(self):
        from conformance.lib.schema import Case, PassCriteria
        case = Case(
            id="REQ-099", path=Path("nowhere.yaml"),
            description="x", sad_section="§Z", evidence="x",
            evidence_paths=[], test_command="false",
            pass_criteria=PassCriteria(type="exit_code", expected=0),
            timeout_seconds=5, blocking=False,
        )
        r = runner.Result(
            case=case, status=runner.FAIL, started_at="2026-05-07T00:00:00Z",
            duration_s=0.01, exit_code=1,
            stdout_tail="line1\nline2\nFAILED here\nline4",
            stderr_tail="error: noisy",
            diagnostic="exit_code=1, expected=0",
        )
        out = diagnose.diagnose(r)
        self.assertIn("FAIL", out)
        self.assertIn("FAILED here", out)


# --- coverage -----------------------------------------------------------

class CoverageTests(unittest.TestCase):
    def test_finds_declared_reqs(self):
        with tempfile.NamedTemporaryFile("w", suffix=".md", delete=False) as f:
            f.write("REQ-001 REQ-002 REQ-002 REQ-003\n")
            path = Path(f.name)
        try:
            self.assertEqual(coverage.discover_declared(path),
                             {"REQ-001", "REQ-002", "REQ-003"})
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
