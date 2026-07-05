from __future__ import annotations

import argparse
import csv
import html
import json
import math
from pathlib import Path
from typing import Any


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


def float_value(row: dict[str, str], key: str) -> float:
    value = row.get(key, "0") or "0"
    try:
        parsed = float(value)
    except ValueError:
        return 0.0
    return parsed if math.isfinite(parsed) else 0.0


def int_value(row: dict[str, str], key: str) -> int:
    value = row.get(key, "0") or "0"
    try:
        return int(float(value))
    except ValueError:
        return 0


def trajectory_payload(solution_dir: Path) -> list[dict[str, Any]]:
    path = solution_dir / "best_trajectory.csv"
    if not path.exists():
        return []

    return [
        {
            "step": int_value(row, "step"),
            "x": float_value(row, "x"),
            "y": float_value(row, "y"),
            "target_x": float_value(row, "target_x"),
            "target_y": float_value(row, "target_y"),
            "distance": float_value(row, "distance"),
            "heading": float_value(row, "heading"),
            "speed_command": float_value(row, "speed_command"),
            "turn_command": float_value(row, "turn_command"),
            "spikes": int_value(row, "cumulative_spikes"),
            "foods": int_value(row, "foods_collected"),
        }
        for row in read_csv(path)
    ]


def load_solutions(run_dir: Path) -> list[dict[str, Any]]:
    manifest_path = run_dir / "pareto_front.csv"
    if not manifest_path.exists():
        raise FileNotFoundError(f"Missing Pareto front manifest: {manifest_path}")

    solutions: list[dict[str, Any]] = []
    for row in read_csv(manifest_path):
        solution_dir = run_dir / row["trajectory_dir"]
        points = trajectory_payload(solution_dir)
        solutions.append(
            {
                "index": int_value(row, "solution_index"),
                "genome": int_value(row, "genome_id"),
                "species": int_value(row, "species_id"),
                "rank": int_value(row, "pareto_rank"),
                "crowding": float_value(row, "crowding_distance"),
                "task": float_value(row, "task_score_norm"),
                "spike": float_value(row, "spike_energy_norm"),
                "synapse": float_value(row, "synapse_count_norm"),
                "neuron": float_value(row, "neuron_count_norm"),
                "time": float_value(row, "time_cost_norm"),
                "rawFoods": float_value(row, "raw_foods"),
                "rawFitness": float_value(row, "raw_fitness"),
                "recordedFoods": float_value(row, "recorded_foods"),
                "recordedFitness": float_value(row, "recorded_fitness"),
                "recordedSpikes": float_value(row, "recorded_spikes"),
                "neurons": int_value(row, "neurons"),
                "hidden": int_value(row, "hidden_nodes"),
                "enabledSynapses": int_value(row, "enabled_synapses"),
                "disabledSynapses": int_value(row, "disabled_synapses"),
                "path": row["trajectory_dir"],
                "points": points,
            }
        )
    return solutions


def render_html(run_dir: Path, metadata: dict[str, float], solutions: list[dict[str, Any]]) -> str:
    title = f"Pareto Front - {run_dir.name}"
    payload = {
        "title": title,
        "metadata": metadata,
        "solutions": solutions,
    }
    payload_json = json.dumps(payload, separators=(",", ":"))

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{html.escape(title)}</title>
  <style>
    :root {{
      color-scheme: light;
      --bg: #f7f8fa;
      --panel: #ffffff;
      --text: #172033;
      --muted: #667085;
      --border: #d7dce5;
      --blue: #2563eb;
      --green: #12805c;
      --orange: #d97706;
      --red: #dc2626;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      background: var(--bg);
      color: var(--text);
      font-family: Segoe UI, system-ui, -apple-system, BlinkMacSystemFont, sans-serif;
    }}
    main {{
      display: grid;
      grid-template-columns: minmax(320px, 0.9fr) minmax(420px, 1.4fr);
      min-height: 100vh;
      gap: 16px;
      padding: 16px;
    }}
    .panel, .solution {{
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 8px;
      box-shadow: 0 1px 2px rgb(15 23 42 / 7%);
    }}
    .panel {{
      display: grid;
      align-content: start;
      gap: 14px;
      padding: 16px;
    }}
    h1, h2, h3 {{
      margin: 0;
      font-weight: 650;
    }}
    h1 {{ font-size: 20px; }}
    h2 {{ font-size: 14px; }}
    h3 {{ font-size: 13px; }}
    .muted {{
      color: var(--muted);
      font-size: 12px;
    }}
    canvas {{
      width: 100%;
      aspect-ratio: 1 / 1;
      border: 1px solid var(--border);
      border-radius: 6px;
      background: #fbfcfe;
      display: block;
    }}
    .legend {{
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      color: var(--muted);
      font-size: 12px;
    }}
    .legend span {{
      display: inline-flex;
      align-items: center;
      gap: 6px;
    }}
    .dot {{
      width: 10px;
      height: 10px;
      border-radius: 50%;
      background: var(--blue);
      display: inline-block;
    }}
    .metrics {{
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 8px;
    }}
    .metric {{
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 8px;
      min-height: 54px;
    }}
    .metric span {{
      display: block;
      color: var(--muted);
      font-size: 11px;
      margin-bottom: 3px;
    }}
    .metric strong {{
      display: block;
      font-size: 16px;
      overflow-wrap: anywhere;
    }}
    .solutions {{
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
      gap: 16px;
      align-content: start;
    }}
    .solution {{
      display: grid;
      gap: 10px;
      padding: 12px;
    }}
    .solution.selected {{
      outline: 2px solid var(--blue);
      outline-offset: 2px;
    }}
    .solution-header {{
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 8px;
    }}
    .compact {{
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 6px;
      font-size: 11px;
      color: var(--muted);
    }}
    .compact strong {{
      display: block;
      color: var(--text);
      font-size: 13px;
      margin-top: 2px;
    }}
    @media (max-width: 960px) {{
      main {{
        grid-template-columns: 1fr;
      }}
    }}
  </style>
