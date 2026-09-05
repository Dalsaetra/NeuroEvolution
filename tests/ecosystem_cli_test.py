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
            self.assertEqual(len(metadata["input_labels"]), 97)
            self.assertEqual(len(final["creatures"]), population)
            self.assertEqual(len({c["id"] for c in final["creatures"]}), population)
            self.assertGreater(final["totals"]["spikes"], 0)
            for creature in final["creatures"]:
                self.assertEqual(len(creature["observation"]), 97)
                self.assertIn("forward", creature)
                self.assertIn("potentials", creature["brain"])
            with (path / "ecosystem_stats.csv").open(newline="") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(int(rows[-1]["population"]), population)
            self.assertEqual(json.loads((path / "summary.json").read_text())["status"], "completed")
            worlds.append(metadata)
        # Changing population cannot also change the experimental map/food assignment.
        self.assertEqual(worlds[0]["terrain"], worlds[1]["terrain"])
        self.assertEqual(worlds[0]["resources"], worlds[1]["resources"])

    def test_nursery_frontier_preset_and_resume(self):
        first = self.run_world("frontier", "--habitat", "nursery-frontier", "--steps", 3,
                               "--establishment", 1, "--archive-eval-trials", 5)
        summary = json.loads((first / "summary.json").read_text())
        metadata = json.loads((first / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertEqual((metadata["width"], metadata["height"]), (80, 80))
        self.assertTrue(metadata["nursery"]["enabled"])
        self.assertFalse(metadata["establishment"])
        self.assertEqual(metadata["archive_eval_trials"], 0)
        self.assertEqual(summary["founder_brain"], "sparse-ancestor")
        resumed = self.run_world("frontier_resumed", "--resume", first / "checkpoint.eco", "--steps", 2)
        whole = self.run_world("frontier_whole", "--habitat", "nursery-frontier", "--steps", 5)
        self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())
        custom = self.run_world("frontier_custom", "--width", 88, "--habitat", "nursery-frontier",
                                "--nursery-food-energy", 11, "--steps", 1)
        meta = json.loads((custom / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertEqual(meta["width"], 88)
        self.assertEqual(meta["nursery"]["food_energy"], 11)

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

    def test_sparse_ancestor_nursery_is_exposed_in_the_cli(self):
        path = self.run_world("ancestor", "--creatures", 1, "--habitat", "ancestor-nursery",
                              "--steps", 5, "--record-every", 5, "--record-brains", 0,
                              "--record-observations", 0)
        lines = [json.loads(line) for line in (path / "ecosystem.jsonl").read_text().splitlines()]
        brain = lines[0]["brains"][0]
        self.assertEqual((brain["inputs"], brain["outputs"]), (97, 5))
        self.assertEqual(len(brain["neurons"]), 109)
        self.assertEqual(len(brain["synapses"]), 41)
        self.assertTrue(all(c["controller"] == "spiking" for c in lines[-1]["creatures"]))
        summary = json.loads((path / "summary.json").read_text())
        self.assertEqual(summary["founder_brain"], "sparse-ancestor")
        self.assertEqual(summary["habitat"], "ancestor-nursery")

        group = self.run_world("ancestor_group", "--creatures", 4, "--founder-brain", "sparse-ancestor",
                               "--steps", 1, "--record-every", 1, "--no-reproduction")
        frame = json.loads((group / "ecosystem.jsonl").read_text().splitlines()[1])
        self.assertEqual(len({creature["genome_id"] for creature in frame["creatures"]}), 1)
        self.assertEqual(len({creature["id"] for creature in frame["creatures"]}), 4)

    def test_compact_history_and_bounded_detailed_tail(self):
        path = self.run_world("compact_tail", "--creatures", 3, "--steps", 35,
                              "--record-every", 20, "--record-brains", 0,
                              "--record-observations", 0, "--record-brain-graphs", 0,
                              "--record-routine-events", 0, "--detailed-tail-seconds", 1,
                              "--tail-record-every", 5, "--no-reproduction")
        history = [json.loads(x) for x in (path / "ecosystem.jsonl").read_text().splitlines()]
        self.assertEqual(history[0]["brains"], [])
        self.assertEqual([frame["step"] for frame in history[1:]], [0, 20, 35])
        self.assertNotIn("observation", history[-1]["creatures"][0])
        self.assertNotIn("brain", history[-1]["creatures"][0])
        tail = [json.loads(x) for x in (path / "ecosystem_tail.jsonl").read_text().splitlines()]
        self.assertEqual([frame["step"] for frame in tail[1:]], [25, 30, 35])
        self.assertEqual(len(tail[0]["brains"]), 3)
        self.assertEqual(len(tail[-1]["creatures"][0]["observation"]), 97)
        self.assertIn("potentials", tail[-1]["creatures"][0]["brain"])
        summary = json.loads((path / "summary.json").read_text())
        self.assertFalse(summary["record_brains"])
        self.assertFalse(summary["record_observations"])
        self.assertEqual(summary["detailed_tail_seconds"], 1)

    def test_storms_can_be_disabled_without_changing_sensors(self):
        path = self.run_world("no_storms", "--creatures", 2, "--steps", 20, "--record-every", 1,
                              "--no-storms", "--calm-duration", 0.2, "--warning-duration", 0.2,
                              "--storm-duration", 0.2, "--no-reproduction")
        records = [json.loads(line) for line in (path / "ecosystem.jsonl").read_text().splitlines()]
        self.assertIn("storm_cue", records[0]["input_labels"])
        self.assertFalse(records[0]["storms_enabled"])
        self.assertTrue(all(frame["weather"] == "calm" and frame["cue"] == 0 for frame in records[1:]))
        self.assertEqual(records[-1]["totals"]["exposure"], 0)
        self.assertFalse(json.loads((path / "summary.json").read_text())["storms_enabled"])

    def test_establishment_survives_empty_intervals_and_resumes(self):
        arguments = ("--creatures", 4, "--basal-cost", 100, "--no-reproduction",
                     "--establishment", 1, "--immigration-floor", 2, "--record-every", 1)
        first = self.run_world("supported_first", *arguments, "--steps", 63)
        resumed = self.run_world("supported_resume", "--resume", first / "checkpoint.eco", "--steps", 48)
        whole = self.run_world("supported_whole", *arguments, "--steps", 111)
        self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())
        summary = json.loads((whole / "summary.json").read_text())
        self.assertEqual(summary["steps_run"], 111)
        self.assertEqual(summary["status"], "awaiting_immigration")
        self.assertEqual(summary["immigrants"], 0)
        self.assertEqual(summary["births"], 0)
        self.assertEqual(summary["archive_entries"], 0)
        frames = [json.loads(x) for x in (whole / "ecosystem.jsonl").read_text().splitlines()][1:]
        self.assertGreater(summary["archive_empty_checks"], 0)
        arrivals = [c for f in frames for c in f["creatures"] if "immigrant" in c["origin"]]
        self.assertFalse(arrivals)
        self.assertTrue(any(not f["creatures"] and f["establishment"]["active"] for f in frames))
        self.assertEqual(frames[-1]["totals"]["immigrant_energy"], 0)
        self.assertTrue((whole / "archive.csv").exists())
        pure = self.run_world("unsupported", "--creatures", 4, "--basal-cost", 100,
                              "--no-reproduction", "--steps", 111)
        pure_summary = json.loads((pure / "summary.json").read_text())
        self.assertEqual(pure_summary["status"], "extinct")
        self.assertLess(pure_summary["steps_run"], 111)

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
                          ("--mutate-weight-prob", "3"), ("--establishment", "yes"),
                          ("--storms", "yes"), ("--sensorimotor", "unknown"),
                          ("--calibrated-io", "yes"), ("--archive-eval-trials", "33"),
                          ("--archive-eval-seconds", "0"), ("--actuator-tau", "-1"),
                          ("--sensory-rate", "51"), ("--motor-rate-tau", "0"), ("--motor-reference-hz", "0"),
                          ("--archive-capacity", "3"), ("--immigration-batch", "0"),
                          ("--immigration-interval", "0"), ("--archive-min-energy", "0"),
                          ("--establishment", "1", "--immigration-floor", "256"),
                          ("--detailed-tail-seconds", "-1"), ("--tail-record-every", "0"),
                          ("--detailed-tail-seconds", "10001", "--tail-record-every", "1"),
                          ("--record-observations", "2"), ("--mutation-weight-sigma", "-1")):
            result = subprocess.run([str(EXECUTABLE), *arguments], capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0, arguments)
        for arguments in (("--founder-brain", "magic"), ("--habitat", "magic"),
                          ("--habitat", "ancestor-nursery")):
            result = subprocess.run([str(EXECUTABLE), *arguments], capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0, arguments)
        path = self.run_world("kept", "--creatures", 1, "--steps", 1)
        before = (path / "ecosystem.jsonl").read_bytes()
        result = subprocess.run([str(EXECUTABLE), "--out", str(path), "--steps", "1"], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((path / "ecosystem.jsonl").read_bytes(), before)

    def test_legacy_founders_migrate_without_rewiring_existing_connections(self):
        source = self.run_world("legacy_source", "--sensorimotor", "legacy", "--archive-eval-trials", 0,
                                "--creatures", 1, "--steps", 2)
        migrated = self.run_world("migrated", "--founders", source / "checkpoint.eco", "--creatures", 1, "--steps", 1)
        old = json.loads((source / "ecosystem.jsonl").read_text().splitlines()[0])
        new = json.loads((migrated / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertEqual(len(old["input_labels"]), 87)
        self.assertEqual(len(new["input_labels"]), 97)
        self.assertEqual(old["input_labels"], new["input_labels"][:87])
        self.assertTrue(new["calibrated_io"])
        old_edges, new_edges = old["brains"][0]["synapses"], new["brains"][0]["synapses"]
        self.assertEqual(len(old_edges), len(new_edges))
        for before, after in zip(old_edges, new_edges):
            for endpoint in ("pre", "post"):
                self.assertEqual(after[endpoint], before[endpoint] + (10 if before[endpoint] >= 87 else 0))
            self.assertEqual(after["weight"], before["weight"])
        continued = self.run_world("legacy_resumed", "--resume", source / "checkpoint.eco", "--steps", 1)
        metadata = json.loads((continued / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertFalse(metadata["calibrated_io"])
        self.assertEqual(len(metadata["input_labels"]), 87)

    def test_mutation_policy_migration(self):
        first = self.run_world("legacy_mutations", "--creatures", 1, "--steps", 1, "--stable-mutations", 0)
        # v7 has the same layout without the new policy line after interface fields.
        lines = (first / "checkpoint.eco").read_text().splitlines()
        self.assertEqual(lines[0].strip(), "NEUROEVO_ECOSYSTEM_9")
        lines[0] = "NEUROEVO_ECOSYSTEM_7"
        del lines[8:10]
        historical = first / "historical.eco"
        historical.write_text("\n".join(lines) + "\n")
        old = self.run_world("preserved_policy", "--resume", historical, "--steps", 1)
        switched = self.run_world("new_policy", "--resume", historical, "--steps", 1, "--stable-mutations", 1)
        self.assertFalse(json.loads((old / "summary.json").read_text())["mutation"]["stable"])
        self.assertTrue(json.loads((switched / "summary.json").read_text())["mutation"]["stable"])
        again = self.run_world("persisted_policy", "--resume", switched / "checkpoint.eco", "--steps", 1)
        self.assertTrue(json.loads((again / "summary.json").read_text())["mutation"]["stable"])

    def test_newborn_evaluation_output_and_resume(self):
        args = ("--creatures", 1, "--founder-brain", "sparse-ancestor", "--establishment", 1,
                "--width", 12, "--height", 12, "--grazing-patches", 0, "--fruit-patches", 0,
                "--pods", 0, "--shelters", 0, "--no-storms", "--basal-cost", 0, "--movement-cost", 0,
                "--turn-cost", 0, "--forage-cost", 0, "--neuron-cost", 0, "--synapse-cost", 0, "--spike-cost", 0,
                "--maturity-age", 0.2, "--reproduction-threshold", 1, "--reproduction-cost", 1,
                "--offspring-energy", 1, "--archive-min-age", 0, "--immigration-interval", 0.1,
                "--archive-eval-trials", 2, "--archive-eval-seconds", 1, "--record-every", 1)
        first = self.run_world("evaluation_first", *args, "--steps", 3, "--archive-eval-workers", 1)
        resumed = self.run_world("evaluation_resumed", "--resume", first / "checkpoint.eco", "--steps", 4, "--archive-eval-workers", 3)
        whole = self.run_world("evaluation_whole", *args, "--steps", 7, "--archive-eval-workers", 2)
        self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())

        self.assertEqual((resumed / "newborn_evaluations.csv").read_bytes(), (whole / "newborn_evaluations.csv").read_bytes())
        timing = json.loads((whole / "summary.json").read_text())["performance"]
        self.assertEqual(timing["evaluation_workers"], 2)
        self.assertGreater(timing["evaluation_calls"], 0)
        self.assertGreaterEqual(timing["evaluation_wall_seconds"], 0)
        self.assertLessEqual(timing["evaluation_wall_seconds"], timing["step_wall_seconds"])
        with (whole / "performance.csv").open() as stream:
            performance = list(csv.DictReader(stream))
        self.assertEqual(int(performance[-1]["evaluation_calls"]), timing["evaluation_calls"])
        with (whole / "newborn_evaluations.csv").open() as stream:
            rows = list(csv.DictReader(stream))
        summary = json.loads((whole / "summary.json").read_text())
        self.assertGreater(summary["newborn_evaluated_genomes"], 0)
        self.assertEqual(len(rows), 2 * summary["newborn_evaluated_genomes"])
        self.assertTrue(all(float(row["first_birth"]) >= 0.2 for row in rows))
        self.assertTrue(all(int(row["descendant_births"]) > 0 for row in rows))
        frames = [json.loads(line) for line in (whole / "ecosystem.jsonl").read_text().splitlines()]
        self.assertTrue(any(frame.get("establishment", {}).get("archive") for frame in frames[1:]))
        for entry in frames[-1]["establishment"]["archive"]:
            self.assertEqual(entry["newborn_trials"], 2)
            self.assertEqual(entry["breeding_lineage_fraction"], 1)


if __name__ == "__main__":
    unittest.main()
