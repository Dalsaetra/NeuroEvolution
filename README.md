# NeuroEvolution

A C++17 ecosystem in which creatures survive, feed, reproduce, and inherit mutated spiking brains and body traits. The nursery and frontier habitat is the default foundation for development. Evolution happens through reproduction in the shared world.

## Build and run

Requires CMake 3.24+, a C++17 compiler, and Python 3.11+ for replay generation. Python tools use only the standard library. Node.js is optional and enables the viewer's JavaScript test.

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

```powershell
.\scripts\ecosystem.ps1 -Resume runs/nursery/checkpoint.eco -Steps 2400
.\scripts\ecosystem.ps1 -StartingGenomes runs/nursery -Creatures 24 -Seed 42
.\scripts\rebuild-ecosystem.ps1 runs/nursery -Open
```

Resume restores saved biological settings, neural state, food, and RNG streams. Editing defaults affects new worlds; it does not rewrite a checkpoint. `-StartingGenomes` samples distinct surviving genome IDs uniformly for a fresh population, resetting lifetime state. It imports founders once at startup.

Checkpoint format **22** contains consolidated settings and natural lineage state. Formats 1–21 require the previous build for continuation or founder import. Existing JSONL/gzip replays remain viewable. Fitness runners, NEAT/NSGA-II, search/dashboard tooling, archive selection, newborn scoring trials, and immigration have been removed.

## Tests and implementation notes

`ctest --test-dir build --output-on-failure` runs retained mechanics, sensing, predation, ancestry, mutation, food, checkpoint, CLI, and replay tests. Controlled fixtures specify their own conditions so routine tuning does not redefine mechanical expectations.

- [Nursery and frontier](docs/nursery-frontier.md)
- [Mutation and inheritance](docs/stable-mutations.md)
- [Sensory and motor interface](docs/sensorimotor-calibration.md)
- [Predation and energy accounting](docs/predation.md)
- [Moving nursery food](docs/moving-nursery-food.md)
- [Outdoor food](docs/dynamic-food.md)
- [Frontier layout](docs/frontier-layout.md)

[ROADMAP.md](ROADMAP.md) describes the development direction; [EXPLORATION.md](EXPLORATION.md) contains ongoing experiment notes.
