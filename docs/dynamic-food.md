# Dynamic outdoor food and shelter forage

New worlds in both habitats enable dynamic outdoor grazing and fruit. Each loses
0.005 biomass per simulated second, versus a maximum ingestion rate of 1 biomass
per second for one fully foraging creature. Decay is linear, runs during storms,
and is applied after feeding. Outdoor patches no longer passively regrow.
At world creation, each outdoor grazing/fruit patch starts with an independently
uniform random fraction of its capacity (0–100%), staggering its first depletion.
This uses a separate seeded RNG so geography is unchanged and runs remain
reproducible. Initial available biomass averages half the full-map capacity;
patch counts and subsequent full replacements are unchanged. Nursery food,
shelter forage and pods still start full; checkpoint loading preserves saved stock.

When eaten or decayed to empty, a patch reappears full at a random traversable
cell outside the nursery and shelters. Its ID, kind, nutrition and capacity stay
unchanged: the configured grazing, fruit A/B and pod counts remain constant.
Other patches do not move. Replacement avoids other food and the old location;
creature positions do not exclude otherwise valid locations. Newly spawned food
cannot be eaten until the next step. If every alternative cell is occupied, the
empty slot remains and retries each step. Pods retain their existing mechanics.
Decay contributes to `spoiled_biomass`; replacements contribute to `regrown_biomass`.

Each outside shelter gains one additional static patch of low-quality shelter
forage at its center. It has 2 biomass capacity, 1 energy per biomass, and regrows
at 0.6 biomass per second even during storms. It never decays or relocates. Its
steady gross supply is 0.6 energy per second, covering the default basal cost of
0.2 and full feeding effort of 0.3 for one stationary creature, with a small margin
for neural costs. Food still requires active feeding and the normal digestion
delay; it does not guarantee survival for multiple occupants, moving creatures,
or arbitrarily expensive brains. Custom nutrition/metabolic settings can change
this balance. Default shelter nutrition is below all outdoor food types.

Shelter forage shares the existing grazing sensor and diet category, preserving
brain interfaces. Replay metadata marks it with `shelter_food: true` and the viewer
renders it tan; the nutrition toggle shows its energy value.

The executable accepts `--graze-decay`, `--fruit-decay`, `--shelter-food-energy`,
`--shelter-food-capacity`, and `--shelter-food-regrowth`. Decay must be nonnegative
and below the configured ingestion rate. `--outdoor-food-relocates 0` restores
static outdoor regrowth. The PowerShell runner exposes the five numeric settings
as `-GrazeDecay`, `-FruitDecay`, `-ShelterFoodEnergy`, `-ShelterFoodCapacity`, and
`-ShelterFoodRegrowth`, and forwards them only when explicitly supplied.

Version 14 checkpoints preserve the policy, settings, shelter-food flags,
positions and RNG state. Older checkpoints continue with their original outdoor
regrowth and do not gain shelter patches. Start a new world for these changes.
