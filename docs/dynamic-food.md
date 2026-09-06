# Dynamic outdoor food and shelter forage

New nursery-frontier maps no longer create low-quality shelter food. Ordinary
grazing can spawn and relocate on shelter floors as well as open ground, using
the same shared grazing quota, nutrition, randomized starting stock and decay.
Fruit and pods remain outside shelters. Nursery rules are unchanged. The older
generated habitat retains low-quality shelter patches as described below.
Existing checkpoints retain their saved patches; ordinary frontier grazing uses
the expanded relocation area when resumed. Start a new frontier map to remove
the old low-quality patches.

New worlds in both habitats enable dynamic outdoor grazing and fruit. Each loses
0.005 biomass per simulated second, versus a maximum ingestion rate of 1 biomass
per second for one fully foraging creature. Decay is linear, runs during storms,
and is applied after feeding. Outdoor patches no longer passively regrow.
At world creation, each outdoor grazing/fruit patch starts with an independently
uniform random fraction of its capacity (0–100%), staggering its first depletion.
This uses a separate seeded RNG so geography is unchanged and runs remain
reproducible. Initial available biomass averages half the full-map capacity;
patch counts and subsequent full replacements are unchanged. Shelter forage and pods still start full; checkpoint loading preserves saved stock.
Nursery food also starts randomly decayed and loses 0.005 biomass per second,
including during storms. With the default moving-patch policy, depletion replaces
it full at another spot inside the nursery. Configure this separately with
`--nursery-food-decay` or the runner’s `-NurseryFoodDecay`; zero disables nursery
decay and starts nursery patches full. Historical static nursery patches retain
their passive regrowth in addition to any explicitly configured decay.

When eaten or decayed to empty, a patch reappears full at a random traversable
cell outside the nursery (and outside shelters for fruit). Its ID, kind, nutrition and capacity stay
unchanged: the configured grazing, fruit A/B and pod counts remain constant.
Other patches do not move. Replacement avoids other food and the old location;
creature positions do not exclude otherwise valid locations. Newly spawned food
cannot be eaten until the next step. If every alternative cell is occupied, the
empty slot remains and retries each step. Pods retain their existing mechanics.
Decay contributes to `spoiled_biomass`; replacements contribute to `regrown_biomass`.

In the generated habitat, each shelter has one low-quality forage patch with 2 biomass capacity
and 1 energy per biomass. It now decays at 0.005 biomass per second, including
during storms, and no longer passively regrows. When consumed or decayed to empty,
it respawns full at a randomly selected different walkable tile within its
original shelter footprint. It never drifts into an adjoining shelter or nursery,
and skips walls and other food. Patch IDs and counts stay fixed. The initial
patch starts full. Replacements are available for feeding on the next step.

Creatures must actively forage, digest, and follow relocated patches. Nutrition
remains lower than outside food; the old stationary steady-supply guarantee no
longer applies. If the shelter has no alternative free tile (including one-tile
shelters), its patch remains empty and retries each step.

`--shelter-food-decay` / `-ShelterFoodDecay` controls the rate; setting it to zero
restores static passive regrowth using `shelter_food_regrowth`. Version 17
checkpoints preserve the rate and each patch's original shelter center. Older
checkpoints retain static shelter regrowth with decay disabled.

Shelter forage shares the existing grazing sensor and diet category, preserving
brain interfaces. Replay metadata marks it with `shelter_food: true` and the viewer
renders it tan; the nutrition toggle shows its energy value.

The executable accepts `--graze-decay`, `--fruit-decay`, `--shelter-food-energy`,
`--shelter-food-capacity`, and `--shelter-food-regrowth`. Decay must be nonnegative
and below the configured ingestion rate. `--outdoor-food-relocates 0` restores
static outdoor regrowth. The PowerShell runner exposes the five numeric settings
as `-GrazeDecay`, `-FruitDecay`, `-ShelterFoodEnergy`, `-ShelterFoodCapacity`, and
`-ShelterFoodRegrowth`, and forwards them only when explicitly supplied.

Version 15 adds the nursery decay rate; earlier saves load with nursery decay
disabled to preserve continuation. Version 14 checkpoints preserve the policy, settings, shelter-food flags,
positions and RNG state. Older checkpoints continue with their original outdoor
regrowth and do not gain shelter patches. Start a new world for these changes.

## Storm feeding

During a storm, an unsheltered creature cannot harvest any food, including fruit,
grazing, open pods or meat, or contribute work to a closed pod. The brain can
still issue forage actions and pays the normal effort cost, but consumes no
biomass and queues no food energy. Sheltered creatures can feed normally. The
rule uses the creature's position after movement, not the food's tile, and the
weather at the start of the simulation step. Food decay and relocation continue.
Previously swallowed food still digests normally. This rule also applies when
continuing saved worlds with the updated executable.
