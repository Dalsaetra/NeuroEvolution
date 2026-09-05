# Ecosystem evolution and recording experiments

The [calibrated interface and newborn evaluation package](sensorimotor-calibration.md) is now the default for new runs. Results below describe earlier experiments; archive selection now uses repeated newborn trials unless `--archive-eval-trials 0` is selected.

## Archive and mutation revision

The current policy starts sweeps from the sparse ancestor by default. Exact ancestor copies share one genome ID, archive admission requires a mature breeder or repeated efficient feeding, and immigration uses 40% archive clones, 40% slight archive mutations, and 20% strong archive mutations from a 16-genome archive. Immigration selects a food niche uniformly and then runs a three-entry tournament without replacement inside that niche. Natural births use 50% exact inheritance and 50% slight mutation. Random immigrant brains are no longer generated. Scalar spiking brains can now add hidden neurons as well as synapses, and the ecosystem mutation defaults are stronger.

A seed-7 validation with 48 ancestors and the breeding preset ran for 3,000 simulated seconds. It produced 12 births: 2 in the first 100 seconds and 10 afterward. Six births came from founders and six from archive immigrants. The archive grew to 28 entries rather than filling at its first checks. Its 380 immigrants split into 228 slight mutations, 76 strong mutations, and 76 clones, with no random brains. Living brains ranged up to 9 hidden neurons from an ancestral 7. Nine offspring matured, but no naturally born creature reproduced, so multi-generation continuity remains the next bottleneck.

A separate three-seed 1,000-second screen produced 10, 6, and 8 births, with 8, 2, and 2 respectively occurring after the first 100 seconds. Every seed produced six or seven mature offspring. Seed 19 produced one birth from a naturally born descendant; the other two did not, so this is an encouraging existence result rather than evidence of reliable lineage establishment. The complete results, including origin and brain-size columns, are in `runs/policy_sweep_20260904/sweep.csv`.

## What the 50,000-second run showed

The run `ecosystem_20260902_211647387` began with 48 creatures and maintained a floor of 24. It produced 13 births, 6,693 immigrants, and 6,730 deaths. Ten parents were archive mutants and three were archive clones. None of the 13 naturally born creatures reproduced; only one survived to the 120-second maturity age.

Every birth happened about 120 seconds after its parent was introduced. This is the earliest allowed age. A parent needed to rise from 90 to 150 energy, then immediately paid 75. Its child began with 45. Twelve children died before maturity. The run therefore discovered occasional strategies that could cross the birth threshold, but did not establish multi-generation lineages.

The food system credited about 71,879 energy while immigration injected 602,370. Metabolism, movement, foraging, neural activity, and storm exposure consumed far more energy than food supplied. External support dominated the energy budget. Increasing the immigration rate would produce more trials but would not fix this reproductive bottleneck.

## First controlled experiment

Current defaults raise all food energy densities by 25%: grazing is 2.5, poor fruit 5, rich fruit 12.5, and pods 15 energy per biomass unit. The proportional change preserves the food-learning contrast and makes successful feeding provide a larger travel reserve. The archive energy gate rises proportionally from 30 to 37.5, preserving the amount of feeding required for admission. Keep these values fixed when comparing reproduction or mutation variants.

Use the `breeding` preset for a deliberately less brittle reproductive transition:

| Parameter | Baseline | Breeding preset | Reason |
|---|---:|---:|---|
| Maturity age | 120 s | 60 s | Lets viable offspring express reproduction before their small reserve is exhausted |
| Reproduction threshold | 150 | 130 | Requires a meaningful food surplus of 40 rather than 60 |
| Parent cost | 75 | 65 | Leaves a reproducing parent with at least 65 energy |
| Offspring energy | 50 | 60 | Gives children more time to locate food while retaining a positive birth overhead |
| Cooldown | 120 s | 90 s | Allows repeat success without rapid cost-free multiplication |

Run it with:

```powershell
.\scripts\ecosystem.ps1 -Build -Establishment -FounderBrain sparse-ancestor -EvolutionPreset breeding -Creatures 24 -Steps 120000 -Recording compact -Open
```

This is an experimental setting rather than a new claim about the right ecology. It deliberately leaves mutation at its baseline values so the reproductive transition is the only changed factor. Compare it with the baseline using the same seeds. Judge it in this order:

1. Number and fraction of naturally born creatures reaching maturity.
2. Number of distinct naturally born spiking creatures that reproduce.
3. Births per 100 immigrants and immigrants per 1,000 simulated seconds.
4. Food energy relative to external immigrant energy and operating costs.
5. Only then, total birth count and population size.

A run with many births but no breeding descendants is still failing at lineage continuity. A setting that obtains births by making food effectively unlimited has weakened selection too far.

## Multi-seed parameter sweep

The sweep tool runs one-factor comparisons with minimal recording:

```powershell
python tools/sweep_ecosystem.py --steps 20000 --seeds 7,11,19
```

It compares:

