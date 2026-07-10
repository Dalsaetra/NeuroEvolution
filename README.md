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

Scalar and NEAT evolution can add a two-hidden-neuron reciprocal connection motif in one mutation. Control its probability with `--mutate-reciprocal-motif-prob`. Delivered synaptic spike current is scaled by `--synaptic-gain`. Initial genomes retain their evolvable direct sensory-to-motor scaffold; auxiliary start/clock inputs are excluded from forced I/O repair and direct motor seeding.

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
