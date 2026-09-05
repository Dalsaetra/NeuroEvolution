# NeuroEvolution

Prototype evolutionary simulator for creatures with spiking neural-network brains.

## Current Prototype

- C++17 simulation runtime built with CMake.
- Modular 2D tasks and independently selectable fitness regimes.
- Leaky integrate-and-fire neurons with spatially embedded synapses and distance-based delays.
- Evolution loop with elitism, tournament selection, neuron/synapse mutation, and simple reciprocal recurrent-motif mutation.
- CSV outputs for generation statistics and the best-run trajectory.
- Python plotting script for fitness and trajectory summaries.
- Interactive HTML viewer for replaying a creature trajectory, brain activity, and synapse firing.
- Shared ecosystem with continuous movement, local senses, food competition, cooperative pods, storms, and energy-funded reproduction.

## Build

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Or:

```powershell
.\scripts\build.ps1
```

## Shared Ecosystem

New runs use calibrated sensory spike rates, smoothed motor output, 97 local inputs including depleted-food and shelter cues, and repeated newborn trials for archive selection. See [interface calibration and newborn evaluation](docs/sensorimotor-calibration.md) for controls, scoring, and checkpoint compatibility.

Run the first ecosystem with independent spiking brains:

```powershell
.\scripts\ecosystem.ps1 -Build -Creatures 24 -Steps 4800 -Open
```

Validate the sparse ancestral spiking brain with one founder and exact inheritance:

```powershell
.\scripts\ecosystem.ps1 -Build -SoloAncestorTrial -Steps 4800 -Recording detailed -Open
```

This controlled nursery uses the normal energy and reproduction rules. It removes storms and supplies dense rich fruit so the test isolates neural feeding and lineage continuity. The founder has 7 hidden neurons, 41 synapses, and connections from 25 of the 97 inputs. The automated test verifies that a child matures and produces a grandchild.

New ecosystems use [stable mutations](docs/stable-mutations.md): each ordinary mutation makes one structural change or up to two local parameter edits, with weak new pathways. Use `-MutationMode stable` to explicitly switch a resumed run; otherwise checkpoints retain their saved mutation policy.

For ecological population support without immigration, use `-Habitat nursery-frontier`:
an 80×80 world with a storm-protected central nursery, finite food, richer frontier
resources, and smaller outer shelters. See [nursery and frontier](docs/nursery-frontier.md)
for the launch command and tuning controls.

Nursery food now [relocates on depletion](docs/moving-nursery-food.md). Configure
it with `-NurseryFoodPatches` and `-NurseryFoodEnergy`; existing checkpoints keep
their saved food policy.

Add `-NoStorms` to any PowerShell ecosystem run to hold the weather in calm conditions. The selected sensory interface is unchanged and the storm-cue sensor remains present at zero.

To establish a population with archive-based immigration (40% archive clones, 40% slight archive mutations, 20% strong archive mutations):

```powershell
.\scripts\ecosystem.ps1 -Build -Establishment -FounderBrain sparse-ancestor -Creatures 24 -Steps 12000 -Open
```

Repeated, efficient feeding or reproduction seeds the archive. Limited immigration keeps exploration running through population crashes, with separate birth/immigration accounting and automatic withdrawal after sustained descendant breeding. Every immigrant now descends from archived evidence; support waits when the archive is empty. Archive parents are chosen by a three-entry tournament within a uniformly selected food niche. Natural births use 25% exact inheritance, 50% slight mutation, and 25% strong mutation. Baseline children start with 50 energy; the breeding preset gives them 60. Checkpoints preserve both the world and archive.

For longer evolution runs, the runner now defaults to a compact history and saves full neural/sensory detail only for a bounded final window. A multi-seed sweep and a less brittle experimental reproduction preset are available:

```powershell
python tools/sweep_ecosystem.py --steps 20000 --seeds 7,11,19
.\scripts\ecosystem.ps1 -Establishment -FounderBrain sparse-ancestor -EvolutionPreset breeding -Steps 120000 -Recording compact
```

See [evolution tuning and recording](docs/evolution-tuning.md) for the measured bottleneck in the 50,000-second run, parameter rationale, success criteria, recording profiles, and an existing-replay compactor.

