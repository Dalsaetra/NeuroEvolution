"""Stream an existing ecosystem replay into a much smaller diagnostic history."""
from __future__ import annotations

import argparse
import gzip
import json
from pathlib import Path


ROUTINE_EVENTS = {"ingestion", "digestion", "pod_work"}


def compact(record: dict, keep_observations: bool, keep_graphs: bool) -> dict:
    if record.get("type") == "metadata":
        if not keep_graphs:
            record["brains"] = []
        record["compacted"] = True
        return record
    for creature in record.get("creatures", []):
        if not keep_observations:
            creature.pop("observation", None)
        if not keep_graphs:
            creature.pop("brain", None)
        elif isinstance(creature.get("brain"), dict):
            creature["brain"].pop("potentials", None)
            creature["brain"].pop("spiked", None)
    record["events"] = [event for event in record.get("events", []) if event.get("type") not in ROUTINE_EVENTS]
    return record


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Run directory, ecosystem.jsonl, or ecosystem.jsonl.gz")
    parser.add_argument("--output", type=Path, help="Default: RUN_DIR/ecosystem_compact.jsonl.gz")
    parser.add_argument("--every", type=int, default=50, help="Keep every Nth recorded frame, plus first/final (default 50)")
    parser.add_argument("--keep-observations", action="store_true")
    parser.add_argument("--keep-brain-graphs", action="store_true")
    args = parser.parse_args()
    if args.every < 1:
        parser.error("--every must be positive")
    if args.source.is_dir():
        source = args.source / "ecosystem.jsonl"
        if not source.exists():
            source = args.source / "ecosystem.jsonl.gz"
        output = args.output or args.source / "ecosystem_compact.jsonl.gz"
    else:
        source = args.source
        output = args.output or source.parent / "ecosystem_compact.jsonl.gz"
    if not source.is_file():
        parser.error(f"source not found: {source}")
    if output.exists() or output.resolve() == source.resolve():
        parser.error(f"choose a fresh output path: {output}")
    incoming = gzip.open(source, "rt", encoding="utf-8-sig") if source.suffix == ".gz" else source.open(encoding="utf-8-sig")
    output.parent.mkdir(parents=True, exist_ok=True)
    frames_read = frames_written = 0
    pending_final: dict | None = None
    with incoming, gzip.open(output, "wt", encoding="utf-8", compresslevel=6, newline="\n") as outgoing:
        for line_number, line in enumerate(incoming, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                parser.error(f"{source}, line {line_number}: {error}")
            kind = record.get("type")
            if kind == "metadata":
                outgoing.write(json.dumps(compact(record,args.keep_observations,args.keep_brain_graphs),separators=(",",":"))+"\n")
            elif kind == "frame":
                record = compact(record,args.keep_observations,args.keep_brain_graphs)
                if frames_read % args.every == 0:
                    outgoing.write(json.dumps(record,separators=(",",":"))+"\n")
                    frames_written += 1
                    pending_final = None
                else:
                    pending_final = record
                frames_read += 1
            else:
                parser.error(f"{source}, line {line_number}: unknown record type")
        if pending_final is not None:
            outgoing.write(json.dumps(pending_final,separators=(",",":"))+"\n")
            frames_written += 1
    if frames_read == 0:
        output.unlink(missing_ok=True)
        parser.error("source contains no frames")
    print(f"Compacted {frames_read} frames to {frames_written}: {output.resolve()}")
    print(f"Size: {output.stat().st_size / (1024*1024):.2f} MiB")


if __name__ == "__main__":
    main()
