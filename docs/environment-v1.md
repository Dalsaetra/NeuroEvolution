# Ecosystem v1

The ecosystem is a shared world for one or many creatures, each with its own spiking neural network, neural state, energy reserve, and random-number stream. Creatures can compete for food, help open pods, call to others, retreat to shelter, and reproduce when they accumulate enough energy.

This is an environment for experiments. Newly generated brains are random and may fail to find food or die out. The implementation does not establish that cooperation, food learning, or intelligence will evolve under its initial parameters.

## Run it

From the repository root in PowerShell:

```powershell
.\scripts\ecosystem.ps1 -Build -Creatures 24 -Steps 4800 -Open
```

The script builds `neuroevo_ecosystem`, runs the simulation, creates a standalone HTML replay, and opens it when `-Open` is supplied. The default output is a fresh timestamped directory under `runs/`; the printed path identifies the recording. Python is needed only for generating the replay, and the viewer uses its standard library. The runner uses `uv run python` when available, with `python` or `py -3` as fallbacks.

For a solitary trial or a larger starting population:

```powershell
.\scripts\ecosystem.ps1 -Creatures 1 -Steps 4800 -NoReproduction -Open
.\scripts\ecosystem.ps1 -Creatures 64 -Steps 4800 -Open
```

`-Creatures` specifies the starting population. Births and deaths can change it. `-NoReproduction` disables births but creatures can still die. The default safety cap is 256 living creatures; use the executable's `--max-population` option for another cap. The run stops if the population goes extinct or reaches capacity, and records the reason in `summary.json`.

The executable exposes additional settings:

```powershell
.\build\neuroevo_ecosystem.exe --help
.\build\neuroevo_ecosystem.exe --creatures 12 --steps 2400 --seed 19 --out runs/ecosystem_example
python tools/view_ecosystem.py runs/ecosystem_example
```

With a Visual Studio CMake generator the executable can instead be under `build/Release/` or `build/Debug/`. The PowerShell runner locates those automatically. Run folders must be fresh: the executable refuses to overwrite an existing recording or checkpoint.

## World rules

The default habitat is a bounded 48 × 48 grid with continuous creature positions and headings. Bodies are circles of radius 0.25. Walls and boundaries obstruct motion and vision; other creatures collide physically. Ground is traversable, rough terrain raises movement cost, and shelter removes storm exposure cost. Map generation preserves connected traversable terrain and places resources outside shelters.

One world step is 0.1 seconds. Default brain steps are 0.02 seconds, so each brain executes five neural updates per world step. All creatures decide from the same pre-action world state. Movement conflicts and consumption are resolved collectively, so iterating earlier in the creature vector does not confer first access to food.

| Resource | Default patches | Capacity per patch | Energy per unit | Regrowth per second |
|---|---:|---:|---:|---:|
| Ground forage | 80 | 8 | 2 | 0.02 |
| Fruit A and B combined | 32 | 12 | 10 or 4 | 0.01 |
| Food pods | 8 | 12 | 12 | 0.08 while refilling |

The richer fruit appearance is assigned once per world. Creatures can sense appearance and remaining quantity but not nutritional value. Consumed food is removed immediately; its energy arrives after a 3-second digestion delay. Simultaneous eaters receive proportional shares of their attempted consumption when stock is insufficient. The default ingestion limit is one biomass unit per second per creature.

Closed pods require 10 units of work. Opening speed is the square of the combined foraging effort, capped at two full workers: one full worker takes 10 seconds, and two take 2.5 seconds. Unattended progress decays by one work unit per second. Open pods have no owner and can be eaten by anyone in reach. They refill after depletion or expiry; uneaten stock spoils 30 seconds after opening. Pod work is logged separately from food eaten so that contribution and benefit can be compared.

The default weather cycle is 150 seconds calm, 30 seconds warning, and 60 seconds storm. A perceptible warning cue rises before the storm. Storms pause food regrowth and add 0.8 energy cost per second outside shelter. Normal metabolism continues everywhere. There is no food inside shelter.

### Energy and reproduction