The first command runs 480 simulated seconds; the establishment example runs 1,200 seconds. Both save a fresh timestamped directory under `runs/`. Change `-Creatures` for solitary or group experiments; use `-NoReproduction` to keep births disabled, or `-Controller reactive` for a scripted comparison. The replay includes terrain, depleted food, cooperative pod opening, weather, individual diets, sensory inputs, neural activity, and population histories.

The ecosystem has its own `neuroevo_ecosystem` executable and offline replay. It runs independently of the existing generation-based experiment dashboard. See [the environment guide](docs/environment-v1.md) for mixed populations, food-learning comparisons, continuation from checkpoints, parameters, and current limitations.

Default food energy densities are 2.5 for grazing, 5 for poor fruit, 12.5 for rich fruit, and 15 for cooperative pods. The PowerShell runner exposes these as `-GrazeEnergy`, `-PoorFruitEnergy`, `-RichFruitEnergy`, and `-PodEnergy` for controlled nutrition sweeps.

## Local Dashboard

Launch the experiment dashboard with:

```powershell
.\scripts\dashboard.ps1
```

The dashboard opens at `http://127.0.0.1:8765` and provides:

- schema-driven controls for every simulator command-line parameter;
- saved browser-local parameter presets and workload estimates;
- buttons to configure/build/test the C++ simulator, start a run, or stop the active process;
- live generation progress, fitness curves, task metrics, topology summaries, and process logs;
- a recent-run archive and embedded creature/brain episode replays;
- Pareto-front visualization for completed NEAT + NSGA-II runs.

You do not need to build first: use **Build & test** in the dashboard. If you prefer the terminal, run `scripts/build.ps1` before opening it. Generated controls are defined in `tools/dashboard/parameters.json`; adding a parameter descriptor there makes it appear in the GUI without editing the dashboard frontend. A test checks that the schema and simulator CLI stay in sync.

Use a different port or prevent automatic browser opening with:

```powershell
.\scripts\dashboard.ps1 -Port 9000 -NoOpen
```

## Run A Simulation

```powershell
.\build\neuroevo_sim.exe --generations 80 --population 96 --steps 600 --trials 3 --record-trials 16 --sensorimotor directional-fov --seed 7 --out runs/latest
```

This writes:

- `runs/latest/metadata.csv`
- `runs/latest/stats.csv`
- `runs/latest/best_trajectory.csv`
- `runs/latest/brain_activity.csv`
- `runs/latest/brain_synapses.csv`
- `runs/latest/synapse_events.csv`

The default `--ea-mode scalar` preserves the original scalar-fitness evolutionary loop. The NEAT + NSGA-II path is available with:

```powershell
.\build\neuroevo_sim.exe --ea-mode neat-nsga2 --generations 80 --population 96 --steps 600 --trials 3 --record-trials 16 --sensorimotor directional-fov --seed 7 --out runs/neat_latest
```

Useful NEAT/NSGA-II controls include `--compat-threshold`, `--species-c1`, `--species-c2`, `--species-c3`, `--mutate-weight-prob`, `--mutate-add-node-prob`, `--mutate-add-conn-prob`, `--mutate-enable-disable-prob`, `--interspecies-mate-prob`, `--target-foods`, `--target-spike-rate`, `--synapse-budget`, `--neuron-budget`, and `--objective-set basic|extended`.

In `neat-nsga2` mode, selection uses normalized objectives instead of raw weighted scalar fitness: task score is maximized, while spike energy above the target rate, enabled synapses above budget, and time cost are minimized. Species assignment uses NEAT compatibility distance, and survival selection is global NSGA-II over parents plus offspring.

NEAT/NSGA-II runs also record representative final Pareto-front solutions by default:

- `runs/neat_latest/pareto_front.csv`: manifest with objective values, species, topology counts, and replay metrics.
- `runs/neat_latest/pareto_front/solution_XX/`: one replay directory per selected front solution, using the same CSV names as the normal best run.

Control this with `--record-pareto-front N` and `--record-pareto-trials N`.

## Plot Results

```powershell
uv run python tools/plot_run.py runs/latest
```

This writes:

- `runs/latest/fitness.png`
- `runs/latest/objectives.png` when NEAT/NSGA-II objective columns are present
- `runs/latest/pareto_front.png` when `pareto_front.csv` is present
- `runs/latest/trajectory.png`

