# Simulation performance

## Parallel controllers and spatial filtering (22 September 2026)

The executable defaults to up to eight OpenMP workers once the population reaches
32. `--threads N` / PowerShell `-Threads N` selects the count; 1 keeps controllers
serial and 0 selects automatic execution. `RunConfig::worker_threads` controls the
invocation default. C++ callers set `world.worker_threads` (library default: 1).
This is an execution setting, so existing checkpoints remain compatible and a
resumed run can use a different worker count.

Each worker computes a creature's observation and all its neural substeps. Its
brain, neural RNG and spike counters are private to that creature. The world
geometry and previous actions remain unchanged until all controllers finish.
Actions occupy fixed creature slots, and movement, shared resource consumption,
damage accumulation, births, mutation and statistics retain their original order.
Worker exceptions are captured and rethrown on the calling thread. OpenMP reuses
its workers across steps instead of creating operating-system threads per creature.

An immutable index sorted by x coordinate narrows overlap validation, sensing,
swept collision and attack searches. Queries filter the y coordinate too and
return candidates in original vector order. The existing distance, visibility,
tie-breaking and swept-path predicates still make the final decisions. Collision
queries include both creatures' maximum possible movement and remain conservative
when a previously proposed path stops. The index is rebuilt after movement before
attack queries. `--spatial-index 0` / `world.spatial_index = false` preserves an
all-pairs reference path. Dense local clusters can still require quadratic work;
this does not claim a better worst-case bound. Resource searches and birth
placement remain potential follow-up optimizations.

Measurements on the local Ryzen 9 7900X (12 cores / 24 logical processors), MSVC
Release, are medians of three sequential runs (300 steps for populations 48/300,
100 steps for population 600). Speedups compare with
an unchanged pre-optimization executable. The 300-creature case resumes
`runs/ecosystem_20260921_235716420/checkpoint.eco`; the 48-creature case uses fresh
filtered-LIF ancestors in a generated habitat. The 600-creature case uses that same
generated setup with a raised population cap and ends with 599 survivors; the other
cases retain their population. Timings measure `world.step()` and exclude recording, loading,
checkpoint writing and HTML generation, which limit end-to-end gains.

| Case | Previous | Index, 1 worker | 2 workers | 4 workers | 8 workers | 12 workers |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Evolved population 300 | 9.601 s | 8.457 s (1.14x) | 4.560 s (2.11x) | 2.552 s (3.76x) | 1.708 s (5.62x) | 1.457 s (6.59x) |
| Fresh population 48 | 0.812 s | 0.794 s (1.02x) | 0.434 s (1.87x) | 0.250 s (3.24x) | 0.197 s (4.12x) | 0.159 s (5.11x) |
| Fresh population 600 | 5.776 s | 4.129 s (1.40x) | 2.178 s (2.65x) | 1.190 s (4.85x) | 0.720 s (8.02x) | 0.593 s (9.74x) |

All seven configurations (including new serial/all-pairs) in every repetition
produce byte-identical initial/final checkpoints, detailed brain and sensory
recordings, statistics and event logs. Comparisons are within one compiler/runtime;
this does not remove the existing MSVC/GCC RNG serialization distinction. The
dedicated `ecosystem_parallel` test additionally covers all three neuron models,
random/reactive controllers, births/deaths, weather, resume, crowded path rejection,
range boundaries and worker exceptions.

Reproduce the measurements after preserving a baseline executable:

```powershell
python tools/benchmark_parallel.py --baseline build/parallel-baseline/neuroevo_ecosystem.exe --candidate build/Release/neuroevo_ecosystem.exe --resume runs/ecosystem_20260921_235716420/checkpoint.eco --out build/parallel-comparison
```

Omit `--resume` and set `--creatures N` for a fresh generated habitat. The tool
alternates execution order and fails on any output mismatch; `report.json` records
all timings, flags and executable/output hashes. Local measurement artifacts are
in `build/parallel-benchmark-300`, `build/parallel-benchmark-48` and
`build/parallel-benchmark-600`.

## Earlier scalar optimizations

The September 2026 optimization pass removes repeated work in sensing and neural
control. It retains the integration equations, timesteps, floating-point
precision, RNG draw order, and checkpoint format.

- Obstacle sensing calculates each sector's ray directions once per observation,
  and each rectangle's closest distance/bearing once per rectangle. Low and high
  sector endpoints retain their original expressions: sharing a rounded boundary
  between adjacent sectors could change an edge case.
- Objects beyond sensory/contact range are rejected before calculating bearings.
- The controller reuses one motor-output buffer across neural substeps and
  updates its observation buffer directly instead of copying it.
- Motor decay and spike increments are calculated when brain runtime state is
  rebuilt. Configuration is immutable through the brain's public API; loading
  checkpoints and mutation rebuild these derived values as well.

## Reproduce the comparison

Measured on Windows with GCC 15.1.0, CMake Release (`-O3 -DNDEBUG`), on
2026-09-15. Values are median simulation seconds across three 3,000-step runs
using the working tree's defaults and seed 42, with the overrides named below.
Speedups depend on the habitat, population, and hardware.

| Scenario | Before | After | Speedup |
| --- | ---: | ---: | ---: |
| Default LIF, 24 founders | 4.942 s | 2.239 s | 2.21x |
| Filtered LIF, 5 ms | 4.180 s | 2.012 s | 2.08x |
| Filtered LIF, 2 ms | 4.703 s | 2.354 s | 2.00x |
| Izhikevich, 1 ms | 7.254 s | 3.791 s | 1.91x |
| LIF, 100 founders | 21.971 s | 11.027 s | 1.99x |
| Generated habitat, legacy IO, random brains, no predation | 2.254 s | 0.703 s | 3.21x |

All 18 comparison pairs matched exactly for all five output files listed below.

Preserve the release executable **before** editing or rebuilding, then compare it
with the updated release executable:

```powershell
python tools/benchmark_ecosystem.py --baseline build/optimization-baseline/neuroevo_ecosystem.exe --candidate build/neuroevo_ecosystem.exe --out build/performance-comparison
```

The output directory must be new. The default suite uses seed 42, 3,000 world
steps, and three pairs per scenario, alternating execution order. Processes run
sequentially to avoid competing with each other. Avoid other CPU-intensive work
while benchmarking. `--scenario`, `--steps`, `--repeats`, and `--seed` customize
the comparison.

Timing uses accumulated `world.step()` time from `performance.csv`, excluding
initialization and replay/checkpoint writing. Runs record neural and sensory
states every 100 steps. Every pair must have byte-identical initial and final
checkpoints, replay frames, statistics, and events; mismatches fail the benchmark.
`report.json` retains timings and SHA-256 hashes of executables and outputs.
Speed is reported rather than used as a flaky test threshold.

The sensing tests also compare 512 poses across four fields of view against the
original scalar obstacle geometry with exact equality. Neural tests exercise
reused output buffers across all three models, supported calibrated/legacy encoding, and
changing output sizes, including zero outputs.
