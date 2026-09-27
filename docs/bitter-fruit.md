# Bitter fruit

Bitter-fruit trees add a distinct purple fruit to new `fields-and-trees` nursery-frontier worlds. For carnivory `c` from 0 to 1 and an ingested biomass amount `b`:

```
digestible energy = b * bitter_fruit_energy * max(0, 1 - 2*c)
health damage     = b * bitter_fruit_damage * max(0, 2*c - 1)
```

At exactly 50% carnivory, the fruit supplies neither energy nor damage. Above that point, damage rises linearly. Damage applies when swallowed; energy arrives after the usual digestion delay and follows the normal survival/reproduction allocation. In control worlds with predation/body mechanics disabled, creatures retain the ordinary herbivore behavior.

| Carnivory | Energy per biomass | Health damage per biomass |
|---|---:|---:|
| 0% | 120 | 0 |
| 25% | 60 | 0 |
| 50% | 0 | 0 |
| 75% | 0 | 5 |
| 100% | 0 | 10 |

The fruit remains physically edible at all carnivory values, so neural creatures must learn when to avoid it. Normal ingestion speed, mass scaling, limited stock, and proportional sharing apply. Poisoning can kill, is recorded separately from predation, prevents healing during the same step, and produces the normal single carcass. Nursery protection against attacks does not block poisoning. Diet conversion losses enter discarded-energy accounting; undigested packets do not enter carcass reserves.

## Tuning

Defaults are starting values for experiments, not a calibrated ecological equilibrium. Nutrition is slightly higher than ordinary rich fruit at zero carnivory, while a full default fruit site deals 30 damage to a fully carnivorous creature if entirely consumed.

| Config field in `include/neuroevo/config.hpp` | Executable option | Default |
|---|---|---:|
| `food_sources.bitter_trees` | `--bitter-trees` | 3 |
| `food_sources.bitter_production` | `--bitter-tree-production` | 0.4 biomass/tree/second |
| `bitter_fruit_energy` | `--bitter-fruit-energy` | 120 energy/biomass |
| `bitter_fruit_damage` | `--bitter-fruit-damage` | 10 health/biomass at 100% carnivory |
| `bitter_fruit_capacity` | `--bitter-fruit-capacity` | 3 biomass/site |

Set the tree count to zero to disable new bitter-fruit sources. Energy and damage may also be zero; production and capacity must be positive. Trees share ordinary `fruit_sites` and `tree_radius`, and fruit shares `fruit_decay`. With 12 sites and these defaults, a depleted site ripens after 90 non-storm seconds. Initial ripening is staggered; growth pauses during storms. Like ordinary tree fruit, these sites use ripening rather than the scattered-food respawn delay. The `scattered` preset does not create bitter trees.

## Vision and compatibility

Three new directional sensor groups are appended to the predation interface, increasing it from 91 to 100 inputs:

- **Bitter-fruit presence:** 1 when a nonempty bitter fruit is visible in that sector.
- **Bitter-fruit proximity:** `1 - distance / vision_range` for the nearest visible nonempty bitter fruit.
- **Bitter-fruit amount:** remaining biomass divided by capacity for that same fruit.

Each group has left, middle, and right inputs, using the creature's evolved field of view and existing occlusion rules. General food and plant proximity also detect bitter fruit; meat and ordinary fruit-specific sensors retain their meanings. Depleted bitter sites contribute to the existing depleted-food cue. Structural mutations add these inputs in groups, just like other directional senses. The ancestor's new inputs start disconnected before initial mutation.

Checkpoint version 46 saves settings, sources, ripening, digestion, and bitter-fruit consumption totals. Replay metadata and run summaries expose the settings, and the viewer distinguishes the fruit, trees, and poisoning events. Older checkpoints resume with their original input layout and no bitter trees. Importing old genomes into a fresh world appends the new disconnected inputs and uses the new world's food settings.
