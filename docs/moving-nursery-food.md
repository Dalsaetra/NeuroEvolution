# Moving nursery food

New nursery-frontier worlds now use a fixed number of randomly positioned patches.
Only the patch that is depleted moves; all other patches stay in place. The new
position is inside the nursery, separated from other resources and from its old
position, and outside creatures' immediate feeding reach. Placement uses seeded
randomness and a small continuous offset rather than a regular grid.

Defaults in `EcosystemConfig` are 18 patches, 24 biomass capacity per patch, and
50 energy per biomass. This halves the previous 16×16 nursery's 36 patches and
doubles its nutrition, preserving its initial 21,600 food energy. It does not
preserve the old energy replenishment rate: depleted patches reappear **full**,
and moving nursery patches do not also receive passive regrowth. Regeneration
therefore depends on consumption, rather than time. Replacement biomass is
included in `regrown_biomass` so energy accounting remains explicit.

```powershell
.\scripts\ecosystem.ps1 -Habitat nursery-frontier -NurseryFoodPatches 18 -NurseryFoodEnergy 50
```

The runner only sends these options when explicitly provided, allowing header
defaults to be changed and rebuilt. The executable accepts
`--nursery-food-patches`, `--nursery-food-energy`, and `--nursery-food-capacity`.
Use `--nursery-food-relocates 0` for the historical grid and passive regrowth;
in that mode the grid size determines the count and `nursery_food_patches` is
unused. For outdoor decay, relocation and shelter forage, see [dynamic food](dynamic-food.md).

The resource ID and total patch count remain stable. If crowding blocks every
new position, the depleted patch remains empty and retries next step. It is not
refilled underneath a creature. Oversized initial patch counts fail with an
actionable placement error rather than overlapping food.

The normal feeding-angle, slowing-to-harvest, digestion and metabolic rules still
apply. Relocation removes dependable routes between fixed patches but does not
guarantee more complex behavior or a stable population.

Version 12 checkpoints preserve patch count, relocation policy, positions and
RNG state. Older checkpoints retain static food. Start a new nursery-frontier
world to use the new layout. Replay frames record current resource positions;
the viewer supports both these recordings and older static recordings.