Founders start with 90 energy and have capacity 200. Basal metabolism costs 0.20 per second; movement, turning, foraging, calling, neurons, synapses, and spikes have additional explicit costs. Rough terrain multiplies movement cost by two. At zero energy a creature dies, and its remaining body energy and undigested food leave the system.

Reproduction is automatic and asexual. The default requirements are age 120 seconds, at least 150 energy, a 120-second cooldown, and free nearby space. A birth deducts 75 energy from the parent, gives 45 energy to the offspring, and spends the remaining 30 as reproductive overhead. The offspring receives a mutated copy of its parent's brain, with neural activity reset. It gets its own independent neural random-number stream.

The safety population cap stops the simulation rather than silently killing or replacing creatures. Treat a capacity-limited run as truncated. `maturations` counts all creatures reaching maturity, including founders; use maturation events' parent IDs to measure offspring survival separately.

## Senses and actions

Each ecological brain has **87 input neurons and 5 output neurons**. Hidden-neuron count defaults to 16 and can be selected with `--hidden`. The default visual range is 6 units over a 150-degree forward arc, divided into five sectors.

Each visual sector has 14 channels: obstacle proximity; food presence, proximity, four appearance categories, and remaining stock; pod closed/open state; other-creature presence, proximity, foraging activity, and calling activity. Food and other-creature attributes refer to the nearest visible item in that sector. Walls occlude both visual observations and calls.

Additional inputs are four directional hearing channels, four directional contact channels, energy, speed, left and right turn feedback, shelter status, storm cue, ingestion, digestion gain, and a brief pulse at birth. Hearing range defaults to 6 units, and call strength decreases with distance. There are no creature identities, global coordinates, absolute compass headings, maps, partner assignments, global clocks, or hidden food values in the observation vector.

The five outputs control forward movement, left turn, right turn, forage/work, and call. Movement and actions are continuous intensities decoded from output spike traces. Foraging slows forward motion to make sustained resource interaction possible. A creature can interact with a resource within 0.8 units and its forward 120-degree interaction arc. Calls cost energy and have no assigned semantic meaning.

The network uses the project's leaky integrate-and-fire neurons, recurrence, delayed synapses, and stochastic background activity. Ecosystem founders use sparse random connections (probability 0.08), no forced dense sensory-to-motor scaffold, and synaptic gain 32. This gain allows local sensory activity to drive movement and feeding attempts; gain 8 from the earlier task configuration left most initial ecosystem motor neurons silent. There is no scripted fallback for spiking controllers. Neural state persists throughout a lifetime. Synaptic parameters mutate at reproduction; there is no within-lifetime synaptic plasticity or separate learned food-value table. Persistent behavioral adaptation must initially use the network's evolving activity state.

## Controlled comparisons

`spiking` is the default controller. `reactive` is a scripted local-sensing forager, and `random` supplies stochastic actions. Both use the same available observations and physical action rules. A reactive creature assumes equal values for both fruit appearances; it has no privileged access to the hidden nutritional assignment. Baselines are comparison tools, not evolved neural policies.

Mix spiking creatures with scripted companions:

```powershell
.\build\neuroevo_ecosystem.exe --creatures 8 --companions 4 --companion-controller reactive --steps 4800 --no-reproduction --out runs/social_comparison
```

`--companions` changes the controllers of the last N initial creatures, so this example has four spiking and four reactive creatures. Every creature still owns a brain object, but scripted controllers bypass neural stepping. The replay identifies the selected creature's controller.

Disable communication with `--communication 0`, disable reproduction with `--reproduction 0`, or force the nutritional assignment with `--food-assignment a-rich|b-rich`. Keep the seed and starting brains fixed when comparing conditions. One way to test the same brains against reversed fruit values is:

```powershell
.\build\neuroevo_ecosystem.exe --creatures 8 --seed 7 --food-assignment a-rich --no-reproduction --out runs/food_a
.\build\neuroevo_ecosystem.exe --creatures 8 --seed 7 --food-assignment b-rich --founders runs/food_a/initial.eco --no-reproduction --out runs/food_b
```

