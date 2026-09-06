# Mutation strategy review

The implemented offspring mix is 50% exact copies, 45% slight mutations and 5%
strong mutations. This applies to new and resumed worlds and to both brain and
body inheritance. Archive immigration keeps its separate mix. These proportions
are a conservative starting hypothesis, not an empirically established optimum.

Current ecosystem pruning overrides are 0.40 for synapses and 0.12 for neurons.
These weights are now normalized within an independent 25% slight / 35% strong
structural budget. Enabled add/remove pairs are balanced; the unpaired motif
operator is disabled. Parameter batches favor weights and contain at most two
slight or four strong local edits.
The generic MutationConfig's 0.04/0.01 pruning defaults do not describe ecosystems.

## Review recommendations and implementation status

1. Implemented: separate the structural-edit budget from operator weights. Start slight
   offspring at 25–35% structural operations, with the remainder doing one or two
   small parameter edits. Choose growth versus pruning within that budget from
   measured complexity and survival trends. Simply equalizing add/remove event
   rates does not balance graph size: removing a neuron deletes many edges.
2. Implemented: bound strong mutations to four local edits or one structural
   operation, independent of brain size. Previously they used per-element edits.
   Keep a small probability of broader changes for exploration.
3. Bias some new connections toward hidden neurons that already reach a motor
   output, retaining an unrestricted portion for discovering new circuits.
   Category-balanced source selection helps exposure but does not ensure the
   destination can influence behavior. Reachability is only a proxy for utility.
4. Calibrate weak growth using measured activity and behavior. A new pathway
   should sometimes influence the output without overwhelming the parent circuit.
   The current weak-edge magnitude is 0.15 at gain 32; that number alone does not
   establish that a spiking pathway is functional. Test several strengths against
   the same sensory histories, including rare storm and contact events.
5. Try small coordinated motifs or duplication of a functional subcircuit, with
   weak outgoing coupling. This can reduce the number of independently lucky
   mutations needed for a new behavior. It can also increase neural costs and
   disturb timing, so it is not behavior-neutral by construction.

Safe-mutation research motivates measuring behavioral sensitivity rather than
assuming equal parameter perturbations have equal effects:
[Lehman et al., Safe Mutations](https://arxiv.org/abs/1712.06563).
Its gradient method is not directly implemented for this spiking simulator;
matched-input probes or finite perturbations would be possible adaptations.

## Propagation and optional twins

Every child starts from the parent's whole genome, including previous beneficial
mutations. Copies preserve that genome exactly; slight mutations usually retain
most of it. Preservation does not guarantee survival or future reproduction.

An optional two-child birth could create one exact copy and one mutant drawn
90% slight / 10% strong, preserving the 50/45/5 mix in expectation across the pair.
Charge each child's full cost, require room for both plus a parent energy reserve,
and give other eligible parents their first child before allocating second slots.
This avoids using scarce capacity exclusively for a high-energy lineage.

Twins are not implemented. At baseline, two births cost 150 of the maximum 200
energy. With predation enabled, two mass-1 children cost 210 including body
construction, so they cannot fit within that capacity even without a reserve.
Any twin experiment therefore needs an explicit energy-budget design. A shorter
cooldown conditional on recovered energy is another experiment, with its own
population and food-competition effects.

## Measure mutation quality before claiming better rates

Compare mutated and exact-copy offspring from the same diverse set of parents,
starting with equal newborn reserves in matched environments and random seeds.
Report by operator: topology no-ops, behavioral change, survival to maturity,
food gained versus energy spent, offspring, and reproducing descendants. Include
rare events and interactions; a short quiet replay cannot establish fitness.

Start with hundreds of candidates across multiple parents and conditions, then
validate promising settings on independent long ecosystem runs. Repeat trials
to distinguish mutation effects from noise and lucky spawning. No finite small
sample can guarantee a beneficial-mutation rate or rule out extremely rare gains.
Current evaluate_newborn rejects predation worlds, so it must be extended or
replaced for those comparisons. Keep diagnostic evaluation separate from live
offspring selection unless deliberate fitness screening is desired.

## Three-sector sensory review

Vision now has three sectors spanning the same cone, reducing normal input count
97 to 65 and predation inputs 124 to 82. The ancestor also shrinks from seven to
five hidden neurons. Category sampling remains uniform: this raises the chance
of a specific direction within a category from one fifth to one third, not the
chance of selecting the category itself. The tradeoff is coarser steering and
more objects competing for the nearest-object slot in each sector.

Other potential obstacles to useful mutations, for future experiments:

- Food type and food proximity are separate signals, and only the nearest plant
  per sector is encoded. A preference such as approaching one fruit type may
  need a circuit that combines both. Type-specific proximity signals could make
  useful behavior accessible with fewer mutations, at the cost of more inputs.
- Very weak new edges may have no observable effect in a thresholded spiking
  circuit. Test whether growth produces small motor-rate changes, including rare
  sensory events, before increasing strengths globally.
- New edges can target dormant hidden neurons that cannot influence a motor.
  A mixture of motor-reachable and unrestricted targets could reduce dead ends.
- The budgeted mutation policy never edits input thresholds, although calibrated
  input thresholds control sensory firing rate. Small bounded category-level gain
  mutations could make useful sensitivity changes easier to discover.
- The current local presets have been changed to 40%/60% structural attempts and
  5/10 local edits, exceeding the earlier conservative budgets. These changes
  were preserved during the sector reduction; the older budget regression test
  now fails. This remains an independent source of circuit disruption to assess.

Fewer sensors alone does not establish a higher beneficial-mutation rate. Compare
lineage survival and reproducing descendants across multiple seeds and conditions.
