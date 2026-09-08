#pragma once
#include "neuroevo/ecosystem.hpp"

namespace neuroevo {
// Explicit small-world conditions for mechanics tests. Production defaults are
// exercised separately in the nursery and CLI tests.
inline EcosystemConfig controlled_config()
{
    EcosystemConfig c;
    c.nursery_frontier = false;
    c.sparse_ancestor = false;
    c.set_predation(false);
    c.width = c.height = 48;
    c.max_population = 128;
    c.shelters = 12; c.shelter_size = 6;
    c.grazing_patches = 80; c.fruit_patches = 32; c.pods = 8;
    c.graze_capacity = 8; c.fruit_capacity = 12; c.pod_capacity = 12;
    c.graze_energy = 2.5; c.poor_fruit_energy = 5; c.rich_fruit_energy = 12.5; c.pod_energy = 15;
    c.interaction_degrees = 120;
    c.calm_duration = 150; c.warning_duration = 30; c.storm_cost = 3;
    c.maturity_age = 120; c.reproduction_cost = 75; c.offspring_energy = 50; c.reproduction_cooldown = 120;
    return c;
}
} // namespace neuroevo
