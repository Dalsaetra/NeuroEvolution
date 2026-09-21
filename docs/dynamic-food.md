# Outdoor food

## Distribution presets

Fresh nursery-frontier runs default to **`fields-and-trees`**. Select **`scattered`** to restore the previous distribution and renewal rules:

```powershell
.\scripts\ecosystem.ps1 -FoodDistribution fields-and-trees -Steps 10000
.\scripts\ecosystem.ps1 -FoodDistribution scattered -Steps 10000
```

The executable accepts `--food-distribution fields-and-trees|scattered`. The default is set by `EcosystemConfig::food_distribution` in `include/neuroevo/config.hpp`. The generated and ancestor-nursery control habitats retain scattered food; explicitly selecting fields-and-trees for those habitats is rejected. Nursery food and carcasses use their existing rules in either preset. Neither preset adds sensory channels or changes the ingestion limit.

The preset, source geometry, density, production rates and ripening state are saved in checkpoint version 34. Resuming retains the saved distribution; importing starting genomes into a fresh world uses the selected new-run preset. Historical checkpoints load as scattered. Changing the distribution of a running saved ecosystem is intentionally unsupported.

### Fields and trees

The default 140 × 140 layout contains **three grazing fields, 18 fruit trees and three pod trees**. These counts are independently configurable; the old `grazing_patches`, `fruit_patches` and `pods` quotas apply only to scattered food.

- **Fields:** connected, irregular areas of vegetation with smoothly varying density and thinner edges. Local biomass is depleted by grazing and regrows in place. A 1.5-cell sampling grid represents the continuous field; its cells cover the field in the viewer, coloured by remaining stock. Capacity and growth are weighted by density and cell area, so increasing resolution does not multiply food production. Field nutrition defaults to 25 energy/biomass, below scattered grazing's 40.
- **Fruit trees:** fixed centres with eight fixed fruit sites each. A tree produces one existing fruit type, alternating between fruit A and B across trees. Ripe fruit can be partially eaten and decays at `fruit_decay`. A depleted site becomes unavailable for `capacity / site_production_rate` calm/warning seconds, then receives one ripe batch. The default capacity 4 and total tree production 0.4 give an 80-second ripening cycle per site. Initial ripe fruit and remaining ripening timers are staggered deterministically.
- **Pod trees:** three fixed pod sites per tree. They retain the existing closed/open/refilling and cooperative opening rules. Each tree shares a total production budget of 0.12 biomass/second among its sites. Initial pods start at different points in refilling.
- Growth and ripening pause during storms; ripe-fruit decay continues. Fields do not decay or globally relocate. Source plants ignore the scattered-food relocation flag and respawn delay. Trees confer no storm shelter and their drawn trunks/canopies add no collision geometry.
- Each creature selects at most one food resource per step. Dense cells and clusters therefore retain the existing intake limit, delayed digestion, dietary efficiency and sharing rules.

The seeded generator reserves source discs before placing shelters and walls. A best-candidate placement pass puts the larger fields first, followed by trees, enforcing a five-cell edge-to-edge gap between source reservations and nursery clearance. Shelters keep a two-cell margin from reserved sources, and walls stay outside them. An impossible requested layout fails with a descriptive error; counts and spacing are never silently reduced. Changing founder population does not change geography.

Tune `food_sources` in `config.hpp`, or use these CLI options:

| Option | Default | Meaning |
|---|---:|---|
| `--grazing-fields` | 3 | Field count |
| `--field-radius` | 12 | Maximum reserved field radius |
| `--field-spacing` | 1.5 | Vegetation sampling spacing, 1–2 |
| `--field-energy` | 25 | Energy per ingested biomass before diet efficiency |
| `--field-capacity` | 3 | Maximum biomass per unit area at density 1 |
| `--field-regrowth` | 0.04 | Biomass per unit area per non-storm second at density 1 |
| `--fruit-trees` / `--pod-trees` | 18 / 3 | Tree counts |
| `--fruit-sites` / `--pod-sites` | 8 / 3 | Sites sharing each tree's production |
| `--tree-radius` | 3 | Reserved growing radius |
| `--food-source-gap` | 5 | Minimum edge-to-edge source separation |
| `--fruit-tree-production` / `--pod-tree-production` | 0.4 / 0.12 | Biomass per tree per non-storm second while growing |

These are initial ecological tuning values, not a demonstrated long-run equilibrium. Stored stock, production, harvest losses and diet efficiency all affect carrying capacity. Metadata records source IDs/geometry and per-resource production; frame records include remaining fruit ripening time, and events include `fruit_ripened` and `pod_ripened`. Resource IDs map feeding events to their source for analysis. The viewer draws density-coloured field cells, tree growing areas and dashed unripe fruit sites.

### Scattered food (previous preset)

Default frontier grazing and fruit lose biomass at their configured decay rates, including during storms. Initial stock is a seeded random fraction of capacity, staggering depletion. A depleted patch relocates and returns full; moving food does not additionally regrow. Replacement biomass is accounted for explicitly.

Ordinary grazing can occupy shelter floors and open ground. Fruit and pods stay outside shelters. The frontier does not create special low-quality shelter patches. Pods start full and use their cooperative opening, open-duration, and refilling cycle.

Tune quotas, capacity, nutrition, decay, and regrowth in the food section of [config.hpp](../include/neuroevo/config.hpp). `outdoor_food_relocates = false` enables a static regrowth control. Nursery food has its own configuration. The generated-map control can create special shelter forage using `shelter_food_*`; these fields do not change ordinary nursery-frontier grazing.

During storms an exposed creature cannot harvest plants or meat, or contribute work to pods. Forage effort still costs energy. Sheltered creatures can feed, swallowed packets still digest, and food decay/relocation continues. Protection uses the creature's post-movement position and weather at step start.

Checkpoints retain positions, stocks, renewal policy, and RNG state. The viewer shows moving patches and their recorded nutrition.
