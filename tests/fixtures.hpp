#pragma once
#include "neuroevo/ecosystem.hpp"

namespace neuroevo {
inline std::string without_eye_extension(std::string state)
{
    const auto eyes=state.find("EYES_1");
    if(eyes!=std::string::npos)state.erase(eyes,state.find("END_ECOSYSTEM",eyes)-eyes);
    return state;
}
// Synthetic historical checkpoint fixtures remove the v40 config preamble.
inline std::string without_allometry_header(std::string state)
{
    state=without_eye_extension(std::move(state));
    const auto start=state.find("MASS_ALLOMETRY_1");
    if(start!=std::string::npos)state.erase(start,state.find('\n',state.find('\n',start)+1)+1-start);
    return state;
}
inline EcosystemConfig scattered_config()
{
    EcosystemConfig c;c.mass_allometry=false;c.funded_reproduction=false;c.mass_scaled_energy_capacity=false;c.mutation.meta_mutation_enabled=false;c.food_distribution=FoodDistribution::Scattered;
    c.background_food_patches=0;c.storm_ramp=false;return c;
}
// Explicit small-world conditions for mechanics tests. Production defaults are
// exercised separately in the nursery and CLI tests.
inline EcosystemConfig controlled_config()
{
    EcosystemConfig c;c.mass_allometry=false;c.funded_reproduction=false;c.mass_scaled_energy_capacity=false;c.mutation.meta_mutation_enabled=false;
    c.background_food_patches=0;c.storm_ramp=false;
    c.food_distribution = FoodDistribution::Scattered;
    c.nursery_frontier = false;
    c.nursery_food_respawn_delay = c.outdoor_food_respawn_delay = 0;
    c.sparse_ancestor = false;
    c.mutate_initial_ancestors = false; // Keep controlled ancestral circuits unmodified.
    c.set_predation(false);
    c.storm_energy_drain = true;
    c.storm_health_damage = false; // Controlled historical energy-drain storms.
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