Generate a side-by-side Pareto-front replay dashboard with:

```powershell
uv run python tools/view_pareto_front.py runs/neat_latest
```

This writes `runs/neat_latest/pareto_front.html`.

## Interactive Viewer

```powershell
uv run python tools/view_run.py runs/latest
```

Open `runs/latest/viewer.html` in a browser to replay the best recorded trajectory, live neuron activations, and fired synapses with playback controls.

## Sensorimotor Regimes

The default regime is `directional-fov`.

- `directional-fov`: directional creature with a facing angle, 120 degree food visibility arc, 4 sensory inputs (`food_visible`, `food_left`, `food_right`, `food_distance`) and 3 motor outputs (`walk_speed`, `turn_left`, `turn_right`).
- `target-vector`: older direct relative-target regime with 5 sensory inputs (`target_right`, `target_left`, `target_up`, `target_down`, `target_distance`) and 4 motor outputs (`move_left`, `move_right`, `move_down`, `move_up`).

Motor/output neurons have no bias, and motor commands are decoded from output spike traces only. Hidden-neuron bias is evolvable but is clamped so the isolated steady-state membrane potential remains below a configurable fraction of threshold (`--max-bias-fraction`, default `0.95`). Bias can therefore change excitability without creating a free-running neuron by itself.

The default autonomous-activity mechanisms are an episode-start pulse (`--episode-start-input`, `--episode-start-pulse-steps`) and low-rate Poisson background-current events (`--background-activity`, `--background-rate`, `--background-current`). Every neuron has its own inherited and mutable non-negative background sensitivity. The tonic clock remains available as a legacy experimental control through `--clock-input 1`, but is disabled by default.

Scalar and NEAT evolution can add a two-hidden-neuron reciprocal connection motif in one mutation. Scalar ecosystem brains can also grow one hidden neuron as a side branch of an inherited connection. Control these with `--mutate-reciprocal-motif-prob` and `--mutate-add-neuron-prob`. Delivered synaptic spike current is scaled by `--synaptic-gain`. Initial genomes retain their evolvable direct sensory-to-motor scaffold; auxiliary start/clock inputs are excluded from forced I/O repair and direct motor seeding.

NEAT genomes are repaired after creation and mutation so every ordinary sensory input neuron has at least one enabled outgoing synapse and every output neuron has at least one enabled incoming synapse. Auxiliary inputs are excluded from this repair, so they become useful through evolved wiring rather than hard-coded motor drive.

## Task and Fitness Regimes

Tasks and external fitness are separate strategy interfaces. Select them independently with `--task` and `--fitness`.

- `--task food-seeking`: the original continuously observable target task.
- `--task cue-occlusion`: shows each target for `--cue-steps`, hides it for a duration sampled between `--occlusion-min-steps` and `--occlusion-max-steps`, then reveals it again. An occluded target also reappears inside `--reveal-distance`.
- `--fitness shaped`: food reward plus progress, closest-approach, visibility, and final-distance shaping.
- `--fitness sparse`: food collection reward only; generic spike, motion, and structural penalties remain separate.

For example:

```powershell
.\build\neuroevo_sim.exe --ea-mode neat-nsga2 --task cue-occlusion --fitness shaped --initial-hidden 2 --cue-steps 40 --occlusion-min-steps 80 --occlusion-max-steps 160 --episode-start-input 1 --background-activity 1 --clock-input 0 --out runs/cue_occlusion
```

Trajectory output records both geometric target visibility and task-controlled sensory availability, along with the current `cue`, `occluded`, or `revealed` phase. Brain activity output includes each neuron's evolved background sensitivity.

The directional regime rewards food collection most strongly. It also adds a one-time shaping reward whenever the creature reaches a new closest distance to the current food, configurable with `--distance-reward`. Initial target bearing is sampled within the visible FOV but over a wide range controlled by `--initial-heading-fov-frac`, so steering is useful without making the first target invisible. The visibility-alignment reward is small, speed-gated, and configurable with `--visibility-reward`, so a stationary creature is not rewarded for merely looking at food. Turning, stillness, spikes, and structural complexity use budget-excess penalties: costs apply only after `--turn-budget`, `--inactivity-budget`, `--spike-budget-rate`, `--structural-synapse-budget`, or `--structural-neuron-budget` are exceeded.

