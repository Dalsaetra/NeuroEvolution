# Calibrated ecosystem interface and newborn evaluation

New ecosystem runs default to a calibrated spiking interface and repeated newborn evaluation for archive selection. The generation-based task simulator keeps its original brain defaults.

## Run and compare

```powershell
.\scripts\ecosystem.ps1 -Build -Establishment -FounderBrain sparse-ancestor -EvolutionPreset breeding -Steps 120000 -Recording compact
```

The runner exposes `-Sensorimotor calibrated|legacy`, `-ArchiveEvalTrials` (default 5), `-ArchiveEvalSeconds` (600), `-ArchiveEvalSeed` (17071), and `-ActuatorTau` (optional override). The runner builds with Release optimization, including with single-configuration CMake generators. Evaluation adds computation when a qualifying genome first appears. It does not advance simulation time or add energy or creatures to the live world.

Use the same world seeds and ecology for these comparisons:

| Variant | Extra runner arguments |
|---|---|
| Historical interface and observed archive score | `-Sensorimotor legacy -ArchiveEvalTrials 0` |
| Calibrated interface and observed archive score | `-ArchiveEvalTrials 0` |
| Complete package | None |

The interface preset is applied before individual runner overrides. For the executable, place `--sensorimotor` before numeric overrides. `--calibrated-io 0` independently disables calibrated neural encoding/decoding without removing the extended cues. `--actuator-tau 0` disables actuator smoothing. Direct controls also include `--sensory-rate`, `--motor-rate-tau`, `--motor-reference-hz`, `--motor-gain`, and `--conduction-speed`.

## Sensory and motor dynamics

The calibrated input neurons encode a normalized input `x` using a deterministic phase accumulator. Their target rate is `20*x/threshold` Hz, limited to one spike per brain step. Threshold remains sensory sensitivity; a threshold of one yields 20 Hz at full input. Small inputs accumulate toward spikes instead of disappearing below a leaky integrate-and-fire DC threshold. This is a time-averaged encoding: weaker inputs still take longer to communicate. Input neurons receive no background noise in this mode. Hidden and motor neurons remain leaky integrate-and-fire neurons with evolvable parameters and background sensitivity.

Output spikes feed an exponentially weighted rate estimate with a 0.20-second time constant. Each spike adds `(1-exp(-dt/tau))/dt`, so the estimate has units of Hz and its mean does not increase when the time constant is lengthened. Divide by a 10 Hz reference rate, apply the bounded motor nonlinearity, then smooth physical commands with a 0.30-second time constant. Left and right rates are subtracted before decoding and smoothing. Motor gain defaults to one. The legacy preset retains the raw spike traces and gain of eight.

New brains use conduction speed 6 and a maximum delay of eight neural steps (0.16 seconds at the default 0.02-second step). This reduces sensorimotor latency; membrane and recurrent state are still available for internal dynamics. Smoothing is applied to spiking controllers; supplied actions and the scripted comparison controllers retain their own behavior.

## Observation layout

The original indices 0–86 are retained. Five depleted-food proximity channels occupy 87–91, and five directional shelter proximity channels occupy 92–96. The complete interface has 97 inputs and five outputs. Sector order remains right to left, with sector two facing forward.

The existing food channels now select the nearest stocked resource that is not a refilling pod. Empty patches and refilling pods remain perceptible through the separate depleted-food channels, without hiding stocked opportunities in the same sector. Closed stocked pods remain visible as work opportunities. Physical foraging uses the same availability rule. Existing food stock channels continue to report the selected patch's stock/capacity ratio; nutritional value is not revealed visually.

Shelter signals encode the nearest visible shelter-cell center in each sector, respecting field of view, range, and wall occlusion. The existing body-relative `sheltered` input remains present. Digestion feedback scales by the largest configured food energy density times ingestion rate and world timestep, preserving differences between poor and rich food when nutrition settings change.

The sparse ancestor keeps its 7 hidden neurons, 41 synapses, and 25 connected inputs. Storm intensity, current shelter occupancy, and all five directional shelter cues have weak excitatory connections into existing hidden neurons (nominal weight 0.15 at synaptic gain 32). These connections can evolve normally; they do not encode a shelter-seeking policy. Stock cues remain initially unconnected. Legacy ancestors receive only the storm and shelter-occupancy connections. Existing checkpoints and imported genomes retain their saved wiring. The ancestor's existing input thresholds now set rate sensitivity in calibrated mode; old LIF threshold interpretations apply only in legacy mode. Its automated nursery test still requires a child to mature and produce a grandchild.

## Repeated newborn evaluation

Observed efficient feeding or reproduction still gates archive consideration. On first qualification, each genome is evaluated in five independently generated habitats using the configured resource densities, costs, weather durations, and reproduction parameters. Trial seeds, uniform valid-cell placements, headings, and neural random streams are shared across candidates. Weather starts at stratified phases of the cycle. `--archive-eval-seed` chooses another fixed suite independently of the live world seed.

Every focal creature starts with `offspring_energy`, age zero, empty digestion, zero actions, and reset brain state. Trials allow ordinary energy-funded reproduction with mutation disabled. Descendants remain in the trial, compete locally, and can reproduce. Immigration and nested archive evaluation are disabled. A trial ends after 600 simulated seconds, extinction, or the trial population cap of `min(32, max(2, configured cap))`. Cap-limited trials are explicitly marked as truncated.

The archive score is the mean of the following per-trial score:

```
100 * any breeding descendant
 + 10 * focal creature reproduced
 + log(1 + focal offspring count)
 + 0.1 * focal creature matured
 + 0.05 * min(1, focal age / evaluation horizon)
 + 0.01 * clamp(family food energy / max(1, family operating energy), 0, 2)
```