`--founders` copies living brains from an ecosystem checkpoint, resets their neural activity, and places them into a newly generated world. If the new population is larger than the saved living population, it cycles through the saved brains. It preserves brain configurations, so neural timestep must match. Existing single-creature task genomes do not directly fit the ecosystem's different sensor and motor layout.

For tightly controlled experiments, C++ callers can construct `EcosystemWorld(config, false)`, arrange public terrain/resources/creatures explicitly, and call `step(actions)` with one action per living creature. Calling `step()` without supplied actions invokes each creature's own controller. This supports partner/helping experiments without changing ecological mechanics.

### Continue a population

```powershell
.\scripts\ecosystem.ps1 -Resume runs/food_a/checkpoint.eco -Steps 4800 -Open
```

`--resume` restores the complete world, random-number streams, delayed neural currents, refractory states, motor traces, digestion queues, and lifecycle state. Steps are additional steps, and outputs go to a fresh directory. World or brain configuration overrides are rejected for exact continuation; `--founders` is the option for placing saved brains in a changed environment. A checkpoint with no living creatures remains extinct.

## Outputs and replay

| File | Contents |
|---|---|
| `ecosystem.jsonl` | Metadata followed by chronological world frames; events between frames are retained |
| `ecosystem_stats.csv` | Population, energy, food biomass, births/deaths/maturations, weather, spikes, and energy accounting |
| `events.csv` | Individual births, deaths, maturations, consumption, digestion, pod work/opening/spoilage, and world events |
| `initial.eco` | Exact state at the start of the recording |
| `checkpoint.eco` | Exact state at the end, including interrupted runs handled by the executable |
| `summary.json` | Completion status, elapsed simulation steps/time, population, births, deaths, and wall time |
| `ecosystem.html` | Generated offline replay, when the Python viewer or PowerShell runner is used |

Replay frames default to every 10 world steps (one simulated second), plus initial and final states. Use `--record-every 1` for finer motion and `--record-brains 0` to reduce recording size while preserving static network graphs. Potentials and spikes are sampled at the recorded instant; the brain diagram does not display every spike that occurred between frames. Cumulative spike counters retain the total.

Generate a replay later, or choose another HTML destination:

```powershell
python tools/view_ecosystem.py runs/social_comparison
python tools/view_ecosystem.py runs/social_comparison --output runs/social_comparison/review.html
```

Open the resulting HTML directly in a browser. It embeds the recording and needs no server or internet access. Playback supports scrubbing, stepping, speed selection, creature selection, movement trails, diets, membrane potentials, sampled spikes, raw sensory inputs, and milestone/activity event filters. The vision overlay is an approximate geometric illustration clipped at walls; the Senses tab contains the actual numeric observations. The full map, pod progress, lineage IDs, and optional nutritional-value labels are observer tools rather than additional creature inputs.

## Scope and practical limits

- The world is a CPU simulation, with pairwise creature interactions and individual neural updates. Larger populations, denser brains, and frequent brain recording increase runtime and file size. The HTML viewer loads the recording into memory; reduce recording frequency and disable activity recording for long runs.
- Grid dimensions are limited to 5–256 cells per axis, and requested resources/shelters must fit. Neural buffer and connectivity bounds keep generated brains compatible with checkpoints. This implementation is intended for modest populations rather than GPU-scale ecosystems.
- Body shape and hidden-neuron count remain fixed within a lineage in this version. Inherited synapse structure and neuron parameters mutate; this ecosystem does not run the existing generational NEAT/NSGA-II selection loop.
- There is no combat, predation, food carrying, construction, mating, identity recognition, or semantic communication system yet.
- Resource/energy balance is provisional. Use replicated seeds and competent baselines before drawing conclusions about the reproductive benefits of a neural strategy. Eating together does not establish cooperation; compare each participant's work, intake, cost, and survival against relevant solitary and non-helping controls.
- The existing dashboard still controls the earlier task/fitness experiments. Ecosystem runs use their own executable, runner, recording format, and replay.
