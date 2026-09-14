# Simulation performance

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
