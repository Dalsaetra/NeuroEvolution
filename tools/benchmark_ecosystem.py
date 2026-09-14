"""Compare release executables for speed and exact simulation output equality.

Keep an executable built before a change, then run:
  python tools/benchmark_ecosystem.py --baseline old.exe --candidate new.exe
Runs are sequential (alternating order), with no timing-based pass/fail threshold.
"""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import tempfile


SCENARIOS = {
    "lif": [],
    "filtered-lif": ["--neuron-model", "filtered-lif"],
    "filtered-lif-2ms": ["--neuron-model", "filtered-lif", "--brain-dt", "0.002"],
    "izhikevich": ["--neuron-model", "izhikevich"],
    "crowded": ["--creatures", "100"],
    "legacy": ["--habitat", "generated", "--predation", "0",
               "--sensorimotor", "legacy", "--founder-brain", "random"],
}
EXACT_FILES = ("initial.eco", "checkpoint.eco", "ecosystem.jsonl",
               "ecosystem_stats.csv", "events.csv")


def fingerprint(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(executable, destination, flags, steps, seed):
    command = [str(executable), "--steps", str(steps), "--seed", str(seed),
               "--out", str(destination), "--record-every", "100",
               "--record-brains", "1", "--record-observations", "1",
               "--record-brain-graphs", "1", "--record-routine-events", "1"] + flags
    completed = subprocess.run(command, capture_output=True, text=True)
    if completed.returncode:
        raise RuntimeError(f"Run failed: {command}\n{completed.stdout}\n{completed.stderr}")
    with (destination / "performance.csv").open(newline="") as source:
        timing = list(csv.DictReader(source))[-1]
    return float(timing["step_wall_seconds"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--steps", type=int, default=3000)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--scenario", choices=SCENARIOS, action="append")
    parser.add_argument("--out", type=Path, help="Fresh directory; otherwise use a temporary directory")
    args = parser.parse_args()
    if args.steps < 1 or args.repeats < 1:
        parser.error("steps and repeats must be positive")
    executables = {"baseline": args.baseline.resolve(strict=True),
                   "candidate": args.candidate.resolve(strict=True)}
    temporary = tempfile.TemporaryDirectory(prefix="neuroevo-benchmark-") if args.out is None else None
    root = Path(temporary.name) if temporary else args.out.resolve()
    if not temporary:
        root.mkdir(parents=True, exist_ok=False)
    report = {"steps": args.steps, "repeats": args.repeats, "seed": args.seed,
              "executables": {name: {"path": str(path), "sha256": fingerprint(path)}
                              for name, path in executables.items()}, "scenarios": {}}
    try:
        for scenario in args.scenario or SCENARIOS:
            samples = {"baseline": [], "candidate": []}
            expected = None
            for repeat in range(args.repeats):
                order = ("baseline", "candidate") if repeat % 2 == 0 else ("candidate", "baseline")
                for label in order:
                    destination = root / f"{scenario}-{repeat}-{label}"
                    seconds = run(executables[label], destination, SCENARIOS[scenario], args.steps, args.seed)
                    samples[label].append(seconds)
                    actual = {name: fingerprint(destination / name) for name in EXACT_FILES}
                    if expected is None:
                        expected = actual
                    elif actual != expected:
                        changed = [name for name in EXACT_FILES if actual[name] != expected[name]]
                        raise RuntimeError(f"Simulation differs in {scenario}, {label}, repeat {repeat}: {changed}")
                print(f"{scenario}, pair {repeat + 1}: exact outputs; "
                      f"{samples['baseline'][-1]:.3f}s -> {samples['candidate'][-1]:.3f}s", flush=True)
            baseline = statistics.median(samples["baseline"])
            candidate = statistics.median(samples["candidate"])
            report["scenarios"][scenario] = {"seconds": samples, "speedup": baseline / candidate,
                                               "exact_sha256": expected}
            print(f"{scenario}: {baseline / candidate:.2f}x speedup (median)", flush=True)
        (root / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
    finally:
        if temporary:
            temporary.cleanup()


if __name__ == "__main__":
    main()
