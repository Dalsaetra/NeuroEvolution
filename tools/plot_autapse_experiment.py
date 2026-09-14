"""Plot the C++ experiment outputs; requires matplotlib, no pandas."""
import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser()
parser.add_argument("directory", nargs="?", default="runs/autapse")
args = parser.parse_args()
out = Path(args.directory)

def read(name):
    with (out / name).open(newline="") as stream:
        return [{key: float(value) for key, value in row.items()}
                for row in csv.DictReader(stream)]

rates, pairs, perturb = (read(name) for name in
                         ("rates.csv", "competition.csv", "inhibition.csv"))
plt.rcParams.update({"font.size": 10, "axes.spines.top": False,
                     "axes.spines.right": False})
fig, axes = plt.subplots(2, 2, figsize=(13, 8.5), layout="constrained")
colors = ["#2369a1", "#df8534", "#24856c"]
ax = axes[0, 0]
for d, color in zip((3, 5, 8), colors):
    selected = [r for r in rates if r["delay_steps"] == d
                and r["self_weight"] == 2 and r["drive_weight"] == 1
                and r["phase"] == 0]
    ax.plot([r["input_rate_hz"] for r in selected],
            [r["output_rate_hz"] for r in selected], "o-", ms=3,
            label=f"Self delay {d*20} ms", color=color)
ax.set(title="A. More excitation does not guarantee a higher rate",
       xlabel="External excitatory spike rate (Hz); weight = 1",
       ylabel="Neuron spike rate (Hz)", ylim=(-0.5, 18))
ax.legend(loc="lower right")

ax = axes[0, 1]
selected = [r for r in rates if r["delay_steps"] == 8 and r["self_weight"] == 2
            and r["drive_weight"] == 1 and r["phase"] == 0]
ax.plot([r["input_rate_hz"] for r in selected],
        [r["output_rate_hz"] for r in selected], "o-", ms=3, label="During input", color=colors[0])
ax.plot([r["input_rate_hz"] for r in selected],
        [r["after_withdrawal_hz"] for r in selected], "s--", ms=3,
        label="20–40 s after input stops", color=colors[1])
ax.set(title="B. A 160 ms loop can retain two distinct rates",
       xlabel="Previous external excitatory spike rate (Hz)",
       ylabel="Neuron spike rate (Hz)", ylim=(0, 15))
ax.legend()

ax = axes[1, 0]
weights = [1.5625 + i/100 for i in range(95)]
for d, color in zip((3, 5, 8), colors):
    factor = sum(0.8**j for j in range(d-2))
    ax.plot(weights, [(w-1.5625)/factor for w in weights], color=color,
            label=f"Delay {d*20} ms")
    # Highest tested continuous inhibition survived, bounded by the coarse grid.
    for w in (1.6, 1.8, 2.0, 2.5):
        survived = [r["inhibitory_weight"] for r in perturb
                    if r["delay_steps"] == d and r["self_weight"] == w
                    and r["mode"] == 2 and r["rate_hz"] > 0]
        if survived:
            ax.scatter([w], [max(survived)], color=color, s=20)
ax.set(title="C. Timing-independent tolerance has a finite bound",
       xlabel="Self-excitatory weight",
       ylabel="Maximum inhibitory weight arriving every step", ylim=(0, 1))
ax.legend(title="Lines: analytic bound\nDots: largest tested survivor")

ax = axes[1, 1]
labels, strong, ties, weak = [], [], [], []
for d in (1, 3, 5, 8):
    rows = [r for r in pairs if r["cross_delay"] == d and r["cross_weight"] > 0]
    n = len(rows)
    labels.append(f"{20*d} ms")
    strong.append(100*sum(r["rate_strong"] > r["rate_weak"]+0.1 for r in rows)/n)
    weak.append(100*sum(r["rate_weak"] > r["rate_strong"]+0.1 for r in rows)/n)
    ties.append(100-strong[-1]-weak[-1])
ax.bar(labels, strong, color=colors[2], label="Stronger input fires faster")
ax.bar(labels, ties, bottom=strong, color="#c6ced5", label="Tie (including both silent)")
ax.bar(labels, weak, bottom=[a+b for a,b in zip(strong,ties)],
       color="#be5b5b", label="Weaker input fires faster")
ax.set(title="D. Equal-weight mutual inhibition is not a reliable selector",
       xlabel="Symmetric inhibitory travel delay",
       ylabel="Fraction of tested parameter cases (%)", ylim=(0, 100))
ax.legend(loc="lower right", fontsize=8)
fig.suptitle("Delayed self-excitation in the actual NeuroEvolution spiking runtime",
             fontsize=15, fontweight="bold")
fig.savefig(out/"autapse-results.png", dpi=160)
fig.savefig(out/"autapse-results.svg")
print(out/"autapse-results.png")
