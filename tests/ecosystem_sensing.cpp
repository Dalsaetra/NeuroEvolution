#include "fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace neuroevo;
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t center = (eco_sectors / 2) * eco_sector_channels;
constexpr std::size_t hearing = eco_sectors * eco_sector_channels;
constexpr std::size_t contact = hearing + 4;
constexpr std::size_t body = contact + 4;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

bool near(double a, double b)
{
    return std::abs(a - b) < 1e-8;
}

EcosystemWorld empty_world()
{
    EcosystemConfig config = neuroevo::controlled_config();
    config.width = config.height = 16;
    // The geometric expectations below use a six-cell vision radius.
    config.vision_range = 6;
    config.initial_creatures = 0;
    config.shelters = config.grazing_patches = config.fruit_patches = config.pods = 0;
    config.reproduction = false;
    config.brain.background_activity_enabled = false;
    EcosystemWorld world(config, false);
    world.terrain.assign(config.width * config.height, Terrain::Ground);
    return world;
}

EcoCreature creature(const EcosystemWorld& world, Vec2 position, std::uint64_t id = 1)
{
    EcoCreature result;
    result.id = id;
    result.position = position;
    result.energy = world.config.founder_energy;
    result.brain = Brain(world.config.brain);
    result.neural_rng = Random(id + 50);
    return result;
}

EcoResource food(Vec2 position, FoodKind kind = FoodKind::FruitA)
{
    EcoResource result;
    result.position = position;
    result.kind = kind;
    result.capacity = result.stock = 12.0;
    result.energy_per_unit = 10.0;
    return result;
}

bool same_action(const EcoAction& a, const EcoAction& b)
{
    return near(a.forward, b.forward) && near(a.left, b.left) && near(a.right, b.right)
        && near(a.forage, b.forage) && near(a.call, b.call);
}

void test_visibility()
{
    auto world = empty_world();
    world.creatures.push_back(creature(world, {4.5, 5.5}));
    world.creatures.push_back(creature(world, {9.0, 5.5}, 2));
    world.creatures[1].action.call = 1.0;
    world.creatures[1].action.forage = 0.6;
    world.resources.push_back(food({8.5, 5.5}));
    world.terrain[5 * world.config.width + 6] = Terrain::Wall;
    auto inputs = world.observe(0);
    require(inputs[eco_unsheltered_input]==1 && inputs[body+4]==0,
        "Exposed creature must sense unsheltered");
    world.terrain[5*world.config.width+4]=Terrain::Shelter;
    const auto protected_inputs=world.observe(0);
    require(protected_inputs[eco_unsheltered_input]==0 && protected_inputs[body+4]==1,
        "Unsheltered must be the complement of sheltered");
    world.terrain[5*world.config.width+4]=Terrain::Ground;
    require(inputs.size() == eco_input_count, "Incorrect sensory channel count");
    require(near(inputs[center], 0.75), "Wall proximity should use its visible surface");
    require(inputs[center + 1] == 0.0 && inputs[center + 10] == 0.0,
        "Walls must occlude both food and creatures");
    require(inputs[hearing] == 0.0, "Walls must block calls");

    world.terrain[5 * world.config.width + 6] = Terrain::Ground;
    inputs = world.observe(0);
    require(inputs[center + 1] == 1.0 && inputs[center + 10] == 1.0,
        "Objects should be visible through an open passage");
    require(near(inputs[center + 2], 1.0 / 3.0), "Food proximity should use continuous positions");
    require(near(inputs[center + 12], 0.6) && inputs[center + 13] == 1.0,
        "Externally visible activity is missing");

    world.resources[0].position = {11.0, 5.5};
    require(world.observe(0)[center + 1] == 0.0, "Vision must have finite range");
    world.resources[0].position = {3.5, 5.5};
    world.creatures[1].position = {3.5, 5.5};
    inputs = world.observe(0);
    for (std::size_t sector = 0; sector < eco_sectors; ++sector) {
        require(inputs[sector * eco_sector_channels + 1] == 0.0
            && inputs[sector * eco_sector_channels + 10] == 0.0,
            "Food and creatures behind the field of view must remain invisible");
    }
    require(inputs[hearing + 2] > 0.0, "Hearing should work behind the creature");
    world.config.communication = false;
    inputs = world.observe(0);
    for (std::size_t direction = 0; direction < 4; ++direction) {
        require(inputs[hearing + direction] == 0.0, "Communication ablation must suppress hearing");
    }
}

