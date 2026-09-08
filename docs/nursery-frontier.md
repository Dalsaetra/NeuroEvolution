# Nursery and frontier

`EcosystemConfig{}` creates the nursery habitat. Tune its defaults in [config.hpp](../include/neuroevo/config.hpp); no preset function overrides them.

The current map is 80 by 80 cells with a central 16-cell nursery and four centered, three-cell exits. `nursery_exit_width = 0` closes the gates. Founders spawn inside the nursery. Its shelter floor prevents storm exposure, and creatures inside are immune to attack damage. Attacking still consumes energy.

The frontier provides richer grazing, fruit, and cooperative pods, interspersed with shelters, rough ground, and short wall lines. Ordinary grazing can occupy shelter floors; fruit and pods remain outside. Current weather periods are 180 seconds calm, 40 warning, and 60 storm. Sheltered creatures may feed during storms; exposed creatures cannot harvest or work pods.

The sparse ancestral spiking circuit starts with locomotion, food steering, contact avoidance, and forage behavior. Descendants modify it through ordinary inheritance. At the population cap, births pause while the world continues. Deaths free slots. Extinction ends a run.

Tune reproduction age, reserve threshold, cost, cooldown, and offspring energy together. Body construction adds a mass-dependent cost to each birth. Nursery support comes from geography and food supply.

Related: [moving nursery food](moving-nursery-food.md), [outdoor food](dynamic-food.md), [terrain](frontier-layout.md), and [predation](predation.md).
