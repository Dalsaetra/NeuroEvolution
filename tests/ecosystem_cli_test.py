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

    def test_full_population_continues_and_resumes(self):
        first = self.run_world("full", "--creatures", 1, "--max-population", 1, "--steps", 10)
        summary = json.loads((first / "summary.json").read_text())
        self.assertEqual(summary["steps_run"], 10)
        self.assertEqual(summary["status"], "completed")
        resumed = self.run_world("full_resumed", "--resume", first / "checkpoint.eco", "--steps", 5)
        summary = json.loads((resumed / "summary.json").read_text())
        self.assertEqual(summary["steps_run"], 5)
        self.assertEqual(summary["status"], "completed")
        whole = self.run_world("full_whole", "--creatures", 1, "--max-population", 1, "--steps", 15)
        self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())

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
            self.assertEqual(len(metadata["input_labels"]), 83)
            self.assertEqual(len(final["creatures"]), population)
            self.assertEqual(len({c["id"] for c in final["creatures"]}), population)
            self.assertGreater(final["totals"]["spikes"], 0)
            for creature in final["creatures"]:
                self.assertEqual(len(creature["observation"]), 83)
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
        first = self.run_world("frontier", "--steps", 3)
        summary = json.loads((first / "summary.json").read_text())
        metadata = json.loads((first / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertEqual((metadata["width"], metadata["height"]), (80, 80))
        self.assertTrue(metadata["nursery"]["enabled"])
        self.assertNotIn("establishment", metadata)
        self.assertNotIn("archive_eval_trials", metadata)
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
        self.assertEqual((brain["inputs"], brain["outputs"]), (83, 6))
        self.assertEqual(len(brain["neurons"]), 94)
        self.assertEqual(len(brain["synapses"]), 32)
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
        self.assertEqual(len(tail[-1]["creatures"][0]["observation"]), 83)
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

    def test_founders_start_fresh_brains_in_another_world(self):
        first = self.run_world("source", "--creatures", 2, "--steps", 5)
        second = self.run_world("new", "--creatures", 4, "--steps", 1, "--seed", 99,
                                "--food-assignment", "b-rich", "--founders", first / "checkpoint.eco")
        original = json.loads((first / "ecosystem.jsonl").read_text().splitlines()[0])
        lines = [json.loads(x) for x in (second / "ecosystem.jsonl").read_text().splitlines()]
        self.assertFalse(lines[0]["fruit_a_rich"])
        self.assertEqual(lines[0]["brains"][0]["synapses"], original["brains"][0]["synapses"])
        self.assertTrue(all(c["spikes"] == 0 and c["age"] == 0 for c in lines[1]["creatures"]))

    def test_starting_genomes_samples_reproducibly_and_resets_founders(self):
        source = self.run_world("source with spaces", "--founder-brain", "random", "--creatures", 3, "--steps", 20,
                                "--predation", 1, "--founder-mass", 0.7, "--founder-carnivory", 0.2)
        saved = (source / "checkpoint.eco").read_bytes()
        source_meta = json.loads((source / "ecosystem.jsonl").read_text().splitlines()[0])
        source_brains = {b["id"]: b for b in source_meta["brains"]}
        outputs = []
        for name, seed in (("sampled", 49), ("repeated", 49), ("different", 50)):
            out = self.run_world(name, "--starting-genomes", source, "--creatures", 20,
                                 "--seed", seed, "--steps", 1, "--founder-energy", 80,
                                 "--food-assignment", "b-rich")
            outputs.append(out)
        self.assertEqual((outputs[0] / "initial.eco").read_bytes(),
                         (outputs[1] / "initial.eco").read_bytes())
        self.assertNotEqual((outputs[0] / "starting_genomes.csv").read_bytes(),
                            (outputs[2] / "starting_genomes.csv").read_bytes())
        lines = [json.loads(x) for x in (outputs[0] / "ecosystem.jsonl").read_text().splitlines()]
        meta, first = lines[:2]
        rows = list(csv.DictReader((outputs[0] / "starting_genomes.csv").read_text().splitlines()))
        self.assertEqual(len(first["creatures"]), 20)
        self.assertEqual(meta["seed"], 49)
        self.assertFalse(meta["fruit_a_rich"])
        self.assertTrue(meta["predation"])
        self.assertNotEqual(meta["terrain"], source_meta["terrain"])
        self.assertGreater(len({r["source_genome_id"] for r in rows}), 1)
        new_brains = {b["id"]: b for b in meta["brains"]}
        for c, row in zip(first["creatures"], rows):
            self.assertEqual(c["id"], int(row["founder_id"]))
            self.assertEqual(c["genome_id"], int(row["genome_id"]))
            for key in ("age", "generation", "parent", "offspring", "spikes", "attack", "damage"):
                self.assertEqual(c[key], 0)
            self.assertEqual(c["energy"], 80)
            self.assertAlmostEqual(c["mass"], 0.7)
            self.assertAlmostEqual(c["carnivory"], 0.2)
            self.assertEqual(c["health"], c["max_health"])
            self.assertTrue(all(v == 0 for v in c["brain"]["potentials"]))
            self.assertEqual(new_brains[c["id"]]["synapses"],
                             source_brains[int(row["source_creature_id"])]["synapses"])
        self.assertEqual((source / "checkpoint.eco").read_bytes(), saved)

    def test_starting_genomes_rejects_missing_empty_and_conflicting_sources(self):
        source = self.run_world("source", "--predation", 1, "--steps", 1)
        empty = self.run_world("empty", "--creatures", 1, "--basal-cost", 10000,
                               "--no-reproduction", "--steps", 1)
        for path, args, message in (
            (self.root / "missing", (), "checkpoint.eco"),
            (empty, (), "no living creatures"),
            (source, ("--resume", source / "checkpoint.eco"), "cannot combine"),
            (source, ("--founders", source / "checkpoint.eco"), "cannot combine"),
            (source, ("--founder-brain", "sparse-ancestor"), "cannot combine"),
            (source, ("--predation", 0), "interface cannot be reduced"),
        ):
            result = subprocess.run([str(EXECUTABLE), "--out", str(self.root / "invalid"),
                                     "--starting-genomes", str(path), *map(str, args)],
                                    capture_output=True, text=True, timeout=90)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(message, result.stderr)
        self.assertFalse((self.root / "invalid" / "initial.eco").exists())

    def test_retired_paths_are_rejected_and_extinction_is_final(self):
        for option in ("--establishment", "--immigration-floor", "--archive-eval-trials", "--stable-mutations"):
            result = subprocess.run([str(EXECUTABLE), option, "1"], capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Unknown option", result.stderr)
        path = self.run_world("extinct", "--creatures", 1, "--basal-cost", 10000, "--steps", 100)
        summary = json.loads((path / "summary.json").read_text())
        self.assertEqual(summary["status"], "extinct")
        self.assertEqual(summary["steps_run"], 1)
        self.assertEqual(summary["population"], 0)
        self.assertFalse((path / "archive.csv").exists())
        self.assertFalse((path / "newborn_evaluations.csv").exists())
        resumed = self.run_world("still_extinct", "--resume", path / "checkpoint.eco", "--steps", 10)
        self.assertEqual((path / "checkpoint.eco").read_bytes(), (resumed / "checkpoint.eco").read_bytes())
        old = path / "old.eco"
        old.write_text("NEUROEVO_ECOSYSTEM_21\n")
        result = subprocess.run([str(EXECUTABLE), "--resume", str(old)], capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("previous build", result.stderr)

    def test_invalid_values_and_existing_output_are_rejected(self):
        for arguments in (("--creatures", "-1"), ("--creatures", "0"), ("--steps", "2oops"),
                          ("--dt", "nan"), ("--record-every", "0"), ("--dt", "0.03"),
                          ("--controller", "magic"), ("--background-rate", "-1"),
                          ("--mutate-weight-prob", "3"), ("--storms", "yes"),
                          ("--sensorimotor", "unknown"), ("--calibrated-io", "yes"),
                          ("--actuator-tau", "-1"), ("--sensory-rate", "51"),
                          ("--motor-rate-tau", "0"), ("--motor-reference-hz", "0")):
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


if __name__ == "__main__":
    unittest.main()
