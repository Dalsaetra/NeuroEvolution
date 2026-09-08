# Mutation and inheritance

All mutation tuning lives in `MutationConfig` in [config.hpp](../include/neuroevo/config.hpp), accessed through `EcosystemConfig::mutation`. The runtime has one local-edit policy.

## Birth mixture

A single draw selects copies, slight mutations, or strong mutations. Defaults are 50%, 45%, and 5%; the strong fraction is `1 - copy_probability - slight_probability`. Brain and body use the same choice. Independent disconnected-neuron cleanup runs afterward with its own probability, currently 25%, including on copies.

Copies preserve genome identity unless cleanup changes the brain. Mutated offspring receive a new genome ID. The parent is unchanged. Neural activity, delayed currents, and motor traces reset for the child's lifetime.

## Profiles and structural edits

`slight` and `strong` profiles scale base sigmas and operator weights and select a structural budget or a bounded local edit batch. Slight defaults allow one structural attempt with probability 0.30 or up to two local edits; strong defaults use 0.50 or up to four edits. Limits are independent of genome size.

Enabled add/remove pairs are balanced by default. `allow_birth_motifs` defaults to false because motif addition is unpaired growth. Zero operator weights remain zero. Impossible operations consume their attempt without a fallback. Direct `Brain::mutate` calls use direct edit controls; births derive those controls from the selected profile.

Structural operations add/remove a synapse, add/remove a hidden neuron, or rewire an existing connection. A new neuron is a side branch that keeps the original connection. Removal never deletes sensory or motor slots. Rewiring changes one endpoint, preserves signed weight, recalculates delay, and rejects duplicates/self-loops. New connections prioritize disconnected hidden endpoints and otherwise use category-balanced sensory sampling.

Local batches select one parameter family, favoring weights. Weight perturbations are capped relative to connection strength. Neuron edits change thresholds, bias, background sensitivity, or position; sensory threshold edits select categories before individual inputs. Position changes update conduction delays. Hidden bias stays below its isolated self-spiking bound.

## Body and cleanup

Mass mutates in log space within 0.5–2; carnivory mutates additively within 0–1. Each has a base probability and sigma. The selected profile supplies body probability and sigma multipliers; probabilities are capped at one. Zero probability or sigma disables that trait's mutation.

Independent cleanup removes at most one hidden neuron missing incoming or outgoing edges. It does not cascade. To freeze all inheritance, select copies with probability one and disable cleanup.

Checkpoints preserve the complete base configuration, both profiles, and birth mixture. Resume uses saved tuning. No offspring scoring or selection trial runs between parent and child.
