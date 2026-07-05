from __future__ import annotations

import argparse
import concurrent.futures
import csv
import itertools
import json
import math
import random
import re
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path
from typing import Any


Row = dict[str, Any]


FIXED_COLUMNS = [
    "trial_index",
    "status",
    "objective",
    "objective_metric",
    "seed",
    "duration_sec",
    "returncode",
    "run_dir",
    "trial_key",
    "command",
]


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def read_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, sort_keys=True)
        handle.write("\n")


def numeric(value: Any, default: float = math.nan) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def finite(value: Any) -> bool:
    parsed = numeric(value)
    return math.isfinite(parsed)


def normalize_flag(name: str) -> str:
    return name if name.startswith("--") else f"--{name}"


def format_cli_value(value: Any) -> str:
    if isinstance(value, float):
        return f"{value:.12g}"
    return str(value)


def args_from_mapping(values: dict[str, Any]) -> list[str]:
    args: list[str] = []
    for key, value in values.items():
        if value is None or value is False:
            continue
        flag = normalize_flag(key)
        if value is True:
            args.append(flag)
        else:
            args.extend([flag, format_cli_value(value)])
    return args


def objective_settings(config: dict[str, Any]) -> tuple[str, str]:
    objective = config.get("objective", "best_foods_collected")
    if isinstance(objective, str):
        return objective, "max"
    return str(objective.get("metric", "best_foods_collected")), str(objective.get("mode", "max")).lower()


def better(lhs: float, rhs: float, mode: str) -> bool:
    if not math.isfinite(rhs):
        return math.isfinite(lhs)
    if not math.isfinite(lhs):
        return False
    return lhs < rhs if mode == "min" else lhs > rhs


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_output_dir(config_path: Path, config: dict[str, Any]) -> Path:
    name = str(config.get("name") or config_path.stem)
    return Path("runs") / "searches" / sanitize_filename(name)


