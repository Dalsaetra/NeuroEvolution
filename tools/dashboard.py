"""Local, dependency-free dashboard for configuring and inspecting NeuroEvolution runs."""

from __future__ import annotations

import argparse
import csv
import json
import math
import mimetypes
import os
import re
import subprocess
import sys
import threading
import time
import traceback
import urllib.parse
import webbrowser
from dataclasses import dataclass, field
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable
from uuid import uuid4


REPO_ROOT = Path(__file__).resolve().parents[1]
DASHBOARD_ROOT = REPO_ROOT / "tools" / "dashboard"
SCHEMA_PATH = DASHBOARD_ROOT / "parameters.json"
RUNS_ROOT = REPO_ROOT / "runs"
RUN_NAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def read_json(path: Path, fallback: Any = None) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return fallback


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    try:
        with path.open(newline="", encoding="utf-8") as handle:
            return list(csv.DictReader(handle))
    except (OSError, csv.Error, UnicodeDecodeError):
        return []


def read_metadata(path: Path) -> dict[str, str]:
    rows = read_csv_rows(path)
    return {row.get("key", ""): row.get("value", "") for row in rows if row.get("key")}


def numeric_row(row: dict[str, str]) -> dict[str, Any]:
    converted: dict[str, Any] = {}
    for key, value in row.items():
        if value is None:
            converted[key] = None
            continue
        try:
            number = float(value)
            if not math.isfinite(number):
                converted[key] = None
            else:
                converted[key] = int(number) if number.is_integer() else number
        except ValueError:
            converted[key] = value
    return converted


def safe_run_dir(run_name: str) -> Path:
    if not RUN_NAME_PATTERN.fullmatch(run_name):
        raise ValueError("Run name may contain letters, numbers, dots, underscores, and hyphens only.")
    candidate = (RUNS_ROOT / run_name).resolve()
    if candidate.parent != RUNS_ROOT.resolve():
        raise ValueError("Run directory must be directly inside runs/.")
    return candidate


def executable_candidates() -> list[Path]:
    suffix = ".exe" if os.name == "nt" else ""
    return [
        REPO_ROOT / "build" / f"neuroevo_sim{suffix}",
        REPO_ROOT / "build" / "Release" / f"neuroevo_sim{suffix}",
        REPO_ROOT / "build" / "Debug" / f"neuroevo_sim{suffix}",
    ]


def find_executable() -> Path | None:
    return next((path for path in executable_candidates() if path.is_file()), None)


def schema() -> dict[str, Any]:
    loaded = read_json(SCHEMA_PATH)
    if not isinstance(loaded, dict) or not isinstance(loaded.get("parameters"), list):
        raise RuntimeError(f"Invalid dashboard parameter schema: {SCHEMA_PATH}")
    return loaded


def validate_parameters(values: dict[str, Any]) -> tuple[list[str], dict[str, Any]]:
    specs = schema()["parameters"]
    known = {spec["id"]: spec for spec in specs}
    unknown = sorted(set(values) - set(known))
    if unknown:
        raise ValueError(f"Unknown parameters: {', '.join(unknown)}")

    args: list[str] = []
    normalized: dict[str, Any] = {}
    for spec in specs:
        identifier = spec["id"]
        value = values.get(identifier, spec["default"])
        kind = spec["type"]
        if kind == "boolean":
            if not isinstance(value, bool):
                raise ValueError(f"{spec['label']} must be true or false.")
            cli_value = "1" if value else "0"
        elif kind == "integer":
            if isinstance(value, bool):
                raise ValueError(f"{spec['label']} must be an integer.")
            try:
                number = float(value)
            except (TypeError, ValueError) as error:
                raise ValueError(f"{spec['label']} must be an integer.") from error
            if not math.isfinite(number) or not number.is_integer():
                raise ValueError(f"{spec['label']} must be an integer.")
            value = int(number)
            cli_value = str(value)
        elif kind == "number":
            if isinstance(value, bool):
                raise ValueError(f"{spec['label']} must be a number.")
            try:
                value = float(value)
            except (TypeError, ValueError) as error:
                raise ValueError(f"{spec['label']} must be a number.") from error
            if not math.isfinite(value):
                raise ValueError(f"{spec['label']} must be finite.")
            cli_value = format(value, ".15g")
        elif kind == "enum":
            if value not in spec.get("choices", []):
                raise ValueError(f"{spec['label']} has an invalid choice.")
            cli_value = str(value)
        else:
            raise ValueError(f"Unsupported parameter type for {identifier}: {kind}")

        if kind in {"integer", "number"}:
            if "min" in spec and value < spec["min"]:
                raise ValueError(f"{spec['label']} must be at least {spec['min']}.")
            if "max" in spec and value > spec["max"]:
                raise ValueError(f"{spec['label']} must be at most {spec['max']}.")
        normalized[identifier] = value
        args.extend([spec["flag"], cli_value])
    return args, normalized


