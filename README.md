# NeuroEvolution

A C++17 ecosystem in which creatures survive, feed, reproduce, and inherit mutated spiking brains and body traits. The nursery and frontier habitat is the default foundation for development. Evolution happens through reproduction in the shared world.

## Build and run

Requires CMake 3.24+, a C++17 compiler, and Python 3.11+ for replay generation. Replay tools use only the standard library; optional experiment plots require NumPy and Matplotlib. Node.js is optional and enables the viewer's JavaScript test.

```powershell
.\scripts\build.ps1
.\scripts\ecosystem.ps1 -Build -Open
```

The default world has an 80-by-80 frontier, a protected central nursery, 24 sparse ancestral founders, predation, moving food, and weather. The ancestor has five hidden neurons, 83 local inputs, and six motor outputs. Its attack output starts disconnected. Descendants inherit through natural births; an extinct population stays extinct.

For a longer run with a compact history and a detailed final window:

```powershell
.\scripts\ecosystem.ps1 -Steps 60000 -Recording compact -DetailedTailSeconds 300
```

Or run the executable directly:

```powershell
.\build\neuroevo_ecosystem.exe --steps 4800 --out runs/nursery
python tools/view_ecosystem.py runs/nursery
```

## Tune in code

Edit **[include/neuroevo/config.hpp](include/neuroevo/config.hpp)** and rebuild. Default construction, the executable, and the PowerShell launcher use the same settings. There are no habitat preset overrides or launcher-level biological defaults.

The default neuron model is LIF. Select the experimental Izhikevich alternative
with `--neuron-model izhikevich`, or `-NeuronModel izhikevich` in the PowerShell
launcher. It uses regular-spiking neurons and a 1 ms neural timestep. In C++, use
`config.brain.select_model(NeuronModel::Izhikevich)` before constructing a world.
Intrinsic parameter mutation is disabled by default. See the
[implementation and experiments](docs/izhikevich-neurons.md) for timing, mutation,
performance, and winner-take-all results.

Filtered LIF is selectable with `--neuron-model filtered-lif` (5 ms), plus
`--brain-dt 0.002` for 2 ms. In PowerShell use `-NeuronModel filtered-lif`
and optionally `-BrainDt 0.002`. See [filtered LIF](docs/filtered-lif.md)
for configuration, saved state, and validation. The
[cheaper neuron experiments](docs/cheaper-neuron-models.md) compare its behavior
with adaptive LIF and continuous rate units; those latter models remain research prototypes.

| Configuration | What to tune |
| --- | --- |
| `EcosystemConfig` | Nursery and frontier layout, resources, weather, energy costs, reproduction, body/combat rules, founder type |
| `EcosystemConfig::brain` (`BrainConfig`) | Neural timing, gains, background activity, sensory and motor rates; random-founder topology |
| `EcosystemConfig::mutation` (`MutationConfig`) | All inherited brain and body mutation settings, copy/slight/strong mix, edit budgets, and disconnected-neuron cleanup |
| `RunConfig` | Run length, recording interval/detail, and detailed-tail recording |

Within `MutationConfig`, `copy_probability` and `slight_probability` define the birth mixture; strong mutations use the remaining probability. The `slight` and `strong` profiles explicitly name sigma scales, operator scales, structural budgets, local edit limits, and body scales. Body mutation probabilities and sigmas live beside the neural controls.

The current mixture is 50% copies, 45% slight mutations, and 5% strong mutations. Each birth independently has a 25% chance to prune one disconnected hidden neuron, including copy births. To freeze inheritance, set `copy_probability = 1`, `slight_probability = 0`, and `disconnected_neuron_prune_probability = 0`.

`brain.hidden_count` controls random founders. The sparse ancestor's circuit and initial inherited neuron values are defined in [src/ecosystem_ancestor.cpp](src/ecosystem_ancestor.cpp). Set `sparse_ancestor = false` for random founder brains. Use `set_predation(false)` for per-experiment configurations so input/output dimensions follow body rules.

Optional CLI overrides remain available through `--help`. The launcher passes biological settings only when explicitly requested; omitted options use compiled defaults. Its `-Recording` presets are explicit recording conveniences. Generated maps and the single-ancestor nursery remain available for controlled experiments via `--habitat`.

## Replay and continuation

