# Inherited mutation scale

Each genome has a `mutation_scale` gene, initially 1.0 and bounded to 0.5–2.0. The parent's value chooses the offspring's brain/body mutation profile:

| Scale | Copy | Slight | Strong |
|---|---:|---:|---:|
| 0.5 | 75% | 25% | 0% |
| 1.0 | 50% | 45% | 5% |
| 2.0 | 0% | 50% | 50% |

Probabilities interpolate linearly between adjacent rows. Profile strengths and operator budgets are unchanged. Independent disconnected-neuron cleanup still applies, including to copy births.

After brain/body inheritance, the child inherits the parent's scale. Independently, with global probability `meta_mutation_probability` (default 0.10), it changes to `clamp(parent_scale * exp(Normal(0, meta_mutation_sigma)), 0.5, 2)`. Global sigma defaults to 0.10; zero probability or sigma disables gene changes. Log-space clamping avoids overflow. The child's scale affects its future offspring, not its own birth. A changed scale receives a new genome ID even on a brain/body copy.

`MutationConfig::meta_mutation_enabled` defaults to true for new runs. When enabled, these fixed knots replace the global copy/slight mixture. Set it false to use `copy_probability` and `slight_probability` as before and freeze the scale gene. This works independently of predation. CLI options: `--meta-mutation 0|1`, `--meta-mutation-probability X`, and `--meta-mutation-sigma X`.

Version 35 checkpoints preserve genes, controls, and RNG continuation. Older checkpoints load scale 1.0 with meta mutation disabled to preserve their reproduction policy. Importing starting genomes preserves their scale while using the new world's meta-mutation controls. New ancestors start at 1.0.

Replay creature records and the inspector expose the gene. Statistics CSV records population mean, minimum, and maximum scale.
