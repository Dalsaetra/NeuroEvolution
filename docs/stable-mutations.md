# Budgeted ecological mutations

Natural reproduction uses 50% exact genome copies, 45% slight mutations and 5%
strong mutations. Each mutated child receives either one structural operation
or a bounded batch of local parameter edits. The parent is never modified.

| Preset | Structural attempt | Otherwise | Weight-change cap |
| --- | --- | --- | --- |
| Slight | 25% | Up to 2 local edits | `0.1 * abs(weight) + 0.01` |
| Strong | 35% | Up to 4 local edits | Twice the slight cap |

These controls are derived in `src/ecosystem_mutation.hpp` when mutation is
applied; they are not additional saved world configuration. Both ecosystem modes
and archive mutation presets use this bounded policy, including after resume.
The generic Brain stable/legacy policies and separate NEAT workflow remain
available outside these presets. The saved `mutation.stable` field does not
switch ecosystem offspring back to unbounded strong mutation.

## Structural selection

The structural budget is independent of the sum of operator weights. Each
preset averages the configured add/remove weights within each enabled pair,
so adding and removing have equal selection probability. Explicit zero settings
remain disabled and can intentionally break that symmetry. The unpaired
reciprocal-motif operator is disabled in ecosystem presets.

With the current ecosystem weights (0.40 for each synapse operation and 0.12
for each neuron operation), conditional on choosing a structural attempt:

- Add one synapse: 38.46%.
- Remove one synapse: 38.46%.
- Add one hidden neuron: 11.54%.
- Remove one hidden neuron: 11.54%.

Unavailable operations are no-ops, not redistributed to the opposite operation.
For example, an add-neuron attempt at the hidden-neuron limit does not increase
pruning probability. Removing a neuron deletes all incident edges, whereas
adding a neuron adds two edges; equal event probabilities do not guarantee equal
edge-count changes. Input and output neurons cannot be removed.

New neurons form a side branch while retaining the original edge. New edges
and branch outputs start at magnitude at most 0.5 at synaptic gain 32, scaled
inversely with gain and capped at 6. Branch inputs retain inherited drive; new
branch neurons start with zero background sensitivity.

New sensory edges sample uniformly across available categories, then available
neurons within the category, then legal destinations. Vision channels span
sectors, hearing and contact span directions, and body cues are singleton
categories. Storm cue and all shelter-proximity sectors combined therefore have
equal sensory-source probability when both can connect. Fully occupied sensory
categories are excluded by connection search. This does not redistribute the
structural operation into deletion. Existing edges and random initialization
are unchanged.

## Parameter edits

A batch edits either weights or neuron parameters. Weight selection receives
four times the configured weight probability relative to neuron probability.
With defaults 0.22 and 0.16, about 84.6% of parameter batches edit weights only.
Across all mutated children, weight-only batches are approximately 63.5% for
slight mutation and 55% for strong mutation; structural attempts are 25%/35%.
These rates assume available edges, neurons and default nonzero settings.

Each edit opportunity occurs with probability
`min(1, 8 * (4 * weight_probability + neuron_probability))`, after excluding
unavailable parameter families. An element may be selected repeatedly. Setting
both parameter probabilities to zero disables all parameter edits.

Weight perturbation sigma is the smaller of the configured sigma and the cap
in the table. Slight sigmas are scaled by 0.45; strong sigmas by 1.75. This keeps
weak pathways protected while allowing zero-weight edges to become active.

Neuron edits select threshold (40%), bias (30%), background sensitivity (25%),
or one position axis (5%). Output neurons substitute background sensitivity
for bias and threshold for position edits. Within non-weight parameter batches, 25% of edit opportunities select a
sensory threshold instead. These sample a category uniformly, then a neuron
within that category. Sigma is capped at 5% of its current threshold (and by
`threshold_sigma`), with thresholds clamped to 0.2–5. Each threshold change uses
one local edit; zero neuron-mutation probability disables sensory edits too.
In calibrated mode, a lower threshold produces a higher sensory firing rate. Threshold changes can also clamp hidden bias to its subthreshold
bound. Position edits recalculate delays; other parameter edits preserve them.
Runtime adjacency and buffers are rebuilt after mutation.

No fitness screening, twin births or evaluation-based offspring rejection is
introduced by this policy. Checkpoints retain the base mutation weights and
future mutations use the current derived presets. Older checkpoints without
neuron pruning retain a zero neuron-removal weight until explicitly changed.