def run_record(run_dir: Path) -> dict[str, Any]:
    stats = read_csv_rows(run_dir / "stats.csv")
    final = numeric_row(stats[-1]) if stats else {}
    metadata = read_metadata(run_dir / "metadata.csv")
    config = read_json(run_dir / "dashboard_config.json", {}) or {}
    modified = max(
        (path.stat().st_mtime for path in (run_dir / "stats.csv", run_dir / "metadata.csv", run_dir) if path.exists()),
        default=run_dir.stat().st_mtime,
    )
    return {
        "name": run_dir.name,
        "modified": modified,
        "modifiedIso": datetime.fromtimestamp(modified, timezone.utc).isoformat(),
        "generationsCompleted": len(stats),
        "generationsTarget": int(config.get("parameters", {}).get("generations", metadata.get("generations", 0)) or 0),
        "eaMode": metadata.get("ea_mode", config.get("parameters", {}).get("ea-mode", "")),
        "task": metadata.get("task_regime", config.get("parameters", {}).get("task", "")),
        "final": final,
        "hasTrajectory": (run_dir / "best_trajectory.csv").is_file(),
        "hasViewer": (run_dir / "viewer.html").is_file(),
        "hasPareto": (run_dir / "pareto_front.csv").is_file(),
        "hasParetoViewer": (run_dir / "pareto_front.html").is_file(),
    }


def list_runs() -> list[dict[str, Any]]:
    RUNS_ROOT.mkdir(parents=True, exist_ok=True)
    records = []
    for path in RUNS_ROOT.iterdir():
        if not path.is_dir() or path.name.startswith("."):
            continue
        if not (path / "stats.csv").exists() and not (path / "dashboard_config.json").exists():
            continue
        try:
            records.append(run_record(path))
        except OSError:
            continue
    records.sort(key=lambda item: item["modified"], reverse=True)
    return records


@dataclass
class Job:
    id: str
    kind: str
    label: str
    run_name: str | None = None
    status: str = "queued"
    started_at: str | None = None
    finished_at: str | None = None
    return_code: int | None = None
    log: list[str] = field(default_factory=list)
    process: subprocess.Popen[str] | None = field(default=None, repr=False)
    stop_requested: bool = False

    def public(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "kind": self.kind,
            "label": self.label,
            "runName": self.run_name,
            "status": self.status,
            "startedAt": self.started_at,
            "finishedAt": self.finished_at,
            "returnCode": self.return_code,
            "log": self.log[-300:],
        }


class JobManager:
    def __init__(self) -> None:
        self.jobs: dict[str, Job] = {}
        self.lock = threading.RLock()

    def active_job(self) -> Job | None:
        with self.lock:
            return next((job for job in self.jobs.values() if job.status in {"queued", "running", "stopping"}), None)

    def create(self, kind: str, label: str, worker: Callable[[Job], int], run_name: str | None = None) -> Job:
        with self.lock:
            active = self.active_job()
            if active is not None:
                raise RuntimeError(f"{active.label} is already {active.status}.")
            job = Job(uuid4().hex[:12], kind, label, run_name=run_name)
            self.jobs[job.id] = job

        def execute() -> None:
            job.status = "running"
            job.started_at = utc_now()
            try:
                job.return_code = worker(job)
                if job.stop_requested:
                    job.status = "stopped"
                else:
                    job.status = "succeeded" if job.return_code == 0 else "failed"
            except Exception:
                job.return_code = -1
                job.log.extend(traceback.format_exc().splitlines())
                job.status = "failed"
            finally:
                job.process = None
                job.finished_at = utc_now()

        threading.Thread(target=execute, name=f"dashboard-{job.id}", daemon=True).start()
        return job

    def run_command(self, job: Job, command: list[str]) -> int:
        job.log.append("› " + subprocess.list2cmdline(command))
        creation_flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        process = subprocess.Popen(
            command,
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            creationflags=creation_flags,
        )
        job.process = process
        assert process.stdout is not None
        for line in process.stdout:
            job.log.append(line.rstrip())
            if len(job.log) > 1000:
                del job.log[:200]
        return process.wait()

    def stop(self, job_id: str) -> Job:
        with self.lock:
            job = self.jobs.get(job_id)
            if job is None:
                raise KeyError(job_id)
            job.stop_requested = True
            if job.status in {"queued", "running"}:
                job.status = "stopping"
            process = job.process
        if process is not None and process.poll() is None:
            process.terminate()
        return job

    def public_jobs(self) -> list[dict[str, Any]]:
        with self.lock:
            ordered = sorted(self.jobs.values(), key=lambda job: job.started_at or "", reverse=True)
            return [job.public() for job in ordered[:20]]


