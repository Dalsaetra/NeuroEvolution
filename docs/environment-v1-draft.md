# Environment v1 — discussion draft

Status: proposed rules for agreement before implementation. Numerical values are starting hypotheses, not calibrated or measured results. The existing food-seeking experiments remain available as benchmarks.

## 1. Purpose and intended experience

Create a shared, continuous 2D world where creatures must acquire energy, survive recurring harsh weather, and accumulate enough energy to reproduce. The first behavioral questions are:

1. Can creatures exploit or respond to other creatures when acquiring food?
2. Can mutually beneficial behavior emerge around a shared resource?
3. Can creatures change food choices after experiencing their consequences?

A representative episode: a creature samples two fruit types, subsequently favors the more nutritious one, approaches a pod another creature is opening, helps or waits nearby, competes for the exposed food, and returns to shelter before its energy reserve becomes dangerously low.

These are behaviors to investigate, not scripted goals or guaranteed outcomes. A modest solitary grazer should have a viable niche. Efficient social foraging should have the potential to support more offspring.

## 2. World and terrain

Proposed default: a bounded 48 × 48 world, represented by a 48 × 48 terrain grid. Creatures have continuous positions and headings; they do not jump between cells. One cell is one world unit. World size and population are configurable.

| Terrain | Movement | Vision | Ecological role |
| --- | --- | --- | --- |
| Open ground | Normal | Clear | Grazing and fruit patches |
| Rough ground | Higher movement energy cost | Clear | Makes routes differ in cost |
| Rock/wall | Impassable | Blocks sight | Creates corners, occlusion, and alternate routes |
| Shelter floor | Normal | Clear; surrounding walls block sight | Prevents weather exposure cost |

Shelters are small areas with several open approaches. No food grows inside them. Food just outside shelters supports cautious foraging; richer sources also occur farther away. Shelter protection is a fixed terrain property, with no separate reservation system or occupancy bonus.

Generate connected traversable terrain, several shelters, grazing patches, fruit patches of both visible types, and a small number of pods. Validate routes between spawn positions, basic food, and shelter. Avoid one-cell choke points at spawn areas. Use multiple map seeds and mirrored/rotated layouts for experiments.

An initial map recipe is 6 shelters, 80 grazing patches, 32 soft-fruit patches split equally between A and B, and 8 pods. Place resources on distinct traversable cells, with at least two separate approaches to every pod. These counts are configurable. The proposed rates below imply an upper bound of about 9.84 food-energy units/second averaged over a full weather cycle when every source is continuously depleted. Actual usable production will be lower because of capacity limits, pod opening/refill schedules, spoilage, and travel. Compare measured production with the founding population's expenditure before choosing final defaults.

Initial creature bodies are identical circles of radius 0.25 units. Collisions prevent passing through walls or other creatures. There is no damage from contact. Resolve movement as a batch with symmetric collision handling; creature iteration order must not determine who gets through a doorway. Crowding and obstruction can occur, but deliberate pushing is outside this version.

## 3. Food and resource dynamics

Resources exist at persistent locations. Consumption removes actual stored biomass. A consumed resource never teleports to a new position.

| Source | Proposed payoff | Access | Intended opportunity |
| --- | --- | --- | --- |
| Ground forage | 2 energy per biomass unit | Immediately edible | Reliable, low-return fallback |
| Soft fruit A | Either 4 or 10 energy per unit | Immediately edible | Learn its value from experience |
| Soft fruit B | The complementary 10 or 4 energy per unit | Immediately edible | Compare value with A |
| Tough pod | 12 energy per unit; 12 units when full | Work to open, then eat | Shared work, competition, opportunism |

All food is safe in this version. The challenge is opportunity cost, depletion, travel, and exposure. There is no poison system.

Each source has a biomass capacity and finite replenishment rate. Ground forage and soft fruit replenish continuously during calm weather and the warning phase, up to capacity. They stop replenishing during storms. Illustrative capacities are 8 units for a grazing patch and 12 units for a soft-fruit patch; starting regrowth rates are 0.02 and 0.01 units/second respectively. Counts and rates must be calibrated against population energy demand.

Visible appearance indicates source type and approximately how much edible material remains. It does not disclose nutritional value or an exact future replenishment time. Depleted plants remain visible. The creature can therefore encounter a familiar source that is currently empty.

A creature can ingest at most 1 biomass unit per second at full forage effort. Energy arrives after a fixed 3-second digestion delay. Ingested biomass leaves the world immediately and cannot be eaten again. Pending digestive energy dies with its consumer; corpses supply no food in v1. Gains above the creature's energy capacity are lost.

