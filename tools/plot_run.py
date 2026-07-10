from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import Circle


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def read_metadata(run_dir: Path) -> dict[str, float]:
    path = run_dir / "metadata.csv"
    if not path.exists():
        return {}

    metadata: dict[str, float] = {}
    for row in read_csv(path):
        try:
            metadata[row["key"]] = float(row["value"])
        except ValueError:
            continue
    return metadata


def plot_stats(run_dir: Path) -> None:
    rows = read_csv(run_dir / "stats.csv")
    generations = [int(row["generation"]) for row in rows]
    best = [float(row["best_fitness"]) for row in rows]
    mean = [float(row["mean_fitness"]) for row in rows]
    foods = [float(row["mean_foods_collected"]) for row in rows]
    occluded_foods = [float(row.get("mean_occluded_foods_collected", 0) or 0) for row in rows]

    fig, (fitness_ax, food_ax) = plt.subplots(2, 1, figsize=(9, 7), sharex=True)
    fitness_ax.plot(generations, best, label="best fitness", color="#1f77b4")
    fitness_ax.plot(generations, mean, label="mean fitness", color="#444444", linestyle="--")
    fitness_ax.set_ylabel("fitness")
    fitness_ax.legend()
    fitness_ax.grid(True, alpha=0.25)

    food_ax.plot(generations, foods, label="mean food", color="#2ca02c")
    if any(value > 0 for value in occluded_foods):
        food_ax.plot(generations, occluded_foods, label="mean food during occlusion", color="#9467bd")
        food_ax.legend()
    food_ax.set_xlabel("generation")
    food_ax.set_ylabel("foods collected")
    food_ax.grid(True, alpha=0.25)

    fig.tight_layout()
    fig.savefig(run_dir / "fitness.png", dpi=160)
    plt.close(fig)

    if "best_task_score" not in rows[0]:
        return

    task = [float(row["best_task_score"]) for row in rows]
    pareto = [float(row.get("number_non_dominated", 0) or 0) for row in rows]
    species = [float(row.get("species_count", 0) or 0) for row in rows]
    spike_energy = [float(row.get("mean_spike_energy_norm", 0) or 0) for row in rows]
    synapse_norm = [float(row.get("mean_synapse_count_norm", 0) or 0) for row in rows]
    neurons = [float(row.get("mean_neurons", 0) or 0) for row in rows]
    synapses = [float(row.get("mean_enabled_synapses", 0) or 0) for row in rows]

    fig, axes = plt.subplots(2, 2, figsize=(11, 8), sharex=True)
    task_ax, diversity_ax, objective_ax, topology_ax = axes.flat

    task_ax.plot(generations, task, label="best task score", color="#2563eb")
    task_ax.set_ylabel("task score")
    task_ax.grid(True, alpha=0.25)
    task_ax.legend()

    diversity_ax.plot(generations, pareto, label="non-dominated", color="#7c3aed")
    diversity_ax.plot(generations, species, label="species", color="#0f766e")
    diversity_ax.set_ylabel("count")
    diversity_ax.grid(True, alpha=0.25)
    diversity_ax.legend()

    objective_ax.plot(generations, spike_energy, label="mean spike energy", color="#dc2626")
    objective_ax.plot(generations, synapse_norm, label="mean synapse count", color="#f59e0b")
    objective_ax.set_xlabel("generation")
    objective_ax.set_ylabel("normalized cost")
    objective_ax.grid(True, alpha=0.25)
    objective_ax.legend()

    topology_ax.plot(generations, neurons, label="mean neurons", color="#334155")
    topology_ax.plot(generations, synapses, label="mean enabled synapses", color="#16a34a")
    topology_ax.set_xlabel("generation")
    topology_ax.set_ylabel("count")
    topology_ax.grid(True, alpha=0.25)
    topology_ax.legend()

    fig.tight_layout()
    fig.savefig(run_dir / "objectives.png", dpi=160)
    plt.close(fig)


