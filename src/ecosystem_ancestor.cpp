#include "neuroevo/ecosystem.hpp"
#include "ecosystem_mutation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace neuroevo {
namespace {

constexpr std::size_t body_offset = eco_sectors * eco_sector_channels + 8;
constexpr std::size_t contact_offset = eco_sectors * eco_sector_channels + 4;
constexpr std::size_t energy_input = body_offset;
constexpr std::size_t ingestion_input = body_offset + 6;

Brain::Neuron neuron_at(double x, double y, double threshold = 1.0)
{
    Brain::Neuron neuron;
    neuron.position = {x, y};
    neuron.threshold = threshold;
    neuron.background_sensitivity = 0.0;
    return neuron;
}

} // namespace

Brain make_sparse_ancestral_brain(const EcosystemConfig& config)
{
    if (config.brain.input_count != (config.predation ? (config.brain.input_count==eco_bitter_offset ? eco_bitter_offset : config.brain.input_count==eco_carnivory_offset ? eco_carnivory_offset : config.brain.input_count==eco_reproduction_offset && !config.funded_reproduction ? eco_reproduction_offset : eco_predation_input_count) : config.extended_senses ? eco_input_count : eco_legacy_input_count)
        || config.brain.output_count != (config.predation ? eco_predation_output_count : eco_output_count)) {
        throw std::invalid_argument("The sparse ecosystem ancestor requires the ecosystem sensor and motor layout");
    }
    if (!(config.brain.synaptic_gain > 0.0) || !std::isfinite(config.brain.synaptic_gain)) {
        throw std::invalid_argument("The sparse ecosystem ancestor requires positive finite synaptic gain");
    }

    BrainConfig brain_config = config.brain;
    brain_config.hidden_count = 0;
    brain_config.initial_connection_probability = 0.0;

    const std::size_t output = brain_config.input_count;
    const std::size_t forward = output;
    const std::size_t turn_left = output + 1;
    const std::size_t turn_right = output + 2;
    const std::size_t forage = output + 3;

    std::vector<Brain::Neuron> neurons;
    neurons.reserve(brain_config.input_count + brain_config.hidden_count + brain_config.output_count);
    for (std::size_t i = 0; i < brain_config.input_count; ++i) {
        const double y = (static_cast<double>(i) + 0.5) / static_cast<double>(brain_config.input_count);
        neurons.push_back(neuron_at(0.05, y));
    }
    // Energy supplies movement directly, including for low-energy newborns.
    neurons[energy_input].threshold = 0.25;
    for (std::size_t i = 0; i < brain_config.output_count; ++i) {
        const double y = (static_cast<double>(i) + 0.5) / static_cast<double>(brain_config.output_count);
        neurons.push_back(neuron_at(0.95, y));
    }
    // Turning requires repeated steering events rather than saturating from a
    // single sensory spike. Forward and forage retain the standard threshold.
    neurons[turn_left].threshold = 1.4;
    neurons[turn_right].threshold = 1.4;

    std::vector<Brain::Synapse> synapses;
    // Preserve the historical pulse-model calibration. Filtered LIF keeps
    // nominal weights so --synaptic-gain actually adjusts its input strength.
    const double scale = brain_config.neuron_model == NeuronModel::FilteredLif ? 1.0 : 32.0 / brain_config.synaptic_gain;
    const auto connect = [&](std::size_t pre, std::size_t post, double nominal_weight) {
        synapses.push_back({pre, post, nominal_weight * scale, 1});
    };

    // A minimal feed-forward seed: move while alive and slow during ingestion.
    // All connections and intrinsic parameters remain ordinary mutable genes.
    connect(energy_input, forward, 1.8);
    connect(ingestion_input, forward, -3.0);

    // Proximity carries enough information to bias simultaneous signals toward
    // the nearest patch. The ancestor cannot compare stock or nutritional value.
    connect(0 * eco_sector_channels + 2, turn_right, 1.75);
    connect((eco_sectors - 1) * eco_sector_channels + 2, turn_left, 1.75);

    // Contact escape prevents walls and crowded newborns from trapping a
    // lineage. Frontal contact retains one explicit leftward tie-break.
    connect(contact_offset + 0, turn_left, 2.4);
    connect(contact_offset + 1, turn_right, 2.2);
    connect(contact_offset + 3, turn_left, 2.2);

    // Food drives forage directly; there are no hidden relays or recurrence.
    // Other senses and the call/attack motors start disconnected and are
    // available to structural mutation, without seeded environmental policies.
    for (std::size_t sector = 0; sector < eco_sectors; ++sector) {
        connect(sector * eco_sector_channels + 2, forage, 1.5);
    }

    for (auto& n : neurons) n.izhikevich = brain_config.izhikevich_defaults;
    return Brain::from_components(brain_config, std::move(neurons), std::move(synapses));
}