def sanitize_filename(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    return value.strip("_") or "search"


def param_names(config: dict[str, Any]) -> list[str]:
    return list(config.get("parameters", {}).keys())


def param_column(name: str) -> str:
    return f"param:{name}"


def trial_key(params: dict[str, Any], seed: int) -> str:
    return json.dumps({"params": params, "seed": seed}, sort_keys=True, separators=(",", ":"))


def param_signature(params: dict[str, Any]) -> str:
    return json.dumps(params, sort_keys=True, separators=(",", ":"))


def seeds_from_config(config: dict[str, Any]) -> list[int]:
    if "seeds" in config:
        seeds = [int(seed) for seed in config["seeds"]]
        if seeds:
            return seeds
    base_seed = int(config.get("base_args", {}).get("seed", config.get("seed", 7)))
    return [base_seed]


def values_from_range(spec: dict[str, Any]) -> list[Any]:
    if "grid" in spec:
        return list(spec["grid"])
    if "values" in spec:
        return list(spec["values"])
    if "min" not in spec or "max" not in spec or "steps" not in spec:
        raise ValueError("Grid mode needs each parameter to define either values or min/max/steps")

    steps = int(spec["steps"])
    if steps <= 0:
        raise ValueError("Parameter steps must be positive")
    lo = float(spec["min"])
    hi = float(spec["max"])
    scale = str(spec.get("scale", "linear"))
    kind = str(spec.get("type", "float"))
    if steps == 1:
        values = [lo]
    elif scale == "log":
        if lo <= 0 or hi <= 0:
            raise ValueError("Log-scaled grid parameters must have positive min/max")
        log_lo = math.log10(lo)
        log_hi = math.log10(hi)
        values = [10.0 ** (log_lo + (log_hi - log_lo) * i / (steps - 1)) for i in range(steps)]
    else:
        values = [lo + (hi - lo) * i / (steps - 1) for i in range(steps)]
    if kind == "int":
        return sorted(set(int(round(value)) for value in values))
    return values


def grid_values_for_spec(spec: Any) -> list[Any]:
    if isinstance(spec, list):
        return list(spec)
    if isinstance(spec, dict):
        return values_from_range(spec)
    raise ValueError(f"Unsupported parameter spec: {spec!r}")


def generate_grid(config: dict[str, Any]) -> list[tuple[dict[str, Any], int]]:
    parameters = config.get("parameters", {})
    names = list(parameters.keys())
    values = [grid_values_for_spec(parameters[name]) for name in names]
    seeds = seeds_from_config(config)
    candidates: list[tuple[dict[str, Any], int]] = []
    for combo in itertools.product(*values):
        params = dict(zip(names, combo))
        for seed in seeds:
            candidates.append((params, seed))
    return candidates


def sample_from_spec(spec: Any, rng: random.Random) -> Any:
    if isinstance(spec, list):
        return rng.choice(spec)
    if not isinstance(spec, dict):
        raise ValueError(f"Unsupported parameter spec: {spec!r}")
    if "values" in spec:
        return rng.choice(list(spec["values"]))

    kind = str(spec.get("type", "float"))
    lo = float(spec["min"])
    hi = float(spec["max"])
    scale = str(spec.get("scale", "linear"))
    if kind == "int":
        if scale == "log":
            value = 10.0 ** rng.uniform(math.log10(lo), math.log10(hi))
            return int(round(value))
        return rng.randint(int(round(lo)), int(round(hi)))
    if scale == "log":
        if lo <= 0 or hi <= 0:
            raise ValueError("Log-scaled parameters must have positive min/max")
        return 10.0 ** rng.uniform(math.log10(lo), math.log10(hi))
    return rng.uniform(lo, hi)


def sample_random_params(config: dict[str, Any], rng: random.Random) -> dict[str, Any]:
    return {name: sample_from_spec(spec, rng) for name, spec in config.get("parameters", {}).items()}


def transform_numeric(value: float, spec: dict[str, Any]) -> float:
    if str(spec.get("scale", "linear")) == "log":
        return math.log10(max(value, 1e-300))
    return value


def inverse_transform_numeric(value: float, spec: dict[str, Any]) -> float:
    if str(spec.get("scale", "linear")) == "log":
        return 10.0 ** value
    return value


def numeric_bounds(spec: dict[str, Any]) -> tuple[float, float]:
    lo = transform_numeric(float(spec["min"]), spec)
    hi = transform_numeric(float(spec["max"]), spec)
    return min(lo, hi), max(lo, hi)


def gaussian_density(value: float, samples: list[float], lo: float, hi: float) -> float:
    if not samples:
        return 1.0 / max(hi - lo, 1e-9)
    if len(samples) >= 2:
        mean = sum(samples) / len(samples)
        variance = sum((sample - mean) ** 2 for sample in samples) / (len(samples) - 1)
        bandwidth = max(math.sqrt(variance), (hi - lo) / 12.0, 1e-9)
    else:
        bandwidth = max((hi - lo) / 8.0, 1e-9)
    normalizer = bandwidth * math.sqrt(2.0 * math.pi)
    return sum(math.exp(-0.5 * ((value - sample) / bandwidth) ** 2) / normalizer for sample in samples) / len(samples)


def categorical_density(value: Any, samples: list[Any], choices: list[Any]) -> float:
    smoothing = 1.0
    count = sum(1 for sample in samples if sample == value)
    return (count + smoothing) / (len(samples) + smoothing * max(len(choices), 1))


def sample_tpe_value(spec: Any, good_values: list[Any], rng: random.Random) -> Any:
    if isinstance(spec, list):
        choices = list(spec)
        if not good_values:
            return rng.choice(choices)
        weighted = good_values + choices
        return rng.choice(weighted)
    if not isinstance(spec, dict):
        raise ValueError(f"Unsupported parameter spec: {spec!r}")
    if "values" in spec:
        choices = list(spec["values"])
        if not good_values:
            return rng.choice(choices)
        weighted = good_values + choices
        return rng.choice(weighted)

    lo, hi = numeric_bounds(spec)
    transformed_good = [transform_numeric(float(value), spec) for value in good_values if finite(value)]
    if transformed_good:
        center = rng.choice(transformed_good)
        if len(transformed_good) >= 2:
            mean = sum(transformed_good) / len(transformed_good)
            variance = sum((value - mean) ** 2 for value in transformed_good) / (len(transformed_good) - 1)
            bandwidth = max(math.sqrt(variance), (hi - lo) / 12.0, 1e-9)
        else:
            bandwidth = max((hi - lo) / 8.0, 1e-9)
        transformed = min(hi, max(lo, rng.gauss(center, bandwidth)))
        value = inverse_transform_numeric(transformed, spec)
    else:
        value = sample_from_spec(spec, rng)

    if str(spec.get("type", "float")) == "int":
        return int(round(value))
    return value


def tpe_score(params: dict[str, Any], specs: dict[str, Any], good: list[Row], bad: list[Row]) -> float:
    score = 0.0
    for name, spec in specs.items():
        value = params[name]
        good_values = [row.get(param_column(name)) for row in good]
        bad_values = [row.get(param_column(name)) for row in bad]
        if isinstance(spec, list):
            choices = list(spec)
            good_density = categorical_density(value, good_values, choices)
            bad_density = categorical_density(value, bad_values, choices)
        elif isinstance(spec, dict) and "values" in spec:
            choices = list(spec["values"])
            good_density = categorical_density(value, good_values, choices)
            bad_density = categorical_density(value, bad_values, choices)
        elif isinstance(spec, dict):
            lo, hi = numeric_bounds(spec)
            transformed = transform_numeric(float(value), spec)
            good_samples = [transform_numeric(float(item), spec) for item in good_values if finite(item)]
            bad_samples = [transform_numeric(float(item), spec) for item in bad_values if finite(item)]
            good_density = gaussian_density(transformed, good_samples, lo, hi)
            bad_density = gaussian_density(transformed, bad_samples, lo, hi)
        else:
            continue
        score += math.log(good_density + 1e-12) - math.log(bad_density + 1e-12)
    return score


def sample_tpe_params(config: dict[str, Any], completed: list[Row], rng: random.Random, mode: str) -> dict[str, Any]:
    specs = config.get("parameters", {})
    startup_trials = int(config.get("startup_trials", max(8, 2 * max(len(specs), 1))))
    if len(completed) < startup_trials:
        return sample_random_params(config, rng)

    quantile = float(config.get("tpe_quantile", 0.25))
    ordered = sorted(completed, key=lambda row: numeric(row.get("objective")), reverse=(mode != "min"))
    good_count = max(2, int(math.ceil(len(ordered) * quantile)))
    good = ordered[:good_count]
    bad = ordered[good_count:] or ordered[good_count - 1 :]

    best_params: dict[str, Any] | None = None
    best_score = -math.inf
    candidates = int(config.get("tpe_candidates", 128))
    for _ in range(candidates):
        params = {}
        for name, spec in specs.items():
            good_values = [row.get(param_column(name)) for row in good]
            if rng.random() < 0.15:
                params[name] = sample_from_spec(spec, rng)
            else:
                params[name] = sample_tpe_value(spec, good_values, rng)
        score = tpe_score(params, specs, good, bad)
        if score > best_score:
            best_score = score
            best_params = params
    return best_params if best_params is not None else sample_random_params(config, rng)


def parse_stats(run_dir: Path) -> dict[str, Any]:
    rows = read_csv(run_dir / "stats.csv")
    if not rows:
        return {}
    final = rows[-1]
    metrics: dict[str, Any] = {}
    for key, value in final.items():
        if key == "generation":
            metrics["final_generation"] = numeric(value)
        else:
            metrics[key] = numeric(value)
    return metrics


def parse_trajectory(run_dir: Path) -> dict[str, Any]:
    rows = read_csv(run_dir / "best_trajectory.csv")
    if not rows:
        return {}

    x_values = [numeric(row.get("x")) for row in rows]
    y_values = [numeric(row.get("y")) for row in rows]
    distances = [numeric(row.get("distance")) for row in rows]
    speeds = [numeric(row.get("speed_command"), 0.0) for row in rows]
    turns = [abs(numeric(row.get("turn_command"), 0.0)) for row in rows]
    visible = [numeric(row.get("target_visible"), 0.0) for row in rows]
    foods = [numeric(row.get("foods_collected"), 0.0) for row in rows]
    spikes = [numeric(row.get("cumulative_spikes"), 0.0) for row in rows]

    path_length = 0.0
    for i in range(1, len(rows)):
        if all(math.isfinite(value) for value in [x_values[i - 1], y_values[i - 1], x_values[i], y_values[i]]):
            path_length += math.hypot(x_values[i] - x_values[i - 1], y_values[i] - y_values[i - 1])

    return {
        "recorded_foods_collected": max(foods) if foods else math.nan,
        "recorded_final_distance": distances[-1] if distances else math.nan,
        "recorded_min_distance": min(distances) if distances else math.nan,
        "behavior_path_length": path_length,
        "behavior_mean_speed": sum(speeds) / len(speeds) if speeds else math.nan,
        "behavior_max_speed": max(speeds) if speeds else math.nan,
        "behavior_mean_abs_turn": sum(turns) / len(turns) if turns else math.nan,
        "behavior_visible_fraction": sum(visible) / len(visible) if visible else math.nan,
        "behavior_spikes": max(spikes) if spikes else math.nan,
    }


def parse_run_metrics(run_dir: Path) -> dict[str, Any]:
    metrics = parse_stats(run_dir)
    metrics.update(parse_trajectory(run_dir))
    return metrics


def build_command(config: dict[str, Any], params: dict[str, Any], seed: int, run_dir: Path) -> list[str]:
    executable = Path(str(config.get("executable", "build/neuroevo_sim.exe")))
    command = [str(executable)]
    command.extend(args_from_mapping(config.get("base_args", {})))
    command.extend(args_from_mapping(params))
    command.extend(["--seed", str(seed), "--out", str(run_dir)])
    return command


def run_trial(
    config: dict[str, Any],
    search_dir: Path,
    params: dict[str, Any],
    seed: int,
    trial_index: int,
    objective_metric: str,
) -> Row:
    root = repo_root()
    run_dir = search_dir / "trials" / f"trial_{trial_index:04d}_seed_{seed}"
    run_dir.mkdir(parents=True, exist_ok=True)
    command = build_command(config, params, seed, run_dir)
    timeout = config.get("timeout_seconds")
    started = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=float(timeout) if timeout is not None else None,
    )
    duration = time.perf_counter() - started
    (run_dir / "stdout.txt").write_text(completed.stdout, encoding="utf-8")

    metrics = parse_run_metrics(run_dir) if completed.returncode == 0 else {}
    objective = numeric(metrics.get(objective_metric))
    status = "complete" if completed.returncode == 0 and math.isfinite(objective) else "failed"

    row: Row = {
        "trial_index": trial_index,
        "status": status,
        "objective": objective,
        "objective_metric": objective_metric,
        "seed": seed,
        "duration_sec": duration,
        "returncode": completed.returncode,
        "run_dir": str(run_dir),
        "trial_key": trial_key(params, seed),
        "command": " ".join(command),
    }
    for name, value in params.items():
        row[param_column(name)] = value
    row.update(metrics)
    return row


