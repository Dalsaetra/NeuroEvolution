# Predation, body mass, and diet

New `nursery-frontier` worlds enable predation by default. The generated habitat
and older checkpoints retain the previous rules. Archive establishment and
predation cannot be combined; predator evaluation is deliberately deferred.

```powershell
.\scripts\ecosystem.ps1 -Build -Habitat nursery-frontier -Steps 6000 -Recording standard -Open
```

Use `-NoPredation` for a new nursery control run. Initial ancestors are herbivores
with mass 1 and a silent, disconnected attack output. Predatory behavior must
evolve through neural mutation; the addition of an attack neuron does not make
the ancestral controller a hunter. Random controllers occasionally attack;
reactive controllers can forage plants or scavenge meat according to their diet,
but do not have a scripted pursuit strategy.

## Genome and inheritance

The ecosystem genome consists of the existing `Brain` and `BodyGenes`:

| Trait | Range | Default | Mutation |
| --- | --- | --- | --- |
| Body mass | 0.5–2 | 1 | Gaussian change to log mass, clamped to bounds |
| Carnivory | 0–1 | 0 | Additive Gaussian change, clamped to bounds |
| Attack behavior | Sixth motor neuron | Disconnected in sparse ancestor | Existing neuron, synapse, and structural mutations |

The existing birth draw selects 25% exact copies, 50% slight mutations, and 25%
strong mutations. Exact copies preserve both body traits and the brain. For each
body trait independently, the base mutation probability is 0.2; slight mutations
multiply probability by 0.65 and sigma by 0.45, while strong mutations multiply
probability by 2 (capped at 1) and sigma by 1.75. Base sigmas are 0.12 for log mass
and 0.08 for carnivory. Zero probability or sigma disables that trait's mutation.
Health, injury feedback, energy, and actuator state are not inherited.

Body traits share the creature's genome ID with its neural genome. Non-exact
births receive a new ID as before. Founder import copies body genes from a
predation checkpoint and resets health. Importing older founders adds unused
senses and a silent attack neuron, preserving existing synapses and indices;
their initial body traits come from the destination configuration.

The runner exposes `-FounderMass`, `-FounderCarnivory`,
`-MassMutationProbability`, `-MassMutationSigma`,
`-CarnivoryMutationProbability`, and `-CarnivoryMutationSigma`.
For example, `-FounderCarnivory 0.2` starts with 80% plant and 20% meat efficiency.
This changes digestion, not the ancestral brain's food-seeking behavior.

## Bodies, combat, and food

For mass `m`, maximum health is `20*m`, basal metabolism is
`basal_cost*m*(1-(1-carnivore_basal_fraction)*carnivory)`, and
maximum speed is `max_speed/sqrt(m)`. Collision radius remains fixed. Current
injury does not change mass, speed, or corpse yield. Energy capacity, ingestion,
attack strength, and turning retain their configured values independently of mass.

Outside shelter during a storm, energy drain is `storm_cost/m` per second.
Mass 2 takes half the baseline drain, mass 1 takes the baseline, and mass 0.5
takes double. Shelter still prevents storm drain entirely. Larger bodies therefore
trade higher basal metabolism and lower speed for greater storm resistance.
Worlds with body/predation mechanics disabled retain the original fixed storm cost.

`carnivore_basal_fraction` defaults to 0.5: at equal mass, 0%, 50%, and 100%
carnivory consume 100%, 75%, and 50% of the herbivore basal rate respectively.
Only basal maintenance receives this discount; movement, combat, healing, and
storm costs keep their existing rules. The fraction must remain in (0,1], so
even full carnivores have positive maintenance when `basal_cost` is positive.
Configure it with `--carnivore-basal-fraction`. Checkpoint version 21 persists it;
older checkpoints use 1 to preserve their previous metabolism on resume.

Attack effort `a` is a smoothed motor intensity in [0,1]. At full effort it costs
`attack_cost` energy/second (currently 2). Damage scales linearly with the
attacker's carnivory `c`:

```text
strength = attack_base_fraction + (1 - attack_base_fraction) * c
damage = attack_damage * strength * attack_effort * dt
```

The baseline fraction defaults to 0.25 and is configurable with
`--attack-base-fraction` in (0,1]. At the current `attack_damage=15`, full-effort
rates are 3.75, 9.375, and 15 damage/second at 0%, 50%, and 100% carnivory.
This multiplier changes damage only; herbivores still pay the same attack costs.
The target is the
nearest creature within 0.8 center-to-center units, a forward 60-degree cone,
and line of sight. Exact distance ties use a seeded ID hash. Misses still cost
energy; insufficient reserve scales effort and damage to what was actually paid.
All hits use post-movement geometry and apply simultaneously, allowing mutual
kills and avoiding vector-order priority. Herbivores can attack defensively.

