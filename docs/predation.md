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

Meat loses biomass at `nursery_meat_decay` per second inside the nursery and `meat_decay` per second outside, including frontier shelters. Both default to `0.0000001`; zero disables decay in that region. CLI overrides are `--nursery-meat-decay` and `--meat-decay`. Spoilage removes the corresponding stored energy and never replenishes a corpse. Checkpoints preserve both settings; supported older checkpoints retain their original rate in both regions.

Replays include body traits, health, damage, attack effort, and carcasses. Tests exercise direction/range/occlusion, simultaneous hits, protection, paid effort, dietary sharing, healing, body-funded births, corpse recovery, regional decay, and exact continuation.

## Food vision

`vision_N_food_proximity` reports the nearest visible plant or meat in each sector, independent of diet. The sparse ancestor uses this signal for steering and foraging. `vision_N_plant_proximity` preserves the former plant-only behavior as a separate evolvable input group, initially disconnected in the ancestor. Dedicated meat presence, proximity, and amount inputs remain unchanged. Existing food presence, stock, and plant-type inputs still describe plants. Empty resources and refilling pods retain their existing depleted-food behavior.

The predation interface now has 86 inputs. Version 27 checkpoints preserve this layout; older predation checkpoints require the previous build, or a fresh simulation with this build. Non-predation checkpoints retain their existing interface.

Graze and fruit wait `nursery_food_respawn_delay` seconds inside the nursery or `outdoor_food_respawn_delay` seconds outside after depletion, including depletion by decay. Both default to zero. The delay gates relocation/refill or regrowth, according to the existing food policy; weather restrictions still apply. Outside shelters use the outside delay. Pods and meat are unaffected. CLI options are `--nursery-food-respawn-delay` and `--outdoor-food-respawn-delay`. Version 28 checkpoints retain pending deadlines; older supported checkpoints load with zero delays.

Passive graze, fruit, nursery-food, and shelter-food regrowth defaults to zero. A positive rate adds biomass per second up to capacity, including when relocation is enabled, after any depletion cooldown. Decay still subtracts biomass and existing storm restrictions apply. Relocation refills depleted patches independently of regrowth. Pod regrowth remains 0.08 because it drives the pod refill cycle. Existing checkpoints retain their saved rates; positive rates now apply alongside relocation.

Set `storm_health_damage = true` to replace exposed storm energy drain with `storm_damage * dt / mass` health damage. The switch defaults to false; `storm_damage` defaults to 5 health/second at mass 1 and allows zero. Nursery and ordinary shelter protection still apply. Health mode requires predation/body mechanics. Damage triggers injury feedback, blocks healing for that step, and can kill; storm deaths produce normal corpses but do not count as predation kills. CLI options are `--storm-health-damage 0|1` and `--storm-damage X`. Checkpoints preserve both settings; older supported checkpoints retain energy-drain mode.