def csv_value(value: Any) -> str:
    if isinstance(value, float):
        if math.isnan(value):
            return ""
        return f"{value:.12g}"
    return str(value)


def write_trials_csv(path: Path, rows: list[Row], names: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    param_columns = [param_column(name) for name in names]
    extra_columns = sorted({key for row in rows for key in row.keys()} - set(FIXED_COLUMNS) - set(param_columns))
    columns = FIXED_COLUMNS + param_columns + extra_columns
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        for row in sorted(rows, key=lambda item: int(item.get("trial_index", 0))):
            writer.writerow({key: csv_value(row.get(key, "")) for key in columns})


def load_existing_trials(path: Path) -> list[Row]:
    rows: list[Row] = []
    for row in read_csv(path):
        converted: Row = dict(row)
        for key, value in list(converted.items()):
            if key in {"trial_index", "seed", "returncode"} and value != "":
                converted[key] = int(float(value))
            elif key == "status" or key.startswith("param:") or key in {"run_dir", "trial_key", "command", "objective_metric"}:
                continue
            elif value != "":
                parsed = numeric(value)
                if math.isfinite(parsed):
                    converted[key] = parsed
        rows.append(converted)
    return rows


def complete_rows(rows: list[Row]) -> list[Row]:
    return [row for row in rows if row.get("status") == "complete" and finite(row.get("objective"))]


def existing_keys(rows: list[Row], include_failed: bool) -> set[str]:
    keys = set()
    for row in rows:
        if row.get("status") == "complete" or include_failed:
            key = str(row.get("trial_key", ""))
            if key:
                keys.add(key)
    return keys


def next_trial_index(rows: list[Row]) -> int:
    if not rows:
        return 0
    return max(int(row.get("trial_index", 0)) for row in rows) + 1


def run_grid_or_random(
    config: dict[str, Any],
    search_dir: Path,
    rows: list[Row],
    mode: str,
    max_trials: int | None,
    jobs: int,
    rerun_failed: bool,
    dry_run: bool,
) -> list[Row]:
    objective_metric, _ = objective_settings(config)
    names = param_names(config)
    keys = existing_keys(rows, include_failed=not rerun_failed)
    candidates: list[tuple[dict[str, Any], int]] = []
    rng = random.Random(int(config.get("search_seed", 12345)))
    seeds = seeds_from_config(config)

    if mode == "grid":
        source = generate_grid(config)
        for params, seed in source:
            key = trial_key(params, seed)
            if key not in keys:
                candidates.append((params, seed))
    else:
        target_total = max_trials if max_trials is not None else int(config.get("max_trials", 32))
        attempts = 0
        while len(complete_rows(rows)) + len(candidates) < target_total and attempts < target_total * 200:
            attempts += 1
            params = sample_random_params(config, rng)
            seed = seeds[(len(rows) + len(candidates)) % len(seeds)]
            key = trial_key(params, seed)
            if key in keys:
                continue
            keys.add(key)
            candidates.append((params, seed))

    if max_trials is not None:
        remaining = max(0, max_trials - len(complete_rows(rows)))
        candidates = candidates[:remaining]

    print(f"Planned {len(candidates)} {mode} trial(s).")
    if dry_run:
        for i, (params, seed) in enumerate(candidates[:20], start=next_trial_index(rows)):
            print(f"{i}: seed={seed} params={params}")
        return rows

    trial_index = next_trial_index(rows)
    pending = []
    for params, seed in candidates:
        pending.append((trial_index, params, seed))
        trial_index += 1

    trials_path = search_dir / "trials.csv"
    if jobs <= 1:
        for index, params, seed in pending:
            print(f"Running trial {index}: seed={seed} params={params}")
            row = run_trial(config, search_dir, params, seed, index, objective_metric)
            rows.append(row)
            write_trials_csv(trials_path, rows, names)
            print(f"  {row['status']} objective={csv_value(row['objective'])} run={row['run_dir']}")
        return rows

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        future_map = {
            executor.submit(run_trial, config, search_dir, params, seed, index, objective_metric): (index, params, seed)
            for index, params, seed in pending
        }
        for future in concurrent.futures.as_completed(future_map):
            index, _, _ = future_map[future]
            row = future.result()
            rows.append(row)
            write_trials_csv(trials_path, rows, names)
            print(f"Trial {index} {row['status']} objective={csv_value(row['objective'])} run={row['run_dir']}")
    return rows


def run_bayes(
    config: dict[str, Any],
    search_dir: Path,
    rows: list[Row],
    max_trials: int | None,
    rerun_failed: bool,
    dry_run: bool,
) -> list[Row]:
    objective_metric, objective_mode = objective_settings(config)
    names = param_names(config)
    keys = existing_keys(rows, include_failed=not rerun_failed)
    rng = random.Random(int(config.get("search_seed", 12345)))
    seeds = seeds_from_config(config)
    target_total = max_trials if max_trials is not None else int(config.get("max_trials", 32))
    trials_path = search_dir / "trials.csv"

    while len(complete_rows(rows)) < target_total:
        completed = complete_rows(rows)
        seed = seeds[len(rows) % len(seeds)]
        params: dict[str, Any] | None = None
        for _ in range(256):
            candidate = sample_tpe_params(config, completed, rng, objective_mode)
            key = trial_key(candidate, seed)
            if key not in keys:
                params = candidate
                keys.add(key)
                break
        if params is None:
            raise RuntimeError("Could not find a non-duplicate Bayesian candidate")

        index = next_trial_index(rows)
        print(f"Running bayes trial {index}: seed={seed} params={params}")
        if dry_run:
            rows.append({
                "trial_index": index,
                "status": "planned",
                "objective": math.nan,
                "objective_metric": objective_metric,
                "seed": seed,
                "run_dir": str(search_dir / "trials" / f"trial_{index:04d}_seed_{seed}"),
                "trial_key": trial_key(params, seed),
            } | {param_column(name): params[name] for name in names})
            break

        row = run_trial(config, search_dir, params, seed, index, objective_metric)
        rows.append(row)
        write_trials_csv(trials_path, rows, names)
        print(f"  {row['status']} objective={csv_value(row['objective'])} run={row['run_dir']}")
    return rows


def aggregate_parameter_sets(search_dir: Path, rows: list[Row], names: list[str], objective_mode: str) -> None:
    groups: dict[str, list[Row]] = defaultdict(list)
    for row in complete_rows(rows):
        params = {name: row.get(param_column(name), "") for name in names}
        groups[param_signature(params)].append(row)

    output_rows: list[dict[str, Any]] = []
    for signature, group in groups.items():
        objectives = [numeric(row.get("objective")) for row in group if finite(row.get("objective"))]
        if not objectives:
            continue
        best_objective = min(objectives) if objective_mode == "min" else max(objectives)
        params = json.loads(signature)
        output: dict[str, Any] = {
            "parameter_set": signature,
            "trials": len(group),
            "objective_mean": sum(objectives) / len(objectives),
            "objective_best": best_objective,
            "objective_min": min(objectives),
            "objective_max": max(objectives),
        }
        for name in names:
            output[param_column(name)] = params.get(name, "")
        for metric in ["recorded_foods_collected", "behavior_path_length", "behavior_mean_speed", "behavior_mean_abs_turn"]:
            values = [numeric(row.get(metric)) for row in group if finite(row.get(metric))]
            if values:
                output[f"{metric}_mean"] = sum(values) / len(values)
        output_rows.append(output)

    output_rows.sort(key=lambda row: numeric(row["objective_best"]), reverse=(objective_mode != "min"))
    columns = ["parameter_set", "trials", "objective_mean", "objective_best", "objective_min", "objective_max"]
    columns += [param_column(name) for name in names]
    extra = sorted({key for row in output_rows for key in row.keys()} - set(columns))
    with (search_dir / "parameter_sets.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns + extra)
        writer.writeheader()
        for row in output_rows:
            writer.writerow({key: csv_value(row.get(key, "")) for key in columns + extra})


def best_rows(rows: list[Row], mode: str, count: int) -> list[Row]:
    complete = complete_rows(rows)
    complete.sort(key=lambda row: numeric(row.get("objective")), reverse=(mode != "min"))
    return complete[:count]


def write_best_trials(search_dir: Path, rows: list[Row], names: list[str], objective_mode: str, count: int = 20) -> None:
    top = best_rows(rows, objective_mode, count)
    write_trials_csv(search_dir / "best_trials.csv", top, names)


def plot_search(search_dir: Path, rows: list[Row], names: list[str], objective_mode: str) -> None:
    import matplotlib.pyplot as plt

    complete = complete_rows(rows)
    if not complete:
        print("No complete trials to plot.")
        return

    ordered = sorted(complete, key=lambda row: int(row.get("trial_index", 0)))
    indices = [int(row["trial_index"]) for row in ordered]
    objectives = [numeric(row["objective"]) for row in ordered]
    best_so_far: list[float] = []
    current = math.inf if objective_mode == "min" else -math.inf
    for objective in objectives:
        if better(objective, current, objective_mode):
            current = objective
        best_so_far.append(current)

    fig, ax = plt.subplots(figsize=(9, 5))
    ax.scatter(indices, objectives, color="#1f77b4", label="trial objective")
    ax.plot(indices, best_so_far, color="#d62728", linewidth=2.0, label="best so far")
    ax.set_xlabel("trial")
    ax.set_ylabel(str(ordered[0].get("objective_metric", "objective")))
    ax.grid(True, alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(search_dir / "objective.png", dpi=160)
    plt.close(fig)

    if names:
        columns = [param_column(name) for name in names]
        fig, axes = plt.subplots(len(columns), 1, figsize=(9, max(3, 2.5 * len(columns))), squeeze=False)
        for ax, column in zip(axes.flat, columns):
            values = [row.get(column, "") for row in complete]
            numeric_values = [numeric(value) for value in values]
            if all(math.isfinite(value) for value in numeric_values):
                ax.scatter(numeric_values, [numeric(row["objective"]) for row in complete], color="#1f77b4", alpha=0.8)
                ax.set_xlabel(column.removeprefix("param:"))
            else:
                categories = sorted({str(value) for value in values})
                category_index = {value: i for i, value in enumerate(categories)}
                x_values = [category_index[str(value)] for value in values]
                ax.scatter(x_values, [numeric(row["objective"]) for row in complete], color="#1f77b4", alpha=0.8)
                ax.set_xticks(range(len(categories)), categories, rotation=30, ha="right")
                ax.set_xlabel(column.removeprefix("param:"))
            ax.set_ylabel("objective")
            ax.grid(True, alpha=0.25)
        fig.tight_layout()
        fig.savefig(search_dir / "parameter_effects.png", dpi=160)
        plt.close(fig)

    behavior_metrics = [
        ("recorded_foods_collected", "recorded foods"),
        ("behavior_path_length", "path length"),
        ("behavior_mean_speed", "mean speed"),
        ("behavior_visible_fraction", "visible fraction"),
    ]
    fig, axes = plt.subplots(2, 2, figsize=(10, 8))
    objective_values = [numeric(row["objective"]) for row in complete]
    for ax, (metric, label) in zip(axes.flat, behavior_metrics):
        metric_values = [numeric(row.get(metric)) for row in complete]
        scatter = ax.scatter(indices[: len(metric_values)], metric_values, c=objective_values, cmap="viridis", alpha=0.85)
        ax.set_xlabel("trial")
        ax.set_ylabel(label)
        ax.grid(True, alpha=0.25)
    fig.colorbar(scatter, ax=axes.ravel().tolist(), label="objective")
    fig.savefig(search_dir / "behavior.png", dpi=160)
    plt.close(fig)


def write_summary(search_dir: Path, rows: list[Row], names: list[str], objective_mode: str) -> None:
    top = best_rows(rows, objective_mode, 10)
    lines = [
        "# Hyperparameter Search Summary",
        "",
        f"Complete trials: {len(complete_rows(rows))}",
        "",
        "## Top Trials",
        "",
    ]
    if top:
        header = ["trial", "objective", "seed", "run_dir"] + names
        lines.append("| " + " | ".join(header) + " |")
        lines.append("| " + " | ".join(["---"] * len(header)) + " |")
        for row in top:
            values = [
                str(row.get("trial_index", "")),
                csv_value(row.get("objective", "")),
                str(row.get("seed", "")),
                str(row.get("run_dir", "")),
            ]
            values += [csv_value(row.get(param_column(name), "")) for name in names]
            lines.append("| " + " | ".join(values) + " |")
    else:
        lines.append("No complete trials yet.")
    lines += [
        "",
        "## Plots",
        "",
        "- `objective.png`: objective by trial and best-so-far curve.",
        "- `parameter_effects.png`: parameter values versus objective.",
        "- `behavior.png`: behavioral metrics from recorded best trajectories.",
        "- `best_trials.csv`: top individual runs.",
        "- `parameter_sets.csv`: aggregate view grouped by parameter values.",
        "",
    ]
    (search_dir / "summary.md").write_text("\n".join(lines), encoding="utf-8")


def visualize_top_runs(search_dir: Path, rows: list[Row], objective_mode: str, count: int) -> None:
    if count <= 0:
        return
    root = repo_root()
    plot_script = root / "tools" / "plot_run.py"
    view_script = root / "tools" / "view_run.py"
    for row in best_rows(rows, objective_mode, count):
        run_dir = Path(str(row["run_dir"]))
        if not run_dir.exists():
            continue
        print(f"Visualizing top trial {row['trial_index']}: {run_dir}")
        subprocess.run([sys.executable, str(plot_script), str(run_dir)], cwd=root, check=True)
        subprocess.run([sys.executable, str(view_script), str(run_dir)], cwd=root, check=True)


def resolve_config_path(value: str) -> Path:
    path = Path(value)
    if path.is_dir():
        return path / "search_config.json"
    return path


def main() -> None:
    parser = argparse.ArgumentParser(description="Run grid/random/TPE-style Bayesian searches over neuroevo_sim CLI parameters.")
    parser.add_argument("config", type=str, help="JSON config path, or search directory containing search_config.json")
    parser.add_argument("--mode", choices=["grid", "random", "bayes"], default="grid", help="Search mode")
    parser.add_argument("--max-trials", type=int, default=None, help="Maximum complete trials to keep/run")
    parser.add_argument("--jobs", type=int, default=1, help="Parallel workers for grid/random modes")
    parser.add_argument("--rerun-failed", action="store_true", help="Retry failed trial keys instead of treating them as already attempted")
    parser.add_argument("--dry-run", action="store_true", help="Print planned trials without running simulations")
    parser.add_argument("--plot-only", action="store_true", help="Regenerate summary CSVs and plots from an existing search directory")
    parser.add_argument("--visualize-top", type=int, default=0, help="Generate per-run plots and viewer.html for the top N trials")
    args = parser.parse_args()

    config_path = resolve_config_path(args.config).resolve()
    config = read_json(config_path)
    search_dir = Path(config.get("output_dir", default_output_dir(config_path, config))).resolve()
    search_dir.mkdir(parents=True, exist_ok=True)
    write_json(search_dir / "search_config.json", config)

    names = param_names(config)
    objective_metric, objective_mode = objective_settings(config)
    trials_path = search_dir / "trials.csv"
    rows = load_existing_trials(trials_path)

    if not args.plot_only:
        if args.mode == "bayes":
            if args.jobs > 1:
                print("Bayes mode is sequential in this prototype; ignoring --jobs.")
            rows = run_bayes(config, search_dir, rows, args.max_trials, args.rerun_failed, args.dry_run)
        else:
            rows = run_grid_or_random(
                config,
                search_dir,
                rows,
                args.mode,
                args.max_trials,
                max(1, args.jobs),
                args.rerun_failed,
                args.dry_run,
            )

    if args.dry_run:
        return

    write_trials_csv(trials_path, rows, names)
    aggregate_parameter_sets(search_dir, rows, names, objective_mode)
    write_best_trials(search_dir, rows, names, objective_mode)
    plot_search(search_dir, rows, names, objective_mode)
    write_summary(search_dir, rows, names, objective_mode)
    visualize_top_runs(search_dir, rows, objective_mode, args.visualize_top)

    top = best_rows(rows, objective_mode, 5)
    print(f"Search directory: {search_dir}")
    if top:
        print("Top trials:")
        for row in top:
            print(f"  trial {row['trial_index']}: objective={csv_value(row['objective'])} run={row['run_dir']}")


if __name__ == "__main__":
    main()