</head>
<body>
  <main>
    <section class="panel">
      <h1>{html.escape(title)}</h1>
      <canvas id="scatter" width="760" height="760"></canvas>
      <div class="legend">
        <span><i class="dot" style="background: var(--blue);"></i>solution</span>
        <span><i class="dot" style="background: var(--green);"></i>selected</span>
        <span><i class="dot" style="background: var(--orange);"></i>larger point means more enabled synapses</span>
      </div>
      <div class="metrics" id="selectedMetrics"></div>
    </section>
    <section class="solutions" id="solutions"></section>
  </main>

  <script id="payload" type="application/json">{payload_json}</script>
  <script>
    const payload = JSON.parse(document.getElementById("payload").textContent);
    const metadata = payload.metadata || {{}};
    const solutions = payload.solutions || [];
    const width = metadata.environment_width || 1;
    const height = metadata.environment_height || 1;
    const targetRadius = metadata.environment_target_radius || 0.075;
    let selected = 0;

    function fmt(value, digits = 3) {{
      if (!Number.isFinite(Number(value))) return String(value);
      return Number(value).toFixed(digits);
    }}

    function pointScale(values, fallback = 1) {{
      const finite = values.filter((value) => Number.isFinite(value));
      if (!finite.length) return fallback;
      const maxValue = Math.max(...finite);
      return maxValue > 0 ? maxValue : fallback;
    }}

    function drawTrajectory(canvas, solution, highlight) {{
      const ctx = canvas.getContext("2d");
      const points = solution.points || [];
      const pad = 26;
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = "#fbfcfe";
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.strokeStyle = "#d7dce5";
      ctx.strokeRect(pad, pad, canvas.width - 2 * pad, canvas.height - 2 * pad);
      if (!points.length) return;

      const sx = (x) => pad + (x / width) * (canvas.width - 2 * pad);
      const sy = (y) => canvas.height - pad - (y / height) * (canvas.height - 2 * pad);
      const sr = (r) => r * Math.min(canvas.width - 2 * pad, canvas.height - 2 * pad) / Math.max(width, height);

      ctx.strokeStyle = highlight ? "#12805c" : "#2563eb";
      ctx.lineWidth = highlight ? 3 : 2;
      ctx.beginPath();
      points.forEach((point, index) => {{
        if (index === 0) ctx.moveTo(sx(point.x), sy(point.y));
        else ctx.lineTo(sx(point.x), sy(point.y));
      }});
      ctx.stroke();

      const first = points[0];
      const last = points[points.length - 1];
      ctx.fillStyle = "#12805c";
      ctx.beginPath();
      ctx.arc(sx(first.x), sy(first.y), 5, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = "rgba(217, 119, 6, 0.18)";
      ctx.strokeStyle = "#d97706";
      ctx.beginPath();
      ctx.arc(sx(last.target_x), sy(last.target_y), sr(targetRadius), 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();

      ctx.fillStyle = "#172033";
      ctx.beginPath();
      ctx.arc(sx(last.x), sy(last.y), 6, 0, Math.PI * 2);
      ctx.fill();
    }}

    function drawScatter() {{
      const canvas = document.getElementById("scatter");
      const ctx = canvas.getContext("2d");
      const pad = 58;
      const maxSpike = pointScale(solutions.map((solution) => solution.spike), 1);
      const maxTask = pointScale(solutions.map((solution) => solution.task), 1);
      const maxSynapse = pointScale(solutions.map((solution) => solution.enabledSynapses), 1);
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = "#fbfcfe";
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.strokeStyle = "#d7dce5";
      ctx.strokeRect(pad, pad, canvas.width - 2 * pad, canvas.height - 2 * pad);
      ctx.fillStyle = "#667085";
      ctx.font = "14px Segoe UI, sans-serif";
      ctx.fillText("spike energy norm", canvas.width * 0.38, canvas.height - 16);
      ctx.save();
      ctx.translate(18, canvas.height * 0.58);
      ctx.rotate(-Math.PI / 2);
      ctx.fillText("task score norm", 0, 0);
      ctx.restore();

      solutions.forEach((solution, index) => {{
        const x = pad + (solution.spike / maxSpike) * (canvas.width - 2 * pad);
        const y = canvas.height - pad - (solution.task / maxTask) * (canvas.height - 2 * pad);
        const radius = 7 + 13 * Math.sqrt(Math.max(0, solution.enabledSynapses) / maxSynapse);
        ctx.fillStyle = index === selected ? "#12805c" : "#2563eb";
        ctx.strokeStyle = index === selected ? "#064e3b" : "#1e3a8a";
        ctx.lineWidth = index === selected ? 3 : 1;
        ctx.beginPath();
        ctx.arc(x, y, radius, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
        ctx.fillStyle = "#ffffff";
        ctx.font = "12px Segoe UI, sans-serif";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(String(solution.index), x, y);
      }});
      ctx.textAlign = "left";
      ctx.textBaseline = "alphabetic";
    }}

    function metric(label, value) {{
      return `<div class="metric"><span>${{label}}</span><strong>${{value}}</strong></div>`;
    }}

    function renderSelectedMetrics() {{
      const solution = solutions[selected];
      const panel = document.getElementById("selectedMetrics");
      if (!solution) {{
        panel.innerHTML = "";
        return;
      }}
      panel.innerHTML = [
        metric("Solution", solution.index),
        metric("Genome", solution.genome),
        metric("Task", fmt(solution.task)),
        metric("Spike Energy", fmt(solution.spike)),
        metric("Synapse Cost", fmt(solution.synapse)),
        metric("Recorded Food", fmt(solution.recordedFoods, 1)),
        metric("Neurons", solution.neurons),
        metric("Enabled Synapses", solution.enabledSynapses),
      ].join("");
    }}

    function renderSolutions() {{
      const container = document.getElementById("solutions");
      container.innerHTML = "";
      solutions.forEach((solution, index) => {{
        const article = document.createElement("article");
        article.className = `solution${{index === selected ? " selected" : ""}}`;
        article.innerHTML = `
          <div class="solution-header">
            <h3>Solution ${{solution.index}}</h3>
            <span class="muted">species ${{solution.species}}</span>
          </div>
          <canvas width="420" height="420"></canvas>
          <div class="compact">
            <span>task<strong>${{fmt(solution.task)}}</strong></span>
            <span>spike<strong>${{fmt(solution.spike)}}</strong></span>
            <span>synapses<strong>${{solution.enabledSynapses}}</strong></span>
            <span>recorded food<strong>${{fmt(solution.recordedFoods, 1)}}</strong></span>
            <span>fitness<strong>${{fmt(solution.recordedFitness, 2)}}</strong></span>
            <span>hidden<strong>${{solution.hidden}}</strong></span>
          </div>
        `;
        article.addEventListener("click", () => {{
          selected = index;
          drawAll();
        }});
        container.appendChild(article);
      }});
    }}

    function drawAll() {{
      renderSolutions();
      for (const [index, article] of [...document.querySelectorAll(".solution")].entries()) {{
        drawTrajectory(article.querySelector("canvas"), solutions[index], index === selected);
      }}
      drawScatter();
      renderSelectedMetrics();
    }}

    drawAll();
  </script>
</body>
</html>
"""


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate an HTML Pareto-front comparison viewer.")
    parser.add_argument("run_dir", type=Path, help="Run directory containing pareto_front.csv")
    parser.add_argument("--out", type=Path, default=None, help="Output HTML path, default: RUN_DIR/pareto_front.html")
    args = parser.parse_args()

    run_dir = args.run_dir.resolve()
    output = args.out.resolve() if args.out else run_dir / "pareto_front.html"
    metadata = read_metadata(run_dir)
    solutions = load_solutions(run_dir)
    output.write_text(render_html(run_dir, metadata, solutions), encoding="utf-8")
    print(f"Wrote {output}")


if __name__ == "__main__":
    main()