### Learning which food is valuable

Assign A/B nutritional values once when a world is created. The assignment is shared by all creatures and stays fixed for that world's run. Both assignments occur equally often across controlled evaluation worlds.

Appearance is available before eating. Nutritional consequences arrive through the creature's own energy gain after digestion. There is no supplied preference table, remembered food value, or label identifying the better food.

The persistent ecosystem initially keeps its assignment stable. Controlled food-learning experiments use fresh worlds with both assignments and reset brain state between lifetimes. A separate diagnostic may reverse the assignment during a lifetime; this is an experimental manipulation, not part of the default weather cycle.

A population can evolve a fixed preference in one stable world. That would demonstrate evolutionary adaptation, not learning during life. To claim the latter, the same unmodified genome must adjust its choices from experience in worlds with different assignments.

The current brain has dynamic recurrent state and inherited weights. V1 adds the environmental feedback needed to investigate adaptation through that state. It does not assume synaptic plasticity is already implemented. Plasticity can be a later brain-side experiment if needed.

## 4. Cooperation and competition around pods

A closed pod has opening progress between 0 and 10. Creatures within interaction reach and facing it can apply forage effort, paying energy for their effort. Let W be the sum of their effort, capped at 2. Opening progress increases at W² units per second. With continuous full effort, one creature opens it in 10 seconds and two in 2.5 seconds. More than two workers provide no further acceleration.

When nobody works on a closed pod, progress decays at 1 unit per second. When it opens, its stored food becomes available to everybody within reach. Keeping the forage action active transitions naturally from working to eating.

Opening assistance gives no ownership, exclusive access, or special reward. A creature can help, join late, wait for others to finish, approach a call, or leave for another source. Helpers and opportunists pay the same ordinary consumption and movement costs.

Opened pods remain available for up to 30 seconds or until depleted. Any remainder then spoils. The pod closes and starts an empty refill period. Biomass grows back at a proposed 0.08 units/second during non-storm periods; it becomes workable again only when fully replenished. Appearance distinguishes refilling from ready-to-open pods. Opening never creates free biomass.

When multiple creatures request the same food in one step, allocate available biomass proportionally to their requested intake. Total allocated biomass cannot exceed the stock. Newly opened food becomes edible on the following world step, giving every creature the same state boundary.

This deliberately creates a modest benefit from joint work while allowing solitary access. Its payoff must be tested: too little advantage makes assistance irrelevant, while too much may make congregating at pods the only viable strategy.

### Minimal communication

Creatures can emit one continuous, nonsemantic call with intensity from 0 to 1. Calling costs energy. Other creatures hear directional intensity within 6 units; walls block transmission in this first version. No sender identity, genotype, or message meaning is supplied. Multiple calls combine with saturation.

There is no built-in meaning such as “help” or “food.” Calling may attract helpers, competitors, or nobody. Communication can be disabled experimentally to measure its contribution. Repeated interaction with the same individual can occur spatially, but individual identity recognition and reputation are outside this version.

## 5. Weather and shelter

Use a repeating 240-second cycle:

| Phase | Duration | Observable conditions | Effect |
| --- | --- | --- | --- |
| Calm | 150 seconds | Low storm cue | Normal metabolism and regrowth |
| Warning | 30 seconds | Storm cue rises gradually to maximum | Normal metabolism; time to seek shelter |
| Storm | 60 seconds | Maximum storm cue | Additional energy loss outside shelter; regrowth paused |

Proposed storm exposure cost: 0.8 energy per second outside shelter. Shelter removes this additional cost; normal metabolism and brain costs continue. Weather reduces energy directly, with no separate health, temperature, or stamina system.

Creatures sense local weather and whether they are sheltered. They do not receive an absolute clock, remaining phase duration, or a vector to the nearest shelter. Randomize the starting phase of controlled evaluations, with safe placement and sufficient initial reserves; the initial population of the persistent world starts in calm conditions.

A full storm costs 48 extra energy outside. Thus shelter can matter without making storm exposure instantly fatal. Stockpiling enough energy and continuing to forage remains a possible strategy. Permanent shelter camping eventually exhausts reserves because shelter contains no food.

## 6. Body, energy, and reproduction

Proposed initial values:

| Quantity | Initial value |
| --- | --- |
| Maximum speed | 1.5 units/second |
| Maximum turn speed | 180 degrees/second |
| Interaction reach | 0.8 units from creature center |
| Interaction arc | 120 degrees in front |
| Energy capacity | 200 |
| Founder energy | 90 |
| Basal metabolism | 0.20 energy/second |
| Movement cost | Up to 0.12 energy/second, proportional to speed command squared |
| Turning cost | Up to 0.02 energy/second, proportional to absolute turn command |
| Forage/work effort cost | Up to 0.30 energy/second, proportional to effort |
| Calling cost | Up to 0.05 energy/second, proportional to call intensity |
| Reproductive maturity | 120 seconds old |
| Reproductive threshold | 150 energy |
| Parent expenditure per birth | 75 energy |
| Offspring initial energy | 45 energy, included in the parent's expenditure |
| Reproductive cooldown | 120 seconds |

The remaining 30 energy spent at birth is lost as reproductive overhead. Reproduction cannot increase total creature energy. At zero energy, a creature dies and is removed.

Movement commands consume energy even when blocked, so pushing continually into a wall is not free. Rough ground doubles movement energy cost. Foraging reduces movement speed to at most 25% of normal at full effort, creating a tradeoff between feeding and departure. An unsuccessful forage attempt still costs effort.

Brain maintenance and spike costs also debit actual energy. Calibrate an initial reference brain's combined neural cost to roughly 5–15% of basal metabolism under representative activity, then freeze coefficients for a given experiment. No extra reward or penalty is attached to standing still, facing food, brain size, or proximity to another creature in ecological mode.

Reproduction is automatic, asexual, and local when maturity, reserve, cooldown, and space requirements are satisfied. It produces a mutated copy of the parent's brain genome. Birth resets the child's neural activity and digestive state; it does not copy the parent's memories. Bodies and sensor layouts remain fixed.

Place offspring in an available nearby location, without overlap or wall penetration. If no location is available, birth waits and spends no energy. Resolve competing birth placements without permanent ID priority.

A configurable maximum population protects computational resources. If it is reached, mark the run as capacity-limited and pause for inspection; do not silently cull creatures or treat that run as an unconstrained ecological result. Extinction is also recorded explicitly. The default world does not automatically reseed itself.

## 7. Senses and actions

All observations come from the creature's local body position and heading. Initial vision range is 6 units with a 150-degree field of view, divided into five angular sectors. Walls occlude objects behind them; creatures do not occlude one another in v1.

For each sector, expose a compact sensory encoding of:

- Distance to a visible obstacle, if present.
- Nearest visible food source: distance, appearance/type, visible stock, and pod state where applicable.
- Nearest visible creature: distance and externally visible forage/call activity.

Use explicit presence channels to distinguish “absent” from “near.” Source types are categorical sensory channels, not ranked numeric values. The closest visible source in each sector is reported, so creatures may need to turn or move to inspect alternatives.

Additional inputs: directional hearing in four quadrants; contact in four body-relative directions; normalized energy reserve; own forward and turn motion; sheltered status; storm cue; and recent ingestion and digestion-gain pulses. Ingestion does not reveal the eventual nutritional yield. Internal sensory feedback is transient, with any lasting association left to the brain.

Never supply global coordinates, another creature's energy/genome, food's true nutritional value, optimal targets, partner assignments, or global resource summaries.

Five motor outputs: forward movement, turn left, turn right, forage/work, and call. Turning is decoded from the difference between left and right outputs. The current spike-trace approach can drive these controls. Reproduction is automatic, so no mating or reproduction command is needed initially.

Forage targets the nearest reachable source within the interaction arc. Distance ties use an order-neutral seeded rule. The same action eats available food or works on a closed pod. Choosing sources therefore occurs through movement, orientation, and effort rather than an explicit target-ID action.

The exact numeric channel layout will be versioned when implementing the sensor adapter. This draft fixes what information exists and what it means.

## 8. Simulation modes and architecture

The C++ world owns terrain, resource stocks, creature bodies, energy, digestion, weather, and birth/death events. Controllers receive observations and return actions. They can be spiking brains, scripted baselines, or other experimental policies.

Proposed world timestep: 0.10 seconds. With the current brain timestep of 0.02 seconds, run five neural updates per world step. Keep rendering independent of simulation speed.

Every creature observes the same beginning-of-step world. Collect all actions, resolve movement/contact, allocate interaction work and consumption, apply digestive gains and energetic costs, remove deaths, and resolve births in documented stages. Dead creatures cannot reproduce. A creature whose net energy reaches zero before receiving a future digestive gain dies. Resource updates and weather transitions follow fixed, recorded boundaries.

