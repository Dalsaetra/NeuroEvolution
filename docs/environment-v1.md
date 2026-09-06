# Ecosystem v1

New nursery-frontier runs include [predation, inherited body mass, and diet](predation.md).
That guide supersedes the historical body, death, and neural-interface descriptions below for those runs.

Current ecosystem runs use the [calibrated interface and newborn archive evaluation](sensorimotor-calibration.md). That guide specifies the 97-input schema, motor dynamics, evaluation score, and legacy checkpoint behavior; historical descriptions below are superseded where noted.

The ecosystem is a shared world for one or many creatures, each with its own spiking neural network, neural state, energy reserve, and random-number stream. Creatures can compete for food, help open pods, call to others, retreat to shelter, and reproduce when they accumulate enough energy.

This is an environment for experiments. Newly generated brains are random and may fail to find food or die out. The implementation does not establish that cooperation, food learning, or intelligence will evolve under its initial parameters.

## Run it

From the repository root in PowerShell:

```powershell
.\scripts\ecosystem.ps1 -Build -Creatures 24 -Steps 4800 -Open
```

The script builds `neuroevo_ecosystem`, runs the simulation, creates standalone HTML replays, and opens the compact history when `-Open` is supplied. The default output is a fresh timestamped directory under `runs/`; the printed path identifies the recording. Python is needed only for generating/compressing the replay, and the viewer uses its standard library. The runner uses `uv run python` when available, with `python` or `py -3` as fallbacks.

## Sparse ancestral brain

Before mixing founders in the shared habitat, run the controlled one-founder nursery:

```powershell
.\scripts\ecosystem.ps1 -Build -SoloAncestorTrial -Steps 4800 -Recording detailed -Open
```

The runner selects `sparse-ancestor`, disables mutation for exact inheritance, and creates a fixed 24 × 24 nursery with dense rich fruit and a calm phase longer than the trial. Energy capacity, metabolism, feeding mechanics, maturity age, reproduction threshold, reproduction cost, offspring energy, and cooldown retain their baseline values. The founder begins 1.5 units from food rather than on top of it. This isolates the question “can this spiking genome feed and continue a lineage?” from shelter learning, food-value learning, and competition.

The ancestor is an ordinary inheritable `Brain`, not a scripted controller. It has 97 input neurons, 7 hidden neurons, 5 motor neurons, 41 synapses, and sensory connections from 25 inputs:

- obstacle proximity in five visual sectors;
- food proximity in five visual sectors;
- contact in four body-relative directions;
- energy, ingestion feedback, digestion feedback, and the newborn pulse.

Two recurrent hidden neurons generate forward movement. Side food signals must accumulate before producing a turn spike, five threshold relays restrict foraging to nearby food, and obstacle input thresholds reduce sensitivity to distant walls. Ingestion suppresses new forward and turn spikes so the creature remains over a patch; energy and digestion restart locomotion. One locomotion neuron also accepts low-rate background events, which can restart a rhythm that mutation, inhibition, or low energy has silenced. Contact turns the ancestor away from frontal and side pressure, helping touching groups separate without a scripted movement reflex. The brain ignores food appearance and stock, pods, shelter and weather, calls, hearing, and non-contact creature sensing. Its call output is disconnected. These omissions leave obvious niches for later evolution.

The integration test runs exact copies through normal births and requires a naturally born child to mature and produce a grandchild. Zero mutation now preserves unused sensory inputs; mutation no longer attaches every disconnected input as an automatic repair step. Structural mutation can still add new connections during evolutionary runs.

The nursery is deliberately an existence test, not evidence that the ancestor solves the harsh ecology. To place the ancestor in an ordinary generated world with normal mutation and weather, use:

```powershell
.\scripts\ecosystem.ps1 -FounderBrain sparse-ancestor -Creatures 1 -EvolutionPreset breeding -Steps 12000 -Recording standard -Open
```

