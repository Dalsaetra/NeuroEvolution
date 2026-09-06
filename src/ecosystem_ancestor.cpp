#include "neuroevo/ecosystem.hpp"

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
constexpr std::size_t digestion_input = body_offset + 7;
constexpr std::size_t episode_start_input = body_offset + 8;

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
    if (config.brain.input_count != (config.predation ? eco_predation_input_count : config.extended_senses ? eco_input_count : eco_legacy_input_count)
        || config.brain.output_count != (config.predation ? eco_predation_output_count : eco_output_count)) {
        throw std::invalid_argument("The sparse ecosystem ancestor requires the ecosystem sensor and motor layout");
    }
    if (!(config.brain.synaptic_gain > 0.0) || !std::isfinite(config.brain.synaptic_gain)) {
        throw std::invalid_argument("The sparse ecosystem ancestor requires positive finite synaptic gain");
    }

    BrainConfig brain_config = config.brain;
    brain_config.hidden_count = 7;
    brain_config.background_activity_enabled = true;
    brain_config.initial_connection_probability = 0.0;
    brain_config.seed_input_output_synapses = false;

    const std::size_t hidden = brain_config.input_count;
    const std::size_t output = hidden + brain_config.hidden_count;
    const std::size_t locomotion_a = hidden;
    const std::size_t locomotion_b = hidden + 1;
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
    // Energy is a tonic drive even for a low-energy newborn. It therefore
    // restarts locomotion after ingestion has paused the rhythm.
    neurons[energy_input].threshold = 0.25;
    // Distant obstacle signals should not compete continuously with food
    // steering. Legacy LIF inputs fire only in the nearer 40% of vision;
    // calibrated inputs retain distant information at a reduced spike rate.
    for (std::size_t sector = 0; sector < eco_sectors; ++sector) {
        neurons[sector * eco_sector_channels].threshold = 1.5;
    }
    neurons.push_back(neuron_at(0.46, 0.30));
    neurons.push_back(neuron_at(0.54, 0.30));
    // A rare background event restarts the locomotion rhythm after mutation,
    // inhibition, or low energy has silenced it. Other ancestral neurons remain
    // noise-insensitive, and this sensitivity can itself evolve.
    neurons[locomotion_a].background_sensitivity = 2.0;
    for (std::size_t sector = 0; sector < eco_sectors; ++sector) {
        neurons.push_back(neuron_at(0.50, 0.48 + 0.08 * static_cast<double>(sector), 1.2));
    }
    for (std::size_t i = 0; i < brain_config.output_count; ++i) {
        const double y = (static_cast<double>(i) + 0.5) / static_cast<double>(brain_config.output_count);
        neurons.push_back(neuron_at(0.95, y));
    }
    // Turning requires repeated steering events rather than saturating from a
    // single sensory spike. Forward and forage retain the standard threshold.
    neurons[turn_left].threshold = 1.4;
    neurons[turn_right].threshold = 1.4;

    std::vector<Brain::Synapse> synapses;
    // Nominal weights were tuned at gain 32. Scaling keeps the circuit's
    // delivered current stable when experiments alter --synaptic-gain.
    const double scale = 32.0 / brain_config.synaptic_gain;
    const auto connect = [&](std::size_t pre, std::size_t post, double nominal_weight) {
        synapses.push_back({pre, post, nominal_weight * scale, 1});
    };

    // A newborn pulse starts a two-neuron rhythm. Energy can restart it if a
    // perturbation silences it. This rhythm is the ancestor's only forward drive.
    connect(episode_start_input, locomotion_a, 2.0);
    connect(energy_input, locomotion_a, 2.0);
    connect(locomotion_a, locomotion_b, 2.0);
    connect(locomotion_b, locomotion_a, 2.0);
    connect(locomotion_a, forward, 1.8);
    // Once feeding begins, pause over the patch instead of merely skimming it.
    // Digestion provides a restart pulse after the available biomass is gone.
    connect(ingestion_input, locomotion_a, -3.5);
    connect(ingestion_input, locomotion_b, -3.5);
    connect(digestion_input, locomotion_a, 2.0);
    connect(ingestion_input, forward, -3.0);
    connect(ingestion_input, turn_left, -2.5);
    connect(ingestion_input, turn_right, -2.5);

    // Weak environmental access for evolution: these weights can change sign
    // or disappear like any other synapse. No direct shelter-steering reflex.
    connect(body_offset + 5, locomotion_a, -0.15); // storm warning/intensity
    connect(body_offset + 4, locomotion_b, -0.15); // currently sheltered
    if (config.extended_senses) {
        for (std::size_t sector = 0; sector < eco_sectors; ++sector)
            connect(eco_shelter_offset + sector, hidden + 2 + sector, -0.15);
    }

    // Proximity carries enough information to bias simultaneous signals toward
    // the nearest patch. The ancestor cannot compare stock or nutritional value.
    connect(0 * eco_sector_channels + 2, turn_right, 1.75);
    connect(1 * eco_sector_channels + 2, turn_right, 1.55);
    connect(3 * eco_sector_channels + 2, turn_left, 1.55);
    connect(4 * eco_sector_channels + 2, turn_left, 1.75);

    // Weak initial avoidance (half the original weights) leaves food attraction
    // more influence near walls. These ordinary synapses remain evolvable.
    // A symmetric obstacle ahead still biases left.
    connect(0 * eco_sector_channels, turn_left, 1.1);
    connect(1 * eco_sector_channels, turn_left, 1.0);
    connect(2 * eco_sector_channels, turn_left, 1.1);
    connect(3 * eco_sector_channels, turn_right, 1.0);
    connect(4 * eco_sector_channels, turn_right, 1.1);

    // Contact is body-relative: front, left, back, right. Side contacts turn
    // away; a frontal contact picks a stable side; pressure from behind keeps
    // forward drive alive. This lets crowded bodies separate through their
    // brain rather than adding a non-neural movement reflex.
    connect(contact_offset + 0, turn_left, 2.4);
    connect(contact_offset + 1, turn_right, 2.2);
    connect(contact_offset + 2, forward, 1.8);
    connect(contact_offset + 3, turn_left, 2.2);

    // Each sector has a high-pass relay: close food spikes rapidly enough to
    // cross its threshold, while the slower signal from distant food decays.
    for (std::size_t sector = 0; sector < eco_sectors; ++sector) {
        const std::size_t near_food = hidden + 2 + sector;
        connect(sector * eco_sector_channels + 2, near_food, 1.5);
        connect(near_food, forage, 2.0);
    }

    return Brain::from_components(brain_config, std::move(neurons), std::move(synapses));
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
    config.establishment = false;
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
    founder.brain = make_sparse_ancestral_brain(world.config);
    founder.neural_rng = Random(config.seed ^ 0x6e757273657279ULL);
    world.initialize_body(founder);
    world.creatures.push_back(std::move(founder));
    world.next_creature_id = 2;
    return world;
}

} // namespace neuroevo