Runs save `ecosystem.jsonl`, `ecosystem_stats.csv`, `events.csv`, `summary.json`, `performance.csv`, and initial/final `.eco` checkpoints. Detailed tails add `ecosystem_tail.jsonl`. The viewer displays terrain, food, creatures, ancestry, diets, body traits, sensory values, and neural activity where recorded.

Main recording defaults are compact: one frame every 500 world steps, without
brain states, graphs, sensory arrays, or routine feeding events. A detailed tail
is enabled by default: the final 600 simulation seconds, recorded every 10 world
steps (normally one second), with brain states/graphs and sensory arrays. Neural
details stay in the tail even if old main-detail flags are supplied.
`-Recording detailed` explicitly selects this same compact-history/detailed-tail
combination; it is no longer required. `-Recording standard` records more frequent
main-history frames without neural diagnostics. Use `-DetailedTailSeconds 0` to
disable the tail. All recording options apply afresh on resume and genome import.

The HTML generator makes `ecosystem.html` an overview with at most 500 frames,
including the first and final frames. It omits neural/sensory arrays from old
recordings too, retains milestone events and the full statistics CSV, and leaves
the original recording intact. `ecosystem_tail.html` retains recorded detail and
every recorded tail frame, including brain dynamics and observations. Tail HTML
uses lossless gzip compression and stores repeated resource properties once;
the browser reconstructs resources for the displayed frame. Open it directly in
a current Edge, Chrome, or Firefox browser. No server or internet is required.
Only an explicit `--max-frames N` samples a tail. The main overview retains its
64 MiB frame budget. Outputs are replaced only after successful generation.

The detailed tail is recorded automatically. `-TailRecordEvery 1` records every world step (0.1 s by default);
the default interval is 10 steps (1 s). These are snapshots of the neural state
at each recorded world step, not every internal neural substep. A shorter tail
duration reduces size without changing its frame interval or brain detail.
The launcher also gzip-compresses the source recording unless `-KeepJsonl` is set.

```powershell
.\scripts\ecosystem.ps1 -Resume runs/nursery/checkpoint.eco -Steps 2400
.\scripts\ecosystem.ps1 -StartingGenomes runs/nursery -Creatures 24 -Seed 42
.\scripts\rebuild-ecosystem.ps1 runs/nursery -Open
```

After changing constants, use `.\scripts\ecosystem.ps1 -Build` to compile and
start a new simulation. `rebuild-ecosystem.ps1` regenerates HTML from an existing
recording; it does not compile or rerun the simulation. The launcher selects the
Release executable for Visual Studio builds, ignoring old binaries from other
build layouts.

Historical GCC and MSVC checkpoints use different random-generator formats.
When resuming or importing starting genomes from a GCC checkpoint in an MSVC setup, the launcher automatically
builds/uses `build/resume-gnu` with the installed `g++` and Ninja. This keeps the
original runtime's random sequence and distributions instead of converting the
saved state. Direct executable invocations require the matching runtime.

Resume restores saved biological settings, neural state, food, and RNG streams. Editing defaults affects new worlds; it does not rewrite a checkpoint. `-StartingGenomes` samples distinct surviving genome IDs uniformly for a fresh population, resetting lifetime state. It imports founders once at startup.

Checkpoint format **24** stores the neuron model, intrinsic parameters, recovery state, and filtered synaptic currents. Formats 22 and 23 remain readable; formats 1–21 require the previous build for continuation or founder import. Existing JSONL/gzip replays remain viewable. Fitness runners, NEAT/NSGA-II, search/dashboard tooling, archive selection, newborn scoring trials, and immigration have been removed.

## Tests and implementation notes

`ctest --test-dir build --output-on-failure` runs retained mechanics, sensing, predation, ancestry, mutation, food, checkpoint, CLI, and replay tests. Controlled fixtures specify their own conditions so routine tuning does not redefine mechanical expectations.

- [Nursery and frontier](docs/nursery-frontier.md)
- [Mutation and inheritance](docs/stable-mutations.md)
- [Sensory and motor interface](docs/sensorimotor-calibration.md)
- [Predation and energy accounting](docs/predation.md)
- [Moving nursery food](docs/moving-nursery-food.md)
- [Outdoor food](docs/dynamic-food.md)
- [Frontier layout](docs/frontier-layout.md)
- [Performance and exact-output benchmarking](docs/performance.md)

[ROADMAP.md](ROADMAP.md) describes the development direction; [EXPLORATION.md](EXPLORATION.md) contains ongoing experiment notes.
