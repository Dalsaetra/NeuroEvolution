"""Turn an ecosystem JSONL recording into an offline, self-contained replay.

Usage: python tools/view_ecosystem.py runs/ecosystem
No web server, package installation, or internet connection is required.
"""
from __future__ import annotations

import argparse
import csv
import gzip
import html
import json
import math
import re
from pathlib import Path
from typing import Any


def _reject_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON number: {value}")


def read_replay(path: Path) -> dict[str, Any]:
    """Read and validate the recording envelope; optional diagnostics stay optional."""
    if path.is_dir():
        plain = path / "ecosystem.jsonl"
        compressed = path / "ecosystem.jsonl.gz"
        source = plain if plain.exists() else compressed
    else:
        source = path
    metadata: dict[str, Any] | None = None
    frames: list[dict[str, Any]] = []
    opener = gzip.open if source.suffix == ".gz" else Path.open
    with opener(source, mode="rt", encoding="utf-8-sig") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line, parse_constant=_reject_constant)
            except (ValueError, json.JSONDecodeError) as error:
                raise ValueError(f"{source.name}, line {line_number}: {error}") from error
            if not isinstance(record, dict):
                raise ValueError(f"{source.name}, line {line_number}: expected an object")
            if record.get("type") == "metadata":
                if metadata is not None or frames:
                    raise ValueError("A recording must start with exactly one metadata record")
                metadata = record
            elif record.get("type") == "frame":
                if metadata is None:
                    raise ValueError("A recording must start with a metadata record")
                moment = record.get("time")
                if isinstance(moment, bool) or not isinstance(moment, (int, float)) or not math.isfinite(moment):
                    raise ValueError(f"Frame on line {line_number} needs a finite time")
                if moment < 0 or (frames and moment < frames[-1]["time"]):
                    raise ValueError("Frame times must be nonnegative and chronological")
                for key in ("creatures", "resources", "events"):
                    record.setdefault(key, [])
                    if not isinstance(record[key], list) or any(not isinstance(item, dict) for item in record[key]):
                        raise ValueError(f"Frame on line {line_number}: {key} must contain objects")
                record.setdefault("totals", {})
                frames.append(record)
            else:
                raise ValueError(f"{source.name}, line {line_number}: unknown record type {record.get('type')!r}")
    if metadata is None:
        raise ValueError("The recording has no metadata")
    if metadata.get("version", 1) != 1:
        raise ValueError(f"Unsupported recording version: {metadata['version']}")
    for key in ("width", "height"):
        value = metadata.get(key)
        if isinstance(value, bool) or not isinstance(value, int) or value < 1:
            raise ValueError(f"Metadata {key} must be a positive integer")
    cells = metadata["width"] * metadata["height"]
    metadata.setdefault("terrain", [0] * cells)
    if not isinstance(metadata["terrain"], list) or len(metadata["terrain"]) != cells:
        raise ValueError("Terrain must have width × height cells in row-major order")
    if any(isinstance(cell, bool) or cell not in (0, 1, 2, 3) for cell in metadata["terrain"]):
        raise ValueError("Terrain cells must be 0 (ground), 1 (rough), 2 (wall), or 3 (shelter)")
    for key in ("resources", "brains", "input_labels"):
        metadata.setdefault(key, [])
        if not isinstance(metadata[key], list):
            raise ValueError(f"Metadata {key} must be an array")
    if not frames:
        raise ValueError("The recording contains no frames")
    stats = []
    stats_path = source.parent / "ecosystem_stats.csv"
    if stats_path.exists():
        with stats_path.open(encoding="utf-8-sig", newline="") as handle:
            for row in csv.DictReader(handle):
                numeric = {}
                for key, value in row.items():
                    try:
                        number = float(value)
                    except (ValueError, TypeError):
                        continue
                    if math.isfinite(number):
                        numeric[key] = number
                if "time" in numeric:
                    stats.append(numeric)
        stats.sort(key=lambda row: row["time"])
    return {"metadata": metadata, "frames": frames, "name": source.parent.name, "stats": stats}


def render_html(payload: dict[str, Any]) -> str:
    # Escaping '<' is essential: JSON strings can contain a closing script tag.
    data = json.dumps(payload, separators=(",", ":"), ensure_ascii=False, allow_nan=False)
    for character, replacement in (("&", "\\u0026"), ("<", "\\u003c"), (">", "\\u003e"), ("\u2028", "\\u2028"), ("\u2029", "\\u2029")):
        data = data.replace(character, replacement)
    replacements = {
        "__REPLAY_TITLE__": html.escape(str(payload.get("name", "Ecosystem"))),
        "__REPLAY_PAYLOAD__": data,
    }
    return re.sub(r"__REPLAY_TITLE__|__REPLAY_PAYLOAD__", lambda match: replacements[match.group()], TEMPLATE)