void test_sectors_and_contact()
{
    auto world = empty_world();
    world.creatures.push_back(creature(world, {4.5, 4.5}));
    world.resources.push_back(food({6.0, 6.0}));
    const auto unobstructed = world.observe(0);
    world.terrain[5 * world.config.width + 6] = Terrain::Wall;
    const auto inputs = world.observe(0);
    // The nearest wall corner is inside the wider center sector, but the
    // sector's central ray still misses the wall entirely.
    const double boundary_distance = std::hypot(1.5, 0.5);
    require(near(inputs[center], 1.0 - boundary_distance / world.config.vision_range),
        "A sector must detect partial wall intersections away from its center ray");
    require(inputs[0] == unobstructed[0], "An obstacle outside a sector must not activate it");
    world.terrain[5 * world.config.width + 6] = Terrain::Ground;
    const auto left_inputs = world.observe(0);
    require(left_inputs[(eco_sectors - 1) * eco_sector_channels + 1] > 0.0,
        "Positive bearings should activate a left vision sector");

    world.creatures[0].position = {5.75, 5.5};
    world.terrain[5 * world.config.width + 6] = Terrain::Wall;
    world.creatures.push_back(creature(world, {5.25, 5.5}, 2));
    const auto contacts = world.observe(0);
    require(contacts[contact] == 1.0, "Wall contact should appear in the front quadrant");
    require(contacts[contact + 2] == 1.0, "Creature contact should appear in the rear quadrant");
    world.creatures[1].position = {5.20, 5.5};
    require(world.observe(0)[contact + 2] == 1.0,
        "Near-contact sensing must activate before numerical collision separation");
    world.creatures[0].position = {0.25, 5.5};
    world.creatures[0].heading = pi;
    require(world.observe(0)[contact] == 1.0, "World boundaries must produce contact");
}

void test_nearest_and_hidden_information()
{
    auto world = empty_world();
    world.creatures.push_back(creature(world, {4.5, 5.5}));
    world.creatures.push_back(creature(world, {6.5, 5.5}, 2));
    world.creatures[1].action.forage = 0.2;
    world.creatures[1].action.call = 1.0;
    world.creatures.push_back(creature(world, {7.5, 5.5}, 3));
    world.creatures[2].action.forage = 1.0;
    world.creatures[2].action.call = 1.0;
    world.resources.push_back(food({8.5, 5.5}, FoodKind::FruitA));
    world.resources.push_back(food({5.5, 5.5}, FoodKind::FruitB));
    world.resources[1].stock = 0.0;
    const auto before = world.observe(0);
    require(before[center + 1] == 1.0 && before[center + 5] == 0.0
        && near(before[center + 4], 1.0 - 4.0 / world.config.vision_range) && before[center + 7] == 1.0
        && near(before[eco_depleted_offset + eco_sectors / 2], 1.0 - 1.0 / world.config.vision_range),
        "Depleted food must remain separately visible without hiding stocked food");
    require(near(before[center + 12], 0.2), "Vision should report the nearest creature's activity");
    require(before[hearing] == 1.0, "Multiple calls must combine with saturation");

    world.fruit_a_rich = !world.fruit_a_rich;
    world.resources[0].energy_per_unit = 999;
    world.resources[1].energy_per_unit = 0.01;
    world.resources[0].id = 89;
    world.resources[1].id = 100;
    world.creatures[1].id = 99;
    world.creatures[1].energy = 180;
    world.creatures[1].generation = 50;
    world.creatures[1].parent_id = 1234;
    require(world.observe(0) == before, "Hidden nutrition, identity, ancestry, and energy must not leak into observations");
    std::reverse(world.resources.begin(), world.resources.end());
    require(world.observe(0) == before, "Resource iteration order must not change nearest-source sensing");
}