- `baseline`: current ecology and mutation defaults;
- `reproduction`: only the five reproduction settings above;
- `local-mutation`: only the mutation settings above;
- `lower-cost`: basal cost 0.16 and storm cost 0.60;
- `combined`: reproduction and local mutation changes.

Each result has an exact checkpoint and summary, while `sweep.csv` reports births before and after 100 seconds, parent class, mature offspring, breeding descendants, slight and strong immigration counts, archive waiting, evolved hidden-neuron counts, immigration demand, and wall time. Pass `--founder-brain random` only when you specifically want to compare against random founders. Use at least three seeds for screening and more seeds before adopting a change. If reproduction alone helps, keep normal metabolic pressure. If only lower-cost helps, vary basal and storm costs separately to find which constraint is excessive. Mutation effects need longer runs because they act through descendants and archive challengers.

In an initial three-seed, 1,200-second screen, the baseline median was zero births and zero mature offspring. The reproduction-only variant reached 0.83 births per 1,000 seconds and a median of one mature offspring. Mutation-only and combined variants had median zero births in the same short window. None produced a breeding descendant. This is enough to select reproduction-only for the next longer test, but far too little evidence to adopt the parameters permanently.

The executable exposes mutation magnitude and probability controls through `--help`, including weight, bias, threshold, position, background sensitivity, neuron mutation, hidden-neuron growth, reciprocal motifs, and synapse addition/removal.

## Methods to add after parameter calibration

Parameter tuning can create enough births for selection to act, but it cannot by itself solve weak or noisy credit assignment. The strongest next additions are:

1. **Lineage credit.** Credit an archived genome when its naturally born descendants mature and reproduce. Food efficiency remains the admission gate; descendant reproduction becomes the primary tie-breaker.
2. **Controlled archive reevaluation.** Re-run promising genomes in several fresh placements and seeds, both alone and with partners. Select using median performance or a lower confidence bound so one lucky food encounter cannot dominate.
3. **Scheduled challengers.** Introduce a small number of archive mutants even when population is at its floor. Current immigration occurs only after deaths, so a stable mediocre population can stop exploration.
4. **Ecological curriculum.** Begin with the breeding preset, then gradually restore the baseline maturity, thresholds, and storm cost after breeding descendants appear. Every transition should be logged and checkpointed.
5. **Reward-modulated plasticity.** The current spiking network has persistent neural state but no synaptic learning. Evolution can encode a fixed fruit preference, but within-life learning of which fruit is rich requires plastic synapses or another adaptive memory mechanism.

For creature interaction, first establish individual food competence. Then compare solo, paired-relative, paired-stranger, and non-cooperative pod controls with the same genomes. Pod access should be valuable enough to matter but should not be the only viable food source.

## Recording profiles

The PowerShell runner defaults to `-Recording compact`:

| Profile | Main history interval | Sensors | Brain graphs | Neural activity | Routine events |
|---|---:|---|---|---|---|
| `compact` | 50 s | No | No | No | No |
| `standard` | 1 s | Yes | Yes | No | No |
| `detailed` | 1 s | Yes | Yes | Yes | Yes |

The default `-DetailedTailSeconds 300` additionally creates a full-detail, one-second replay of only the last five simulated minutes. Set it to zero to omit the tail. After HTML generation, JSONL sources are gzip-compressed unless `-KeepJsonl` is supplied. Both plain and compressed sources can be reopened by `view_ecosystem.py`.

Compact history retains positions, actions, energy, diet totals, resources, weather, population statistics, birth/death/immigration milestones, and archive summaries. It omits per-creature sensor vectors, static neural graphs, membrane potentials, spikes per neuron, and high-frequency ingestion/digestion/pod-work events. Aggregate food, energy, pod, and spike totals remain in `ecosystem_stats.csv`.

The replay now charts the best archive score over time, and `ecosystem_stats.csv` records both best and median archive scores. A rising score indicates improvement under the partial-food heuristic; breeding-descendant metrics are still the stronger test of evolutionary progress.

Direct executable users can combine `--record-every`, `--record-brains`, `--record-observations`, `--record-brain-graphs`, `--record-routine-events`, `--detailed-tail-seconds`, and `--tail-record-every`.

To salvage an existing large recording without loading it into memory:

```powershell
python tools/compact_replay.py runs/ecosystem_20260902_211647387 --every 50 --output runs/ecosystem_20260902_211647387/ecosystem_compact_50s.jsonl.gz
python tools/view_ecosystem.py runs/ecosystem_20260902_211647387/ecosystem_compact_50s.jsonl.gz --output runs/ecosystem_20260902_211647387/ecosystem_compact_50s.html
```

The example keeps every fiftieth recorded frame plus the first and final frames, removes routine events and detailed neural/sensory data, and streams directly to gzip. It never deletes the original recording.

Sampling every tenth recorded frame on the 50,000-second run converted the 3.07 GB JSONL to 9.38 MB. Its standalone HTML became 98.74 MB instead of 3.07 GB. Sampling every fiftieth frame, as in the command above, is smaller again. The original files remain available until explicitly removed.