void EcosystemWorld::initialize_ancestral_genome(EcoCreature& creature)
{
    creature.brain = make_sparse_ancestral_brain(config);
    creature.genome_id = 1;
    if (config.mutate_initial_ancestors) {
        // Keep founder variation independent of map, spawning, and later birth RNGs.
        Random rng(config.seed ^ (creature.id * 104729ULL) ^ 0x616e636573746f72ULL);
        const double initial_mass = creature.body.mass;
        creature.body = inherit_body(creature.body, true, rng);
        creature.health = max_health(creature);
        if (config.predation)
            totals.external_body_energy += config.body_energy_per_mass * (creature.body.mass - initial_mass);
        creature.brain.mutate(detail::strong_mutation(config.mutation), rng,
            ecosystem_input_groups(config.extended_senses, config.predation,config.brain.input_count>eco_reproduction_offset,config.brain.input_count>eco_carnivory_offset,config.brain.input_count>eco_bitter_offset));
        creature.genome_id = creature.id;
    }
    creature.brain.reset_state();
}

EcosystemWorld make_ancestral_nursery(EcosystemConfig config)
{
    if (config.max_population < 2) {
        throw std::invalid_argument("The ancestral nursery requires a population cap of at least two");
    }
    config.width = config.height = 24;
    config.initial_creatures = 0;
    config.shelters = config.grazing_patches = config.pods = 0;
    config.fruit_patches = 100;
    config.calm_duration = 100000.0;
    config.nursery_frontier = false;
    config.food_distribution = FoodDistribution::Scattered;
    config.shelter_size = 3;
    config.controller = ControllerKind::Spiking;

    EcosystemWorld world(config, false);
    world.config.initial_creatures = 1;
    world.terrain.assign(config.width * config.height, Terrain::Ground);
    for (std::size_t y = 0; y < config.height; ++y) {
        for (std::size_t x = 0; x < config.width; ++x) {
            if (x == 0 || y == 0 || x + 1 == config.width || y + 1 == config.height) {
                world.terrain[y * config.width + x] = Terrain::Wall;
            }
        }
    }

    std::uint64_t resource_id = 1;
    for (double y = 2.5; y <= 21.5; y += 2.0) {
        for (double x = 2.5; x <= 21.5; x += 2.0) {
            EcoResource resource;
            resource.id = resource_id++;
            resource.kind = FoodKind::FruitA;
            resource.position = {x, y};
            resource.stock = resource.capacity = config.fruit_capacity;
            resource.regrowth = config.fruit_regrowth;
            resource.energy_per_unit = config.rich_fruit_energy;
            world.resources.push_back(resource);
        }
    }
    world.fruit_a_rich = true;

    EcoCreature founder;
    founder.id = 1;
    founder.genome_id = 1;
    founder.position = {11.0, 12.5};
    founder.heading = 0.0;
    founder.energy = config.founder_energy;
    founder.controller = ControllerKind::Spiking;
    founder.neural_rng = Random(config.seed ^ 0x6e757273657279ULL);
    world.initialize_body(founder);
    world.initialize_ancestral_genome(founder);
    world.creatures.push_back(std::move(founder));
    world.next_creature_id = 2;
    return world;
}

} // namespace neuroevo