void test_typed_food_proximity()
{
    auto world = empty_world();
    world.creatures.push_back(creature(world, {4.5, 5.5}));
    world.resources = {food({5.5,5.5},FoodKind::Graze), food({7.5,5.5},FoodKind::FruitA),
        food({8.5,5.5},FoodKind::FruitA), food({6.5,5.5},FoodKind::FruitB)};
    const auto seen = world.observe(0);
    require(near(seen[center+3],5.0/6) && near(seen[center+4],0.5)
        && near(seen[center+5],4.0/6) && seen[center+6]==0,
        "Each food type must independently report its nearest visible distance");
    std::reverse(world.resources.begin(),world.resources.end());
    require(world.observe(0)==seen,"Typed food sensing depends on resource order");
    world.terrain[5*world.config.width+6]=Terrain::Wall;
    require(world.observe(0)[center+4]==0,"Typed food proximity leaks through walls");
    world.terrain[5*world.config.width+6]=Terrain::Ground;
    world.config.typed_food_proximity=false;
    const auto old=world.observe(0);
    require(old[center+3]==1 && old[center+4]==0 && old[center+5]==0,
        "Historical binary food-type encoding changed");
    require(ecosystem_input_labels()[center+4]=="vision_1_food_fruit_a_proximity",
        "Typed food label does not describe its signal");
}

void test_feedback()
{
    auto world = empty_world();
    world.creatures.push_back(creature(world, {4.5, 5.5}));
    auto& self = world.creatures[0];
    self.energy = world.config.energy_capacity * 0.25;
    self.speed = world.config.max_speed * 0.5;
    self.turn = -world.config.max_turn_rate * 0.75;
    self.ingestion_pulse = world.config.ingestion_rate * world.config.dt;
    self.digestion_pulse = 4.0 * world.config.ingestion_rate * world.config.dt;
    auto inputs = world.observe(0);
    require(near(inputs[body], 0.25) && near(inputs[body + 1], 0.5), "Own energy and motion must be normalized");
    require(inputs[body + 2] == 0.0 && near(inputs[body + 3], 0.75), "Turn feedback has incorrect direction");
    require(inputs[body + 6] == 1.0 && near(inputs[body + 7], 4.0 / 15.0),
        "Feedback must distinguish ingested biomass from energy gained later");
    require(inputs[body + 8] == 1.0, "A newborn must receive the initial activity pulse");
    self.age = 0.2;
    inputs = world.observe(0);
    require(inputs[body + 8] == 0.0, "The initial pulse must end after 0.2 seconds");
    for (double value : inputs) require(std::isfinite(value) && value >= 0.0 && value <= 1.0,
        "Every sensory channel must remain finite and normalized");
    const auto labels = ecosystem_input_labels();
    require(labels.size() == eco_input_count && labels[body] == "energy"
        && labels[body + 8] == "episode_start" && labels[eco_unsheltered_input] == "unsheltered"
        && labels.back() == "vision_2_shelter_proximity", "Channel labels must match the sensory schema");
}

