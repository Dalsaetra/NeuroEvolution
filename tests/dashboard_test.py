from __future__ import annotations

import importlib.util
import json
import math
import re
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_SPEC = importlib.util.spec_from_file_location("neuroevo_dashboard", ROOT / "tools" / "dashboard.py")
assert MODULE_SPEC and MODULE_SPEC.loader
DASHBOARD = importlib.util.module_from_spec(MODULE_SPEC)
sys.modules[MODULE_SPEC.name] = DASHBOARD
MODULE_SPEC.loader.exec_module(DASHBOARD)


class DashboardSchemaTests(unittest.TestCase):
    def setUp(self) -> None:
        self.schema = json.loads((ROOT / "tools" / "dashboard" / "parameters.json").read_text(encoding="utf-8"))

    def test_schema_ids_and_flags_are_unique(self) -> None:
        parameters = self.schema["parameters"]
        self.assertEqual(len({item["id"] for item in parameters}), len(parameters))
        self.assertEqual(len({item["flag"] for item in parameters}), len(parameters))

    def test_defaults_validate_and_generate_every_argument(self) -> None:
        arguments, normalized = DASHBOARD.validate_parameters({})
        self.assertEqual(len(arguments), 2 * len(self.schema["parameters"]))
        self.assertEqual(set(normalized), {item["id"] for item in self.schema["parameters"]})

    def test_schema_covers_simulator_help(self) -> None:
        executable = DASHBOARD.find_executable()
        if executable is None:
            self.skipTest("simulator has not been built")
        output = subprocess.run([str(executable), "--help"], cwd=ROOT, check=True, capture_output=True, text=True).stdout
        help_flags = set(re.findall(r"^\s+(--[a-z0-9-]+)", output, flags=re.MULTILINE)) - {"--help", "--out"}
        schema_flags = {item["flag"] for item in self.schema["parameters"]}
        self.assertEqual(help_flags, schema_flags)

    def test_run_names_cannot_escape_runs_directory(self) -> None:
        with self.assertRaises(ValueError):
            DASHBOARD.safe_run_dir("../outside")
        self.assertEqual(DASHBOARD.safe_run_dir("experiment_01").parent, DASHBOARD.RUNS_ROOT.resolve())

    def test_non_finite_csv_values_are_json_safe(self) -> None:
        row = DASHBOARD.numeric_row({"fitness": "inf", "mean_foods": "nan", "generation": "2"})
        self.assertIsNone(row["fitness"])
        self.assertIsNone(row["mean_foods"])
        self.assertEqual(row["generation"], 2)
        self.assertFalse(any(isinstance(value, float) and not math.isfinite(value) for value in row.values()))

    def test_invalid_numeric_parameters_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "Generations must be an integer"):
            DASHBOARD.validate_parameters({"generations": 2.5})
        with self.assertRaisesRegex(ValueError, "Background rate.*finite"):
            DASHBOARD.validate_parameters({"background-rate": math.inf})


if __name__ == "__main__":
    unittest.main()