Evolution ranks genomes by average foods collected first and fitness second. Fitness still controls shaping among genomes with the same food count.

Switch regimes with:

```powershell
.\build\neuroevo_sim.exe --sensorimotor target-vector --out runs/target_vector
```

Tune directional vision with:

```powershell
.\build\neuroevo_sim.exe --sensorimotor directional-fov --episode-start-input 1 --episode-start-pulse-steps 2 --background-activity 1 --background-rate 2 --background-current 25 --clock-input 0 --fov-degrees 120 --initial-heading-fov-frac 0.9 --turn-rate 3.14159 --turn-penalty 0.0001 --turn-budget 0.25 --inactivity-penalty 0.0005 --inactivity-budget 0.5 --synaptic-gain 8 --seed-io-weight 3 --distance-reward 8 --visibility-reward 0.001 --out runs/fov_test
```

## Combined Visualization

```powershell
.\scripts\visualize.ps1 -RunDir runs\latest
```

This runs the plotter and writes `viewer.html` for the best recorded run. If `pareto_front.csv` exists, it also writes `pareto_front.html` and per-solution viewers under `pareto_front/solution_XX/viewer.html`.

Use `-Open` to open the generated best-run viewer and Pareto dashboard. Use `-SkipParetoSolutionViewers` to skip generating detailed viewers for each saved Pareto solution.

## Hyperparameter Searches

Use `tools/hypersearch.py` to run grid, random, or TPE-style Bayesian searches over any `neuroevo_sim.exe` CLI parameter. The example search focuses on the current food-seeking fitness and motor/synapse tuning parameters.

Quick grid check:

```powershell
.\scripts\search.ps1 -Config experiments/fitness_search.example.json -Mode grid -MaxTrials 12 -Jobs 2 -Build
```

Bayesian-style search:

```powershell
.\scripts\search.ps1 -Config experiments/fitness_search.example.json -Mode bayes -MaxTrials 40 -VisualizeTop 5
```

Regenerate search plots without running new simulations:

```powershell
.\scripts\search.ps1 -Config runs/searches/fitness_search_example -PlotOnly
```

Each search writes:

- `runs/searches/<name>/trials.csv`: one row per simulation run, including parameters, final stats, and trajectory behavior metrics.
- `runs/searches/<name>/parameter_sets.csv`: aggregate results grouped by parameter set.
- `runs/searches/<name>/best_trials.csv`: top individual runs by objective.
- `runs/searches/<name>/objective.png`: objective and best-so-far over trials.
- `runs/searches/<name>/parameter_effects.png`: parameter values versus objective.
- `runs/searches/<name>/behavior.png`: path length, speed, visibility, and recorded food behavior.
- `runs/searches/<name>/trials/trial_*/`: normal run outputs for each simulation, including `stats.csv` and `best_trajectory.csv`.

The objective defaults to `best_foods_collected`, but the JSON config can point at any metric produced in `stats.csv` or derived from `best_trajectory.csv`, such as `recorded_foods_collected`, `best_fitness`, `behavior_path_length`, or `behavior_mean_speed`.

For each parameter, use `grid` to control explicit grid-search values/order, `values` for categorical choices, and `min`/`max` ranges for random and Bayesian-style search.

## Prototype Defaults To Revisit

- Keep the first benchmark as food seeking, or switch quickly to memory/computation challenges.
- Keep 2D as the main environment, or make 3D support an early architecture requirement.
- Continue with fixed neuron counts for a few iterations, or start evolving neuron count and neuron types.
- Decide whether brain topology visualization should be generated from C++ output or directly from Python.

To open the detailed tail replay automatically after a successful simulation and HTML generation, add `-OpenTail`:

```powershell
.\scripts\ecosystem.ps1 -Steps 120000 -OpenTail
```

The tail defaults to the final 300 simulated seconds; use `-DetailedTailSeconds 60` for a shorter tail. `-OpenTail` also enables the default tail with `-Recording detailed` and requires a positive tail duration. `-Open` opens the main replay; passing both switches opens both.

Outdoor food now decays and relocates on depletion; shelters have static, low-quality forage. See [dynamic food settings](docs/dynamic-food.md) for rates and checkpoint compatibility.

New nursery-frontier maps use sparse wall lines and bends, including across shelters. See [frontier layout](docs/frontier-layout.md).
