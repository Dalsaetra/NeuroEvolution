# Moving nursery food

The default nursery has 16 patches with capacity 2 and energy density 20. Edit the `nursery_food_*` fields in [config.hpp](../include/neuroevo/config.hpp) to tune them.

Each patch loses biomass through feeding and linear decay. A depleted patch relocates within the nursery and returns full. Other patches stay in place. Placement avoids other resources, the old position, and creatures' immediate feeding reach. Seeded randomness and continuous offsets keep placement reproducible.

Moving patches do not also receive passive regrowth. Replacement biomass is recorded in `regrown_biomass`. If crowding blocks replacement, the patch stays empty and retries on subsequent steps. Its ID stays stable, and replays record its current position.

Set `nursery_food_relocates = false` for a controlled static-grid experiment with passive regrowth. That grid determines its own patch count. Food decay must be below the ingestion rate. Ordinary forage angles, movement-related harvest limits, metabolism, and digestion still apply.

See [outdoor food](dynamic-food.md) for the frontier policy.
