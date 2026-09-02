"""End-to-end runs exercise independent brains, durable output, and continuation."""
from __future__ import annotations

import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXECUTABLE = Path(sys.argv.pop(1)).resolve()


class EcosystemCliTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="neuroevo_ecosystem_")
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def run_world(self, name, *arguments):
        path = self.root / name
        completed = subprocess.run(
            [str(EXECUTABLE), "--out", str(path), *map(str, arguments)],
            text=True, capture_output=True, timeout=90,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        return path

    def test_population_and_brain_recording(self):
        worlds = []
        for population in (1, 7):
            path = self.run_world(str(population), "--creatures", population,
                                  "--steps", 30, "--record-every", 10,
                                  "--no-reproduction", "--seed", 17)
            lines = [json.loads(line) for line in (path / "ecosystem.jsonl").read_text().splitlines()]
            metadata, final = lines[0], lines[-1]
            self.assertEqual(metadata["type"], "metadata")
            self.assertEqual(len(metadata["brains"]), population)
            self.assertEqual(len(metadata["input_labels"]), 87)
            self.assertEqual(len(final["creatures"]), population)
            self.assertEqual(len({c["id"] for c in final["creatures"]}), population)
            self.assertGreater(final["totals"]["spikes"], 0)
            for creature in final["creatures"]:
                self.assertEqual(len(creature["observation"]), 87)
                self.assertIn("potentials", creature["brain"])
            with (path / "ecosystem_stats.csv").open(newline="") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(int(rows[-1]["population"]), population)
            self.assertEqual(json.loads((path / "summary.json").read_text())["status"], "completed")
            worlds.append(metadata)
        # Changing population cannot also change the experimental map/food assignment.
        self.assertEqual(worlds[0]["terrain"], worlds[1]["terrain"])
        self.assertEqual(worlds[0]["resources"], worlds[1]["resources"])

    def test_resume_matches_uninterrupted_world(self):
        first = self.run_world("first", "--creatures", 3, "--steps", 17, "--no-reproduction")
        resumed = self.run_world("resumed", "--resume", first / "checkpoint.eco", "--steps", 23)
        whole = self.run_world("whole", "--creatures", 3, "--steps", 40, "--no-reproduction")
        self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())

    def test_default_spiking_founders_drive_motors(self):
        path = self.run_world("motor_activity", "--creatures", 24, "--steps", 100,
                              "--record-every", 10, "--record-brains", 0, "--no-reproduction")
        with (path / "ecosystem_stats.csv").open(newline="") as handle:
            final = list(csv.DictReader(handle))[-1]
        # Sensory spikes alone do not establish that bodies can explore. Check
        # for motor effort and actual displacement with the default initializer.
        self.assertGreater(float(final["movement"]), 0.1)
        self.assertGreater(float(final["foraging"]), 0.1)
        records = [json.loads(x) for x in (path / "ecosystem.jsonl").read_text().splitlines()]
        initial = {c["id"]: c for c in records[1]["creatures"]}
        moved = [c for c in records[-1]["creatures"]
                 if abs(c["x"] - initial[c["id"]]["x"]) + abs(c["y"] - initial[c["id"]]["y"]) > 0.1]
        self.assertGreater(len(moved), 1)

    def test_founders_start_fresh_brains_in_another_world(self):
        first = self.run_world("source", "--creatures", 2, "--steps", 5)
        second = self.run_world("new", "--creatures", 4, "--steps", 1, "--seed", 99,
                                "--food-assignment", "b-rich", "--founders", first / "checkpoint.eco")
        original = json.loads((first / "ecosystem.jsonl").read_text().splitlines()[0])
        lines = [json.loads(x) for x in (second / "ecosystem.jsonl").read_text().splitlines()]
        self.assertFalse(lines[0]["fruit_a_rich"])
        self.assertEqual(lines[0]["brains"][0]["synapses"], original["brains"][0]["synapses"])
        self.assertTrue(all(c["spikes"] == 0 and c["age"] == 0 for c in lines[1]["creatures"]))

    def test_invalid_values_and_existing_output_are_rejected(self):
        for arguments in (("--creatures", "-1"), ("--creatures", "0"), ("--steps", "2oops"),
                          ("--dt", "nan"), ("--record-every", "0"), ("--dt", "0.03"),
                          ("--controller", "magic"), ("--background-rate", "-1"),
                          ("--mutate-weight-prob", "3")):
            result = subprocess.run([str(EXECUTABLE), *arguments], capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0, arguments)
        path = self.run_world("kept", "--creatures", 1, "--steps", 1)
        before = (path / "ecosystem.jsonl").read_bytes()
        result = subprocess.run([str(EXECUTABLE), "--out", str(path), "--steps", "1"], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((path / "ecosystem.jsonl").read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
