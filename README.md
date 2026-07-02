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

## Plot Results

```powershell
uv run python tools/plot_run.py runs/latest
```

This writes:

- `runs/latest/fitness.png`
- `runs/latest/trajectory.png`

## Interactive Viewer

```powershell
uv run python tools/view_run.py runs/latest
```

Open `runs/latest/viewer.html` in a browser to replay the best recorded trajectory, live neuron activations, and fired synapses with playback controls.

## Sensorimotor Regimes

The default regime is `directional-fov`.

- `directional-fov`: directional creature with a facing angle, 120 degree food visibility arc, 4 sensory inputs (`food_visible`, `food_left`, `food_right`, `food_distance`) and 3 motor outputs (`walk_speed`, `turn_left`, `turn_right`).
- `target-vector`: older direct relative-target regime with 5 sensory inputs (`target_right`, `target_left`, `target_up`, `target_down`, `target_distance`) and 4 motor outputs (`move_left`, `move_right`, `move_down`, `move_up`).

Motor/output neurons have no bias, and motor commands are decoded from output spike traces only. Hidden neurons can still evolve large positive or negative bias, including rare large-magnitude hidden-bias jumps controlled by `--hidden-bias-jump` and `--hidden-bias-jump-min`, which allows self-spiking "clock" neurons if evolution finds them useful. Delivered synaptic spike current is scaled by `--synaptic-gain` so spikes can drive downstream neurons without adding motor bias. Initial genomes are seeded with direct sensory-to-motor synapses controlled by `--seed-io-weight`; those synapses remain evolvable and removable.

The directional regime rewards food collection most strongly. It also adds a one-time shaping reward whenever the creature reaches a new closest distance to the current food, configurable with `--distance-reward`. The visibility-alignment reward is small, speed-gated, and configurable with `--visibility-reward`, so a stationary creature is not rewarded for merely looking at food. Turning is penalized with `--turn-penalty`, and total stillness has a small configurable penalty through `--inactivity-penalty`.

Evolution ranks genomes by average foods collected first and fitness second. Fitness still controls shaping among genomes with the same food count.

Switch regimes with:

```powershell
.\build\neuroevo_sim.exe --sensorimotor target-vector --out runs/target_vector
```

Tune directional vision with:

```powershell
.\build\neuroevo_sim.exe --sensorimotor directional-fov --fov-degrees 120 --turn-rate 3.14159 --turn-penalty 0.001 --inactivity-penalty 0.0005 --hidden-bias-jump 0.08 --hidden-bias-jump-min 8 --synaptic-gain 8 --seed-io-weight 1.6 --distance-reward 8 --visibility-reward 0.001 --out runs/fov_test
```

## Combined plot

```powershell
.\scripts\visualize.ps1 -RunDir runs\latest
```

## Prototype Defaults To Revisit

- Keep the first benchmark as food seeking, or switch quickly to memory/computation challenges.
- Keep 2D as the main environment, or make 3D support an early architecture requirement.
- Continue with fixed neuron counts for a few iterations, or start evolving neuron count and neuron types.
- Decide whether brain topology visualization should be generated from C++ output or directly from Python.
