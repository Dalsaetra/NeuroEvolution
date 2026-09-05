# Stable ecological mutations

New ecosystems default to stable mutations. Natural reproduction still produces
50% exact genome copies and 50% children using the slight-mutation settings.
Stable mutation gives each mutated child either one structural operation or at
most two local parameter edits, independent of genome size. The parent is never
modified. This limits disruption; it does not guarantee unchanged behavior in a
recurrent spiking circuit.

Structural operator probabilities are added, capped at one for the chance of
choosing structural mutation, then used as relative weights to select one
operation. With the current slight defaults this is a 44.2% structural chance
among mutated children. Unavailable growth/motif/removal operations are excluded.
A failed connection search can leave the topology unchanged.

Otherwise, each of two parameter-edit opportunities occurs with probability
`min(1, 8 * (weight_probability + neuron_probability))`. Those two probabilities
also determine the relative choice of weight versus neuron edits. A uniformly
selected edge or non-input neuron is edited; an element may be chosen twice.
Zero probabilities disable the corresponding operators.

- Weight perturbation uses standard deviation
  `min(weight_sigma, 0.1 * abs(weight) + 0.01)`, protecting weak pathways while
  allowing zero-weight connections to become active.
- A hidden-neuron edit chooses threshold (40%), bias (30%), background
  sensitivity (25%), or one position axis (5%). Output neurons substitute noise
  sensitivity for bias edits and threshold for position edits. Input neurons
  are not edited by this policy. Threshold edits also clamp hidden bias when
  necessary to retain the existing subthreshold safety bound.
- Position changes recalculate delays; other parameter edits preserve delays.
- New edges and reciprocal motifs start at magnitude 0.15 at synaptic gain 32,
  scaled inversely with gain and capped at 6.
- New neurons retain the original edge. The new branch output is capped at that
  weak magnitude, while its input retains the inherited drive. New branch
  neurons start with zero background sensitivity.
- Removal still selects a random edge. Useful edges are not permanently locked.

Strong archive immigrants retain the previous broad mutation operator for
exploration. The separate generic brain and NEAT workflows retain their defaults.
No offspring screening or offline evaluation audit is added.

Use `-MutationMode stable` or `-MutationMode legacy` in `scripts/ecosystem.ps1`,
or `--stable-mutations 1|0` in the executable. An explicit setting can be used
with resume and affects future mutations; it does not rewire living or archived
brains. Without an explicit override, resume retains the saved policy.
Ecosystem checkpoint version 8 stores this choice. Versions 1–7 load with legacy
mutation to preserve their continuation behavior. The summary records the choice
in `mutation.stable`.