Direct executable users can select `--founder-brain sparse-ancestor`. The reproducible nursery is `--habitat ancestor-nursery --creatures 1`; set the six mutation probabilities to zero when exact inheritance is required.

For a solitary trial or a larger starting population:

```powershell
.\scripts\ecosystem.ps1 -Creatures 1 -Steps 4800 -NoReproduction -Open
.\scripts\ecosystem.ps1 -Creatures 64 -Steps 4800 -Open
```

`-Creatures` specifies the starting population. Births and deaths can change it. `-NoReproduction` disables births but creatures can still die. The default safety cap is 256 living creatures; use the executable's `--max-population` option for another cap. Without establishment support, the run stops if the population goes extinct. At capacity, births pause while the simulation continues; deaths can free slots for subsequent births.

The executable exposes additional settings:

```powershell
.\build\neuroevo_ecosystem.exe --help
.\build\neuroevo_ecosystem.exe --creatures 12 --steps 2400 --seed 19 --out runs/ecosystem_example
python tools/view_ecosystem.py runs/ecosystem_example
```

With a Visual Studio CMake generator the executable can instead be under `build/Release/` or `build/Debug/`. The PowerShell runner locates those automatically. Run folders must be fresh: the executable refuses to overwrite an existing recording or checkpoint.

## Establish an initial population

Enable archive-based immigration to keep exploring when the starting population declines:

```powershell
.\scripts\ecosystem.ps1 -Build -Establishment -FounderBrain sparse-ancestor -Creatures 24 -Steps 12000 -Open
```

This mode maintains a bounded archive of partial feeding successes and introduces new spiking creatures when population falls below a floor. It preserves the current weather, depleted resources, and existing creatures. Immigration supplies external energy, which is recorded separately from food energy and natural births. Establishment is opt-in; runs without `-Establishment` retain ordinary extinction behavior.

### Admission and selection

An initial archive candidate must be controlled by its spiking brain and be at least 60 simulated seconds old. It then qualifies either by reproducing or by gaining at least 37.5 food energy across at least three feeding bouts while covering at least 60% of its operating energy. Scripted reactive/random controllers never contribute genomes. Long survival or one lucky bite does not qualify. Living creatures are evaluated at immigration checks, and dying creatures are evaluated before removal.

For each observed lifetime, let `G` be actual credited food energy, `C` be operating energy spent excluding reproduction transfers, `B` be feeding bouts, and `O` be natural offspring. A new bout starts after a gap of more than five seconds without ingestion. With `--archive-eval-trials 0`, the historical observed-lifetime score is:

```text
log(1 + G) * (0.5 + min(2, G / max(1, C)))
    + 0.25 * log(1 + B) + 6.0 * log(1 + O)
```

Each genome retains at most eight lifetime observations. Sampling a living creature updates its observation instead of counting it as another trial. Unchanged archive clones share the genome ID and contribute independent lifetimes, including failures that never find food. All exact sparse-ancestor founders also share one genome ID, so dozens of identical bodies cannot fill the archive. In observed-score mode, the archive score is the average of its retained lifetime scores; live observations are provisional. By default, qualification instead triggers five matched newborn family trials, whose reproductive score controls selection; see the [evaluation specification](sensorimotor-calibration.md). Reproduction has enough score weight to preserve a demonstrated breeder while feeding efficiency still admits useful partial successes. This is a selection heuristic, not a validated fitness estimate or evidence of intelligence.

The default archive holds 16 genomes, with four reserved slots per dominant diet: graze, fruit A, fruit B, or pods. Diet is classified by lifetime biomass eaten when admitted. Empty niches lend their slots to others, reclaiming them when qualifying candidates appear. Within a filled niche, better-scoring newcomers replace weaker entries. Parent selection first chooses an occupied diet uniformly, then chooses the best of three distinct sampled candidates in that diet. This preserves several food strategies while favoring performance within each one.

### Immigration mix and rate

Every complete shuffled group of five arrivals contains:

| Share | Introduction |
|---|---|
| 40% | Slightly mutated copy of an archived brain |
| 20% | Strongly mutated copy of an archived brain |
| 40% | Unchanged copy of an archived brain, tested in a fresh lifetime |

The slight branch uses 45% of the configured mutation magnitudes and 65% of its mutation probabilities. The strong branch uses 175% of the configured magnitudes and raises parameter and structural mutation probabilities substantially. Both begin with an archived brain. All introductions reset neural activity, digestive queues, age, and offspring counts, receive independent neural noise, and start with the configured founder energy. Placement samples unoccupied traversable cell centers without favoring food locations. Immigrants have no biological parent; `source_id` identifies the creature that supplied an archived genome.

Each immigrant first selects one represented food niche uniformly, then samples three distinct archive entries from that niche and uses the highest-scoring one. If the niche has fewer than three entries, every entry competes. This preserves ecological diversity while giving genomes with stronger accumulated evidence more descendants. Set the tournament size with `--archive-tournament-size` or `-ArchiveTournamentSize`.

At each natural birth, the child has a 25% chance of exact inheritance, a 50% chance of slight mutation, and a 25% chance of strong mutation, using the same mutation presets as immigration. Exact children retain the parent's genome ID, allowing repeated outcomes to accumulate evidence for that genotype in the archive. Both slightly and strongly mutated children receive a new genome ID. Child energy is 50 in the baseline ecology and 60 in the breeding preset.

Before any genome qualifies, immigration waits and increments `archive_empty_checks`. It does not inject an unrelated random brain. A complete shuffled group of five arrivals has the exact 40/40/20 proportions; a partial group can differ.

Defaults introduce at most two creatures every five simulated seconds, only up to half the starting population (rounded down, minimum one). A population can temporarily be empty between checks. The world continues advancing until the requested step limit, interruption, or extinction after support has ended. If the step limit lands in an empty interval with support active, `summary.json` reports `awaiting_immigration`; resuming continues from there.

Configure the floor and rate from PowerShell:

```powershell
.\scripts\ecosystem.ps1 -Establishment -Creatures 24 -ImmigrationFloor 12 -ImmigrationBatch 2 -ImmigrationInterval 5 -ArchiveCapacity 32 -Steps 12000
```

### Taper and withdrawal

A first birth leaves support active. Each distinct **naturally born spiking creature that itself reproduces** increases the check interval: the default five seconds becomes 10, 15, then 20 seconds, capped there. Founder and immigrant reproduction do not count toward that milestone, and repeated births by the same creature count once.

Support is permanently withdrawn when at least two such breeding descendants have been observed and the population has stayed at or above its floor without any immigration for three full environmental cycles (720 seconds by default). A dip below the floor or another arrival restarts the stability window. This is a conservative graduation heuristic; a population can still go extinct afterward. Use `-KeepImmigration` together with `-Establishment` to disable both taper and automatic withdrawal.

All controls are also available on the executable:

| Option | Default |
|---|---:|
| `--establishment 0|1` | 0 |
| `--immigration-floor N` | 0 = automatic half-population floor |
| `--immigration-batch N` | 2 |
| `--immigration-interval SECONDS` | 5 |
| `--archive-capacity N` | 16 (allowed 4–256) |
| `--archive-min-energy E` | 37.5 |
| `--archive-min-age SECONDS` | 60 |
| `--archive-min-feeding-bouts N` | 3 |
| `--archive-min-efficiency RATIO` | 0.60 |
| `--immigration-auto-stop 0|1` | 1 |
| `--withdrawal-cycles N` | 3 |

The archive, evidence, policy timers, shuffled mix, and separate immigration random stream are checkpointed for exact continuation. The replay shows support status, archive scores, immigrant counts, and each creature's origin. `archive.csv` provides a final archive summary; the actual genomes are stored in `checkpoint.eco`.

