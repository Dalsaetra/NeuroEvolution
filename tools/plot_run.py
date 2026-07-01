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

    fig, (fitness_ax, food_ax) = plt.subplots(2, 1, figsize=(9, 7), sharex=True)
    fitness_ax.plot(generations, best, label="best fitness", color="#1f77b4")
    fitness_ax.plot(generations, mean, label="mean fitness", color="#444444", linestyle="--")
    fitness_ax.set_ylabel("fitness")
    fitness_ax.legend()
    fitness_ax.grid(True, alpha=0.25)

    food_ax.plot(generations, foods, label="mean food", color="#2ca02c")
    food_ax.set_xlabel("generation")
    food_ax.set_ylabel("foods collected")
    food_ax.grid(True, alpha=0.25)

    fig.tight_layout()
    fig.savefig(run_dir / "fitness.png", dpi=160)
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


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot NeuroEvolution CSV outputs.")
    parser.add_argument("run_dir", type=Path, help="Directory containing stats.csv and best_trajectory.csv")
    args = parser.parse_args()

    run_dir = args.run_dir.resolve()
    plot_stats(run_dir)
    plot_trajectory(run_dir)
    print(f"Wrote {run_dir / 'fitness.png'}")
    print(f"Wrote {run_dir / 'trajectory.png'}")


if __name__ == "__main__":
    main()