void test_independent_spiking_brains()
{
    auto world = empty_world();
    auto first = creature(world, {4.5, 5.5});
    first.energy = world.config.energy_capacity;
    BrainConfig brain_config = world.config.brain;
    std::vector<Brain::Neuron> neurons(brain_config.input_count + brain_config.hidden_count + brain_config.output_count);
    const std::size_t output = brain_config.input_count + brain_config.hidden_count;
    first.brain = Brain::from_components(brain_config, neurons, {{body, output, 10.0, 1}});
    auto second = creature(world, {8.5, 5.5}, 2);
    second.energy = world.config.energy_capacity;
    second.brain = first.brain;
    world.creatures.push_back(first);
    world.creatures.push_back(second);
    world.resources.push_back(food({5.0, 5.5}));
    const auto initial_second = world.creatures[1].brain.neurons();
    const auto observation_before = world.observe(0);
    const auto action_before = world.creatures[0].action;
    const auto action = world.control(0);
    require(action.forward > 0.0, "A connected spiking brain should drive its own motor");
    require(world.creatures[0].step_spikes > 0 && world.creatures[0].spikes == world.creatures[0].step_spikes,
        "Neural updates must record per-step and lifetime spikes");
    require(world.creatures[1].spikes == 0, "Controlling one creature must not advance another's spike totals");
    for (std::size_t i = 0; i < initial_second.size(); ++i) {
        const auto& actual = world.creatures[1].brain.neurons()[i];
        require(actual.potential == initial_second[i].potential
            && actual.refractory_remaining == initial_second[i].refractory_remaining
            && actual.spiked == initial_second[i].spiked, "Brain runtime state must be individual to each creature");
    }
    require(world.observe(0) == observation_before && same_action(world.creatures[0].action, action_before),
        "Control collection must preserve beginning-of-step observations and public actions");

    world.creatures[0].brain = Brain(brain_config);
    require(same_action(world.control(0), {}), "An unconnected spiking brain must not acquire a scripted foraging fallback");

    neurons[output + 1].bias = 100.0;
    neurons[output + 2].bias = 30.0;
    world.creatures[0].brain = Brain::from_components(brain_config, neurons, {});
    const auto turn_action = world.control(0);
    require(turn_action.left > 0.01 && turn_action.left < 0.3 && turn_action.right == 0.0,
        "Differential motor rates must retain steering direction while respecting actuator inertia");
    for (double value : {turn_action.forward, turn_action.left, turn_action.right, turn_action.forage, turn_action.call}) {
        require(std::isfinite(value) && value >= 0.0 && value <= 1.0, "Motor commands must be finite and normalized");
    }
}

void test_baseline_information_boundary()
{
    for (ControllerKind controller : {ControllerKind::Reactive, ControllerKind::Random}) {
        auto world = empty_world();
        world.creatures.push_back(creature(world, {4.5, 5.5}));
        world.creatures[0].controller = controller;
        world.resources.push_back(food({5.0, 5.5}));
        auto changed = world;
        changed.resources[0].energy_per_unit = 0.01;
        changed.fruit_a_rich = !changed.fruit_a_rich;
        changed.resources.push_back(food({13.0, 13.0}, FoodKind::Pod));
        changed.creatures.push_back(creature(changed, {13.0, 12.0}, 30));
        changed.creatures[1].energy = 190;
        require(world.observe(0) == changed.observe(0), "The test worlds must have identical local information");
        require(same_action(world.control(0), changed.control(0)),
            "Baselines must use only the same observations provided to spiking brains");
        require(world.creatures[0].step_spikes == 0, "Scripted baselines must not falsely report brain activity");
        world.config.max_speed = 0.0;
        world.config.max_turn_rate = 0.0;
        const auto stationary_action = world.control(0);
        require(std::isfinite(stationary_action.left) && std::isfinite(stationary_action.right),
            "Stationary controlled trials must not divide by zero in the controller");
    }
}

} // namespace

int main()
{
    try {
        test_visibility();
        test_sectors_and_contact();
        test_nearest_and_hidden_information();
        test_typed_food_proximity();
        test_feedback();
        test_independent_spiking_brains();
        test_baseline_information_boundary();
        std::cout << "Ecosystem sensing and brain-controller tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