Creatures inside the nursery are immune to attack damage. Attacks still activate
and consume energy, but hits on protected targets report zero damage and produce
no injury feedback. Protection uses the target's post-movement center position:
attacks from outside cannot hurt a creature inside, while targets outside remain
vulnerable even when the attacker is inside. Ordinary frontier shelters do not
provide this protection.

Health heals at 0.1 units/second, costing 2 energy per health restored. Healing
cannot exceed maximum health, use unavailable reserve, revive a dead creature,
or occur on a step in which damage was received. Death occurs at zero health or
zero energy. Combat deaths do not digest pending packets or forage after the hit.

Meat uses the same forage output, ingestion budget, proportional sharing, and
delayed digestive packets as plants. All four plant types have efficiency `1-c`;
meat has efficiency `c`, where `c` is carnivory. Meat consumption speed also scales
by `c`: requested biomass is `forage * ingestion_rate * dt * c`, before sharing
limited stock. A 1% carnivore therefore removes meat at 1% of the normal rate and
extracts 1% of that bite's energy. Plant consumption speed is unchanged. Both
foods retain the configured digestion delay (3 seconds by default), measured
after a bite is removed from the ground. Indigestible resources are skipped when
choosing a forage target. Intermediate diets are allowed; specialist populations
are not guaranteed.

Corpses become edible on the next step, have 20 energy per biomass unit, decay
by 0.02 biomass/second everywhere, and disappear when depleted. They never
regrow, relocate, or become nursery/shelter plant patches. Multiple corpses may
occupy a location without affecting creature collisions.

## Energy accounting

Each body stores `B = 30*m` construction energy, paid **in addition to** the
existing reproduction cost. The parent must afford the actual mutated child:

```text
parent debit = reproduction_cost + B_child
child receives = offspring_energy + B_child
reproductive loss = reproduction_cost - offspring_energy >= 0

corpse energy = 0.5 * (B_dead + max(remaining_reserve, 0))
```

Undigested packets are discarded on death. The other half of body/reserve energy
is lost. Health repair does not add construction energy to the body. Thus a
newborn's fully digestible corpse returns less energy than creating that newborn
cost, even before attack, maintenance, decay, and digestion losses. Starvation
still leaves structural meat because that body was already funded at birth.
Founder bodies are recorded as external energy inputs.

For the nursery defaults, a mass-1 birth costs 180 energy: 150 existing birth
cost plus 30 body construction. The child receives 60 reserve plus 30 body;
90 is reproductive overhead. An immediately dead child would leave at most
45 raw meat energy. A mass-2 birth costs 210, below the default 225 threshold.

Totals distinguish transfers (`body_construction`, `carcass_energy`) from losses
(`attacking`, `healing`, `meat_spoiled_energy`, `discarded_energy`) and external
input (`external_body_energy`). Do not add construction or carcass transfers as
losses in a conservation equation. `discarded_energy` includes digestive
inefficiency, overflow, pending packets lost on death, and unrecovered body energy.
Plant regrowth/relocation still supplies external food as before.

## Senses, replay, and persistence

Predation brains have 124 inputs and 6 outputs. The first 97 inputs and first 5
outputs retain their meanings. Appended five-sector groups are meat presence,
meat proximity, meat amount, other-creature mass, and other-creature health
fraction; the final two inputs are own health fraction and damage received in
the previous step as a fraction of maximum health. Every group participates in
category-balanced sensory mutation.

Meat has an independent nearest-visible target per sector, so plants cannot
hide it. Amount is normalized by a shared maximum-corpse reference, rather than
the capacity of the particular corpse. Other-creature mass and health refer to
the same creature as the original presence/proximity channels. Walls occlude
these senses. Own speed feedback uses the creature's mass-dependent speed limit.

Replay frames include body traits, health, damage, attack effort, and full
carcass descriptions so corpses created after the initial frame can be drawn.
The viewer shows meat, attack cones, body traits, and health; CSV statistics add
mass, carnivory, health, meat stock, and predation energy totals. Detailed events
include `attack_hit`, with attacker and victim IDs; `carcass` records recovered
energy, while the existing `death` event remains compatible with lifecycle tools.

Checkpoint version 16 stores these traits, state, settings, food counters, and
resource ID allocation. Versions 1–15 load with predation disabled for historical
continuation. Use `--founders` in a fresh predation world to migrate a population;
`--resume` preserves the saved rules. The executable exposes all balancing
parameters through the corresponding hyphenated flags; see `--help`.

Tests cover simultaneous and directional attacks, misses, walls, energy-limited
damage, timestep scaling, funded births with mutated mass, corpse recovery and
decay, dietary loss, sharing, healing, sensing, neural interface migration, and
exact checkpoint continuation. These establish mechanical correctness, not that
a stable predator–prey ecology will evolve at the initial parameter values.

Checkpoint version 20 persists `attack_base_fraction`. Older checkpoints load
with a fraction of 1 to preserve their diet-independent attack damage. New worlds
use the current default; importing old founders into a new world uses that world's
combat settings.
