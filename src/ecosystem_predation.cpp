#include "neuroevo/ecosystem.hpp"
#include <algorithm>
#include <cmath>

namespace neuroevo {
void EcosystemWorld::initialize_body(EcoCreature& c)
{
    c.body = config.predation ? BodyGenes{config.founder_mass, config.founder_carnivory} : BodyGenes{};
    c.health = max_health(c);
    c.damage_pulse = 0;
    if (config.predation) totals.external_body_energy += config.body_energy_per_mass * c.body.mass;
}

double EcosystemWorld::max_health(const EcoCreature& c) const
{
    return config.health_per_mass * c.body.mass;
}

double EcosystemWorld::maximum_speed(const EcoCreature& c) const
{
    return config.max_speed / (config.predation ? std::sqrt(c.body.mass) : 1.0);
}

double EcosystemWorld::dietary_efficiency(const EcoCreature& c, FoodKind kind) const
{
    if (!config.predation) return kind == FoodKind::Meat ? 0.0 : 1.0;
    return kind == FoodKind::Meat ? c.body.carnivory : 1.0 - c.body.carnivory;
}

BodyGenes EcosystemWorld::inherit_body(const BodyGenes& parent, bool strong, Random& rng) const
{
    BodyGenes child = parent;
    if (!config.predation) return child;
    const auto& profile = strong ? config.mutation.strong : config.mutation.slight;
    const double scale = profile.body_sigma_scale;
    const double probability_scale = profile.body_probability_scale;
    if (config.mutation.mass_mutation_probability > 0 && config.mutation.mass_mutation_sigma > 0
        && rng.chance(std::min(1.0, probability_scale * config.mutation.mass_mutation_probability)))
        child.mass = std::exp(std::clamp(std::log(parent.mass) + rng.normal(0, scale * config.mutation.mass_mutation_sigma),
            std::log(eco_min_mass), std::log(eco_max_mass)));
    if (config.mutation.carnivory_mutation_probability > 0 && config.mutation.carnivory_mutation_sigma > 0
        && rng.chance(std::min(1.0, probability_scale * config.mutation.carnivory_mutation_probability)))
        child.carnivory = std::clamp(parent.carnivory + rng.normal(0, scale * config.mutation.carnivory_mutation_sigma), 0.0, 1.0);
    return child;
}

void EcosystemWorld::remove_dead(double end)
{
    // Process by ID so corpse IDs, events and roundoff do not depend on storage order.
    std::vector<const EcoCreature*> dead;
    for (const auto& c : creatures)
        if (c.energy <= 0 || (config.predation && c.health <= 0)) dead.push_back(&c);
    std::sort(dead.begin(), dead.end(), [](auto a, auto b) { return a->id < b->id; });
    for (const auto* pointer : dead) {
        const auto& c = *pointer;
        double discarded = 0;
        for (const auto& packet : c.digestion) discarded += packet.energy;
        if (config.predation) {
            const double stored = config.body_energy_per_mass * c.body.mass + std::max(0.0, c.energy);
            const double recovered = stored * config.carcass_recovery;
            discarded += stored - recovered;
            if (recovered > 0) {
                // Public experiments may insert resources directly; never reuse their IDs.
                for (const auto& r : resources) next_resource_id = std::max(next_resource_id, r.id + 1);
                EcoResource meat;
                meat.id = next_resource_id++;
                meat.kind = FoodKind::Meat;
                meat.position = c.position;
                meat.stock = meat.capacity = recovered / config.meat_energy;
                meat.energy_per_unit = config.meat_energy;
                resources.push_back(meat);
                totals.carcass_energy += recovered;
                events.push_back({end, "carcass", c.id, 0, meat.id, recovered});
            }
            if (c.health <= 0) ++totals.predation_deaths;
        }
        totals.discarded_energy += discarded;
        ++totals.deaths;
        events.push_back({end, "death", c.id, c.parent_id, 0, discarded});
    }
    creatures.erase(std::remove_if(creatures.begin(), creatures.end(), [&](const auto& c) {
        return c.energy <= 0 || (config.predation && c.health <= 0);
    }), creatures.end());
}
} // namespace neuroevo