This is a reproductive screening score, not a lifetime reproductive rate or a confidence bound. Failed trials count equally. Family operating energy excludes reproductive transfers and overhead, which the physical simulation still charges normally. Food-cost coverage and survival supply small intermediate signals. Terminal net energy is deliberately excluded: unsuccessful creatures that starve all lose their starting reserve, regardless of how much food they found. Observations of subsequent live clones are retained for reporting but do not change the cached newborn score. Archive replacement and niche-preserving tournaments use this score. Setting evaluation trials to zero restores the observed-lifetime score.

All candidate evaluations, including rejected candidates, are cached by genome ID and saved in checkpoints. Their trial records appear in `newborn_evaluations.csv`, with focal offspring, descendant births, mature offspring, first/second birth times, food and operating energy, elapsed time, survival, and truncation. First/second birth times of -1 mean no such birth was observed. A living focal creature at the time limit has a censored lifetime. `archive.csv` retains the live-observation columns; its score comes from the newborn trials when enabled. Replay archive entries show trial count, the fraction of trials in which the focal newborn reproduced, and the fraction with breeding descendants.

These are solo-family trials with initially full resources, not tests against unrelated competitors or depleted live-world snapshots. Successful screening is therefore insufficient evidence of ecological stability. Compare unsupported population trajectories, completed birth-cohort reproduction, food/cost coverage, and immigration demand across multiple world seeds. Use a different evaluation seed in a fresh comparison to check dependence on the fixed suite. Frequent cap truncation calls for a different evaluation horizon or trial design before interpreting score differences as lifetime fitness.

## Checkpoint compatibility

Ecosystem checkpoints now use version 7 and brain checkpoints version 2. Versions 1–6 of the ecosystem and version 1 of brains remain readable. Resuming them preserves the 87-input interface, original neural encoding/decoding, original gains/delays, no added smoothing, and observed archive selection. Resuming always restores the checkpoint configuration; it does not opt an old experiment into new dynamics.

`--founders OLD_CHECKPOINT` starts a new experiment and can migrate 87-input genomes to the calibrated 97-input interface. Existing connections are remapped around the ten appended inputs; new inputs start disconnected. Runtime encoding, decoding, and delays use the new world's settings, and all neural state resets. Importing into `--sensorimotor legacy` preserves the historical interface. Reducing a 97-input genome to 87 inputs is rejected rather than silently deleting connections.

## Initial integration comparison (2026-09-05)

A Release-build pilot used world seed 105, 48 sparse ancestors, 1,200 simulated seconds, storms enabled, maturity 60, reproduction threshold 110, parent cost 65, offspring energy 60, cooldown 90, and food energies 7.5/13/40/60. It compared the three variants above. Commands and full results are saved locally under `runs/calibrated_package_release_20260905/`.

| Variant | Births | Descendant births | Immigrants | Food / operating energy | Wall seconds |
|---|---:|---:|---:|---:|---:|
| Legacy interface, observed archive score | 82 | 12 | 65 | 66.3% | 7.4 |
| Calibrated interface, observed archive score | 89 | 18 | 54 | 72.1% | 6.8 |
| Full package, five 600-second newborn trials | 78 | 8 | 54 | 70.4% | 136.5 |

The full package evaluated 36 genomes in 180 family trials. Wall times were measured with the three arms launched concurrently, so they are approximate workload comparisons. This pilot validates integration and exposes the extra evaluation cost; it does not establish a fitness benefit. In this seed, the interface improved the observed counts while the new archive selection did not improve descendant breeding. No variant was self-sustaining. Keep replicated world seeds and fresh evaluation suites in the next experiment rather than tuning the score to this one outcome.
# Runtime profiling and parallel trials

Newborn evaluation is synchronous: an uncached qualifying genome runs all its
family trials before the main world advances. Several candidates can qualify in
one step. Trial family size and survival time, rather than the visible population
or births alone, determine these pauses. Archive maintenance also continues after
immigration withdrawal.

The executable now runs independent trials with up to four workers by default
(bounded by reported hardware concurrency). Use `--archive-eval-workers N`, or
`-ArchiveEvalWorkers N` in `scripts/ecosystem.ps1`, to choose 1–32 workers.
This execution setting can change on resume. Trial worlds and RNGs are isolated;
results are combined in trial order, preserving scores and checkpoint state.
The C++ library defaults to one worker unless explicitly configured.

`performance.csv` records cumulative wall time, world-step time, evaluation
time, evaluation calls and the slowest step after evaluation bursts, every 1,000
steps and at completion. `summary.json` includes the corresponding `performance`
object. Counters cover only the current invocation; cached evaluations do not
count as new calls. Evaluation time is included in step time, not additional to
it. Total wall time also includes recording and other run overhead.

Measured on 2026-09-05 using the saved initial checkpoint from
`runs/ecosystem_20260905_014009778`, advancing 3,000 steps (300 simulated seconds)
without replay recording:

| Measurement | Original serial | Four workers |
| --- | ---: | ---: |
| Total step wall time | 26.20 s | 11.37 s |
| Evaluation-containing steps | 24.82 s | 9.93 s |
| Longest step | 7.97 s | 3.26 s |

Only five steps triggered evaluation of six genomes, consuming 94.7% of baseline
time. The final checkpoint files were byte-identical. This sample improved 2.3×;
speedup depends on hardware and trial duration imbalance. Parallel execution
reduces pauses but does not remove them: the main world still waits so archive
selection timing remains unchanged. Benchmark harness and per-step CSVs are in
`runs/runtime_investigation_20260905`.