JOBS = JobManager()


def visualization_commands(run_dir: Path) -> list[list[str]]:
    commands = [
        [sys.executable, str(REPO_ROOT / "tools" / "plot_run.py"), str(run_dir)],
        [sys.executable, str(REPO_ROOT / "tools" / "view_run.py"), str(run_dir)],
    ]
    if (run_dir / "pareto_front.csv").is_file():
        commands.append([sys.executable, str(REPO_ROOT / "tools" / "view_pareto_front.py"), str(run_dir)])
    return commands


def build_worker(job: Job) -> int:
    commands = [
        ["cmake", "-S", str(REPO_ROOT), "-B", str(REPO_ROOT / "build")],
        ["cmake", "--build", str(REPO_ROOT / "build")],
        ["ctest", "--test-dir", str(REPO_ROOT / "build"), "--output-on-failure"],
    ]
    for command in commands:
        code = JOBS.run_command(job, command)
        if code != 0 or job.stop_requested:
            return code
    return 0


def simulation_worker(job: Job, command: list[str], run_dir: Path) -> int:
    code = JOBS.run_command(job, command)
    if code != 0 or job.stop_requested:
        return code
    job.log.append("Generating dashboard visualizations…")
    for visualization in visualization_commands(run_dir):
        visualization_code = JOBS.run_command(job, visualization)
        if visualization_code != 0:
            job.log.append("Visualization generation failed; simulation results are still available.")
            break
    return code


