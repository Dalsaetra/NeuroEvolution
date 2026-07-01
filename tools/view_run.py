from __future__ import annotations

import argparse
import csv
import html
import json
from pathlib import Path


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


def trajectory_payload(run_dir: Path) -> tuple[list[dict[str, float]], dict[str, float]]:
    rows = read_csv(run_dir / "best_trajectory.csv")
    points = [
        {
            "step": int(row["step"]),
            "x": float(row["x"]),
            "y": float(row["y"]),
            "target_x": float(row["target_x"]),
            "target_y": float(row["target_y"]),
            "distance": float(row["distance"]),
            "motor_x": float(row["motor_x"]),
            "motor_y": float(row["motor_y"]),
            "cumulative_spikes": int(row["cumulative_spikes"]),
            "foods_collected": int(row["foods_collected"]),
        }
        for row in rows
    ]
    metadata = read_metadata(run_dir)
    if points:
        max_x = max(max(p["x"], p["target_x"]) for p in points)
        max_y = max(max(p["y"], p["target_y"]) for p in points)
        metadata.setdefault("environment_width", 1.0 if max_x <= 1.05 else max_x)
        metadata.setdefault("environment_height", 1.0 if max_y <= 1.05 else max_y)
        metadata.setdefault("environment_target_radius", 0.075)
    return points, metadata


