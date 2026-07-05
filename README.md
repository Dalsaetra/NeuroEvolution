# NeuroEvolution

Prototype evolutionary simulator for creatures with spiking neural-network brains.

## Current Prototype

- C++17 simulation runtime built with CMake.
- 2D food-seeking environment.
- Leaky integrate-and-fire neurons with spatially embedded synapses and distance-based delays.
- Evolution loop with elitism, tournament selection, weight/bias/position mutation, and synapse add/remove mutation.
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

Motor/output neurons have no bias, and motor commands are decoded from output spike traces only. Hidden neurons can still evolve large positive or negative bias, including rare large-magnitude hidden-bias jumps controlled by `--hidden-bias-jump` and `--hidden-bias-jump-min`, which allows self-spiking "clock" neurons if evolution finds them useful. The environment also adds a tonic clock input by default, controlled by `--clock-input` and `--clock-input-value`, so brains can evolve internal exploratory activity when no food is visible. The clock input is not seeded directly to motor outputs and is exempt from the required sensory-to-motor scaffold; later add-connection mutations may connect it into hidden neurons. Its spiking speed is evolvable through the clock input threshold, controlled by `--clock-threshold-sigma`, `--clock-threshold-min`, and `--clock-threshold-max`. Delivered synaptic spike current is scaled by `--synaptic-gain` so spikes can drive downstream neurons without adding motor bias. Initial genomes are seeded with direct sensory-to-motor synapses controlled by `--seed-io-weight`; those synapses remain evolvable and removable. In the directional-FOV regime, default seed wiring is structured so food visibility/distance drive walking, food-left drives left turning, and food-right drives right turning.

NEAT genomes are repaired after creation and mutation so every sensory input neuron has at least one enabled outgoing synapse and every output neuron has at least one enabled incoming synapse. The tonic clock input is excluded from this repair so it must become useful through evolved clock-to-hidden wiring rather than hard-coded motor drive. This prevents Pareto pressure from collapsing the brain into a single speed-only synapse while still allowing topology to evolve around that minimal viable I/O scaffold.

The directional regime rewards food collection most strongly. It also adds a one-time shaping reward whenever the creature reaches a new closest distance to the current food, configurable with `--distance-reward`. Initial target bearing is sampled within the visible FOV but over a wide range controlled by `--initial-heading-fov-frac`, so steering is useful without making the first target invisible. The visibility-alignment reward is small, speed-gated, and configurable with `--visibility-reward`, so a stationary creature is not rewarded for merely looking at food. Turning, stillness, spikes, and structural complexity use budget-excess penalties: costs apply only after `--turn-budget`, `--inactivity-budget`, `--spike-budget-rate`, `--structural-synapse-budget`, or `--structural-neuron-budget` are exceeded.

Evolution ranks genomes by average foods collected first and fitness second. Fitness still controls shaping among genomes with the same food count.

Switch regimes with:

```powershell
.\build\neuroevo_sim.exe --sensorimotor target-vector --out runs/target_vector
```

Tune directional vision with:

```powershell
.\build\neuroevo_sim.exe --sensorimotor directional-fov --clock-input 1 --clock-input-value 1 --clock-threshold-sigma 0.08 --fov-degrees 120 --initial-heading-fov-frac 0.9 --turn-rate 3.14159 --turn-penalty 0.0001 --turn-budget 0.25 --inactivity-penalty 0.0005 --inactivity-budget 0.5 --hidden-bias-jump 0.08 --hidden-bias-jump-min 8 --synaptic-gain 8 --seed-io-weight 3 --distance-reward 8 --visibility-reward 0.001 --out runs/fov_test
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
