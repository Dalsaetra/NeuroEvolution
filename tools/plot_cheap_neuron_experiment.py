"""Summarize reproducible research results; python tools/plot_cheap_neuron_experiment.py."""
import csv
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

out = Path(sys.argv[1] if len(sys.argv) > 1 else "runs/cheap-neurons")

def read(name):
    with (out / name).open() as stream:
        return list(csv.DictReader(stream))

profile = defaultdict(list)
for row in read("profile.csv"):
    profile[row["workload"], row["model"]].append(float(row["wall_ms"]))
summary = {"profile": {}, "validation": {}}
for (workload, model), values in profile.items():
    summary["profile"][f"{workload}/{model}"] = {
        "median_ms": statistics.median(values),
        "min_ms": min(values), "max_ms": max(values),
        "speedup_vs_izh": statistics.median(profile[workload, "izh_rs"]) / statistics.median(values),
    }
validation = defaultdict(list)
for row in read("validation.csv"):
    validation[row["model"], row["mode"]].append(row)
for (model, mode), rows in validation.items():
    summary["validation"][f"{model}/{mode}"] = {
        "passed": sum(int(row["pass"]) for row in rows), "total": len(rows)
    }
summary["search_rows"] = len(read("sweep.csv")) + len(read("focused.csv"))
(out / "analysis.json").write_text(json.dumps(summary, indent=2) + "\n")

colors = {"filtered_lif": "#087e8b", "adaptive_lif": "#d47b25", "rate": "#7855ab"}
names = {"filtered_lif": "Filtered LIF", "adaptive_lif": "Adaptive LIF", "rate": "Rate unit"}
plt.rcParams.update({"font.size": 10, "axes.spines.top": False, "axes.spines.right": False})
fig, axes = plt.subplots(2, 2, figsize=(13, 9), layout="constrained")
models = ["legacy_lif", "izh_rs", "filtered_lif", "filtered_lif_2ms", "adaptive_lif", "rate"]
labels = ["Existing LIF · 20 ms", "Izhikevich · 1 ms", "Filtered LIF · 5 ms", "Filtered LIF · 2 ms", "Adaptive LIF · 5 ms", "Rate unit · 20 ms"]
costs = [statistics.median(profile["active", model]) / statistics.median(profile["active", "izh_rs"]) for model in models]
ax = axes[0, 0]
bars = ax.barh(labels, costs, color=["#999999", "#555555", "#087e8b", "#087e8b", "#d47b25", "#7855ab"])
ax.invert_yaxis()
for bar, cost in zip(bars, costs):
    ax.text(cost + .02, bar.get_y() + .4, f"{cost:.3f}×", va="center")
ax.set(xlim=(0, 1.23), xlabel="Cost per simulated second / Izhikevich cost", title="Minimal active circuits; excludes ecosystem overhead")

ax = axes[0, 1]
memory = read("retention.csv")
for model, dt in [("filtered_lif", ".005"), ("adaptive_lif", ".005"), ("rate", ".02")]:
    rows = [r for r in memory if r["model"] == model and float(r["dt"]) == float(dt) and r["inhibition"] == "0"]
    ax.plot([float(r["drive"]) for r in rows], [float(r["late_rate"]) for r in rows], "o-", color=colors[model], label=names[model])
rows = [r for r in memory if r["model"] == "filtered_lif" and r["dt"] == "0.001" and r["inhibition"] == "0"]
ax.plot([float(r["drive"]) for r in rows], [float(r["late_rate"]) for r in rows], "--", color=colors["filtered_lif"], label="Filtered LIF · 1 ms check")
ax.set(xlabel="External DC drive (onset = 1)", ylabel="Spikes/s, or 100 × rate-unit activity", title="Self-excitation preserves activity after a 100 ms kick")
ax.legend(fontsize=8)

ax = axes[1, 0]
trace = [r for r in read("trace.csv") if r["model"] == "rate"]
ax.plot([float(r["time"]) for r in trace], [float(r["output_a"]) for r in trace], label="A", color="#087e8b")
ax.plot([float(r["time"]) for r in trace], [float(r["output_b"]) for r in trace], label="B", color="#d47b25")
ax.axvline(4, color="black", linestyle="--", linewidth=1)
ax.text(4.1, .94, "Inputs reverse")
ax.set(xlabel="Time (s)", ylabel="Continuous activity (0–1)", ylim=(0, 1.04), title="Rate-unit pair: drive 3.6 vs 1.2, then 1.2 vs 3.6")
ax.legend()

ax = axes[1, 1]
for model in colors:
    rows = validation[model, "contrast"]
    def dominance(row):
        a, b = float(row["a_after"]), float(row["b_after"])
        return (b-a)/(a+b) if a+b else 0
    ax.plot([float(r["strong"]) for r in rows], [dominance(r) for r in rows], "o-", label=names[model], color=colors[model])
ax.axhline(.5, color="gray", linestyle=":", label="3:1 criterion")
ax.set(xlabel="Stronger drive after reversal (weaker drive = 1.2)", ylabel="Dominance of new winner", ylim=(-1.1, 1.1), title="Persistent states resist smaller input reversals")
ax.legend(fontsize=8)
fig.suptitle("Cheaper neuron alternatives: memory, competition, and their tradeoff", fontsize=15)
fig.savefig(out / "cheap-neurons.png", dpi=170)
fig.savefig(out / "cheap-neurons.svg")
print(json.dumps(summary, indent=2))
