# Predation, mass, and diet

Predation is enabled in the default nursery. Initial sparse ancestors use the configured founder mass and carnivory and a template with a disconnected attack motor. With `mutate_initial_ancestors` enabled (the default), each founder independently receives one strong mutation pass affecting brain and body genes before the run starts. Brain topology and body traits are inherited; health, energy, injury feedback, and activity are lifetime state.

Tune body and combat rules in `EcosystemConfig` and inherited variation in `MutationConfig`, both in [config.hpp](../include/neuroevo/config.hpp).

## Bodies and combat

New runs use mass allometry (`mass_allometry = true`), with mass 1 as the reference body. The default mass range remains 0.5 to 2. Each exponent is configurable in `EcosystemConfig` or via the corresponding CLI flag (replace underscores with hyphens).

| Mechanic | Multiplier | Exponent setting |
| --- | --- | --- |
| Bite/harvesting rate | `m^0.8` | `ingestion_mass_exponent` |
| Pod-opening strength | `m^(2/3)` | `pod_mass_exponent` |
| Attack damage | `m^(2/3)` | `attack_mass_exponent` |
| Basal maintenance | `m^0.75` | `metabolism_mass_exponent` |
| Survival reserve capacity | `m^1` | `energy_mass_exponent` |
| Maximum speed | `m^0.25` | `speed_mass_exponent` |
| Acceleration and braking | `m^-0.5` | `acceleration_mass_exponent` |

Maximum health remains `health_per_mass * m`. Basal metabolism is `basal_cost * m^metabolism_mass_exponent * (1 - (1 - carnivore_basal_fraction) * carnivory)`. With linear reserves, basal fasting endurance at equal diet scales as `m^0.25`; movement, neural costs, injury, and other expenses change actual survival time. Collision radius remains fixed. The historical energy-drain storm mode still uses `storm_cost / m`.

Attack damage per step is `attack_damage * m^attack_mass_exponent * (attack_base_fraction + (1 - attack_base_fraction) * carnivory) * effort * dt`. Thus body strength multiplies the existing carnivory advantage. Attacks choose the nearest visible creature in the configured forward cone and range. Misses consume energy; insufficient energy reduces effort. Post-movement hits resolve simultaneously, including mutual kills. Nursery targets are always protected.

Closed pods retain their cooperative opening rule: `min(2, sum(effort))^2`, multiplied by the effort-weighted mean of each worker's `m^pod_mass_exponent`. This gives a lone creature the configured mass exponent without squaring it or losing the benefit at the cooperation cap. Existing dietary eligibility is preserved: pure carnivores ignore pods, while plant-capable diets can open them. Plant digestion still decreases with carnivory.

Movement approaches the motor's target speed at at most `max_acceleration * m^acceleration_mass_exponent` per second, including braking. `max_acceleration` defaults to 3 at mass 1. Existing forage slowdown still reduces the target speed. Small creatures accelerate faster; large ones reach a higher top speed. This uses the already-saved displacement speed once per world step, with no extra neural work or new collision queries. Heading remains controlled by the existing turn motor; this is a scalar speed limit, not a full inertia model.

Checkpoint format 40 saves all exponents and acceleration. Supported older checkpoints load with `mass_allometry = false`, preserving fixed bite rates, mass-independent pod/combat strength, linear maintenance, inverse-square-root speed, immediate speed changes, and their saved reserve policy. `--mass-allometry 0` selects these historical rules for a fresh run. Genome imports into a fresh run use its new settings. Resuming uses checkpoint settings.

`shelter_predation_damage` defaults to `true`, allowing predation damage in ordinary shelters. Set it to `false`, or pass `--shelter-predation-damage 0`, to protect sheltered targets; `--shelter-predation-damage 1` enables damage again. Protection checks the target's position after movement, regardless of where the attacker is. Attacks still consume energy when the target is protected. Shelter storm protection and nursery immunity are unchanged. Version 32 checkpoints preserve this setting; older supported checkpoints retain enabled damage in ordinary shelters.

Healing consumes energy, respects the health cap, and cannot occur in a step where the creature received damage. Zero energy or zero health causes death.

## Diet and accounting

Plant digestion efficiency is `1 - carnivory`; meat efficiency is `carnivory`. Bite rate is `ingestion_rate * m^ingestion_mass_exponent`. Requested meat biomass also scales by carnivory. Forage effort, available stock, proportional sharing, nursery settling, and delayed digestion still apply. Ingestion and digestion feedback are normalized to the individual's bite rate. With ramped storms, exposed harvesting falls linearly to 50% effectiveness at the midpoint and returns to 100% at the end. Historical flat storms retain the full harvesting cutoff.

A birth costs `reproduction_cost + body_energy_per_mass * child_mass`. New runs gradually fund this from digested income in a separate reproductive reserve, with an evolved allocation fraction defaulting to 50%. The child is mutated when investment begins, so its actual mass determines the target. Birth spends the reproductive reserve; the child receives `offspring_energy` plus its funded body. Historical checkpoints retain the immediate survival-reserve debit. See [funded reproduction](reproduction.md).

