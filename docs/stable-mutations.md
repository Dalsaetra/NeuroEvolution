# Stable ecological mutations

New ecosystems default to stable mutations. Natural reproduction still produces
25% exact genome copies, 50% children using slight mutations, and 25% using
strong mutations. Strong mutations use the broad legacy operator, even when
the world uses stable mutations for slight offspring.
Stable mutation gives each mutated child either one structural operation or at
most two local parameter edits, independent of genome size. The parent is never
modified. This limits disruption; it does not guarantee unchanged behavior in a
recurrent spiking circuit.

Structural operator probabilities are added, capped at one for the chance of
choosing structural mutation, then used as relative weights to select one
operation. With the current slight defaults this is a 44.85% structural chance
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
- Synapse pruning selects one random edge (base probability 0.04).
- Neuron pruning selects one random hidden neuron and deletes its incoming and
  outgoing edges, including recurrent/self connections (base probability 0.01).
  Inputs and outputs are protected; the last hidden neuron can be removed.
  Surviving neuron order, connection weights and delays are preserved, with
  endpoints remapped to the compacted neuron indices. Runtime buffers are rebuilt.
  This is one structural operation, even when it removes several incident edges.
- Slight mutations scale both pruning probabilities by 0.65: 2.6% synapse
  removal and 0.65% hidden-neuron removal per mutated child when available at
  default settings. Including both slight and strong offspring, the default removal-operation
  probabilities are 3.8% for synapses and 0.825% for hidden neurons per birth
  when eligible (neuron removal also deletes its incident synapses). These are conservative starting rates, not empirically tuned
  optima; neuron removal is rarer because it can disrupt multiple pathways.
  Useful edges and hidden neurons are not permanently locked.

Strong archive immigrants use the broad mutation operator for exploration,
with 10% synapse removal and 2% neuron removal at default settings. Generic
brain mutations also support the new 1% neuron pruning operator. The separate
NEAT workflow is unchanged (connection toggling, no physical neuron pruning).
No offspring screening or offline evaluation audit is added.

Use `-MutationMode stable` or `-MutationMode legacy` in `scripts/ecosystem.ps1`,
or `--stable-mutations 1|0` in the executable. An explicit setting can be used
with resume and affects future mutations; it does not rewire living or archived
brains. Without an explicit override, resume retains the saved policy.
Ecosystem checkpoint version 8 stores this choice. Versions 1–7 load with legacy
mutation to preserve their continuation behavior. The summary records the choice
in `mutation.stable`.

Checkpoint version 13 also stores `mutation.remove_neuron_probability` and the
summary reports both pruning rates. Versions 1–12 load with neuron pruning
disabled to preserve continuation behavior. Use `--mutate-remove-neuron-prob
0.01` to enable it explicitly on resume, or `0` to disable it. The same option
configures new worlds. `--mutate-remove-synapse-prob` configures new-world edge
pruning and retains its existing default of 0.04.

Both stable and legacy worlds use the 25% copy / 50% slight / 25% strong birth
split, including resumed worlds. Resuming an older run therefore adopts the new
birth split for future offspring. Explicitly zero mutation probabilities remain
disabled in both slight and strong mutations.
