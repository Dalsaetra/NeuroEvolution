# Nursery and frontier habitat

Start a new ecosystem with:

```powershell
.\scripts\ecosystem.ps1 -Habitat nursery-frontier -Steps 60000 -Build
```

This selects sparse ancestral founders, stable mutations, and an 80×80 world.
Archive maintenance, newborn archive evaluations, and immigration are disabled
for this habitat, even if establishment options are passed. No replacement
creatures are inserted. Existing generated habitats and the controlled solo
ancestor nursery remain available.

The central 32×32 nursery has sheltered ground, a wall perimeter with four
three-cell exits, and 196 separated grazing patches. All founders start inside.
Its food continues regrowing during storms, and its local storm input is zero.
The shelter input and directional shelter cues still work. Ordinary metabolism,
brain costs, movement, feeding, digestion, crowding, reproduction, and death all
apply unchanged. Bodies can leave and re-enter normally; there is no forced
return or protected population floor.

Each nursery patch has 8 biomass, yields 12.5 energy per biomass, and regrows at
0.0144 biomass per second. Its long-term supply is 0.18 energy per second, below
the 0.20 basal cost alone. Occupants must actively forage and move between
patches. The feeding arc is 80 degrees in this habitat, requiring more deliberate
alignment than the standard 120-degree arc. Finite initial stocks can support an
early population surge; the subsequent population depends on sustained feeding.

Nursery food also rewards pausing to harvest: intake efficiency is
`0.35 + 0.65 * (1 - forward_drive)^2`. A moving creature still gets a taste,
allowing ingestion feedback to trigger the ancestor's existing pause, while a
stationary forager gets full intake. The rule is identical for every controller;
it never checks genotype, ancestry, or population. Frontier food uses normal
intake. Resource depletion and delayed digestion account for the actual intake.

The frontier has 120 scattered grazing patches (14 energy per biomass), 64 fruit
patches (16 or 36), and 24 pods (60). Fruit appearances retain the per-world
nutrition assignment; pods require the existing sustained opening work. Food is
sparser than inside, with rough terrain, isolated rock pillars, and 16 separate
3×3 shelters without food. The highest rewards require exploration, food choice,
or pod handling. These pressures allow useful complexity; they do not guarantee
it will evolve or make a simple strategy impossible.

Weather defaults are 180 seconds calm, 45 warning, and 60 storm, with 1.2 energy
per second of extra exposure cost outside shelters. Frontier food stops regrowing
during storms. Nursery protection is spatial; the global weather cycle continues.

Tune defaults in `nursery_frontier_config()` in `src/ecosystem_frontier.cpp`.
The executable also accepts `--nursery-size`, `--nursery-food-energy`,
`--nursery-food-capacity`, `--nursery-food-regrowth`, `--interaction-degrees`, and
the existing world, food and weather flags. Habitat defaults are applied first,
so explicit CLI values override them irrespective of argument order. Customized
values can make the nursery easier or harder than the tested preset.

The replay labels the nursery, leaves it clear during the storm overlay, and
the statistics dropdown includes `nursery_population` and `frontier_population`.
Checkpoint version 9 saves the habitat settings and exact geography. Resume with
`-Resume PATH` without specifying `-Habitat`; it restores the saved environment.
Older checkpoints retain their original habitat. Start a new world to adopt this
map layout.

Validation covers connected traversable geography, founder placement, resource
nutrition, storm protection, local weather input, regrowth, passive starvation,
absence of archive support, and exact checkpoint continuation. Population pilots
are evidence over finite runs, not a guarantee against eventual extinction.

Final-preset pilots (24 founders, 3,000 simulated seconds, stable mutations):

| Controller / seed | Final population | Births | Descendant births |
| --- | ---: | ---: | ---: |
| Sparse ancestor / 7 | 21 | 69 | 37 |
| Sparse ancestor / 19 | 14 | 59 | 33 |
| Random actions / 7 | 0 (extinct at 459.1 s) | 0 | 0 |
| Random spiking brains / 7 | 0 (extinct at 518.4 s) | 6 | 1 |

All had zero immigrants and archive evaluations. The raw runs are
`runs/nursery_frontier_release7`, `runs/nursery_frontier_release19`, and
`runs/nursery_frontier_release_random`. Random actions are an unresponsive
behavioral baseline, not a claim that every randomly initialized neural genome
must fail; some random networks may already implement useful feeding behavior.

The ancestral seed-7 run was also continued to 6,000 simulated seconds. It ended
with 19 living creatures and 107 cumulative births, still without any external
population support (`runs/nursery_frontier_release7_continued`). The final random
brain control is saved in `runs/nursery_frontier_release_random_brains`.