TEMPLATE = r'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Ecosystem replay · __REPLAY_TITLE__</title>
<style>
:root{color-scheme:light;--ink:#203d38;--sub:#6a7c75;--line:#dce4d9;--paper:#fffef9;--bg:#f1f3ea;--green:#277a62;--gold:#b58836;--red:#bf6559;--blue:#5799b7}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.5 "Segoe UI",system-ui,sans-serif}button,input,select{font:inherit}button,select{color:var(--ink);border:1px solid var(--line);background:var(--paper);border-radius:7px;padding:7px 12px}button{cursor:pointer}button:hover{background:#edf2e8}button:focus-visible,select:focus-visible,input:focus-visible{outline:2px solid #328b70;outline-offset:3px}button.primary{background:var(--green);border-color:var(--green);color:white;min-width:85px}button.primary:hover{background:#1c604c}input[type=range]{accent-color:var(--green);cursor:pointer}input[type=checkbox]{accent-color:var(--green)}header{max-width:1616px;margin:auto;padding:25px 28px 17px;display:flex;align-items:center;justify-content:space-between;gap:20px}.eyebrow{font-size:10px;font-weight:750;letter-spacing:2px;text-transform:uppercase;color:var(--green)}h1{font-size:27px;letter-spacing:-.8px;font-weight:650;line-height:1.15;margin:5px 0 7px}p{margin:0}header p{color:var(--sub);font-size:13px}.run-tag{font:12px/1.7 ui-monospace,Consolas,monospace;background:#e5eadd;padding:8px 13px;border-radius:8px;text-align:right;max-width:40%;overflow-wrap:anywhere}.shell{max-width:1616px;padding:0 28px 28px;margin:auto}.metrics{display:grid;grid-template-columns:repeat(6,1fr);gap:12px;margin-bottom:16px}.metric{padding:13px 17px;background:var(--paper);border:1px solid var(--line);border-radius:11px}.metric .label,.small-label{font-size:10px;font-weight:650;letter-spacing:1px;color:var(--sub);text-transform:uppercase}.metric .value{font-variant-numeric:tabular-nums;font-size:26px;line-height:1.3;letter-spacing:-.5px}.metric small{font-size:11px;color:var(--sub)}.layout{display:grid;grid-template-columns:minmax(420px,1fr) 365px;gap:16px;align-items:start}.panel{background:var(--paper);border:1px solid var(--line);border-radius:12px;overflow:hidden}.panel-head{padding:13px 16px;display:flex;align-items:center;justify-content:space-between;gap:8px;border-bottom:1px solid var(--line)}h2{font-size:14px;letter-spacing:-.15px;margin:0;font-weight:650}.muted{color:var(--sub)}.tiny{font-size:11px}.weather{font-size:11px;font-weight:700;letter-spacing:.5px;padding:4px 9px;border-radius:30px;background:#ecf0e0;color:#617744;text-transform:uppercase}.weather.warning{background:#faf0cd;color:#8d6d25}.weather.storm{background:#e3eaf1;color:#4a667d}.world-wrap{position:relative;background:#e6ebd9;padding:12px;display:grid;place-items:center}#world{display:block;width:100%;max-height:calc(100vh - 370px);min-height:300px;cursor:crosshair;touch-action:manipulation;object-fit:contain}.world-note{position:absolute;top:20px;left:20px;background:#fffefaeb;box-shadow:0 2px 12px #22382a12;padding:5px 9px;border-radius:5px;font-size:10px;pointer-events:none;color:#687867}.world-controls{padding:10px 15px;display:flex;flex-wrap:wrap;gap:7px 16px;font-size:11px;color:var(--sub)}.world-controls label{display:flex;align-items:center;gap:4px;cursor:pointer}.legend{border-top:1px solid var(--line);display:flex;flex-wrap:wrap;gap:7px 17px;padding:10px 15px;font-size:11px;color:var(--sub)}.legend span{display:flex;align-items:center;gap:6px}.swatch{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--c)}.swatch.square{border-radius:2px}.transport{margin-top:12px;padding:12px 16px;display:flex;align-items:center;gap:10px}.transport input{flex:1;min-width:55px}.time{font:12px ui-monospace,Consolas,monospace;white-space:nowrap;min-width:122px;text-align:center}.side{display:grid;gap:14px}.selection{padding:14px 16px}.selection select{width:100%;margin:0 0 13px;font-size:12px;padding:7px 8px}.agent-title{display:flex;align-items:center;gap:7px;font-size:13px;font-weight:650}.agent-dot{width:9px;height:9px;display:inline-block;border-radius:50%}.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:13px 6px;margin-top:12px}.stat strong{display:block;font-size:17px;font-weight:550;line-height:1.2;font-variant-numeric:tabular-nums}.stat span{font-size:10px;color:var(--sub)}.energy-track{height:5px;background:#edf0e6;border-radius:9px;margin-top:9px;overflow:hidden}.energy-track i{display:block;height:100%;background:var(--green);transition:width .08s}.subsection{border-top:1px solid var(--line);margin-top:14px;padding-top:12px}.actions{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:6px}.action span{font-size:10px;color:var(--sub)}.action div{height:4px;background:#edf0e6;margin-top:4px;border-radius:5px;overflow:hidden}.action i{display:block;height:100%;background:#8fa782}.diet{height:9px;display:flex;overflow:hidden;border-radius:10px;background:#edf0e6;margin:8px 0}.diet span{height:100%;min-width:0}.diet-labels{font-size:10px;display:flex;justify-content:space-between;color:var(--sub)}.tabs{display:flex;gap:4px}.tabs button{padding:3px 8px;font-size:10px;background:transparent;border-color:transparent}.tabs button.active{background:#e9efe2;color:var(--green)}.brain-body{padding:5px 10px 10px;position:relative}#brain{display:block;width:100%;height:220px}.brain-caption{font-size:10px;color:var(--sub);padding:0 6px;min-height:29px}.brain-empty{display:none;position:absolute;top:50%;left:25px;right:25px;text-align:center;color:var(--sub);font-size:12px}.sensors{height:268px;overflow:auto;padding:8px 15px}.sensor{display:grid;grid-template-columns:1fr 67px 28px;gap:8px;align-items:center;margin:5px 0;font-size:10px}.sensor>span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.sensor-track{height:5px;background:#eef0e9}.sensor-track i{display:block;height:100%;background:#72a48b}.sensor b{font-weight:400;color:var(--sub);text-align:right}.charts{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-top:14px}.chart-head{display:flex;align-items:center;justify-content:space-between;padding:12px 14px 0}.chart-head h2{font-size:12px}.chart{display:block;width:100%;height:115px}.event-list{height:169px;overflow:auto;padding:7px 14px}.event{font-size:11px;display:flex;gap:10px;padding:6px 0;border-bottom:1px solid #eef0e9}.event time{font:10px/1.7 ui-monospace,Consolas,monospace;color:var(--sub);flex:none}.event span{overflow-wrap:anywhere}.empty{padding:15px 0;color:var(--sub);font-size:12px}.footer{display:flex;justify-content:space-between;gap:12px;margin:13px 2px 0;font-size:10px;color:var(--sub)}[hidden]{display:none!important}#worldStatus{color:#896924}.tooltip{position:fixed;z-index:20;pointer-events:none;border:1px solid #dbe3d4;border-radius:5px;padding:6px 8px;background:#fffef9f5;color:var(--ink);box-shadow:0 3px 16px #23362e14;font-size:11px;max-width:270px}
@media(min-width:1400px){.layout{grid-template-columns:minmax(520px,1fr) 390px}#world{max-height:calc(100vh - 335px)}}
@media(max-width:1000px){.layout{grid-template-columns:minmax(380px,1fr) 315px}header,.shell{padding-left:17px;padding-right:17px}.metric{padding:10px 12px}.metric .value{font-size:23px}#world{max-height:650px}.transport{gap:6px;padding:12px 10px}.transport button,.transport select{padding:7px}.time{font-size:10px;min-width:95px}}
@media(max-width:760px){header{padding-top:20px;align-items:flex-start}h1{font-size:25px}.run-tag{font-size:10px}.metrics{gap:6px;grid-template-columns:repeat(3,1fr)}.metric{padding:9px 11px}.metric:nth-child(4),.metric:nth-child(5){display:none}.layout{display:flex;flex-direction:column}.main-col,.side{width:100%}.side{grid-template-columns:1fr 1fr;align-items:start}.side>.panel:last-child{grid-column:1/-1}.world-wrap{padding:7px}#world{min-height:0;max-height:620px}.world-note{left:13px;top:13px}.transport{flex-wrap:wrap}.transport input{order:5;flex-basis:100%;width:100%}.charts{margin-top:12px}.footer{flex-wrap:wrap}.time{flex:1}.selection{padding:12px}.actions{gap:5px}.stats{gap:12px 4px}}
@media(max-width:440px){.side{grid-template-columns:1fr}.side>.panel:last-child{grid-column:auto}.charts{grid-template-columns:1fr}.run-tag{max-width:36%}.metric .label{font-size:9px}.metric .value{font-size:22px}.metric small{font-size:10px}}
.main-col{min-width:0}.brain-panel{margin-top:14px}.brain-scroll{position:relative;max-height:560px;overflow:auto}#brain{cursor:pointer}.neuron-controls{display:flex;flex-wrap:wrap;align-items:center;gap:8px;padding:12px 6px}.neuron-controls select{flex:1;min-width:0;max-width:100%}.neuron-controls label{font-size:12px}.synapse-lists{display:grid;grid-template-columns:1fr 1fr;gap:16px;padding:0 6px 12px}.synapse-list{min-width:0}.synapse-table-wrap{max-height:260px;overflow:auto}.synapse-list table{width:100%;border-collapse:collapse;font-size:12px}.synapse-list th,.synapse-list td{text-align:left;padding:6px;border-bottom:1px solid var(--line);overflow-wrap:anywhere}.synapse-list td:last-child{font-family:Consolas,monospace}.synapse-list h3{font-size:12px}.neuron-summary{padding:0 6px;font-size:12px}@media(max-width:600px){.synapse-lists{grid-template-columns:1fr}}
/* Keep the habitat visible while scrolling through neuron diagnostics. */
.workspace-panes{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:16px;align-items:start}
.workspace-panes>.panel{min-width:0;margin-top:0}.habitat-panel{position:sticky;top:16px}
#world{min-height:0;max-height:max(180px,calc(100dvh - 240px))}
.details-panes{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px;align-items:start;margin-top:16px}
.details-panes #establishmentPanel{grid-column:1/-1}.details-panes .event-list{height:300px}
.transport{position:fixed;z-index:30;left:0;right:0;bottom:0;margin:0;border-radius:0;border-width:1px 0 0;box-shadow:0 -4px 20px #203d3815;padding:12px max(16px,calc((100vw - 1560px)/2));padding-bottom:calc(12px + env(safe-area-inset-bottom));background:var(--paper)}
body{padding-bottom:calc(90px + env(safe-area-inset-bottom));scroll-padding-bottom:100px}
@media(max-width:760px){.workspace-panes,.details-panes{grid-template-columns:1fr}.habitat-panel{position:static}.transport{padding:10px 12px;padding-bottom:calc(10px + env(safe-area-inset-bottom))}body{padding-bottom:calc(130px + env(safe-area-inset-bottom))}}
</style>
</head>
<body>
<header><div><div class="eyebrow">NeuroEvolution / Living systems</div><h1>A world worth learning.</h1><p>Food, weather, and the company of other creatures.</p></div><div class="run-tag"><span id="runName"></span><br><span id="runInfo" class="muted"></span></div></header>
<main class="shell">
<section class="metrics" aria-label="World statistics">
<div class="metric"><div class="label">Immigrants</div><div class="value" id="immigrants">0</div><small>external arrivals · not births</small></div>
<div class="metric"><div class="label">Population</div><div class="value" id="population">0</div><small id="populationNote">creatures alive</small></div>
<div class="metric"><div class="label">Mean energy</div><div class="value" id="meanEnergy">0</div><small>energy per creature</small></div>
<div class="metric"><div class="label">Births / deaths</div><div class="value" id="birthDeath">0 / 0</div><small>cumulative events</small></div>
<div class="metric"><div class="label">Food consumed</div><div class="value" id="foodConsumed">0</div><small>biomass removed</small></div>
<div class="metric"><div class="label">Maturations</div><div class="value" id="maturations">0</div><small>includes founders and offspring</small></div>
</section>
<div class="workspace-panes">
<section class="panel habitat-panel"><div class="panel-head"><h2>The habitat <span class="muted tiny" id="dimensions"></span></h2><span class="weather" id="weather">Calm</span></div>
<div class="world-wrap"><canvas id="world" aria-label="Ecosystem map. Click a creature to inspect it."></canvas><div class="world-note">Click a creature to follow its decisions</div></div>
<div class="world-controls"><label><input type="checkbox" id="showVision" checked> Selected creature’s vision</label><label><input type="checkbox" id="showTrails"> Movement trails</label><label><input type="checkbox" id="showNutrition"> Nutrition values · observer only</label></div>
<div class="legend"><span><i class="swatch" style="--c:#689951"></i>Ground forage</span><span><i class="swatch" style="--c:#dc8262"></i>Fruit A</span><span><i class="swatch" style="--c:#6c90bd"></i>Fruit B</span><span><i class="swatch" style="--c:#ba9548"></i>Food pod</span><span><i class="swatch" style="--c:#b84c63"></i>Meat</span><span><i class="swatch square" style="--c:#a7c5b3"></i>Shelter</span><span><i class="swatch square" style="--c:#536358"></i>Wall</span></div>
</section>
<section class="panel brain-panel"><div class="panel-head"><h2>Inside the brain</h2><div class="tabs"><button id="networkTab" class="active">Network</button><button id="sensorsTab">Senses</button></div></div><div id="networkView" class="brain-body"><div class="brain-scroll"><canvas id="brain" aria-label="Selected creature's spiking neuron network"></canvas><div class="brain-empty" id="brainEmpty">Neural activity was not recorded for this creature.</div></div><div class="brain-caption" id="brainCaption">Inputs → recurrent neurons → actions. Bright nodes fired at the sampled step.</div><div class="neuron-controls"><label for="neuronSelect">Inspect neuron</label><select id="neuronSelect"><option value="">Click a neuron to inspect its synapses</option></select><button id="clearNeuron">Clear selection</button></div><section id="potentialHistory" hidden><div class="chart-head"><h2 id="potentialTitle">Potential V history</h2></div><canvas id="potentialChart" class="chart" style="height:210px" aria-label="Selected neuron potential over recorded time"></canvas><p id="potentialCaption" class="brain-caption"></p></section><div id="neuronDetails"></div></div><div class="sensors" id="sensors" hidden></div></section>
</div>
<div class="details-panes">
<section class="panel"><div class="panel-head"><h2>Creature inspector</h2><span class="tiny muted" id="selectedStatus">individual brain</span></div><div class="selection"><select id="creatureSelect" aria-label="Select creature"></select><div id="selectedContent"><div class="agent-title"><i class="agent-dot" id="agentDot"></i><span id="agentTitle"></span><span class="tiny muted" id="agentParent"></span></div><div class="energy-track"><i id="energyFill"></i></div><div class="stats"><div class="stat"><strong id="agentEnergy">—</strong><span>energy</span></div><div class="stat"><strong id="agentAge">—</strong><span>age, seconds</span></div><div class="stat"><strong id="agentGeneration">—</strong><span>generation</span></div><div class="stat"><strong id="agentSpikes">—</strong><span>recorded spikes</span></div><div class="stat"><strong id="agentOffspring">—</strong><span>offspring</span></div><div class="stat"><strong id="agentPosition">—</strong><span>position</span></div></div><div id="bodyTraits" class="subsection tiny" hidden></div><div class="subsection"><div class="small-label">Movement &amp; actions</div><div class="actions" id="actions"></div></div><div class="subsection"><div class="small-label">Lifetime diet · biomass</div><div class="diet" id="diet"></div><div class="diet-labels" id="dietLabels"></div></div></div><p class="empty" id="noCreature" hidden>No creatures remain in this frame.</p></div></section>
<section class="panel"><div class="panel-head"><h2>Recent events</h2><select id="eventFilter" aria-label="Event filter" style="font-size:10px;padding:3px 7px"><option value="milestones">Milestones</option><option value="all">All activity</option></select></div><div class="event-list" id="events"></div></section>
<section class="panel" id="establishmentPanel" hidden><div class="panel-head"><h2>Population establishment</h2><span class="tiny muted" id="supportState"></span></div><div class="selection"><p id="supportDetails" class="tiny muted"></p><p id="immigrantSplit" class="tiny muted" style="margin:8px 0"></p><div class="small-label" id="archiveLabel">Genome archive</div><div id="archiveEntries" style="max-height:180px;overflow:auto"></div><p class="tiny muted" style="margin-top:8px">Scores reflect partial food success. Births and breeding descendants provide stronger evidence of viability.</p></div></section>
</div>
<section class="panel" style="margin-top:14px"><div class="panel-head"><h2>Ecosystem statistics</h2><select id="statSelect" aria-label="Statistic to plot"></select></div><canvas class="chart" style="height:190px" id="statChart" aria-label="Selected ecosystem statistic over time"></canvas><p class="brain-caption" id="statCaption"></p></section><div class="charts"><section class="panel"><div class="chart-head"><h2>Population over time</h2><span class="tiny muted">alive</span></div><canvas class="chart" id="populationChart" aria-label="Population history"></canvas></section><section class="panel"><div class="chart-head"><h2>Mean energy over time</h2><span class="tiny muted">energy</span></div><canvas class="chart" id="energyChart" aria-label="Average energy history"></canvas></section><section class="panel"><div class="chart-head"><h2>Best archive score</h2><span class="tiny muted" id="archiveScoreLabel">selection score</span></div><canvas class="chart" id="archiveChart" aria-label="Best archive score history"></canvas></section></div>
<div class="footer"><span id="recordingInfo"></span><span id="worldStatus"></span><span>Space: play / pause · ← →: step · self-contained replay</span></div>
</main>
<section class="panel transport" aria-label="Playback controls"><button class="primary" id="play" aria-label="Play recording">▶ Play</button><button id="stepBack" title="Previous frame" aria-label="Previous frame">‹</button><button id="stepForward" title="Next frame" aria-label="Next frame">›</button><input id="timeline" type="range" min="0" max="0" value="0" step="1" aria-label="Replay timeline"><span class="time" id="time">0:00 / 0:00</span><select id="speed" aria-label="Playback speed"><option value="1">1×</option><option value="2">2×</option><option value="5" selected>5×</option><option value="10">10×</option><option value="30">30×</option><option value="100">100×</option></select></section>
<div class="tooltip" id="tooltip" hidden></div>
<script id="replay-data" type="application/json">__REPLAY_PAYLOAD__</script>
<script>
'use strict';
const replay=JSON.parse(document.getElementById('replay-data').textContent);
const M=replay.metadata,F=replay.frames,$=id=>document.getElementById(id);
const palette=['#286f65','#a55c77','#506faa','#a67530','#7f659e','#567b3a','#ab6750','#387f9a'];
const foodColors=['#689951','#dc8262','#6c90bd','#ba9548','#b84c63'];
const kindIndex={'graze':0,'fruit-a':1,'fruit-b':2,'fruit_a':1,'fruit_b':2,'pod':3,'meat':4};
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const num=v=>Number.isFinite(Number(v))?Number(v):0;
const color=id=>palette[Math.abs(num(id))%palette.length];
const fmt=(v,d=0)=>num(v).toLocaleString(undefined,{maximumFractionDigits:d});
const clock=t=>{t=Math.max(0,Math.floor(t));return `${Math.floor(t/60)}:${String(t%60).padStart(2,'0')}`};
const energyMax=num(M.max_energy||M.energy_capacity)||120;
const resources=new Map(M.resources.map(r=>[r.id,r]));
const brains=new Map(M.brains.map(b=>[b.id,b]));
let index=0,selected=F[0].creatures[0]?.id??null,playing=false,playTime=F[0].time,lastTick=0;
let worldView={scale:1,x:0,y:0},brainPoints=[],sensorTab=false,selectedNeuron=null,neuronCreature=null;
const world=$('world'),ctx=world.getContext('2d'),brain=$('brain'),bctx=brain.getContext('2d');
const population=F.map(f=>f.creatures.length),energies=F.map(f=>f.creatures.length?f.creatures.reduce((s,c)=>s+num(c.energy),0)/f.creatures.length:0),archiveScores=F.map(f=>Math.max(0,...(f.establishment?.archive||[]).map(e=>num(e.score))));
const firstTime=F[0].time,lastTime=F[F.length-1].time,timeSpan=Math.max(1,lastTime-firstTime);
const allEvents=[],milestones=[],eventEnds=[],milestoneEnds=[];F.forEach((f,fi)=>{f.events.forEach(e=>{const item={...e,frameIndex:fi,time:e.time??f.time};allEvents.push(item);if(!['ingestion','digestion','pod_work'].includes(e.type))milestones.push(item)});eventEnds.push(allEvents.length);milestoneEnds.push(milestones.length)});
// Definitions may appear with newborns; activity remains local to the chosen frame.
F.forEach(f=>f.creatures.forEach(c=>{if(c.brain?.neurons?.length&&!brains.has(c.id))brains.set(c.id,{id:c.id,inputs:c.brain.inputs,outputs:c.brain.outputs,neurons:c.brain.neurons,synapses:c.brain.synapses||[]})}));
$('runName').textContent=replay.name;$('runInfo').textContent=`seed ${M.seed??'—'} · ${M.controller||'spiking'} controllers`;
$('dimensions').textContent=`/ ${M.width} × ${M.height}`;
$('timeline').max=F.length-1;
$('recordingInfo').textContent=`${fmt(F.length)} recorded frames · ${fmt(M.resources.length)} resource patches · ${fmt(M.dt,3)} s simulation step`;
function fit(canvas,height){const r=canvas.getBoundingClientRect(),dpr=Math.min(2,window.devicePixelRatio||1),w=Math.max(1,r.width),h=height||Math.max(1,r.height);if(canvas.width!==Math.round(w*dpr)||canvas.height!==Math.round(h*dpr)){canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr)}const c=canvas.getContext('2d');c.setTransform(dpr,0,0,dpr,0,0);return {w,h,c};}
const terrain=document.createElement('canvas');terrain.width=M.width*20;terrain.height=M.height*20;
const tctx=terrain.getContext('2d');
for(let y=0;y<M.height;y++)for(let x=0;x<M.width;x++){const tile=M.terrain[y*M.width+x],p=x*20,q=(M.height-1-y)*20;tctx.fillStyle=['#e6ead7','#d1ceac','#536358','#b7d0bd'][tile]||'#e6ead7';tctx.fillRect(p,q,20,20);if(tile===0){tctx.fillStyle=(x*17+y*11)%5===0?'#dbe3cb':'#e2e7d1';tctx.fillRect(p+4,q+7,2,2)}if(tile===1){tctx.strokeStyle='#bbb997';tctx.lineWidth=.7;tctx.beginPath();tctx.moveTo(p+3,q+13);tctx.lineTo(p+9,q+7);tctx.moveTo(p+11,q+16);tctx.lineTo(p+16,q+11);tctx.stroke()}if(tile===2){tctx.fillStyle='#647269';tctx.fillRect(p,q,20,2)}if(tile===3){tctx.strokeStyle='#a7c4b0';tctx.lineWidth=.7;tctx.strokeRect(p+.5,q+.5,19,19)}}
function point(x,y){return {x:worldView.x+x*worldView.scale,y:worldView.y+(M.height-y)*worldView.scale}}
function wall(x,y){return x<0||y<0||x>=M.width||y>=M.height||M.terrain[Math.floor(y)*M.width+Math.floor(x)]===2}
function drawVision(c){const range=num(M.vision_range)||9,half=(num(M.fov_degrees)||120)*Math.PI/360,p=point(c.x,c.y);ctx.beginPath();ctx.moveTo(p.x,p.y);for(let n=0;n<=64;n++){const a=num(c.heading)-half+2*half*n/64,dx=Math.cos(a),dy=Math.sin(a);let d=0;while(d<range&&!wall(c.x+dx*d,c.y+dy*d))d+=.08;d=Math.min(d,range);const v=point(c.x+dx*d,c.y+dy*d);ctx.lineTo(v.x,v.y)}ctx.closePath();ctx.fillStyle=color(c.id)+'17';ctx.fill();ctx.strokeStyle=color(c.id)+'40';ctx.lineWidth=.7;ctx.stroke()}
function drawWorld(){const {w,h}=fit(world);ctx.clearRect(0,0,w,h);worldView.scale=Math.min(w/M.width,h/M.height);worldView.x=(w-M.width*worldView.scale)/2;worldView.y=(h-M.height*worldView.scale)/2;const s=worldView.scale;ctx.drawImage(terrain,worldView.x,worldView.y,M.width*s,M.height*s);const frame=F[index];if(frame.weather==='storm'){ctx.fillStyle='#5e7b961a';ctx.fillRect(worldView.x,worldView.y,M.width*s,M.height*s);if(M.nursery?.enabled){const n=M.nursery;ctx.drawImage(terrain,n.x*20,(M.height-n.y-n.size)*20,n.size*20,n.size*20,worldView.x+n.x*s,worldView.y+(M.height-n.y-n.size)*s,n.size*s,n.size*s)}}if(M.nursery?.enabled){const n=M.nursery;ctx.fillStyle='#27604b';ctx.font='bold 11px Segoe UI';ctx.textAlign='center';ctx.fillText('NURSERY',worldView.x+(n.x+n.size/2)*s,worldView.y+(M.height-n.y-n.size)*s-5)}const chosen=frame.creatures.find(c=>c.id===selected);if(chosen&&$('showVision').checked)drawVision(chosen);
if($('showTrails').checked){const ids=selected!==null?[selected]:frame.creatures.map(c=>c.id);ids.forEach(id=>{ctx.beginPath();let started=false;for(let fi=Math.max(0,index-70);fi<=index;fi++){const c=F[fi].creatures.find(c=>c.id===id);if(!c)continue;const p=point(c.x,c.y);if(!started){ctx.moveTo(p.x,p.y);started=true}else ctx.lineTo(p.x,p.y)}ctx.strokeStyle=color(id)+'68';ctx.lineWidth=1.6;ctx.stroke()})}
frame.resources.forEach(state=>{const r={...(resources.get(state.id)||{}),...state};if(!Number.isFinite(r.x)||!Number.isFinite(r.y))return;const p=point(r.x,r.y),k=kindIndex[r.kind]??0,stock=clamp(num(state.stock)/Math.max(.001,num(r.capacity)||1),0,1),radius=Math.max(3,s*(k===3?.48:.34)),progress=clamp(num(state.progress)/(num(M.pod_work)||10),0,1);ctx.globalAlpha=stock>.01||k===3?1:.23;ctx.fillStyle=(r.shelter_food?'#9c9069':foodColors[k]);ctx.strokeStyle=k===3?'#8e713a':foodColors[k];ctx.lineWidth=1;if(k===3&&state.state!=='open'){ctx.beginPath();for(let i=0;i<6;i++){const a=i*Math.PI/3-Math.PI/2;const px=p.x+Math.cos(a)*radius,py=p.y+Math.sin(a)*radius;i?ctx.lineTo(px,py):ctx.moveTo(px,py)}ctx.closePath();ctx.fillStyle=state.state==='refilling'?'#d3c8a3':'#ad934f';ctx.fill();ctx.stroke();if(progress>0){ctx.beginPath();ctx.arc(p.x,p.y,radius+2,-Math.PI/2,-Math.PI/2+2*Math.PI*progress);ctx.strokeStyle='#f9e2a4';ctx.lineWidth=2;ctx.stroke()}}else{ctx.beginPath();ctx.arc(p.x,p.y,radius,0,2*Math.PI);ctx.fillStyle=(r.shelter_food?'#9c9069':foodColors[k])+'24';ctx.fill();ctx.beginPath();ctx.arc(p.x,p.y,Math.max(.7,radius*Math.sqrt(stock)),0,2*Math.PI);ctx.fillStyle=(r.shelter_food?'#9c9069':foodColors[k]);ctx.fill();if(k===3){ctx.strokeStyle='#f8efce';ctx.lineWidth=1.5;ctx.stroke()}}ctx.globalAlpha=1;if($('showNutrition').checked&&(k>0||r.shelter_food)){ctx.font='9px Segoe UI';ctx.textAlign='center';const label=String(r.value??'?');ctx.fillStyle='#fffefade';const tw=ctx.measureText(label).width;ctx.fillRect(p.x-tw/2-2,p.y-radius-12,tw+4,11);ctx.fillStyle='#4d5546';ctx.fillText(label,p.x,p.y-radius-3)}});
frame.creatures.forEach(c=>{if(M.predation&&num(c.attack)>0.02){const cp=point(c.x,c.y),half=num(M.attack_degrees)*Math.PI/360;ctx.beginPath();ctx.moveTo(cp.x,cp.y);ctx.arc(cp.x,cp.y,num(M.attack_range)*s,num(c.heading)-half,num(c.heading)+half);ctx.closePath();ctx.fillStyle=`rgba(184,76,99,${0.25*num(c.attack)})`;ctx.fill()}const p=point(c.x,c.y),rad=Math.max(3.7,(num(M.radius)||.27)*s),active=c.id===selected;ctx.save();ctx.translate(p.x,p.y);ctx.beginPath();ctx.arc(0,0,rad+1.8,-Math.PI/2,-Math.PI/2+2*Math.PI*clamp(num(c.energy)/energyMax,0,1));ctx.strokeStyle=num(c.energy)<energyMax*.25?'#bd786c':'#7e9e61';ctx.lineWidth=1.1;ctx.stroke();if(num(c.call)>.05){ctx.strokeStyle=color(c.id)+'65';ctx.lineWidth=1;[rad+3,rad+7+Math.sin(frame.time*5+c.id)*1.5].forEach(r=>{ctx.beginPath();ctx.arc(0,0,r,0,2*Math.PI);ctx.stroke()})}if(active){ctx.beginPath();ctx.arc(0,0,rad+4,0,2*Math.PI);ctx.strokeStyle='#fffef9';ctx.lineWidth=4;ctx.stroke();ctx.strokeStyle=color(c.id);ctx.lineWidth=1.5;ctx.stroke()}ctx.rotate(-num(c.heading));ctx.fillStyle=color(c.id);ctx.strokeStyle='#fffdf4';ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(rad*1.6,0);ctx.lineTo(-rad*.8,rad*.8);ctx.quadraticCurveTo(-rad*1.3,0,-rad*.8,-rad*.8);ctx.closePath();ctx.fill();ctx.stroke();if(num(c.forage)>.15){ctx.fillStyle='#f7df99';ctx.beginPath();ctx.arc(rad*1.65,0,1.9,0,2*Math.PI);ctx.fill()}ctx.restore()});}
function action(label,value,max=1){const box=document.createElement('div');box.className='action';const span=document.createElement('span');span.textContent=`${label} ${fmt(value,2)}`;const track=document.createElement('div'),fill=document.createElement('i');fill.style.width=`${clamp(Math.abs(num(value))/max,0,1)*100}%`;track.append(fill);box.append(span,track);return box}
function updateInspector(){const frame=F[index],select=$('creatureSelect'),c=frame.creatures.find(c=>c.id===selected);select.replaceChildren();if(!c&&selected!==null){const o=document.createElement('option');o.value=selected;o.textContent=`Creature ${selected} · absent from this frame`;select.append(o)}frame.creatures.forEach(c=>{const o=document.createElement('option');o.value=c.id;o.textContent=`Creature ${c.id} · generation ${c.generation??0} · ${fmt(c.energy,1)} energy`;select.append(o)});if(selected!==null)select.value=selected;$('selectedContent').hidden=!c;$('noCreature').hidden=!!c;$('noCreature').textContent=frame.creatures.length?'This creature is not alive in this frame. Select another creature above.':'No creatures remain in this frame.';$('selectedStatus').textContent=c?`${c.controller||M.controller||'spiking'} controller`:'no active selection';if(c){$('agentDot').style.background=color(c.id);$('agentTitle').textContent=`Creature ${c.id}`;$('agentParent').textContent=c.parent?`parent ${c.parent}`:`${(c.origin||'founder').replaceAll('-',' ')}${c.source_id?` · source ${c.source_id}`:''}`;$('agentEnergy').textContent=fmt(c.energy,1);$('energyFill').style.width=`${clamp(c.energy/energyMax,0,1)*100}%`;$('energyFill').style.background=c.energy<energyMax*.25?'var(--red)':'var(--green)';$('agentAge').textContent=fmt(c.age,1);$('agentGeneration').textContent=c.generation??0;$('agentSpikes').textContent=fmt(c.spikes);$('agentOffspring').textContent=fmt(c.offspring);$('agentPosition').textContent=`${fmt(c.x,1)}, ${fmt(c.y,1)}`;$('agentPosition').style.fontSize='13px';$('bodyTraits').hidden=!M.predation;$('bodyTraits').textContent=`Mass ${fmt(c.mass,2)} / Carnivory ${fmt(100*num(c.carnivory))}% / Health ${fmt(c.health,1)} of ${fmt(c.max_health,1)}`;$('actions').replaceChildren(action('Drive',c.forward),action('Speed',c.speed,num(c.maximum_speed)||num(M.max_speed)||1.5),action('Turn',c.turn,num(M.max_turn_rate)||Math.PI),action('Forage',c.forage),action('Call',c.call),...(M.predation?[action('Attack',c.attack)]:[]));const eaten=c.eaten||[0,0,0,0],total=eaten.reduce((s,v)=>s+num(v),0);$('diet').replaceChildren();$('dietLabels').replaceChildren();['Graze','A','B','Pod','Meat'].forEach((label,i)=>{const segment=document.createElement('span');segment.style.background=foodColors[i];segment.style.width=total?`${num(eaten[i])/total*100}%`:'0';$('diet').append(segment);const l=document.createElement('span');l.textContent=`${label} ${fmt(eaten[i],1)}`;$('dietLabels').append(l)})}drawBrain(c);if(sensorTab)drawSensors(c);}
function brainData(c){if(!c)return null;const base=brains.get(c.id)||{};return c.brain?{...base,...c.brain}:Object.keys(base).length?base:null}
function connectedSenses(data){
 if(!Array.isArray(data?.synapses))return null;
 const inputs=num(data.inputs)||num(M.inputs)||87, used=new Set();
 data.synapses.forEach(e=>{if(e.pre<inputs)used.add(e.pre);if(e.post<inputs)used.add(e.post)});
 return used;
}
function neuronLabel(data,n){
 const inputs=num(data.inputs)||num(M.inputs)||87,outputs=num(data.outputs)||5;
 return `#${n} · ${n<inputs?(M.input_labels[n]||`Input ${n}`):n>=data.neurons.length-outputs?`Action ${['move','turn left','turn right','forage','call','attack'][n-(data.neurons.length-outputs)]||n}`:'Recurrent neuron'}`;
}
function updateNeuronInspector(data){
 const select=$('neuronSelect'),details=$('neuronDetails');
 // Reuse controls during playback so focus and the open dropdown remain stable.
 const labels=(data?.neurons||[]).map((_,i)=>neuronLabel(data,i)),key=JSON.stringify(labels);
 if(select.dataset.labels!==key){select.replaceChildren(new Option('Click a neuron to inspect its synapses',''));labels.forEach((label,i)=>select.add(new Option(label,String(i))));select.dataset.labels=key}
 select.value=selectedNeuron===null?'':String(selectedNeuron);select.disabled=!labels.length;$('clearNeuron').disabled=selectedNeuron===null;
 const synapses=data?.synapses||[],detailKey=JSON.stringify([neuronCreature,selectedNeuron,labels,synapses]);
 if(details.dataset.key===detailKey)return;details.dataset.key=detailKey;details.replaceChildren();
 if(selectedNeuron===null)return;
 const title=document.createElement('p');title.className='neuron-summary';title.textContent=neuronLabel(data,selectedNeuron)+' · Connected synapses are highlighted in the graph.';details.append(title);
 const lists=document.createElement('div');lists.className='synapse-lists';details.append(lists);
 for(const [name,endpoint] of [['Incoming','post'],['Outgoing','pre']]){
  const edges=synapses.filter(e=>e[endpoint]===selectedNeuron),section=document.createElement('section');section.className='synapse-list';
  const heading=document.createElement('h3');heading.textContent=`${name} synapses (${edges.length})`;section.append(heading);lists.append(section);
  if(!edges.length){const empty=document.createElement('p');empty.textContent=`No ${name.toLowerCase()} synapses.`;empty.className='muted';section.append(empty);continue}
  const wrap=document.createElement('div');wrap.className='synapse-table-wrap';const table=document.createElement('table');table.innerHTML='<thead><tr><th scope="col">From → to</th><th scope="col">Weight</th></tr></thead>';const body=document.createElement('tbody');
  edges.forEach(e=>{const row=document.createElement('tr'),connection=document.createElement('td'),weight=document.createElement('td');connection.textContent=`${neuronLabel(data,e.pre)} → ${neuronLabel(data,e.post)}`;weight.textContent=String(e.weight);row.append(connection,weight);body.append(row)});
  table.append(body);wrap.append(table);section.append(wrap);
 }
}
let potentialCache={};
function potentialSamples(creatureId,neuron){
 return F.map(frame=>{
  const creature=frame.creatures.find(c=>c.id===creatureId),data=brainData(creature),value=creature?.brain?.potentials?.[neuron];
  const phase=M.calibrated_io&&neuron<(num(data?.inputs)||num(M.inputs)||87),threshold=phase?1:data?.neurons?.[neuron]?.threshold;
  return {time:frame.time,value:typeof value==='number'&&Number.isFinite(value)?value:null,
   threshold:creature&&typeof threshold==='number'&&Number.isFinite(threshold)?threshold:null,
   spiked:Array.isArray(creature?.brain?.spiked)?creature.brain.spiked.includes(neuron):null};
 });
}
function drawPotentialHistory(data){
 const panel=$('potentialHistory');panel.hidden=selectedNeuron===null;
 if(panel.hidden)return;
 const phase=M.calibrated_io&&selectedNeuron<(num(data?.inputs)||num(M.inputs)||87),quantity=phase?'Input phase':'Potential V';
 $('potentialTitle').textContent=`${quantity} history · ${neuronLabel(data,selectedNeuron)}`;
 if(potentialCache.creature!==neuronCreature||potentialCache.neuron!==selectedNeuron){potentialCache={creature:neuronCreature,neuron:selectedNeuron,samples:potentialSamples(neuronCreature,selectedNeuron)}}
 const samples=potentialCache.samples,valid=samples.filter(p=>p.value!==null),{w,h,c}=fit($('potentialChart'));
 c.clearRect(0,0,w,h);
 const current=samples[index]?.value,caption=$('potentialCaption');
 const thresholds=samples.filter(p=>p.threshold!==null),spikes=samples.filter(p=>p.spiked===true),spikesRecorded=samples.some(p=>p.spiked!==null);
 caption.textContent=`${valid.length?`${valid.length} recorded samples · ${current===null?'No sample at current time':`Current ${phase?'phase':'V'}: ${current}`}`:`${quantity} was not recorded for this neuron.`} · Green: ${phase?'phase':'V'} · Dashed red: ${phase?'firing phase (1; neuron threshold controls input rate)':`threshold${samples[index]?.threshold!==null?` (${samples[index].threshold})`:''}`} · Purple triangles: ${spikesRecorded?`${spikes.length} recorded spikes`:'spikes not recorded'} · Gold: current time. Time in seconds; activity between samples is not captured.`;
 let lo=Infinity,hi=-Infinity;[...valid.map(p=>p.value),...thresholds.map(p=>p.threshold)].forEach(v=>{lo=Math.min(lo,v);hi=Math.max(hi,v)});
 if(!Number.isFinite(lo)){lo=0;hi=1}

 const pad=(hi-lo)*.1||Math.max(.1,Math.abs(hi)*.1);lo-=pad;hi+=pad;
 const left=64,right=18,top=18,bottom=32,x=t=>left+(t-firstTime)/timeSpan*Math.max(1,w-left-right),y=v=>top+(hi-v)/(hi-lo)*(h-top-bottom);
 c.font='10px Segoe UI';c.textAlign='right';
 for(let i=0;i<3;i++){const v=lo+(hi-lo)*i/2;c.fillStyle='#6a7c75';c.fillText(fmt(v,3),left-7,y(v)+3);c.strokeStyle='#e7ebe1';c.lineWidth=.6;c.beginPath();c.moveTo(left,y(v));c.lineTo(w-right,y(v));c.stroke()}
 c.strokeStyle='#bf6559';c.lineWidth=1.3;c.setLineDash([6,4]);c.beginPath();let previous=null;
 samples.forEach(p=>{if(p.threshold===null){previous=null;return}if(previous){c.lineTo(x(p.time),y(previous.threshold));c.lineTo(x(p.time),y(p.threshold))}else c.moveTo(x(p.time),y(p.threshold));previous=p});c.stroke();c.setLineDash([]);
 c.fillStyle='#bf6559';thresholds.forEach(p=>{c.fillRect(x(p.time)-2,y(p.threshold)-1,4,2)});
 c.strokeStyle='#277a62';c.lineWidth=1.6;c.beginPath();let connected=false;
 samples.forEach(p=>{if(p.value===null){connected=false;return}if(connected)c.lineTo(x(p.time),y(p.value));else c.moveTo(x(p.time),y(p.value));connected=true});c.stroke();
 // Dots keep isolated samples visible without joining across missing frames.
 c.fillStyle='#277a62';valid.forEach(p=>{c.beginPath();c.arc(x(p.time),y(p.value),2,0,Math.PI*2);c.fill()});
 // Spike flags are recorded after reset; place markers in a separate top row.
 c.fillStyle='#8756a0';spikes.forEach(p=>{const sx=x(p.time);c.beginPath();c.moveTo(sx,top);c.lineTo(sx-4,top-8);c.lineTo(sx+4,top-8);c.closePath();c.fill()});
 c.strokeStyle='#b58836';c.lineWidth=1;c.beginPath();c.moveTo(x(F[index].time),top);c.lineTo(x(F[index].time),h-bottom);c.stroke();
 if(current!==null){c.fillStyle='#b58836';c.beginPath();c.arc(x(F[index].time),y(current),4,0,Math.PI*2);c.fill()}
 c.fillStyle='#6a7c75';c.textAlign='left';c.fillText(`${fmt(firstTime,1)} s`,left,h-10);c.textAlign='right';c.fillText(`${fmt(lastTime,1)} s`,w-right,h-10);
}
function drawBrain(c){
 const data=brainData(c),neurons=data?.neurons||[],inputs=num(data?.inputs)||num(M.inputs)||87,outputs=num(data?.outputs)||5;
 if(neuronCreature!==c?.id){selectedNeuron=null;neuronCreature=c?.id}
 if(!neurons[selectedNeuron])selectedNeuron=null;
 updateNeuronInspector(data);
 drawPotentialHistory(data);
 const used=connectedSenses(data),visible=neurons.map((n,i)=>({n,i})).filter(p=>p.i>=inputs||used===null||used.has(p.i));
 const groups=[visible.filter(p=>p.i<inputs),visible.filter(p=>p.i>=inputs&&p.i<neurons.length-outputs),visible.filter(p=>p.i>=neurons.length-outputs)];
 const height=Math.max(420,24*Math.max(...groups.map(g=>g.length))+36);brain.style.height=`${height}px`;
 const {w,h}=fit(brain,height);bctx.clearRect(0,0,w,h);brainPoints=[];
 $('brainEmpty').style.display=neurons.length?'none':'block';
 if(!neurons.length){$('brainCaption').textContent='Enable brain graph recording to inspect connections.';return}
 groups.forEach((group,layer)=>group.forEach((p,j)=>brainPoints.push({...p,x:layer===0?w*.43:layer===1?w*.69:w-22,y:20+(j+.5)/Math.max(1,group.length)*(h-40)})));
 const points=new Map(brainPoints.map(p=>[p.i,p])),spiked=new Set(data.spiked||[]),synapses=data.synapses||[];
 synapses.forEach(e=>{const a=points.get(e.pre),b=points.get(e.post);if(!a||!b)return;bctx.beginPath();bctx.moveTo(a.x,a.y);bctx.lineTo(b.x,b.y);const active=selectedNeuron!==null&&(e.pre===selectedNeuron||e.post===selectedNeuron);bctx.strokeStyle=active?(num(e.weight)<0?'#bf6559':'#277a62'):selectedNeuron!==null?'#dce4d940':num(e.weight)<0?'#b46e7170':'#668b8160';bctx.lineWidth=active?2:.7;if(e.pre===e.post){bctx.arc(a.x+9,a.y-9,12,0,Math.PI*2)}bctx.stroke()});
 brainPoints.forEach(p=>{const input=p.i<inputs,output=p.i>=neurons.length-outputs,r=input?3.5:output?4.3:3.2;
 bctx.beginPath();bctx.arc(p.x,p.y,r,0,Math.PI*2);bctx.fillStyle=spiked.has(p.i)?'#e4b43d':input?'#75a694':output?'#b37d67':`rgba(50,112,96,${.3+clamp(num(data.potentials?.[p.i])/(num(p.n.threshold)||1),0,1)*.7})`;bctx.fill();
 if(p.i===selectedNeuron){bctx.beginPath();bctx.arc(p.x,p.y,8,0,Math.PI*2);bctx.strokeStyle='#203d38';bctx.lineWidth=2;bctx.stroke()}
 if(input){bctx.font='9px Segoe UI';bctx.textAlign='right';bctx.fillStyle='#4b655b';bctx.fillText(M.input_labels[p.i]||`Input ${p.i}`,p.x-8,p.y+3,w*.43-14)}
 });
 $('brainCaption').textContent=`${groups[0].length}/${inputs} senses shown · ${synapses.length} synapses. Gold = firing; red = inhibitory. ${c?.brain?.potentials?'':'Graph only; activity not recorded.'}`;
}
function drawSensors(c){const container=$('sensors');container.replaceChildren();if(!c?.observation?.length){const e=document.createElement('p');e.className='empty';e.textContent='Sensory inputs were not recorded for this creature.';container.append(e);return}const used=connectedSenses(brainData(c));if(used?.size===0){container.textContent='This brain has no connected sensory inputs.';return}c.observation.forEach((v,i)=>{if(used!==null&&!used.has(i))return;const row=document.createElement('div');row.className='sensor';const label=document.createElement('span');label.textContent=M.input_labels[i]||`Input ${i}`;label.title=label.textContent;const track=document.createElement('div');track.className='sensor-track';const fill=document.createElement('i');fill.style.width=`${clamp(Math.abs(num(v)),0,1)*100}%`;if(v<0)fill.style.background='#ba7c70';track.append(fill);const val=document.createElement('b');val.textContent=fmt(v,2);row.append(label,track,val);container.append(row)})}
function drawChart(id,series,line){const {w,h,c}=fit($(id)),left=34,right=12,top=13,bottom=23,cw=w-left-right,ch=h-top-bottom,max=series.reduce((highest,value)=>Math.max(highest,value),1)*1.1;c.clearRect(0,0,w,h);c.font='9px Segoe UI';c.textAlign='right';[0,.5,1].forEach(frac=>{const y=top+ch*(1-frac);c.strokeStyle='#e7ebe1';c.lineWidth=.6;c.beginPath();c.moveTo(left,y);c.lineTo(w-right,y);c.stroke();c.fillStyle='#859085';c.fillText(fmt(max*frac),left-6,y+3)});for(let i=0;i<F.length;i++){if(F[i].weather==='storm'){const x=left+(F[i].time-firstTime)/timeSpan*cw,x2=left+((F[i+1]?.time??F[i].time)-firstTime)/timeSpan*cw;c.fillStyle='#dbe4e747';c.fillRect(x,top,Math.max(1,x2-x),ch)}}c.beginPath();series.forEach((value,i)=>{const x=left+(F[i].time-firstTime)/timeSpan*cw,y=top+ch-value/max*ch;i?c.lineTo(x,y):c.moveTo(x,y)});c.lineTo(left+cw,top+ch);c.lineTo(left,top+ch);c.closePath();c.fillStyle=line+'0f';c.fill();c.beginPath();series.forEach((value,i)=>{const x=left+(F[i].time-firstTime)/timeSpan*cw,y=top+ch-value/max*ch;i?c.lineTo(x,y):c.moveTo(x,y)});c.strokeStyle=line;c.lineWidth=1.6;c.stroke();const cx=left+(F[index].time-firstTime)/timeSpan*cw,cy=top+ch-series[index]/max*ch;c.strokeStyle='#617a6875';c.lineWidth=.8;c.beginPath();c.moveTo(cx,top);c.lineTo(cx,top+ch);c.stroke();c.fillStyle=line;c.beginPath();c.arc(cx,cy,3,0,Math.PI*2);c.fill();c.fillStyle='#859085';c.textAlign='left';c.fillText(clock(firstTime),left,h-6);c.textAlign='right';c.fillText(clock(lastTime),w-right,h-6);}
function eventText(e){const id=e.creature??'?',other=e.other,resource=e.resource,kind=String(e.type||'event').replaceAll('_',' ').replaceAll('-',' ');if(kind==='birth')return `Creature ${id} born${other!==undefined&&other!==null?` · parent ${other}`:''}`;if(kind==='death')return `Creature ${id} died`;if(kind==='attack hit')return `Creature ${id} attacked ${other}: ${fmt(e.amount,2)} damage`;if(kind==='carcass')return `Creature ${id} left meat: ${fmt(e.amount,1)} energy`;if(kind==='maturity'||kind==='mature'||kind==='maturation')return `Creature ${id} reached maturity`;if(kind.includes('pod')&&kind.includes('open'))return `Pod ${resource??'?'} opened${num(id)>0?` · creature ${id}`:''}`;if(kind.startsWith('weather '))return `Weather changed to ${kind.slice(8)}`;if(kind==='extinction')return 'No creatures remain';if(kind==='capacity limited')return 'Population capacity reached';if(kind==='weather')return `Weather changed${e.weather?` to ${e.weather}`:''}`;return `${kind.charAt(0).toUpperCase()+kind.slice(1)}${num(e.creature)>0?` · creature ${id}`:''}${num(resource)>0?` · patch ${resource}`:''}${num(e.amount)?` · ${fmt(e.amount,2)}`:''}`}
const statRows=replay.stats||[],statKeys=[...new Set(statRows.flatMap(row=>Object.keys(row)))].filter(k=>k!=='time');
statKeys.forEach(key=>{const option=document.createElement('option');option.value=key;option.textContent=key.replaceAll('_',' ');$('statSelect').append(option)});
$('statSelect').value=statKeys.includes('population')?'population':statKeys[0]||'';
$('statSelect').disabled=!statKeys.length;
function drawStatChart(){
 const key=$('statSelect').value,points=statRows.filter(row=>Number.isFinite(row[key]));
 const {w,h,c}=fit($('statChart'));c.clearRect(0,0,w,h);
 $('statCaption').textContent=points.length?'Source: ecosystem_stats.csv · vertical line follows replay time':'No numeric statistics available. Regenerate this viewer beside ecosystem_stats.csv.';
 if(!points.length)return;
 let lo=Infinity,hi=-Infinity;points.forEach(p=>{lo=Math.min(lo,p[key]);hi=Math.max(hi,p[key])});
 const pad=(hi-lo)*.08||Math.max(1,Math.abs(hi)*.05);lo-=pad;hi+=pad;
 const start=points[0].time,end=points[points.length-1].time,span=end-start||1;
 const x=t=>64+(t-start)/span*(w-82),y=v=>16+(hi-v)/(hi-lo)*(h-46);
 c.font='10px Segoe UI';c.textAlign='right';
 for(let i=0;i<3;i++){const v=lo+(hi-lo)*i/2;c.fillStyle='#6a7c75';c.fillText(fmt(v,2),57,y(v)+3);c.strokeStyle='#e7ebe1';c.beginPath();c.moveTo(64,y(v));c.lineTo(w-18,y(v));c.stroke()}
 c.beginPath();points.forEach((p,i)=>{i?c.lineTo(x(p.time),y(p[key])):c.moveTo(x(p.time),y(p[key]))});c.strokeStyle='#277a62';c.lineWidth=1.6;c.stroke();
 const t=F[index].time;if(t>=start&&t<=end){c.strokeStyle='#b58836';c.beginPath();c.moveTo(x(t),16);c.lineTo(x(t),h-30);c.stroke()}
 c.fillStyle='#6a7c75';c.textAlign='left';c.fillText(`${fmt(start,1)} s`,64,h-8);c.textAlign='right';c.fillText(`${fmt(end,1)} s`,w-18,h-8);
}
$('statSelect').addEventListener('change',drawStatChart);
function updateEvents(){const container=$('events'),all=$('eventFilter').value==='all',list=all?allEvents:milestones,end=(all?eventEnds:milestoneEnds)[index],items=list.slice(Math.max(0,end-35),end).reverse();container.replaceChildren();if(!items.length){const p=document.createElement('p');p.className='empty';p.textContent='Births, deaths, and resource events will appear here.';container.append(p)}items.forEach(e=>{const div=document.createElement('div');div.className='event';const t=document.createElement('time');t.textContent=clock(e.time);const label=document.createElement('span');label.textContent=eventText(e);div.append(t,label);container.append(div)})}
function updateEstablishment(){
 const f=F[index],p=f.establishment||{},t=f.totals||{};
 $('immigrants').textContent=fmt(t.immigrants);
 $('establishmentPanel').hidden=!p.enabled;
 if(!p.enabled)return;
 $('supportState').textContent=p.withdrawn?'withdrawn':p.active?'active':'paused';
 $('supportDetails').textContent=`Population floor ${fmt(p.floor)} · ${fmt(t.natural_spiking_breeders)} naturally born spiking breeders${p.active?` · next check ${clock(p.next_check)}`:''}`;
 $('immigrantSplit').textContent=`${fmt(t.immigrant_slight_mutations??t.immigrant_mutations)} slight / ${fmt(t.immigrant_strong_mutations)} strong / ${fmt(t.immigrant_clones)} clones · ${fmt(t.immigrant_energy,1)} external energy · ${fmt(t.archive_empty_checks)} waiting checks`;
 const bank=p.archive||[];
 $('archiveLabel').textContent=`Genome archive · ${bank.length} entries`;
 const container=$('archiveEntries');container.replaceChildren();
 if(!bank.length){const row=document.createElement('p');row.className='empty';row.textContent='Waiting for food successes; arrivals are random until then.';container.append(row)}
 [...bank].sort((a,b)=>b.score-a.score).forEach(e=>{const row=document.createElement('div');row.className='event';row.textContent=`Genome ${e.genome_id} · ${e.niche} · score ${fmt(e.score,2)}`+(num(e.newborn_trials)>0?` · ${fmt(e.newborn_trials)} newborn trials · ${fmt(100*e.newborn_breeder_fraction)}% breed · ${fmt(100*e.breeding_lineage_fraction)}% breeding lineages`:` · ${fmt(e.trials)} observed lives · ${fmt(e.mean_food_energy,1)} mean food energy`);container.append(row)});
}
function render(){drawStatChart(); $('archiveScoreLabel').textContent=num(M.archive_eval_trials)>0?'newborn trials':'observed lives';updateEstablishment();const f=F[index];$('timeline').value=index;$('time').textContent=`${clock(f.time)} / ${clock(lastTime)}`;$('population').textContent=fmt(f.creatures.length);$('populationNote').textContent=`${population[0]} at recording start · ${fmt(f.time,1)} s`;$('meanEnergy').textContent=fmt(energies[index],1);$('birthDeath').textContent=`${fmt(f.totals.births)} / ${fmt(f.totals.deaths)}`;$('foodConsumed').textContent=fmt(f.totals.consumed_biomass,1);$('maturations').textContent=fmt(f.totals.maturations);$('weather').textContent=f.weather||'calm';$('weather').className=`weather ${f.weather||'calm'}`;$('worldStatus').textContent=f.capacity_limited?'Population capacity reached · births paused':!f.creatures.length&&f.establishment?.active?'Population empty · immigration remains active':'';drawWorld();updateInspector();drawChart('populationChart',population,'#387961');drawChart('energyChart',energies,'#b18a42');drawChart('archiveChart',archiveScores,'#a55c77');updateEvents();}
function setIndex(value){index=clamp(value,0,F.length-1);playTime=F[index].time;render()}
function pause(){playing=false;$('play').textContent='▶ Play';$('play').setAttribute('aria-label','Play recording')}
function toggle(){if(playing){pause();return}if(index===F.length-1)setIndex(0);playing=true;lastTick=performance.now();$('play').textContent='Ⅱ Pause';$('play').setAttribute('aria-label','Pause recording');requestAnimationFrame(tick)}
function tick(now){if(!playing)return;playTime+=(now-lastTick)/1000*num($('speed').value);lastTick=now;let next=index;while(next<F.length-1&&F[next+1].time<=playTime)next++;if(next!==index){index=next;render()}if(playTime>=lastTime){pause();return}requestAnimationFrame(tick)}
$('eventFilter').addEventListener('change',updateEvents);$('play').addEventListener('click',toggle);$('stepBack').addEventListener('click',()=>{pause();setIndex(index-1)});$('stepForward').addEventListener('click',()=>{pause();setIndex(index+1)});$('timeline').addEventListener('input',e=>{pause();setIndex(Number(e.target.value))});['showVision','showTrails','showNutrition'].forEach(id=>$(id).addEventListener('change',drawWorld));$('creatureSelect').addEventListener('change',e=>{selected=num(e.target.value);render()});
$('networkTab').addEventListener('click',()=>{sensorTab=false;$('networkView').hidden=false;$('sensors').hidden=true;$('networkTab').classList.add('active');$('sensorsTab').classList.remove('active');drawBrain(F[index].creatures.find(c=>c.id===selected))});$('sensorsTab').addEventListener('click',()=>{sensorTab=true;$('networkView').hidden=true;$('sensors').hidden=false;$('sensorsTab').classList.add('active');$('networkTab').classList.remove('active');drawSensors(F[index].creatures.find(c=>c.id===selected))});
world.addEventListener('click',e=>{const r=world.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top;let best=null,distance=18;F[index].creatures.forEach(c=>{const p=point(c.x,c.y),d=Math.hypot(p.x-x,p.y-y);if(d<distance){best=c;distance=d}});if(best){selected=best.id;render()}});
function showTip(text,x,y){const tip=$('tooltip');tip.textContent=text;tip.hidden=false;tip.style.left=`${Math.min(x+12,window.innerWidth-285)}px`;tip.style.top=`${Math.min(y+12,window.innerHeight-65)}px`}
world.addEventListener('mousemove',e=>{const r=world.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top;let nearest=null,best=12;F[index].creatures.forEach(c=>{const p=point(c.x,c.y),d=Math.hypot(x-p.x,y-p.y);if(d<best){nearest=c;best=d}});if(nearest)showTip(`Creature ${nearest.id} · ${fmt(nearest.energy,1)} energy · generation ${nearest.generation??0}`,e.clientX,e.clientY);else $('tooltip').hidden=true});
function selectNeuron(value){selectedNeuron=value;drawBrain(F[index].creatures.find(c=>c.id===selected))}
$('neuronSelect').addEventListener('change',e=>selectNeuron(e.target.value===''?null:Number(e.target.value)));
$('clearNeuron').addEventListener('click',()=>selectNeuron(null));
brain.addEventListener('click',e=>{const r=brain.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top;let nearest=null,best=10;brainPoints.forEach(p=>{const d=Math.hypot(p.x-x,p.y-y);if(d<best){nearest=p;best=d}});selectNeuron(nearest?.i??null)});
brain.addEventListener('mousemove',e=>{const r=brain.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top;let nearest=null,best=7;brainPoints.forEach(p=>{const d=Math.hypot(p.x-x,p.y-y);if(d<best){nearest=p;best=d}});if(nearest){const c=F[index].creatures.find(c=>c.id===selected),d=brainData(c),n=nearest.i,inputs=num(d.inputs)||87,outputs=num(d.outputs)||5,label=n<inputs?(M.input_labels[n]||`Input ${n}`):n>=d.neurons.length-outputs?`Action ${['move','turn left','turn right','forage','call','attack'][n-(d.neurons.length-outputs)]||n}`:`Recurrent neuron ${n}`;showTip(`${label} · ${n<inputs&&M.calibrated_io?`phase ${fmt(d.potentials?.[n],3)} / 1 · rate divisor ${fmt(nearest.n.threshold,3)}`:`V ${fmt(d.potentials?.[n],3)} / threshold ${fmt(nearest.n.threshold,3)}`}`,e.clientX,e.clientY)}else $('tooltip').hidden=true});
[world,brain].forEach(canvas=>canvas.addEventListener('mouseleave',()=>{$('tooltip').hidden=true}));
document.addEventListener('keydown',e=>{if(['INPUT','SELECT','TEXTAREA','BUTTON'].includes(e.target.tagName))return;if(e.code==='Space'){e.preventDefault();toggle()}if(e.code==='ArrowLeft'){e.preventDefault();pause();setIndex(index-1)}if(e.code==='ArrowRight'){e.preventDefault();pause();setIndex(index+1)}});
function resize(){world.style.aspectRatio=`${M.width} / ${M.height}`;render()}window.addEventListener('resize',resize);resize();
</script>
</body>
</html>'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_dir", type=Path, help="Run directory or ecosystem.jsonl file")
    parser.add_argument("--output", type=Path, help="HTML destination (default: RUN_DIR/ecosystem.html)")
    parser.add_argument("--gzip-source", action="store_true",
                        help="After generating HTML, replace an uncompressed JSONL source with ecosystem.jsonl.gz")
    arguments = parser.parse_args()
    try:
        payload = read_replay(arguments.run_dir)
        directory = arguments.run_dir if arguments.run_dir.is_dir() else arguments.run_dir.parent
        destination = arguments.output or directory / "ecosystem.html"
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(render_html(payload), encoding="utf-8")
        source = arguments.run_dir / "ecosystem.jsonl" if arguments.run_dir.is_dir() else arguments.run_dir
        if arguments.gzip_source and source.suffix != ".gz":
            compressed = source.with_suffix(source.suffix + ".gz")
            with source.open("rb") as incoming, gzip.open(compressed, "wb", compresslevel=6) as outgoing:
                while block := incoming.read(1024 * 1024):
                    outgoing.write(block)
            source.unlink()
    except (OSError, ValueError, TypeError) as error:
        parser.error(str(error))
    print(f"Ecosystem viewer: {destination.resolve()}")
    print(f"{len(payload['frames'])} frames, {len(payload['frames'][0]['creatures'])} initial creatures")


if __name__ == "__main__":
    main()