def render_html(run_dir: Path, points: list[dict[str, float]], metadata: dict[str, float]) -> str:
    title = f"NeuroEvolution Viewer - {run_dir.name}"
    payload = {
        "points": points,
        "metadata": metadata,
        "title": title,
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
      --bg: #f6f7f9;
      --panel: #ffffff;
      --text: #1f2933;
      --muted: #6b7280;
      --border: #d6dae1;
      --path: #2563eb;
      --target: #e86f19;
      --creature: #111827;
      --start: #16803c;
      --food: #0f8f62;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      font-family: Segoe UI, system-ui, -apple-system, BlinkMacSystemFont, sans-serif;
      background: var(--bg);
      color: var(--text);
    }}
    main {{
      display: grid;
      grid-template-columns: minmax(280px, 1fr) 320px;
      gap: 16px;
      min-height: 100vh;
      padding: 16px;
    }}
    .stage, .side {{
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 8px;
      box-shadow: 0 1px 2px rgb(15 23 42 / 8%);
    }}
    .stage {{
      display: grid;
      place-items: center;
      min-height: calc(100vh - 32px);
      padding: 16px;
    }}
    canvas {{
      width: min(calc(100vh - 64px), calc(100vw - 384px));
      height: min(calc(100vh - 64px), calc(100vw - 384px));
      max-width: 100%;
      max-height: 100%;
      background: #fbfcfe;
      border: 1px solid var(--border);
      border-radius: 6px;
    }}
    .side {{
      display: flex;
      flex-direction: column;
      gap: 16px;
      padding: 16px;
    }}
    h1 {{
      margin: 0;
      font-size: 18px;
      font-weight: 650;
    }}
    .muted {{
      color: var(--muted);
      font-size: 13px;
      line-height: 1.4;
    }}
    .controls {{
      display: grid;
      gap: 12px;
    }}
    .button-row {{
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 8px;
    }}
    button {{
      min-height: 36px;
      border: 1px solid var(--border);
      border-radius: 6px;
      background: #f8fafc;
      color: var(--text);
      font: inherit;
      cursor: pointer;
    }}
    button:hover {{ background: #eef2f7; }}
    label {{
      display: grid;
      gap: 6px;
      color: var(--muted);
      font-size: 12px;
    }}
    input[type="range"] {{
      width: 100%;
    }}
    .readout {{
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
    }}
    .metric {{
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 10px;
      min-height: 62px;
    }}
    .metric span {{
      display: block;
      color: var(--muted);
      font-size: 12px;
      margin-bottom: 4px;
    }}
    .metric strong {{
      display: block;
      font-size: 18px;
      font-weight: 650;
      overflow-wrap: anywhere;
    }}
    .legend {{
      display: grid;
      gap: 8px;
      font-size: 13px;
    }}
    .legend div {{
      display: flex;
      align-items: center;
      gap: 8px;
    }}
    .swatch {{
      width: 14px;
      height: 14px;
      border-radius: 50%;
      border: 1px solid var(--border);
      flex: 0 0 auto;
    }}
    @media (max-width: 860px) {{
      main {{
        grid-template-columns: 1fr;
      }}
      .stage {{
        min-height: auto;
      }}
      canvas {{
        width: min(92vw, 620px);
        height: min(92vw, 620px);
      }}
    }}
  </style>
</head>
<body>
  <main>
    <section class="stage">
      <canvas id="world" width="900" height="900"></canvas>
    </section>
    <aside class="side">
      <div>
        <h1>{html.escape(title)}</h1>
        <div class="muted">Playback of the best recorded creature trajectory in normalized world coordinates.</div>
      </div>

      <div class="controls">
        <div class="button-row">
          <button id="play" type="button">Play</button>
          <button id="back" type="button">Back</button>
          <button id="forward" type="button">Forward</button>
        </div>
        <label>
          Step
          <input id="step" type="range" min="0" value="0">
        </label>
        <label>
          Speed
          <input id="speed" type="range" min="1" max="120" value="30">
        </label>
        <label>
          Trail length
          <input id="trail" type="range" min="5" max="400" value="160">
        </label>
      </div>

      <div class="readout">
        <div class="metric"><span>Step</span><strong id="stepText">0</strong></div>
        <div class="metric"><span>Food</span><strong id="foodText">0</strong></div>
        <div class="metric"><span>Distance</span><strong id="distanceText">0.000</strong></div>
        <div class="metric"><span>Spikes</span><strong id="spikeText">0</strong></div>
        <div class="metric"><span>Motor X</span><strong id="motorXText">0.000</strong></div>
        <div class="metric"><span>Motor Y</span><strong id="motorYText">0.000</strong></div>
      </div>

      <div class="legend">
        <div><span class="swatch" style="background: var(--creature);"></span>Creature</div>
        <div><span class="swatch" style="background: var(--target);"></span>Current food target</div>
        <div><span class="swatch" style="background: var(--path);"></span>Recent path</div>
        <div><span class="swatch" style="background: var(--start);"></span>Episode start</div>
      </div>
    </aside>
  </main>

  <script id="payload" type="application/json">{payload_json}</script>
  <script>
    const payload = JSON.parse(document.getElementById("payload").textContent);
    const points = payload.points;
    const metadata = payload.metadata;
    const canvas = document.getElementById("world");
    const ctx = canvas.getContext("2d");
    const stepSlider = document.getElementById("step");
    const speedSlider = document.getElementById("speed");
    const trailSlider = document.getElementById("trail");
    const playButton = document.getElementById("play");
    const width = metadata.environment_width || 1;
    const height = metadata.environment_height || 1;
    const targetRadius = metadata.environment_target_radius || 0.075;
    const pad = 48;
    let index = 0;
    let playing = false;
    let lastTime = 0;

    stepSlider.max = Math.max(0, points.length - 1);
    trailSlider.max = Math.max(5, Math.min(600, points.length));
    trailSlider.value = Math.min(160, points.length || 160);

    function sx(x) {{ return pad + (x / width) * (canvas.width - 2 * pad); }}
    function sy(y) {{ return canvas.height - pad - (y / height) * (canvas.height - 2 * pad); }}
    function sr(r) {{ return r * Math.min(canvas.width - 2 * pad, canvas.height - 2 * pad) / Math.max(width, height); }}

    function drawGrid() {{
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = "#fbfcfe";
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.strokeStyle = "#d6dae1";
      ctx.lineWidth = 1;
      ctx.strokeRect(pad, pad, canvas.width - 2 * pad, canvas.height - 2 * pad);
      ctx.fillStyle = "#6b7280";
      ctx.font = "14px Segoe UI, sans-serif";
      ctx.fillText("0", pad - 20, canvas.height - pad + 5);
      ctx.fillText(width.toFixed(2), canvas.width - pad - 30, canvas.height - pad + 24);
      ctx.fillText(height.toFixed(2), pad - 40, pad + 5);
      ctx.strokeStyle = "#edf0f4";
      for (let i = 1; i < 5; i += 1) {{
        const gx = pad + i * (canvas.width - 2 * pad) / 5;
        const gy = pad + i * (canvas.height - 2 * pad) / 5;
        ctx.beginPath();
        ctx.moveTo(gx, pad);
        ctx.lineTo(gx, canvas.height - pad);
        ctx.moveTo(pad, gy);
        ctx.lineTo(canvas.width - pad, gy);
        ctx.stroke();
      }}
    }}

    function draw() {{
      if (!points.length) return;
      const point = points[index];
      const start = points[0];
      const trailLength = Number(trailSlider.value);
      const firstTrail = Math.max(0, index - trailLength);

      drawGrid();

      ctx.strokeStyle = "#2563eb";
      ctx.lineWidth = 3;
      ctx.lineJoin = "round";
      ctx.lineCap = "round";
      ctx.beginPath();
      for (let i = firstTrail; i <= index; i += 1) {{
        const p = points[i];
        if (i === firstTrail) ctx.moveTo(sx(p.x), sy(p.y));
        else ctx.lineTo(sx(p.x), sy(p.y));
      }}
      ctx.stroke();

      ctx.fillStyle = "#16803c";
      ctx.beginPath();
      ctx.arc(sx(start.x), sy(start.y), 7, 0, Math.PI * 2);
      ctx.fill();

      ctx.strokeStyle = "#e86f19";
      ctx.fillStyle = "rgb(232 111 25 / 20%)";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(sx(point.target_x), sy(point.target_y), sr(targetRadius), 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();

      ctx.fillStyle = "#111827";
      ctx.beginPath();
      ctx.arc(sx(point.x), sy(point.y), 9, 0, Math.PI * 2);
      ctx.fill();

      const vx = point.motor_x;
      const vy = point.motor_y;
      const arrowScale = 80;
      ctx.strokeStyle = "#111827";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(sx(point.x), sy(point.y));
      ctx.lineTo(sx(point.x) + vx * arrowScale, sy(point.y) - vy * arrowScale);
      ctx.stroke();

      stepSlider.value = String(index);
      document.getElementById("stepText").textContent = String(point.step);
      document.getElementById("foodText").textContent = String(point.foods_collected);
      document.getElementById("distanceText").textContent = point.distance.toFixed(4);
      document.getElementById("spikeText").textContent = String(point.cumulative_spikes);
      document.getElementById("motorXText").textContent = point.motor_x.toFixed(4);
      document.getElementById("motorYText").textContent = point.motor_y.toFixed(4);
    }}

    function setIndex(next) {{
      index = Math.max(0, Math.min(points.length - 1, next));
      draw();
    }}

    function animate(time) {{
      if (!playing) return;
      const fps = Number(speedSlider.value);
      if (time - lastTime >= 1000 / fps) {{
        setIndex(index + 1 >= points.length ? 0 : index + 1);
        lastTime = time;
      }}
      requestAnimationFrame(animate);
    }}

    playButton.addEventListener("click", () => {{
      playing = !playing;
      playButton.textContent = playing ? "Pause" : "Play";
      lastTime = 0;
      if (playing) requestAnimationFrame(animate);
    }});
    document.getElementById("back").addEventListener("click", () => setIndex(index - 1));
    document.getElementById("forward").addEventListener("click", () => setIndex(index + 1));
    stepSlider.addEventListener("input", () => setIndex(Number(stepSlider.value)));
    trailSlider.addEventListener("input", draw);
    window.addEventListener("keydown", (event) => {{
      if (event.key === " ") {{
        event.preventDefault();
        playButton.click();
      }} else if (event.key === "ArrowLeft") {{
        setIndex(index - 1);
      }} else if (event.key === "ArrowRight") {{
        setIndex(index + 1);
      }}
    }});

    draw();
  </script>
</body>
</html>
"""


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate an interactive HTML viewer for a NeuroEvolution run.")
    parser.add_argument("run_dir", type=Path, help="Directory containing best_trajectory.csv")
    parser.add_argument("--out", type=Path, default=None, help="Output HTML path, default: RUN_DIR/viewer.html")
    args = parser.parse_args()

    run_dir = args.run_dir.resolve()
    output = args.out.resolve() if args.out else run_dir / "viewer.html"
    points, metadata = trajectory_payload(run_dir)
    output.write_text(render_html(run_dir, points, metadata), encoding="utf-8")
    print(f"Wrote {output}")


if __name__ == "__main__":
    main()
