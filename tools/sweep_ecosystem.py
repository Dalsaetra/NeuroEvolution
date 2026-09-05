"""Compare ecosystem/reproduction settings across identical random seeds.

The sweep records only initial/final compact frames. It is designed to identify
settings that produce breeding descendants before committing to a long replay.
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import statistics
import subprocess
import time


VARIANTS: dict[str, list[str]] = {
    "baseline": [],
    "reproduction": [
        "--maturity-age", "60", "--reproduction-threshold", "130",
        "--reproduction-cost", "65", "--offspring-energy", "60",
        "--reproduction-cooldown", "90",
    ],
    "local-mutation": [
        "--mutate-weight-prob", "0.30", "--mutate-neuron-prob", "0.22",
        "--mutate-add-synapse-prob", "0.55", "--mutate-add-neuron-prob", "0.20",
        "--mutate-remove-synapse-prob", "0.05",
    ],
    "lower-cost": ["--basal-cost", "0.16", "--storm-cost", "0.60"],
    "combined": [
        "--maturity-age", "60", "--reproduction-threshold", "130",
        "--reproduction-cost", "65", "--offspring-energy", "60",
        "--reproduction-cooldown", "90", "--mutate-weight-prob", "0.30",
        "--mutate-neuron-prob", "0.22", "--mutate-add-synapse-prob", "0.55",
        "--mutate-add-neuron-prob", "0.20", "--mutate-remove-synapse-prob", "0.05",
    ],
}


def executable_default() -> Path:
    root = Path(__file__).resolve().parents[1]
    for candidate in (root / "build/neuroevo_ecosystem.exe",
                      root / "build/Release/neuroevo_ecosystem.exe",
                      root / "build/neuroevo_ecosystem"):
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("Build neuroevo_ecosystem first or pass --executable")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--seeds", default="7,11,19", help="Comma-separated world seeds")
    parser.add_argument("--variants", default=",".join(VARIANTS), help="Comma-separated variant names")
    parser.add_argument("--steps", type=int, default=20_000)
    parser.add_argument("--creatures", type=int, default=24)
    parser.add_argument("--founder-brain", choices=("random", "sparse-ancestor"), default="sparse-ancestor")
    args = parser.parse_args()
    if args.steps < 1 or args.creatures < 1:
        parser.error("--steps and --creatures must be positive")
    try:
        seeds = [int(value) for value in args.seeds.split(",")]
        variants = args.variants.split(",")
        unknown = set(variants) - set(VARIANTS)
        if unknown:
            parser.error(f"unknown variants: {', '.join(sorted(unknown))}")
        executable = (args.executable or executable_default()).resolve()
    except ValueError:
        parser.error("--seeds must contain comma-separated integers")
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")
    root = Path(__file__).resolve().parents[1]
    destination = args.output or root / "runs" / f"ecosystem_sweep_{time.strftime('%Y%m%d_%H%M%S')}"
    destination = destination.resolve()
    if destination.exists():
        parser.error(f"output already exists: {destination}")
    destination.mkdir(parents=True)
    rows: list[dict[str, object]] = []
    for variant in variants:
        for seed in seeds:
            run = destination / f"{variant}_seed_{seed}"
            command = [str(executable), "--out", str(run), "--creatures", str(args.creatures),
                       "--steps", str(args.steps), "--seed", str(seed), "--establishment", "1",
                       "--founder-brain", args.founder_brain,
                       "--record-every", str(args.steps), "--record-brains", "0",
                       "--record-observations", "0", "--record-brain-graphs", "0",
                       "--record-routine-events", "0", *VARIANTS[variant]]
            print(f"[{variant}, seed {seed}]", flush=True)
            subprocess.run(command, cwd=root, check=True)
            summary = json.loads((run / "summary.json").read_text(encoding="utf-8"))
            simulated_seconds = float(summary["time"])
            births = int(summary["births"])
            immigrants = int(summary["immigrants"])
            food_energy = float(summary["food_energy"])
            operating_energy = float(summary["operating_energy"])
            rows.append({
                "variant": variant, "seed": seed, "steps": summary["steps_run"],
                "simulated_seconds": simulated_seconds, "population": summary["population"],
                "births": births, "births_per_1000s": births * 1000 / simulated_seconds,
                "births_first_100s": summary["births_first_100s"],
                "births_after_100s": summary["births_after_100s"],
                "founder_births": summary["founder_births"],
                "immigrant_births": summary["immigrant_births"],
                "descendant_births": summary["descendant_births"],
                "mature_offspring": summary["mature_offspring"],
                "natural_spiking_breeders": summary["natural_spiking_breeders"],
                "immigrants": immigrants, "births_per_100_immigrants": 100 * births / max(1, immigrants),
                "immigrant_slight_mutations": summary["immigrant_slight_mutations"],
                "immigrant_strong_mutations": summary["immigrant_strong_mutations"],
                "archive_empty_checks": summary["archive_empty_checks"],
                "immigrants_per_1000s": immigrants * 1000 / simulated_seconds,
                "food_energy_per_1000s": food_energy * 1000 / simulated_seconds,
                "food_to_operating_energy": food_energy / max(1, operating_energy),
                "archive_entries": summary["archive_entries"],
                "archive_best_score": summary["archive_best_score"],
                "archive_median_score": summary["archive_median_score"],
                "mean_hidden_neurons": summary["mean_hidden_neurons"],
                "max_hidden_neurons": summary["max_hidden_neurons"],
                "wall_seconds": summary["wall_seconds"],
            })
    columns = list(rows[0])
    with (destination / "sweep.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns); writer.writeheader(); writer.writerows(rows)
    print("\nMedian results")
    print("variant          births/1000s  mature offspring  descendant breeders  births/100 immigrants")
    for variant in variants:
        sample = [row for row in rows if row["variant"] == variant]
        median = lambda key: statistics.median(float(row[key]) for row in sample)
        print(f"{variant:16} {median('births_per_1000s'):12.2f} {median('mature_offspring'):17.1f} "
              f"{median('natural_spiking_breeders'):20.1f} {median('births_per_100_immigrants'):22.2f}")
    print(f"\nSweep results: {destination / 'sweep.csv'}")


if __name__ == "__main__":
    main()
