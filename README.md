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

.\scripts\build.ps1

## Run A Simulation

```powershell
.\build\neuroevo_sim.exe --generations 80 --population 96 --steps 400 --trials 3 --seed 7 --out runs/latest
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

## Combined plot

.\scripts\visualize.ps1 -RunDir runs\latest

## Prototype Defaults To Revisit

- Keep the first benchmark as food seeking, or switch quickly to memory/computation challenges.
- Keep 2D as the main environment, or make 3D support an early architecture requirement.
- Continue with fixed neuron counts for a few iterations, or start evolving neuron count and neuron types.
- Decide whether brain topology visualization should be generated from C++ output or directly from Python.
