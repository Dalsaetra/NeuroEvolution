# Outdoor food

Default frontier grazing and fruit lose biomass at their configured decay rates, including during storms. Initial stock is a seeded random fraction of capacity, staggering depletion. A depleted patch relocates and returns full; moving food does not additionally regrow. Replacement biomass is accounted for explicitly.

Ordinary grazing can occupy shelter floors and open ground. Fruit and pods stay outside shelters. The frontier does not create special low-quality shelter patches. Pods start full and use their cooperative opening, open-duration, and refilling cycle.

Tune quotas, capacity, nutrition, decay, and regrowth in the food section of [config.hpp](../include/neuroevo/config.hpp). `outdoor_food_relocates = false` enables a static regrowth control. Nursery food has its own configuration. The generated-map control can create special shelter forage using `shelter_food_*`; these fields do not change ordinary nursery-frontier grazing.

During storms an exposed creature cannot harvest plants or meat, or contribute work to pods. Forage effort still costs energy. Sheltered creatures can feed, swallowed packets still digest, and food decay/relocation continues. Protection uses the creature's post-movement position and weather at step start.

Checkpoints retain positions, stocks, renewal policy, and RNG state. The viewer shows moving patches and their recorded nutrition.