The full ecosystem mutation defaults are weight probability 0.22, neuron-parameter probability 0.16, synapse-addition probability 0.40, reciprocal-motif probability 0.12, and hidden-neuron-addition probability 0.12. Natural births use the slight profile derived from these values. A hidden-neuron mutation preserves an existing connection and adds a weaker two-edge branch through the new neuron. This lets brain size evolve without usually erasing a working path in one birth. Use `--mutate-add-neuron-prob` and `--mutation-max-hidden` to control topology growth.

## World rules

The default habitat is a bounded 48 × 48 grid with continuous creature positions and headings. Bodies are circles of radius 0.25. Walls and boundaries obstruct motion and vision; other creatures collide physically. Colliding proposed paths are rejected symmetrically, so two brains that keep pushing into each other can remain blocked until one turns or dies. The ancestor now senses contact and turns away, but narrow crowded spaces can still jam. Ground is traversable, rough terrain raises movement cost, and shelter removes storm exposure cost. Map generation preserves connected traversable terrain and places resources outside shelters.

One world step is 0.1 seconds. Default brain steps are 0.02 seconds, so each brain executes five neural updates per world step. All creatures decide from the same pre-action world state. Movement conflicts and consumption are resolved collectively, so iterating earlier in the creature vector does not confer first access to food.

| Resource | Default patches | Capacity per patch | Energy per unit | Regrowth per second |
|---|---:|---:|---:|---:|
| Ground forage | 80 | 8 | 2 | 0.02 |
| Fruit A and B combined | 32 | 12 | 10 or 4 | 0.01 |
| Food pods | 8 | 12 | 12 | 0.08 while refilling |

The richer fruit appearance is assigned once per world. Creatures can sense appearance and remaining quantity but not nutritional value. Grazing, poor fruit, rich fruit, and pods provide 2.5, 5, 12.5, and 15 energy per biomass unit respectively. This preserves the original 2.5-to-1 contrast between rich and poor fruit while raising every food reward by 25%. Consumed food is removed immediately; its energy arrives after a 3-second digestion delay. Simultaneous eaters receive proportional shares of their attempted consumption when stock is insufficient. The default ingestion limit is one biomass unit per second per creature. Energy densities can be changed with `--graze-energy`, `--poor-fruit-energy`, `--rich-fruit-energy`, and `--pod-energy`, or the corresponding PowerShell parameters.

Closed pods require 10 units of work. Opening speed is the square of the combined foraging effort, capped at two full workers: one full worker takes 10 seconds, and two take 2.5 seconds. Unattended progress decays by one work unit per second. Open pods have no owner and can be eaten by anyone in reach. They refill after depletion or expiry; uneaten stock spoils 30 seconds after opening. Pod work is logged separately from food eaten so that contribution and benefit can be compared.

The default weather cycle is 150 seconds calm, 30 seconds warning, and 60 seconds storm. A perceptible warning cue rises before the storm. Storms pause food regrowth and add 0.8 energy cost per second outside shelter. Normal metabolism continues everywhere. There is no food inside shelter.

Use `-NoStorms` with `scripts/ecosystem.ps1`, or `--no-storms` / `--storms 0` with the executable, to hold the simulation in calm weather. This removes warning and storm phases, exposure cost, and storm pauses in food regrowth. The sensory layout remains at 97 inputs, including the storm-cue neuron, whose value stays zero.

### Energy and reproduction

Founders start with 90 energy and have capacity 200. Basal metabolism costs 0.20 per second; movement, turning, foraging, calling, neurons, synapses, and spikes have additional explicit costs. Rough terrain multiplies movement cost by two. At zero energy a creature dies, and its remaining body energy and undigested food leave the system.

Reproduction is automatic and asexual. The default requirements are age 120 seconds, at least 150 energy, a 120-second cooldown, and free nearby space. A birth deducts 75 energy from the parent, gives 50 energy to the offspring, and spends the remaining 25 as reproductive overhead. The offspring has a 25% chance of exact inheritance, 50% slight mutation, and 25% strong mutation, with neural activity reset. It gets its own independent neural random-number stream.

