# NeuroEvolution

A C++17 ecosystem in which creatures survive, feed, reproduce, and inherit mutated spiking brains and body traits. The nursery and frontier habitat is the default foundation for development. Evolution happens through reproduction in the shared world.

## Build and run

Requires CMake 3.24+, a C++17 compiler, and Python 3.11+ for replay generation. Replay tools use only the standard library; optional experiment plots require NumPy and Matplotlib. Node.js is optional and enables the viewer's JavaScript test.

```powershell
.\scripts\build.ps1
.\scripts\ecosystem.ps1 -Build -Open
```

Build scripts show brief progress by default and retain full diagnostics when warnings or failures occur. Use `scripts/build.ps1 -VerboseBuild` or `scripts/ecosystem.ps1 -Build -VerboseBuild` for full build output.

The default world has an 80-by-80 frontier, a protected central nursery, 24 sparse ancestral founders, predation, moving food, and weather. The ancestor template has five hidden neurons, 86 local inputs, and six motor outputs. Its attack output starts disconnected before initial mutation. Descendants inherit through natural births; an extinct population stays extinct.

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

The nursery starts with 16 food patches at 100 energy per biomass unit, and the
population cap is 300. At the end of each step, a nursery population crossing
from 50 or fewer creatures to more than 50 permanently multiplies nursery food
energy by 0.8 (100, 80, 64, ...). Staying above 50 does not trigger additional
reductions; falling back to 50 or below rearms the next crossing. After each
reduction, a 1,000 simulation second cooldown blocks further reductions.
Crossings during that cooldown are ignored, even if the population stays above
50 after it expires; a new upward crossing at or after expiry is required.
The first reduction has no cooldown. The rule affects
existing nursery grazing patches and their later replenishment, leaving meat,
outdoor food, and already ingested digestive packets unchanged. Checkpoints
preserve the current nutrition, crossing state, reduction count, and cooldown
deadline. Checkpoints from before this mechanic keep it disabled when resumed;
version 30 checkpoints retain their original zero-cooldown policy.

Tune `nursery_food_population_threshold` (0 disables) and
`nursery_food_energy_factor` in `EcosystemConfig`, or use the corresponding
`--nursery-food-population-threshold` and `--nursery-food-energy-factor` CLI flags.
Set `nursery_food_reduction_delay` or `--nursery-food-reduction-delay X` to change
the cooldown in simulation seconds (default 1000; 0 removes the cooldown).
Each reduction is recorded as a `nursery_food_energy_reduced` event, and current
nutrition and reduction count appear in the stats, replay, and summary.

Edit **[include/neuroevo/config.hpp](include/neuroevo/config.hpp)** and rebuild. Default construction, the executable, and the PowerShell launcher use the same settings. There are no habitat preset overrides or launcher-level biological defaults.

`shelter_predation_damage` defaults to `true`. Set it to `false` or use
`--shelter-predation-damage 0` to prevent predation damage to creatures in ordinary
shelters. Protection uses the target's position after movement. The nursery
always remains protected. See [predation](docs/predation.md).

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

`MutationConfig::add_autapse_probability` defaults to `0.10` and is a relative choice weight among structural operators, not a per-neuron or per-birth probability. The CLI override is `--mutate-add-autapse-prob`. When selected, it adds one self-connection to a uniformly chosen hidden neuron without an autapse, with an explicit delay uniformly sampled from `1..max_delay_steps`. It uses the weak structural-edge starting strength and the usual excitatory/inhibitory sign distribution. Birth profiles scale this operator like other structural choices; setting it to zero disables creation. Ordinary wiring and random founders still exclude self-connections. Removal and connection repair ignore self-edges when identifying hidden neurons missing incoming or outgoing connections to other neurons, so an isolated autapse receives disconnected-neuron priority.

Ecosystem checkpoint format 26 saves this setting. Loading formats 22–25 sets it to zero to preserve their disabled autapse-creation policy; the updated disconnected-neuron rule applies to all runs.

The current mixture is 50% copies, 45% slight mutations, and 5% strong mutations. Each birth independently has a 25% chance to prune one disconnected hidden neuron, including copy births. To freeze inheritance, set `copy_probability = 1`, `slight_probability = 0`, and `disconnected_neuron_prune_probability = 0`.

`brain.hidden_count` controls random founders. The sparse ancestor's circuit and initial inherited neuron values are defined in [src/ecosystem_ancestor.cpp](src/ecosystem_ancestor.cpp). Set `sparse_ancestor = false` for random founder brains. Use `set_predation(false)` for per-experiment configurations so input/output dimensions follow body rules.

`mutate_initial_ancestors` defaults to `true`. Each fresh sparse ancestor starts
from that template and independently receives one application of the current
strong mutation preset, including brain and body genes. This is one preset pass
per founder, with its configured edit budget and probabilities; it does not
select from the copy/slight/strong mixture or run the separate birth cleanup.
Founder mutations use the simulation seed and creature ID, making runs reproducible
without sharing a single mutated genome across the population. Mutated founders
receive separate genome IDs and start with full health for their resulting mass.
Set `mutate_initial_ancestors = false` or `--mutate-initial-ancestors 0` for the
unmodified ancestors; `--mutate-initial-ancestors 1` enables variation. This applies
to fresh ancestors in both map layouts and the solo ancestor nursery. Random
founders and imported genomes retain their existing initialization. Version 33
checkpoints preserve the toggle and exact genomes; resuming never mutates founders
again, and older checkpoints load with the toggle disabled.

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
