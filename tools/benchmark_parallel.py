"""Benchmark worker counts and the spatial index against an unchanged executable.

All variants must produce byte-identical checkpoints, events, stats and detailed
recordings. Use --resume to measure an evolved population on its actual habitat.
"""
import argparse
import csv
import json
from pathlib import Path
import statistics
import subprocess

from benchmark_ecosystem import EXACT_FILES, fingerprint


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--resume', type=Path)
    parser.add_argument('--creatures', type=int, default=48)
    parser.add_argument('--steps', type=int, default=300)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if min(args.steps, args.repeats, args.creatures) < 1:
        parser.error('steps, repeats and creatures must be positive')
    args.out.mkdir(parents=True, exist_ok=False)
    variants = {'baseline': (args.baseline, []),
                'serial-all-pairs': (args.candidate, ['--threads', '1', '--spatial-index', '0']),
                **{f'indexed-{n}': (args.candidate, ['--threads', str(n)]) for n in (1, 2, 4, 8, 12)}}
    flags = ['--resume', str(args.resume.resolve())] if args.resume else [
        '--creatures', str(args.creatures), '--max-population', str(max(300, args.creatures)),
        '--habitat', 'generated', '--food-distribution', 'scattered',
        '--neuron-model', 'filtered-lif']
    samples = {name: [] for name in variants}
    expected = None
    report = {'steps': args.steps, 'repeats': args.repeats, 'flags': flags,
              'executables': {name: {'path': str(path.resolve()), 'sha256': fingerprint(path)}
                              for name, path in [('baseline', args.baseline), ('candidate', args.candidate)]}}
    for repeat in range(args.repeats):
        order = list(variants)
        if repeat % 2:
            order.reverse()
        for name in order:
            exe, extra = variants[name]
            destination = args.out / f'{repeat}-{name}'
            command = [str(exe.resolve()), '--steps', str(args.steps), '--out', str(destination.resolve()),
                       '--detailed-tail-seconds', '0', '--record-every', '100',
                       '--record-brains', '1', '--record-observations', '1',
                       '--record-brain-graphs', '1', '--record-routine-events', '1'] + flags + extra
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(f'{command}\n{result.stdout}\n{result.stderr}')
            with (destination / 'performance.csv').open(newline='') as source:
                timing = list(csv.DictReader(source))[-1]
            seconds = float(timing['step_wall_seconds'])
            samples[name].append(seconds)
            actual = {file: fingerprint(destination / file) for file in EXACT_FILES}
            if expected is None:
                expected = actual
            if expected != actual:
                raise RuntimeError(f'Output mismatch: {name}, repetition {repeat}: '
                                   f'{[file for file in EXACT_FILES if actual[file] != expected[file]]}')
            print(f'{repeat + 1}: {name}: {seconds:.4f}s, population {timing["population"]}, exact outputs', flush=True)
        baseline = statistics.median(samples['baseline'])
        report['variants'] = {name: {'seconds': values, 'median_seconds': statistics.median(values),
                                    'speedup': baseline / statistics.median(values)} for name, values in samples.items()}
        report['exact_sha256'] = expected
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report['variants'], indent=2))


if __name__ == '__main__':
    main()
