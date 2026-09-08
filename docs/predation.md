# Predation, mass, and diet

Predation is enabled in the default nursery. Initial sparse ancestors have mass 1, carnivory 0, and a disconnected attack motor. Brain topology and body traits are inherited; health, energy, injury feedback, and activity are lifetime state.

Tune body and combat rules in `EcosystemConfig` and inherited variation in `MutationConfig`, both in [config.hpp](../include/neuroevo/config.hpp).

## Bodies and combat

For mass `m`, maximum health is `health_per_mass * m`, speed is `max_speed / sqrt(m)`, basal metabolism is `basal_cost * m * (1 - (1 - carnivore_basal_fraction) * carnivory)`, and exposed storm drain is `storm_cost / m`. Collision radius remains fixed.

Attack damage per step is `attack_damage * (attack_base_fraction + (1 - attack_base_fraction) * carnivory) * effort * dt`. Attacks choose the nearest visible creature in the configured forward cone and range. Misses consume energy; insufficient energy reduces effort. Post-movement hits resolve simultaneously, including mutual kills. Nursery targets are protected; ordinary frontier shelters protect against storms only.

Healing consumes energy, respects the health cap, and cannot occur in a step where the creature received damage. Zero energy or zero health causes death.

## Diet and accounting

Plant digestion efficiency is `1 - carnivory`; meat efficiency is `carnivory`. Requested meat biomass also scales by carnivory. Forage effort, available stock, sharing, ingestion limits, and delayed digestion still apply. Exposed creatures cannot harvest during storms.

A birth debits the parent by `reproduction_cost + body_energy_per_mass * child_mass`. The child receives `offspring_energy` reserve plus its funded body. For current mass-1 defaults this costs 90: 60 reserve and 30 body construction. The parent must afford the actual mutated child's mass.

Corpse energy is `carcass_recovery * (body_energy_per_mass * mass + max(remaining_reserve, 0))`. Recovery is strictly below one. Pending digestion is discarded. Corpses become available on the following step, decay everywhere, and disappear when depleted. They do not regrow or relocate.

`body_construction` and `carcass_energy` track transfers; attack, healing, spoilage, and discarded energy track costs/losses. Founder bodies enter through `external_body_energy`. Reproduction and predation cannot create energy through an unfunded body.

Replays include body traits, health, damage, attack effort, and carcasses. Tests exercise direction/range/occlusion, simultaneous hits, protection, paid effort, dietary sharing, healing, body-funded births, corpse recovery, and exact continuation.