The population cap blocks births without stopping movement, feeding, aging, weather, or deaths. After deaths are processed, eligible parents receive available birth slots in descending energy order; equal-energy ties use the reproducible per-step ordering. A parent unable to place a child is skipped without spending energy. The `capacity_limited` flag reports the current full state and clears when capacity becomes available; it is not a terminal status. This also applies when resuming a checkpoint saved at capacity. `maturations` counts all creatures reaching maturity, including founders; use maturation events' parent IDs to measure offspring survival separately.

## Senses and actions

Each ecological brain has **97 input neurons and 5 output neurons**. Hidden-neuron count defaults to 16 and can be selected with `--hidden`. The default visual range is 6 units over a 150-degree forward arc, divided into five sectors.

Each visual sector has 14 channels: obstacle proximity; food presence, proximity, four appearance categories, and remaining stock; pod closed/open state; other-creature presence, proximity, foraging activity, and calling activity. Food attributes refer to the nearest stocked, non-refilling resource in that sector; other-creature attributes refer to the nearest visible creature. Walls occlude both visual observations and calls.

Ten appended channels report depleted-food proximity and visible shelter proximity in the five sectors. Additional inputs are four directional hearing channels, four directional contact channels, energy, speed, left and right turn feedback, shelter status, storm cue, ingestion, digestion gain, and a brief pulse at birth. Hearing range defaults to 6 units, and call strength decreases with distance. There are no creature identities, global coordinates, absolute compass headings, maps, partner assignments, global clocks, or hidden food values in the observation vector.

The five outputs control forward movement, left turn, right turn, forage/work, and call. Movement and actions are continuous intensities decoded from normalized output firing rates and smoothed by actuator dynamics. Foraging slows forward motion to make sustained resource interaction possible. A creature can interact with a resource within 0.8 units and its forward 120-degree interaction arc. Calls cost energy and have no assigned semantic meaning.

Hidden and motor neurons use the project's leaky integrate-and-fire dynamics, recurrence, delayed synapses, and stochastic background activity. Sensory neurons use deterministic rate encoding in the calibrated interface. Ecosystem founders use sparse random connections (probability 0.08), no forced dense sensory-to-motor scaffold, and synaptic gain 32. This gain allows local sensory activity to drive movement and feeding attempts; gain 8 from the earlier task configuration left most initial ecosystem motor neurons silent. There is no scripted fallback for spiking controllers. Neural state persists throughout a lifetime. Synaptic parameters mutate at reproduction; there is no within-lifetime synaptic plasticity or separate learned food-value table. Persistent behavioral adaptation must initially use the network's evolving activity state.

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

`--founders` copies living brains from an ecosystem checkpoint, resets their neural activity, and places them into a newly generated world. If the new population is larger than the saved living population, it cycles through the saved brains. Neural timestep must match; runtime interface settings come from the new world, and historical genomes can be migrated to the extended interface. Existing single-creature task genomes do not directly fit the ecosystem's different sensor and motor layout.

For tightly controlled experiments, C++ callers can construct `EcosystemWorld(config, false)`, arrange public terrain/resources/creatures explicitly, and call `step(actions)` with one action per living creature. Calling `step()` without supplied actions invokes each creature's own controller. This supports partner/helping experiments without changing ecological mechanics.

### Continue a population

```powershell
.\scripts\ecosystem.ps1 -Resume runs/food_a/checkpoint.eco -Steps 4800 -Open
```

`--resume` restores the complete world, random-number streams, delayed neural currents, refractory states, motor traces, digestion queues, lifecycle state, and establishment archive/policy. Steps are additional steps, and outputs go to a fresh directory. World or brain configuration overrides are rejected for exact continuation; `--founders` is the option for placing saved brains in a changed environment. A checkpoint with no living creatures resumes immigration if support is active; otherwise it remains extinct. New checkpoints use version 7; versions 1–6 remain readable with historical sensorimotor and archive behavior preserved.

## Outputs and replay