Corpse energy is `carcass_recovery * (body_energy_per_mass * mass + max(remaining_survival_reserve, 0) + reproductive_reserve)`. Recovery is strictly below one. Pending digestion is discarded. The pending offspring adds no separate body or corpse. Corpses become available on the following step, decay everywhere, and disappear when depleted. They do not regrow or relocate.

`body_construction` and `carcass_energy` track transfers; attack, healing, spoilage, and discarded energy track costs/losses. Founder bodies enter through `external_body_energy`. Reproduction and predation cannot create energy through an unfunded body.

Meat loses biomass at `nursery_meat_decay` per second inside the nursery and `meat_decay` per second outside, including frontier shelters. Both default to `0.0000001`; zero disables decay in that region. CLI overrides are `--nursery-meat-decay` and `--meat-decay`. Spoilage removes the corresponding stored energy and never replenishes a corpse. Checkpoints preserve both settings; supported older checkpoints retain their original rate in both regions.

Replays include body traits, health, damage, attack effort, and carcasses. Tests exercise direction/range/occlusion, simultaneous hits, protection, paid effort, dietary sharing, healing, body-funded births, corpse recovery, regional decay, and exact continuation.

## Food vision

`vision_N_food_proximity` reports the nearest visible plant or meat in each sector, independent of diet. The sparse ancestor uses this signal for steering and foraging. `vision_N_plant_proximity` preserves the former plant-only behavior as a separate evolvable input group, initially disconnected in the ancestor. Dedicated meat presence, proximity, and amount inputs remain unchanged. Existing food presence, stock, and plant-type inputs still describe plants. Empty resources and refilling pods retain their existing depleted-food behavior.

The predation interface has 91 inputs, including reproductive progress, cooldown, and three directional carnivory-total sensors. Each carnivory sensor sums all visible creatures' carnivory in its sector and encodes the total S as S / (1 + S). Version 41 records this layout. Supported older checkpoints retain their 86- or 88-input layout and saved reproduction mode; importing their genomes into a new run appends the missing disconnected inputs. Non-predation checkpoints retain their existing interface.

Graze and fruit wait `nursery_food_respawn_delay` seconds inside the nursery or `outdoor_food_respawn_delay` seconds outside after depletion, including depletion by decay. Both default to zero. The delay gates relocation/refill or regrowth, according to the existing food policy; weather restrictions still apply. Outside shelters use the outside delay. Pods and meat are unaffected. CLI options are `--nursery-food-respawn-delay` and `--outdoor-food-respawn-delay`. Version 28 checkpoints retain pending deadlines; older supported checkpoints load with zero delays.

Passive graze, fruit, nursery-food, and shelter-food regrowth defaults to zero. A positive rate adds biomass per second up to capacity, including when relocation is enabled, after any depletion cooldown. Decay still subtracts biomass and existing storm restrictions apply. Relocation refills depleted patches independently of regrowth. Pod regrowth remains 0.08 because it drives the pod refill cycle. Existing checkpoints retain their saved rates; positive rates now apply alongside relocation.

Set `storm_health_damage = true` to enable `storm_damage * intensity * dt` health damage, independent of mass. Health capacity remains `health_per_mass * mass`. Health mode defaults to true; `storm_damage` defaults to 0.35 peak health/second and allows zero. Nursery and ordinary shelter protection still apply. Health mode requires predation/body mechanics. Damage triggers injury feedback, blocks healing for that step, and can kill; storm deaths produce normal corpses but do not count as predation kills. Enable energy drain independently with `storm_energy_drain = true` (`--storm-energy-drain 1`); both drains can run together and use the same storm intensity. Energy drain uses `storm_cost` (`--storm-cost`) and retains inverse-mass scaling. CLI health options are `--storm-health-damage 0|1` and `--storm-damage X`. With `storm_ramp = true`, intensity rises linearly from zero to one at the storm midpoint, then falls to zero. See [background food and gradual storms](background-food-and-weather.md) for harvesting, integrated damage, and checkpoint compatibility. Historical flat storms use intensity one; their mass-independent damage rate gives survival time proportional to body mass while exposure continues.

With `mass_scaled_energy_capacity` and `mass_allometry` enabled (the defaults for new runs), survival reserve capacity is `energy_capacity * mass^energy_mass_exponent`, or `250 * mass` by default. Disabling mass-scaled capacity gives fixed reserves. With mass allometry disabled but mass-scaled capacity enabled, the historical formula is `energy_capacity * (reproduction_cost + body_energy_per_mass * mass) / (reproduction_cost + body_energy_per_mass)`. Actual birth cost still depends on the child's mass. The reproduction threshold applies only to legacy reproduction. Capacity creates no energy: digestion fills it and discards overflow. Energy sensors and replay bars use individual capacity. Older checkpoints retain their saved policy.
