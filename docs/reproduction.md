# Funded reproduction

New predation runs default to funded_reproduction. Each genome has reproduction_allocation (founders: 0.5). Digested, diet-adjusted income is split between survival and reproduction. Investment begins only at maturity and after cooldown; no energy is taken from existing survival reserves.

The first investment mutates and freezes one pending offspring genome. Its actual mass determines the funding target: reproduction_cost + body_energy_per_mass * child_mass. The allocation gene uses the existing copy/slight/strong choice; on non-copy branches it mutates with allocation_mutation_probability (0.2) and allocation_mutation_sigma (0.1), scaled by the branch's body mutation profile and clamped to [0,1].

Once funded, births happen automatically when space and population capacity permit. Failed placement keeps the same pending child and reserve. Birth spends that reserve, leaves survival energy unchanged, and starts cooldown. All income during cooldown, before maturity, or beyond the funding target goes to survival; survival overflow is discarded. Reproductive energy cannot fund metabolism, healing, or attacks.

Death produces one corpse from the parent's body energy plus survival and reproductive reserves, multiplied by carcass_recovery. The unfunded portion of a pending child's cost contributes nothing. Pending digestive packets remain discarded as before.

The new self-sensors are reproduction_progress (reserve / target, zero without gestation) and reproduction_cooldown (remaining cooldown / duration). They start disconnected in the sparse ancestor. There is no birth motor.

Replays contain only allocation, reserve, target, cooldown and the two bars, never unborn brain graphs. Checkpoints store at most one current pending genome per living creature, required for exact continuation. This is a bounded snapshot, not a history of unborn genomes. Starting-genome imports copy the allocation gene, not the gestation or reserves.

Old checkpoints retain the old threshold policy and old sensor layout. Set funded_reproduction=false for new controlled runs using the old policy. The old reproduction_threshold applies only to that mode. CLI: --funded-reproduction 0|1, --reproduction-allocation X, --allocation-mutation-probability X, --allocation-mutation-sigma X.