class DashboardHandler(BaseHTTPRequestHandler):
    server_version = "NeuroEvolutionDashboard/1.0"

    def log_message(self, format: str, *args: Any) -> None:
        return

    def send_json(self, payload: Any, status: int = HTTPStatus.OK) -> None:
        encoded = json.dumps(payload, allow_nan=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(encoded)

    def send_error_json(self, message: str, status: int = HTTPStatus.BAD_REQUEST) -> None:
        self.send_json({"error": message}, status)

    def body_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > 2_000_000:
            raise ValueError("Request body is missing or too large.")
        try:
            payload = json.loads(self.rfile.read(length))
        except json.JSONDecodeError as error:
            raise ValueError("Request body must be valid JSON.") from error
        if not isinstance(payload, dict):
            raise ValueError("Request body must be a JSON object.")
        return payload

    def do_GET(self) -> None:
        path = urllib.parse.urlparse(self.path).path
        try:
            if path == "/api/bootstrap":
                executable = find_executable()
                self.send_json({
                    "schema": schema(),
                    "runs": list_runs(),
                    "jobs": JOBS.public_jobs(),
                    "build": {
                        "ready": executable is not None,
                        "executable": str(executable.relative_to(REPO_ROOT)) if executable else None,
                    },
                })
                return
            if path == "/api/state":
                executable = find_executable()
                self.send_json({
                    "runs": list_runs(),
                    "jobs": JOBS.public_jobs(),
                    "build": {"ready": executable is not None},
                })
                return
            if path.startswith("/api/runs/") and path.endswith("/data"):
                run_name = urllib.parse.unquote(path[len("/api/runs/") : -len("/data")]).strip("/")
                run_dir = safe_run_dir(run_name)
                if not run_dir.is_dir():
                    self.send_error_json("Run not found.", HTTPStatus.NOT_FOUND)
                    return
                stats = [numeric_row(row) for row in read_csv_rows(run_dir / "stats.csv")]
                self.send_json({
                    "run": run_record(run_dir),
                    "stats": stats,
                    "metadata": read_metadata(run_dir / "metadata.csv"),
                    "config": read_json(run_dir / "dashboard_config.json", {}),
                })
                return
            if path.startswith("/artifacts/"):
                self.serve_artifact(path[len("/artifacts/") :])
                return
            self.serve_static(path)
        except ValueError as error:
            self.send_error_json(str(error))
        except Exception as error:
            self.send_error_json(str(error), HTTPStatus.INTERNAL_SERVER_ERROR)

    def do_POST(self) -> None:
        path = urllib.parse.urlparse(self.path).path
        try:
            if path == "/api/build":
                job = JOBS.create("build", "Build and test", build_worker)
                self.send_json({"job": job.public()}, HTTPStatus.ACCEPTED)
                return
            if path == "/api/run":
                payload = self.body_json()
                run_name = str(payload.get("runName", "")).strip()
                run_dir = safe_run_dir(run_name)
                if run_dir.exists() and any(run_dir.iterdir()):
                    self.send_error_json("That run name already exists. Choose a new name.", HTTPStatus.CONFLICT)
                    return
                executable = find_executable()
                if executable is None:
                    self.send_error_json("Simulator is not built yet. Use Build & test first.", HTTPStatus.CONFLICT)
                    return
                values = payload.get("parameters", {})
                if not isinstance(values, dict):
                    raise ValueError("parameters must be a JSON object.")
                cli_args, normalized = validate_parameters(values)
                command = [str(executable), *cli_args, "--out", str(run_dir)]
                config_record = {
                    "createdAt": utc_now(),
                    "parameters": normalized,
                    "command": command,
                }

                def simulate(active_job: Job) -> int:
                    run_dir.mkdir(parents=True, exist_ok=True)
                    (run_dir / "dashboard_config.json").write_text(
                        json.dumps(config_record, indent=2), encoding="utf-8"
                    )
                    return simulation_worker(active_job, command, run_dir)

                job = JOBS.create(
                    "simulation",
                    f"Simulation {run_name}",
                    simulate,
                    run_name=run_name,
                )
                self.send_json({"job": job.public(), "runName": run_name}, HTTPStatus.ACCEPTED)
                return
            if path == "/api/visualize":
                payload = self.body_json()
                run_name = str(payload.get("runName", "")).strip()
                run_dir = safe_run_dir(run_name)
                if not (run_dir / "best_trajectory.csv").is_file():
                    self.send_error_json("This run has no completed trajectory yet.", HTTPStatus.CONFLICT)
                    return

                def visualize(job: Job) -> int:
                    for command in visualization_commands(run_dir):
                        code = JOBS.run_command(job, command)
                        if code != 0 or job.stop_requested:
                            return code
                    return 0

                job = JOBS.create("visualize", f"Visualize {run_name}", visualize, run_name=run_name)
                self.send_json({"job": job.public()}, HTTPStatus.ACCEPTED)
                return
            if path == "/api/stop":
                payload = self.body_json()
                job = JOBS.stop(str(payload.get("jobId", "")))
                self.send_json({"job": job.public()})
                return
            self.send_error_json("Unknown endpoint.", HTTPStatus.NOT_FOUND)
        except KeyError:
            self.send_error_json("Job not found.", HTTPStatus.NOT_FOUND)
        except RuntimeError as error:
            self.send_error_json(str(error), HTTPStatus.CONFLICT)
        except ValueError as error:
            self.send_error_json(str(error))
        except Exception as error:
            self.send_error_json(str(error), HTTPStatus.INTERNAL_SERVER_ERROR)

    def serve_static(self, request_path: str) -> None:
        relative = "index.html" if request_path in {"", "/"} else urllib.parse.unquote(request_path.lstrip("/"))
        candidate = (DASHBOARD_ROOT / relative).resolve()
        if DASHBOARD_ROOT.resolve() not in candidate.parents and candidate != DASHBOARD_ROOT.resolve():
            self.send_error_json("Invalid static path.", HTTPStatus.FORBIDDEN)
            return
        if not candidate.is_file():
            self.send_error_json("Not found.", HTTPStatus.NOT_FOUND)
            return
        self.send_file(candidate, cache=False)

    def serve_artifact(self, relative_url: str) -> None:
        parts = [urllib.parse.unquote(part) for part in relative_url.split("/") if part]
        if len(parts) < 2:
            self.send_error_json("Invalid artifact path.", HTTPStatus.NOT_FOUND)
            return
        run_dir = safe_run_dir(parts[0])
        candidate = run_dir.joinpath(*parts[1:]).resolve()
        if run_dir.resolve() not in candidate.parents or not candidate.is_file():
            self.send_error_json("Artifact not found.", HTTPStatus.NOT_FOUND)
            return
        self.send_file(candidate, cache=False)

    def send_file(self, path: Path, cache: bool) -> None:
        data = path.read_bytes()
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "public, max-age=3600" if cache else "no-store")
        self.end_headers()
        self.wfile.write(data)


def main() -> None:
    parser = argparse.ArgumentParser(description="Launch the local NeuroEvolution dashboard.")
    parser.add_argument("--host", default="127.0.0.1", help="Interface to bind (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8765, help="Port to bind (default: 8765)")
    parser.add_argument("--no-open", action="store_true", help="Do not open the dashboard in a browser")
    args = parser.parse_args()

    schema()
    server = ThreadingHTTPServer((args.host, args.port), DashboardHandler)
    url = f"http://{args.host}:{server.server_port}/"
    print(f"NeuroEvolution dashboard: {url}")
    print("Press Ctrl+C to stop the dashboard.")
    if not args.no_open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping dashboard…")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