def plot_trajectory(run_dir: Path) -> None:
    rows = read_csv(run_dir / "best_trajectory.csv")
    metadata = read_metadata(run_dir)
    x = [float(row["x"]) for row in rows]
    y = [float(row["y"]) for row in rows]
    target_x = [float(row["target_x"]) for row in rows]
    target_y = [float(row["target_y"]) for row in rows]
    width = metadata.get("environment_width", 1.0 if max(x + target_x) <= 1.05 else max(x + target_x))
    height = metadata.get("environment_height", 1.0 if max(y + target_y) <= 1.05 else max(y + target_y))
    target_radius = metadata.get("environment_target_radius", 0.075 if width <= 1.05 and height <= 1.05 else 0.0)

    fig, ax = plt.subplots(figsize=(7, 7))
    ax.plot(x, y, color="#1f77b4", linewidth=1.7, label="creature path")
    ax.scatter(x[:1], y[:1], color="#2ca02c", s=60, label="start")
    ax.scatter(x[-1:], y[-1:], color="#d62728", s=60, label="end")
    ax.scatter(target_x, target_y, color="#ff7f0e", s=12, alpha=0.35, label="targets")
    if target_radius > 0.0 and target_x:
        ax.add_patch(Circle((target_x[-1], target_y[-1]), target_radius, fill=False, color="#ff7f0e", alpha=0.9, linewidth=1.5))
    ax.set_aspect("equal", adjustable="box")
    ax.set_xlim(0.0, width)
    ax.set_ylim(0.0, height)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.grid(True, alpha=0.25)
    ax.legend(loc="best")
    fig.tight_layout()
    fig.savefig(run_dir / "trajectory.png", dpi=160)
    plt.close(fig)


def plot_pareto_front(run_dir: Path) -> None:
    path = run_dir / "pareto_front.csv"
    if not path.exists():
        return

    rows = read_csv(path)
    if not rows:
        return

    spike_energy = [float(row.get("spike_energy_norm", 0) or 0) for row in rows]
    task_score = [float(row.get("task_score_norm", 0) or 0) for row in rows]
    synapse_count = [float(row.get("enabled_synapses", 0) or 0) for row in rows]
    labels = [row.get("solution_index", "") for row in rows]

    max_synapses = max(synapse_count) if synapse_count else 1.0
    sizes = [60.0 + 220.0 * (value / max_synapses if max_synapses > 0 else 0.0) for value in synapse_count]

    fig, ax = plt.subplots(figsize=(7.5, 6.5))
    scatter = ax.scatter(
        spike_energy,
        task_score,
        s=sizes,
        c=synapse_count,
        cmap="viridis",
        alpha=0.85,
        edgecolor="#1f2937",
        linewidth=0.8,
    )
    for x, y, label in zip(spike_energy, task_score, labels):
        ax.annotate(label, (x, y), xytext=(5, 5), textcoords="offset points", fontsize=9)

    ax.set_xlabel("spike energy norm")
    ax.set_ylabel("task score norm")
    ax.set_title("Recorded Pareto-front solutions")
    ax.grid(True, alpha=0.25)
    colorbar = fig.colorbar(scatter, ax=ax)
    colorbar.set_label("enabled synapses")
    fig.tight_layout()
    fig.savefig(run_dir / "pareto_front.png", dpi=160)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot NeuroEvolution CSV outputs.")
    parser.add_argument("run_dir", type=Path, help="Directory containing stats.csv and best_trajectory.csv")
    args = parser.parse_args()

    run_dir = args.run_dir.resolve()
    plot_stats(run_dir)
    plot_trajectory(run_dir)
    plot_pareto_front(run_dir)
    print(f"Wrote {run_dir / 'fitness.png'}")
    if (run_dir / "objectives.png").exists():
        print(f"Wrote {run_dir / 'objectives.png'}")
    if (run_dir / "pareto_front.png").exists():
        print(f"Wrote {run_dir / 'pareto_front.png'}")
    print(f"Wrote {run_dir / 'trajectory.png'}")


if __name__ == "__main__":
    main()
