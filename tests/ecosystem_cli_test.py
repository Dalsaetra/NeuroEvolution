"""End-to-end runs exercise independent brains, durable output, and continuation."""
from __future__ import annotations

import csv
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

EXECUTABLE = Path(sys.argv.pop(1)).resolve()


class EcosystemCliTests(unittest.TestCase):
    def test_evolved_eyes_configuration_import_and_resume(self):
        args = ("--creatures", 1, "--founder-eye-separation", 150, "--mutate-initial-ancestors", 0,
                "--eye-mutation-probability", 0.7, "--eye-mutation-sigma", 20)
        source = self.run_world("eyes", *args, "--steps", 1)
        records = [json.loads(x) for x in (source / "ecosystem.jsonl").read_text().splitlines()]
        self.assertEqual(records[0]["fov_degrees"], 150)
        self.assertEqual(records[0]["eye_mutation_probability"], 0.7)
        self.assertEqual(records[1]["creatures"][0]["eye_separation_degrees"], 150)
        self.assertEqual(records[1]["creatures"][0]["fov_degrees"], 300)
        resumed = self.run_world("eyes_resume", "--resume", source / "checkpoint.eco", "--steps", 1)
        whole = self.run_world("eyes_whole", *args, "--steps", 2)
        self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())
        imported = self.run_world("eyes_import", "--starting-genomes", source, "--creatures", 1, "--steps", 1)
        imported_records = [json.loads(x) for x in (imported / "ecosystem.jsonl").read_text().splitlines()]
        self.assertEqual(imported_records[1]["creatures"][0]["eye_separation_degrees"], 150)
        self.assertEqual(imported_records[1]["creatures"][0]["fov_degrees"], 300)

    def test_mass_allometry_configuration_and_resume(self):
        values = dict(ingestion_mass_exponent=0.9, pod_mass_exponent=0.6,
                      attack_mass_exponent=0.7, metabolism_mass_exponent=0.8,
                      energy_mass_exponent=1.1, speed_mass_exponent=0.3,
                      acceleration_mass_exponent=-0.6, max_acceleration=2)
        flags = [item for key, value in values.items() for item in ("--" + key.replace("_", "-"), value)]
        args = ("--creatures", 1, "--no-reproduction", "--detailed-tail-seconds", 0, *flags)
        first = self.run_world("mass", *args, "--steps", 3)
        resumed = self.run_world("mass_resume", "--resume", first / "checkpoint.eco", "--steps", 3,
                                 "--detailed-tail-seconds", 0)
        whole = self.run_world("mass_whole", *args, "--steps", 6)
        self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())
        metadata = json.loads((first / "ecosystem.jsonl").read_text().splitlines()[0])
        summary = json.loads((resumed / "summary.json").read_text())["predation"]
        self.assertTrue(metadata["mass_allometry"])
        for key, value in values.items():
            self.assertEqual(metadata[key], value)
            self.assertEqual(summary[key], value)
        legacy = self.run_world("mass_disabled", *args, "--mass-allometry", 0, "--steps", 1)
        self.assertFalse(json.loads((legacy / "summary.json").read_text())["predation"]["mass_allometry"])

    def test_parallel_tail_and_resume(self):
        flags = ("--creatures", 48, "--neuron-model", "filtered-lif", "--steps", 10,
                 "--record-every", 1, "--detailed-tail-seconds", 1, "--tail-record-every", 1)
        serial = self.run_world("serial", *flags, "--threads", 1, "--spatial-index", 0)
        parallel = self.run_world("parallel", *flags, "--threads", 4)
        for name in ("initial.eco", "checkpoint.eco", "ecosystem.jsonl", "ecosystem_tail.jsonl",
                     "events.csv", "ecosystem_stats.csv"):
            self.assertEqual((serial / name).read_bytes(), (parallel / name).read_bytes(), name)
        resumed = self.run_world("parallel_resume", "--resume", parallel / "checkpoint.eco",
                                 "--steps", 10, "--threads", 2, "--detailed-tail-seconds", 0)
        whole = self.run_world("serial_whole", *flags, "--steps", 20, "--threads", 1,
                               "--spatial-index", 0, "--detailed-tail-seconds", 0)
        self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())

    def test_meta_mutation_configuration_and_import(self):
        args = ("--creatures", 1, "--no-reproduction", "--meta-mutation", 1,
                "--meta-mutation-probability", 0.3, "--meta-mutation-sigma", 0.2)
        first = self.run_world("meta", *args, "--steps", 1)
        resumed = self.run_world("meta_resume", "--resume", first / "checkpoint.eco", "--steps", 1)
        whole = self.run_world("meta_whole", *args, "--steps", 2)
        self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())
        metadata = json.loads((first / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertTrue(metadata["meta_mutation_enabled"])
        self.assertEqual(metadata["meta_mutation_probability"], 0.3)
        self.assertEqual(metadata["meta_mutation_sigma"], 0.2)
        self.assertEqual(metadata["mutation_scale_floor"], 1.0)
        lines = (first / "checkpoint.eco").read_text().splitlines()
        marker = next(i for i, line in enumerate(lines) if line.startswith("META_MUTATION_1"))
        creature_id = lines[marker + 3].split()[0]
        lines[marker + 3] = creature_id + " 1.7"
        (first / "checkpoint.eco").write_text("\n".join(lines) + "\n")
        imported = self.run_world("meta_import", "--starting-genomes", first, "--creatures", 1,
                                  "--steps", 1, "--no-reproduction")
        records = [json.loads(x) for x in (imported / "ecosystem.jsonl").read_text().splitlines()]
        self.assertEqual(records[1]["creatures"][0]["mutation_scale"], 1.7)
        with (imported / "ecosystem_stats.csv").open() as stream:
            rows = list(csv.DictReader(stream))
        self.assertAlmostEqual(float(rows[-1]["mean_mutation_scale"]), 1.7)

    def test_reproduction_allocation_configuration_and_import(self):
        source = self.run_world("allocation", "--creatures", 1, "--steps", 1,
                                "--reproduction-allocation", 0.8,
                                "--allocation-mutation-probability", 0.4, "--allocation-mutation-sigma", 0.15)
        imported = self.run_world("allocation_import", "--starting-genomes", source,
                                  "--creatures", 1, "--steps", 1)
        original = [json.loads(x) for x in (source / "ecosystem.jsonl").read_text().splitlines()]
        records = [json.loads(x) for x in (imported / "ecosystem.jsonl").read_text().splitlines()]
        self.assertTrue(original[0]["funded_reproduction"])
        self.assertEqual(original[0]["allocation_mutation_probability"], 0.4)
        self.assertEqual(original[0]["allocation_mutation_sigma"], 0.15)
        self.assertEqual(records[1]["creatures"][0]["reproduction_allocation"], 0.8)
        self.assertEqual(records[1]["creatures"][0]["reproductive_energy"], 0)
        self.assertEqual(records[1]["creatures"][0]["reproduction_target"], 0)

    def test_import_historical_meta_scale_uses_new_floor(self):
        source = self.run_world("old_meta_source", "--creatures", 1, "--steps", 1)
        checkpoint = source / "checkpoint.eco"
        lines = checkpoint.read_text().splitlines()
        del lines[1:3]  # Remove the v40 mass-allometry preamble.
        lines[0] = "NEUROEVO_ECOSYSTEM_35"
        gestation = next((i for i, line in enumerate(lines) if line.startswith("GESTATION_1")), len(lines)-1)
        lines = lines[:gestation] + ["END_ECOSYSTEM"]
        lines = [line for line in lines if not line.startswith(("BACKGROUND_WEATHER_1", "MASS_ENERGY_1"))]
        marker = next(i for i, line in enumerate(lines) if line.startswith("META_MUTATION_1"))
        lines[marker + 1] = " ".join(lines[marker + 1].split()[:3])
        lines[marker + 3] = lines[marker + 3].split()[0] + " 0.75"
        checkpoint.write_text("\n".join(lines) + "\n")
        imported = self.run_world("floored_import", "--starting-genomes", source,
                                  "--creatures", 1, "--steps", 1)
        records = [json.loads(line) for line in (imported / "ecosystem.jsonl").read_text().splitlines()]
        self.assertEqual(records[0]["mutation_scale_floor"], 1.0)
        self.assertEqual(records[1]["creatures"][0]["mutation_scale"], 1.0)

    def test_fields_and_trees_default_and_resume(self):
        path = self.root / "source_default"
        result = subprocess.run([str(EXECUTABLE), "--out", str(path), "--steps", "5", "--creatures", "1",
            "--record-every", "1", "--record-brains", "0", "--record-observations", "0",
            "--detailed-tail-seconds", "0"], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        records = [json.loads(line) for line in (path / "ecosystem.jsonl").read_text().splitlines()]
        metadata = records[0]
        self.assertEqual(metadata["food_distribution"], "fields-and-trees")
        self.assertEqual(metadata["background_food_patches"], 250)
        self.assertEqual(metadata["background_food_energy"], 10)
        self.assertTrue(metadata["storm_ramp"])
        self.assertEqual(records[1]["storm_intensity"], 0)
        self.assertEqual([s["kind"] for s in metadata["food_sources"]].count("field"), 3)
        self.assertEqual([s["kind"] for s in metadata["food_sources"]].count("pod-tree"), 3)
        source_ids = {s["id"] for s in metadata["food_sources"]}
        self.assertTrue(all(r["source_id"] in source_ids for r in metadata["resources"] if r["source_id"]))
        self.assertTrue(any(r["ripening_remaining"] > 0 for r in records[1]["resources"]))
        resumed = self.run_world("source_resume", "--resume", path / "checkpoint.eco", "--steps", 1)
        saved = json.loads((resumed / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertEqual(saved["food_sources"], metadata["food_sources"])
        self.assertEqual(saved["food_distribution"], "fields-and-trees")
        self.assertEqual(saved["background_food_patches"], 250)
        self.assertTrue(saved["storm_ramp"])
        self.assertEqual(json.loads((resumed / "summary.json").read_text())["food_distribution"], "fields-and-trees")
        result = subprocess.run([str(EXECUTABLE), "--resume", str(path / "checkpoint.eco"),
            "--food-distribution", "scattered"], capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)

    def test_scattered_preset_remains_available(self):
        path = self.run_world("legacy_food", "--food-distribution", "scattered", "--steps", 1, "--creatures", 1)
        metadata = json.loads((path / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertEqual(metadata["food_distribution"], "scattered")
        self.assertEqual(metadata["food_sources"], [])
        self.assertTrue(all(r["source_id"] == 0 for r in metadata["resources"]))

    def test_background_weather_overrides_and_resume(self):
        path = self.run_world("background_override", "--food-distribution", "fields-and-trees",
                              "--background-food-patches", 17, "--background-food-energy", 7,
                              "--storm-ramp", 0, "--steps", 1, "--creatures", 1,
                              "--detailed-tail-seconds", 0)
        resumed = self.run_world("background_override_resume", "--resume", path / "checkpoint.eco", "--steps", 1)
        for run in (path, resumed):
            metadata = json.loads((run / "ecosystem.jsonl").read_text().splitlines()[0])
            summary = json.loads((run / "summary.json").read_text())
            for record in (metadata, summary):
                self.assertEqual(record["background_food_patches"], 17)
                self.assertEqual(record["background_food_energy"], 7)
                self.assertFalse(record["storm_ramp"])
            self.assertEqual(sum(r["source_id"] == 0 and r["value"] == 7 for r in metadata["resources"]), 17)

    @unittest.skipUnless(os.name == "nt" and shutil.which("pwsh"), "Windows PowerShell launcher test")
    def test_launcher_uses_release_binary_with_stale_single_config_binary(self):
        repo = self.root / "launcher repo"
        (repo / "scripts").mkdir(parents=True)
        (repo / "tools").mkdir()
        (repo / "build" / "Release").mkdir(parents=True)
        source_root = Path(__file__).resolve().parents[1]
        shutil.copy2(source_root / "scripts" / "ecosystem.ps1", repo / "scripts")
        shutil.copy2(source_root / "tools" / "view_ecosystem.py", repo / "tools")
        shutil.copy2(EXECUTABLE, repo / "build" / "Release" / "neuroevo_ecosystem.exe")
        (repo / "build" / "neuroevo_ecosystem.exe").write_bytes(b"stale incompatible binary")
        (repo / "build" / "CMakeCache.txt").write_text(
            "CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n", encoding="utf-8")
        output = repo / "output"
        result = subprocess.run([shutil.which("pwsh"), "-NoProfile", "-File",
            str(repo / "scripts" / "ecosystem.ps1"), "-Steps", "1", "-DetailedTailSeconds", "1",
            "-Threads", "2", "-KeepJsonl", "-RunDir", str(output)], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((output / "ecosystem.html").exists())
        self.assertTrue((output / "ecosystem_tail.html").exists())
        imported = repo / "imported"
        result = subprocess.run([shutil.which("pwsh"), "-NoProfile", "-File",
            str(repo / "scripts" / "ecosystem.ps1"), "-StartingGenomes", str(output),
            "-Steps", "1", "-Creatures", "3", "-KeepJsonl", "-RunDir", str(imported)],
            capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((imported / "ecosystem.html").exists())
        with (imported / "starting_genomes.csv").open() as source:
            self.assertEqual(len(list(csv.DictReader(source))), 3)

    def test_storm_health_mode_resume(self):
        first = self.run_world("storm_health", "--creatures", 1, "--steps", 2,
                               "--storm-health-damage", 1, "--storm-damage", 7,
                               "--storm-energy-drain", 1, "--storm-cost", 3)
        resumed = self.run_world("storm_health_resume", "--resume", first / "checkpoint.eco", "--steps", 1)
        metadata = json.loads((resumed / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertTrue(metadata["storm_health_damage"])
        self.assertTrue(metadata["storm_energy_drain"])
        self.assertEqual(metadata["storm_cost"], 3)
        self.assertEqual(metadata["storm_damage"], 7)

    def test_shelter_predation_damage_resume_and_legacy(self):
        for enabled in (0, 1):
            first = self.run_world(f"shelter_{enabled}", "--creatures", 1, "--steps", 1,
                "--shelter-predation-damage", enabled)
            resumed = self.run_world(f"shelter_resume_{enabled}", "--resume", first / "checkpoint.eco", "--steps", 1)
            metadata = json.loads((resumed / "ecosystem.jsonl").read_text().splitlines()[0])
            summary = json.loads((resumed / "summary.json").read_text())
            self.assertEqual(metadata["shelter_predation_damage"], bool(enabled))
            self.assertEqual(summary["predation"]["shelter_predation_damage"], bool(enabled))
            lines = self.legacy_checkpoint_lines(first / "checkpoint.eco")
            self.assertEqual(lines[0].strip(), "NEUROEVO_ECOSYSTEM_33")
            del lines[17]  # Version 33 adds initial ancestor mutation policy.
            del lines[16]  # Version 32 adds ordinary shelter damage policy.
            lines[0] = "NEUROEVO_ECOSYSTEM_31"
            legacy = first / "v31.eco"
            legacy.write_text("\n".join(lines) + "\n")
            old = self.run_world(f"shelter_legacy_{enabled}", "--resume", legacy, "--steps", 1)
            summary = json.loads((old / "summary.json").read_text())
            self.assertTrue(summary["predation"]["shelter_predation_damage"])

    def test_nursery_nutrition_resume_and_legacy(self):
        first = self.run_world("nutrition", "--creatures", 51, "--steps", 2,
            "--no-reproduction", "--no-storms", "--nursery-food-energy", 80,
            "--nursery-food-population-threshold", 50, "--nursery-food-energy-factor", 0.8,
            "--nursery-food-reduction-delay", 12.5)
        summary = json.loads((first / "summary.json").read_text())
        self.assertEqual(summary["nursery"]["current_food_energy"], 64)
        self.assertEqual(summary["nursery"]["food_reductions"], 1)
        resumed = self.run_world("nutrition_resume", "--resume", first / "checkpoint.eco", "--steps", 2)
        summary = json.loads((resumed / "summary.json").read_text())
        self.assertEqual(summary["nursery"]["current_food_energy"], 64)
        self.assertEqual(summary["nursery"]["food_reduction_delay"], 12.5)
        metadata = json.loads((resumed / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertEqual(metadata["nursery"]["food_reduction_delay"], 12.5)
        self.assertEqual(summary["nursery"]["food_reductions"], 1)
        with (resumed / "ecosystem_stats.csv").open() as source:
            rows = list(csv.DictReader(source))
        self.assertEqual(float(rows[-1]["nursery_food_energy"]), 64)
        lines = self.legacy_checkpoint_lines(first / "checkpoint.eco")
        self.assertEqual(lines[0].strip(), "NEUROEVO_ECOSYSTEM_33")
        del lines[17]  # Version 33 adds initial ancestor mutation policy.
        del lines[16]  # Version 32 adds ordinary shelter damage policy.
        del lines[15]  # Version 31 adds the reduction cooldown and deadline.
        lines[0] = "NEUROEVO_ECOSYSTEM_30"
        legacy = first / "v30.eco"
        legacy.write_text("\n".join(lines) + "\n")
        old = self.run_world("nutrition_v30", "--resume", legacy, "--steps", 1)
        summary = json.loads((old / "summary.json").read_text())
        self.assertEqual(summary["nursery"]["food_reduction_delay"], 0)
        self.assertEqual(summary["nursery"]["food_reductions"], 1)
        self.assertEqual(summary["nursery"]["current_food_energy"], 64)
        del lines[14]
        lines[0] = "NEUROEVO_ECOSYSTEM_29"
        legacy = first / "v29.eco"
        legacy.write_text("\n".join(lines) + "\n")
        old = self.run_world("nutrition_old", "--resume", legacy, "--steps", 1)
        summary = json.loads((old / "summary.json").read_text())
        self.assertEqual(summary["nursery"]["food_population_threshold"], 0)
        self.assertEqual(summary["nursery"]["food_reductions"], 0)

    def test_regional_meat_decay_and_old_checkpoint(self):
        first = self.run_world("regional_meat", "--predation", 0, "--storm-health-damage", 0, "--creatures", 1, "--steps", 1,
                               "--meat-decay", 0.012, "--nursery-meat-decay", 0.045,
                               "--mutate-add-autapse-prob", 0.37,
                               "--nursery-food-respawn-delay", 2, "--outdoor-food-respawn-delay", 5)
        resumed = self.run_world("regional_meat_resumed", "--resume", first / "checkpoint.eco", "--steps", 1)
        metadata = json.loads((resumed / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertEqual(metadata["meat_decay"], 0.012)
        self.assertEqual(metadata["nursery_meat_decay"], 0.045)
        self.assertEqual(metadata["nursery_food_respawn_delay"], 2)
        self.assertEqual(metadata["outdoor_food_respawn_delay"], 5)
        summary = json.loads((resumed / "summary.json").read_text())
        self.assertEqual(summary["mutation"]["add_autapse_probability"], 0.37)
        lines = self.legacy_checkpoint_lines(first / "checkpoint.eco")
        self.assertEqual(lines[0].strip(), "NEUROEVO_ECOSYSTEM_33")
        del lines[17]  # Version 33 adds initial ancestor mutation policy.
        del lines[16]  # Version 32 adds ordinary shelter damage policy.
        del lines[15]  # Version 31 adds the reduction cooldown and deadline.
        del lines[14]  # Version 30 adds nursery nutrition policy and crossing state.
        del lines[13]  # Version 29 adds storm mode and damage rate.
        for i in range(24, 24 + int(lines[23])):
            lines[i] = " ".join(lines[i].split()[:-1])  # Remove per-resource cooldown.
        del lines[12]  # Version 28 adds regional respawn delays.
        del lines[8]  # Version 26 adds the autapse operator setting.
        lines[0] = "NEUROEVO_ECOSYSTEM_24"
        del lines[10]  # Version 25 adds nursery decay after the birth profiles.
        historical = first / "v24.eco"
        historical.write_text("\n".join(lines) + "\n")
        old = self.run_world("regional_meat_old", "--resume", historical, "--steps", 1)
        metadata = json.loads((old / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertEqual(metadata["meat_decay"], 0.012)
        self.assertEqual(metadata["nursery_meat_decay"], 0.012)
        self.assertEqual(metadata["nursery_food_respawn_delay"], 0)
        self.assertEqual(metadata["outdoor_food_respawn_delay"], 0)
        summary = json.loads((old / "summary.json").read_text())
        self.assertEqual(summary["mutation"]["add_autapse_probability"], 0)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="neuroevo_ecosystem_")
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def legacy_checkpoint_lines(self, path):
        """Strip the v34 source extension before historical-format migration tests."""
        lines = path.read_text().splitlines()
        self.assertEqual(lines[0].strip(), "NEUROEVO_ECOSYSTEM_43")
        del lines[1:3]  # Version 40's mass-allometry config preamble.
        extension = next(i for i, line in enumerate(lines) if line.startswith("FOOD_SOURCES_1"))
        lines = lines[:extension] + ["END_ECOSYSTEM"]
        lines[0] = "NEUROEVO_ECOSYSTEM_33"
        return lines

    def run_world(self, name, *arguments):
        path = self.root / name
        defaults = [] if "--resume" in arguments else ["--food-distribution", "scattered"]
        completed = subprocess.run(
            [str(EXECUTABLE), "--out", str(path), *defaults, *map(str, arguments)],
            text=True, capture_output=True, timeout=90,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        return path

    def test_full_population_continues_and_resumes(self):
        first = self.run_world("full", "--creatures", 1, "--max-population", 1, "--steps", 10)
        summary = json.loads((first / "summary.json").read_text())
        self.assertEqual(summary["steps_run"], 10)
        self.assertEqual(summary["status"], "completed")
        self.assertEqual(summary["mutation"]["add_autapse_probability"], 0.10)
        resumed = self.run_world("full_resumed", "--resume", first / "checkpoint.eco", "--steps", 5)
        summary = json.loads((resumed / "summary.json").read_text())
        self.assertEqual(summary["steps_run"], 5)
        self.assertEqual(summary["status"], "completed")
        whole = self.run_world("full_whole", "--creatures", 1, "--max-population", 1, "--steps", 15)
        self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())

    def test_autapse_probability_validation(self):
        for value in ("-0.1", "1.1", "nan"):
            result = subprocess.run([str(EXECUTABLE), "--mutate-add-autapse-prob", value,
                                     "--out", str(self.root / "invalid"), "--steps", "1"],
                                    capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)

    def test_filtered_lif_switch_and_resume(self):
        for dt in (0.005, 0.002):
            args = ("--creatures", 1, "--no-reproduction", "--founder-brain", "random", "--brain-dt", dt,
                    "--neuron-model", "filtered-lif", "--record-brain-graphs", 1, "--detailed-tail-seconds", 0)
            first = self.run_world(f"filtered_{dt}", *args, "--steps", 3)
            meta = json.loads((first / "ecosystem.jsonl").read_text().splitlines()[0])
            self.assertEqual(meta["neuron_model"], "filtered-lif")
            self.assertEqual(meta["brain_dt"], dt)
            self.assertEqual(meta["synaptic_tau"], 0.1)
            self.assertEqual(meta["brains"][0]["neuron_model"], "filtered-lif")
            resumed = self.run_world(f"filtered_resume_{dt}", "--resume", first / "checkpoint.eco", "--steps", 5)
            whole = self.run_world(f"filtered_whole_{dt}", *args, "--steps", 8)
            self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())
        default = self.run_world("filtered_default", "--neuron-model", "filtered-lif", "--creatures", 1, "--steps", 1)
        meta = json.loads((default / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertEqual(meta["brain_dt"], .005)
        for args in [("--brain-dt", ".02"), ("--brain-dt", ".003"), ("--calibrated-io", "false"),
                     ("--resume", default / "checkpoint.eco")]:
            result = subprocess.run([str(EXECUTABLE), "--neuron-model", "filtered-lif", *map(str, args)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_izhikevich_switch_and_resume(self):
        first = self.run_world("izh", "--creatures", 1, "--steps", 3,
                               "--neuron-model", "izhikevich", "--izh-d", 2, "--record-brain-graphs", 1, "--detailed-tail-seconds", 0)
        meta = json.loads((first / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertEqual(meta["neuron_model"], "izhikevich")
        self.assertEqual(meta["brain_dt"], 0.001)
        hidden = meta["brains"][0]["neurons"][91]
        self.assertEqual(hidden["izhikevich"]["d"], 2)
        self.assertEqual(hidden["threshold"], 30)
        resumed = self.run_world("izh_resume", "--resume", first / "checkpoint.eco", "--steps", 2)
        whole = self.run_world("izh_whole", "--creatures", 1, "--steps", 5,
                               "--neuron-model", "izhikevich", "--izh-d", 2)
        self.assertEqual((resumed / "checkpoint.eco").read_bytes(), (whole / "checkpoint.eco").read_bytes())
        for args in [("--neuron-model", "wrong"),
                     ("--neuron-model", "izhikevich", "--brain-dt", "0.02"),
                     ("--neuron-model", "izhikevich", "--izh-a", "0")]:
            result = subprocess.run([str(EXECUTABLE), "--steps", "1", *args], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_default_main_recording_is_compact(self):
        path = self.run_world("default_overview", "--creatures", 1, "--steps", 3)
        rows = [json.loads(line) for line in (path / "ecosystem.jsonl").read_text().splitlines()]
        self.assertEqual(len(rows), 3)  # Metadata, first frame, final frame.
        self.assertEqual(rows[0]["brains"], [])
        for row in rows[1:]:
            self.assertTrue(all("brain" not in c and "observation" not in c for c in row["creatures"]))
        summary = json.loads((path / "summary.json").read_text())
        self.assertEqual(summary["record_every"], 500)
        self.assertFalse(summary["record_brains"])
        self.assertEqual(summary["detailed_tail_seconds"], 600)
        tail = [json.loads(line) for line in (path / "ecosystem_tail.jsonl").read_text().splitlines()]
        self.assertEqual([frame["step"] for frame in tail[1:]], [0, 3])
        self.assertTrue(tail[0]["brains"])
        self.assertIn("potentials", tail[-1]["creatures"][0]["brain"])
        self.assertEqual(len(tail[-1]["creatures"][0]["observation"]), 91)
        disabled = self.run_world("tail_disabled", "--creatures", 1, "--steps", 1,
                                  "--detailed-tail-seconds", 0)
        self.assertFalse((disabled / "ecosystem_tail.jsonl").exists())

    def test_population_and_brain_recording(self):
        worlds = []
        for population in (1, 7):
            path = self.run_world(str(population), "--creatures", population,
                                  "--steps", 30, "--record-every", 10,
                                  "--no-reproduction", "--seed", 17, "--record-brains", 1,
                                  "--record-brain-graphs", 1, "--record-observations", 1, "--detailed-tail-seconds", 0)
            lines = [json.loads(line) for line in (path / "ecosystem.jsonl").read_text().splitlines()]
            metadata, final = lines[0], lines[-1]
            self.assertEqual(metadata["type"], "metadata")
            self.assertEqual(len(metadata["brains"]), population)
            self.assertEqual(len(metadata["input_labels"]), 91)
            self.assertEqual(len(final["creatures"]), population)
            self.assertEqual(len({c["id"] for c in final["creatures"]}), population)
            self.assertGreater(final["totals"]["spikes"], 0)
            for creature in final["creatures"]:
                self.assertEqual(len(creature["observation"]), 91)
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
        self.assertGreater(metadata["width"], metadata["nursery"]["size"])
        self.assertGreater(metadata["height"], metadata["nursery"]["size"])
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

    def test_initial_ancestor_mutation_resume_and_legacy(self):
        first = self.run_world("varied_ancestors", "--creatures", 8, "--steps", 2,
            "--mutate-initial-ancestors", 1, "--no-reproduction", "--record-brain-graphs", 1,
            "--detailed-tail-seconds", 0)
        metadata, initial = [json.loads(line) for line in
            (first / "ecosystem.jsonl").read_text().splitlines()[:2]]
        self.assertTrue(metadata["mutate_initial_ancestors"])
        self.assertEqual(len({c["genome_id"] for c in initial["creatures"]}), 8)
        self.assertGreater(len({json.dumps(b["synapses"], sort_keys=True) for b in metadata["brains"]}), 1)
        resumed = self.run_world("varied_resume", "--resume", first / "checkpoint.eco", "--steps", 2)
        summary = json.loads((resumed / "summary.json").read_text())
        self.assertTrue(summary["mutate_initial_ancestors"])
        lines = self.legacy_checkpoint_lines(first / "checkpoint.eco")
        self.assertEqual(lines[0].strip(), "NEUROEVO_ECOSYSTEM_33")
        del lines[17]
        lines[0] = "NEUROEVO_ECOSYSTEM_32"
        legacy = first / "v32.eco"
        legacy.write_text("\n".join(lines) + "\n")
        old = self.run_world("ancestor_legacy", "--resume", legacy, "--steps", 1,
            "--record-brain-graphs", 1, "--detailed-tail-seconds", 0)
        old_meta = json.loads((old / "ecosystem.jsonl").read_text().splitlines()[0])
        self.assertFalse(old_meta["mutate_initial_ancestors"])
        self.assertEqual(old_meta["brains"], metadata["brains"])

    def test_sparse_ancestor_nursery_is_exposed_in_the_cli(self):
        path = self.run_world("ancestor", "--creatures", 1, "--habitat", "ancestor-nursery",
                              "--mutate-initial-ancestors", 0,
                              "--steps", 5, "--record-every", 5, "--record-brains", 0,
                              "--record-observations", 0, "--record-brain-graphs", 1, "--detailed-tail-seconds", 0)
        lines = [json.loads(line) for line in (path / "ecosystem.jsonl").read_text().splitlines()]
        brain = lines[0]["brains"][0]
        self.assertEqual((brain["inputs"], brain["outputs"]), (91, 6))
        self.assertEqual(len(brain["neurons"]), 102)
        self.assertEqual(len(brain["synapses"]), 32)
        self.assertTrue(all(c["controller"] == "spiking" for c in lines[-1]["creatures"]))
        summary = json.loads((path / "summary.json").read_text())
        self.assertEqual(summary["founder_brain"], "sparse-ancestor")
        self.assertEqual(summary["habitat"], "ancestor-nursery")

        group = self.run_world("ancestor_group", "--creatures", 4, "--founder-brain", "sparse-ancestor",
                               "--mutate-initial-ancestors", 0,
                               "--steps", 1, "--record-every", 1, "--no-reproduction")
        frame = json.loads((group / "ecosystem.jsonl").read_text().splitlines()[1])
        self.assertEqual(len({creature["genome_id"] for creature in frame["creatures"]}), 1)
        self.assertEqual(len({creature["id"] for creature in frame["creatures"]}), 4)

    def test_compact_history_and_bounded_detailed_tail(self):
        path = self.run_world("compact_tail", "--creatures", 3, "--steps", 35,
                              "--record-every", 20, "--record-brains", 1,
                              "--record-observations", 1, "--record-brain-graphs", 1,
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
        self.assertEqual(len(tail[-1]["creatures"][0]["observation"]), 91)
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
        first = self.run_world("source", "--creatures", 2, "--steps", 5, "--record-brain-graphs", 1, "--detailed-tail-seconds", 0)
        second = self.run_world("new", "--creatures", 4, "--steps", 1, "--seed", 99,
                                "--food-assignment", "b-rich", "--founders", first / "checkpoint.eco", "--record-brain-graphs", 1, "--detailed-tail-seconds", 0)
        original = json.loads((first / "ecosystem.jsonl").read_text().splitlines()[0])
        lines = [json.loads(x) for x in (second / "ecosystem.jsonl").read_text().splitlines()]
        self.assertFalse(lines[0]["fruit_a_rich"])
        self.assertEqual(lines[0]["brains"][0]["synapses"], original["brains"][0]["synapses"])
        self.assertTrue(all(c["spikes"] == 0 and c["age"] == 0 for c in lines[1]["creatures"]))

    def test_starting_genomes_samples_reproducibly_and_resets_founders(self):
        source = self.run_world("source with spaces", "--founder-brain", "random", "--creatures", 3, "--steps", 20,
                                "--predation", 1, "--founder-mass", 0.7, "--founder-carnivory", 0.2, "--record-brain-graphs", 1, "--detailed-tail-seconds", 0)
        saved = (source / "checkpoint.eco").read_bytes()
        source_meta = json.loads((source / "ecosystem.jsonl").read_text().splitlines()[0])
        source_brains = {b["id"]: b for b in source_meta["brains"]}
        outputs = []
        for name, seed in (("sampled", 49), ("repeated", 49), ("different", 50)):
            out = self.run_world(name, "--starting-genomes", source, "--creatures", 20,
                                 "--seed", seed, "--steps", 1, "--founder-energy", 80,
                                 "--food-assignment", "b-rich", "--record-brain-graphs", 1, "--record-brains", 1, "--detailed-tail-seconds", 0)
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
            (source, ("--predation", 0, "--storm-health-damage", 0), "interface cannot be reduced"),
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