Use recorded seeds and separate random streams for world generation, neural noise, mutation, and order-neutral conflict resolution. Adding an unrelated random draw must not silently change all other subsystems. Checkpoints must preserve the full world and brain runtime state for continuation.

Support three configurations of this same world:

1. **Solo:** one creature, optionally with reproduction disabled for assessment. Pods are accessible through solitary work.
2. **Controlled social trials:** a focal creature with a defined set of scripted or frozen-genome companions/competitors. Resource abundance and density are explicit experimental settings.
3. **Persistent ecosystem:** a proposed initial population of 24 creatures, local reproduction and mutation, and ongoing deaths. No global generation replacement.

Reuse the existing evolutionary algorithms for controlled trials through an adapter. Keep the ecological reproduction driver distinct from tournament/NSGA-II selection. In controlled social trials, rotate partners, competitors, spawn positions, and maps to reduce accidental specialization.

## 9. What we should observe and measure

The viewer should show terrain, food stock and type, pod opening progress, weather, shelters, creature energy, calls, births, deaths, and parent/child relationships. Selecting a creature shows its actual sensory field, recent diet, actions, and the existing brain activity view. An optional observer-only overlay can reveal true food values; these never enter creature observations.

Record energy sources and expenditures, food consumed by source/type, time exposed or sheltered, work contributed to each pod, who subsequently eats its food, calls, maturation, reproduction, and causes of death. Track pending digestive energy and resource replenishment so the energy/biomass accounts can be audited.

Core measures:

- Survival, population history, viable offspring, and descendants surviving to maturity.
- Net energy acquired per unit time and reproductive success across different food assignments.
- Change in food preference after sampling, measured for the same genome across repeated lifetimes.
- Pod work contribution versus food obtained, including help, late arrival, and waiting.
- Outcomes when a partner is present, absent, or unable to contribute.
- Outcomes with communication enabled versus disabled.

Joint presence at a pod is insufficient evidence of cooperation. Compare each participant's benefit with matched solitary/partner-absent conditions. High pod consumption is insufficient evidence of outsmarting: look for behavior that changes in response to a competitor and improves outcomes against controlled baselines.

Useful baselines are a random wanderer, a competent reactive forager, a fixed food-type preference, a simple sampler that remembers food values, and a scripted pod helper/opportunist. Baselines may have internal memory appropriate to their label, but receive the same sensory information and physical capabilities.

Proposed assessment horizon: 20 weather cycles per controlled trial, reporting any right-censored lifespan/offspring outcomes at the time limit. Persistent simulations can run longer. Hold out maps, starting positions, and partner combinations from selection.

## 10. Boundaries and implementation sequence

This iteration includes resource competition, shared work, a simple call, and local physical crowding. Combat, predation, sexual reproduction, identity/reputation systems, explicit teams, carrying/storage, construction, pheromone fields, evolving bodies, and new synaptic plasticity rules are separate future proposals.

Implementation sequence:

1. Build the shared world, local sensing, movement, resource depletion/regrowth, and viewer using scripted controllers.
2. Add energy accounting, digestion, weather, and lifecycle rules. Calibrate whether solitary survival and reproduction are feasible across several seeds.
3. Add shared pod work and calling. Check whether joint work can benefit both participants and whether alternative solitary strategies remain viable.
4. Connect evolved spiking controllers and controlled evaluations. Run social and food-learning comparisons before interpreting persistent ecosystem behavior.

Meaningful verification includes biomass conservation under simultaneous eating, parent/offspring energy accounting, visibility through walls, reproducibility from seeds/checkpoints, and the absence of systematic iteration-order advantages.

Completing this iteration means that the ecological rules and experiment modes work and can be inspected. Evolving sophisticated social behavior is a research outcome to pursue and measure, not a prerequisite for declaring the simulator functional.

## 11. Decisions proposed for agreement

- Use nonviolent food competition and shared pod opening as the first creature interactions.
- Include one costly, nonsemantic call to make recruitment possible.
- Make cooperation advantageous but keep pods individually accessible.
- Use two safe fruit appearances with hidden, world-specific nutritional values to investigate learning.
- Use one energy pool, energy-costing storms, and automatic asexual reproduction.
- Keep bodies fixed and use existing recurrent brain dynamics first.

The main design question is whether the shared-work pod feels like an acceptable first ecological mechanism. A more physically emergent alternative, such as moving heavy objects together, would require a larger physics/manipulation scope.