| File | Contents |
|---|---|
| `ecosystem.jsonl.gz` | Gzip-compressed compact history from the PowerShell runner (`ecosystem.jsonl` with `-KeepJsonl`) |
| `ecosystem_tail.jsonl.gz` | Full sensors and neural activity for the bounded final window |
| `ecosystem_stats.csv` | Population, energy, food biomass, births/deaths/maturations, weather, spikes, and energy accounting |
| `events.csv` | Individual births, deaths, maturations, consumption, digestion, pod work/opening/spoilage, and world events |
| `archive.csv` | Retained genome IDs, niches, selection scores, observed-lifetime counts and mean energy |
| `newborn_evaluations.csv` | Matched newborn trial outcomes, including rejected candidates, birth times, energy and truncation |
| `initial.eco` | Exact state at the start of the recording |
| `checkpoint.eco` | Exact state at the end, including interrupted runs handled by the executable |
| `summary.json` | Completion status, elapsed simulation steps/time, population, births/deaths, immigration counts/energy, archive size, support state, and wall time |
| `ecosystem.html` | Generated offline replay, when the Python viewer or PowerShell runner is used |
| `ecosystem_tail.html` | Full-detail replay of the last five simulated minutes by default |

The PowerShell runner defaults to `-Recording compact`: one frame per 500 world steps (50 simulated seconds), no sensory vectors, neural graphs/activity, or high-frequency routine events. It also saves full detail for the final 300 seconds at one-second intervals. Use `-Recording standard|detailed`, change `-DetailedTailSeconds`, or use `-KeepJsonl` to preserve uncompressed sources. See [evolution tuning and recording](evolution-tuning.md) for the profiles, the streaming compactor for existing runs, and measured size reductions.

Direct executable defaults remain one frame every 10 steps with full diagnostic data. `--record-every 1` gives finer motion. Potentials and spikes are sampled at the recorded instant; the brain diagram does not display every spike between frames. Cumulative spike counters and aggregate energy/food statistics retain their totals even in compact mode.

Generate a replay later, or choose another HTML destination:

```powershell
python tools/view_ecosystem.py runs/social_comparison
python tools/view_ecosystem.py runs/social_comparison --output runs/social_comparison/review.html
```

The viewer accepts plain JSONL and `.jsonl.gz`; a run directory finds either automatically. Open the resulting HTML directly in a browser. It embeds the recording and needs no server or internet access. Playback supports scrubbing, stepping, speed selection, creature selection, movement trails, diets, and milestone/activity event filters. Detailed recordings add membrane potentials, sampled spikes, and raw sensory inputs. The vision overlay is an approximate geometric illustration clipped at walls; the Senses tab contains actual numeric observations when recorded. The full map, pod progress, lineage IDs, and optional nutritional-value labels are observer tools rather than additional creature inputs.

## Scope and practical limits

- The world is a CPU simulation, with pairwise creature interactions and individual neural updates. Larger populations, denser brains, and frequent brain recording increase runtime and file size. The HTML viewer loads the recording into memory; reduce recording frequency and disable activity recording for long runs.
- Grid dimensions are limited to 5–256 cells per axis, and requested resources/shelters must fit. Neural buffer and connectivity bounds keep generated brains compatible with checkpoints. This implementation is intended for modest populations rather than GPU-scale ecosystems.
- Body shape and hidden-neuron count remain fixed within a lineage in this version. Inherited synapse structure and neuron parameters mutate; this ecosystem does not run the existing generational NEAT/NSGA-II selection loop.
- There is no combat, predation, food carrying, construction, mating, identity recognition, or semantic communication system yet.
- Resource/energy balance is provisional. Use replicated seeds and competent baselines before drawing conclusions about the reproductive benefits of a neural strategy. Eating together does not establish cooperation; compare each participant's work, intake, cost, and survival against relevant solitary and non-helping controls.
- The existing dashboard still controls the earlier task/fitness experiments. Ecosystem runs use their own executable, runner, recording format, and replay.
