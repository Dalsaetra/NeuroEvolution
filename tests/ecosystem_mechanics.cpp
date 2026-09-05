#include "neuroevo/ecosystem.hpp"
#include "../src/ecosystem_mutation.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace neuroevo;
constexpr double pi = 3.14159265358979323846;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, const char* message, double tolerance = 1e-8)
{
    if (std::abs(actual - expected) > tolerance) throw std::runtime_error(std::string(message)
        + ": actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected));
}

EcosystemConfig fixture_config()
{
    EcosystemConfig config;
    config.outdoor_food_relocates = false;
    config.width = config.height = 12;
    config.initial_creatures = config.shelters = config.grazing_patches = config.fruit_patches = config.pods = 0;
    config.reproduction = false;
    config.basal_cost = config.movement_cost = config.turn_cost = config.forage_cost = config.call_cost = 0;
    config.neuron_cost = config.synapse_cost = config.spike_cost = config.storm_cost = 0;
    config.graze_regrowth = config.fruit_regrowth = config.pod_regrowth = 0;
    return config;
}

EcoCreature creature(const EcosystemConfig& config, std::uint64_t id, Vec2 position, double heading = 0)
{
    EcoCreature result;
    result.id = id;
    result.position = position;
    result.heading = heading;
    result.energy = 90;
    result.brain = Brain(config.brain);
    return result;
}

EcoResource food(std::uint64_t id, FoodKind kind, Vec2 position, double stock = 12)
{
    EcoResource result;
    result.id = id;
    result.kind = kind;
    result.position = position;
    result.stock = result.capacity = stock;
    result.energy_per_unit = kind == FoodKind::Pod ? 12 : 10;
    return result;
}

const EcoCreature& by_id(const EcosystemWorld& world, std::uint64_t id)
{
    const auto found = std::find_if(world.creatures.begin(), world.creatures.end(), [id](const EcoCreature& c) { return c.id == id; });
    require(found != world.creatures.end(), "Expected creature is absent");
    return *found;
}

EcoAction forage(double intensity = 1) { return {0, 0, 0, intensity, 0}; }

void fair_food_and_delayed_energy()
{
    EcosystemWorld world(fixture_config(), false);
    world.creatures = {creature(world.config, 1, {4.4, 5}), creature(world.config, 2, {5.6, 5}, pi), creature(world.config, 3, {5, 5.6}, -pi / 2)};
    world.resources = {food(1, FoodKind::FruitA, {5, 5}, 0.125)};
    auto reversed = world;
    std::reverse(reversed.creatures.begin(), reversed.creatures.end());
    world.step({forage(1), forage(0.5), forage(1)});
    reversed.step({forage(1), forage(0.5), forage(1)});
    near(world.resources[0].stock, 0, "Simultaneous feeding overdraws stock");
    near(world.totals.consumed_biomass, 0.125, "Consumption accounting mismatch");
    near(by_id(world, 1).ingestion_pulse, 0.05, "First feeder received wrong share");
    near(by_id(world, 2).ingestion_pulse, 0.025, "Half-effort feeder received wrong share");
    near(by_id(world, 3).ingestion_pulse, 0.05, "Last feeder received wrong share");
    double queued = 0;
    for (const auto& c : world.creatures) {
        near(c.energy, 90, "Food credited before digestion delay");
        near(c.ingestion_pulse, by_id(reversed, c.id).ingestion_pulse, "Feeding depends on update order");
        for (const auto& packet : c.digestion) queued += packet.energy;
    }
    near(queued, 1.25, "Digestive energy does not equal removed food energy");

    auto config = fixture_config();
    config.digestion_delay = 0.2;
    EcosystemWorld delay(config, false);
    delay.creatures = {creature(config, 1, {5, 5})};
    delay.creatures[0].energy = 199.5;
    delay.resources = {food(1, FoodKind::FruitA, {5.5, 5})};
    delay.step({forage()});
    delay.step({{}});
    near(delay.creatures[0].energy, 199.5, "Food energy arrived early");
    delay.step({{}});
    near(delay.creatures[0].energy, 200, "Digestion did not respect capacity");
    near(delay.totals.energy_gained, 0.5, "Credited energy accounting wrong");
    near(delay.totals.discarded_energy, 0.5, "Overflow energy was not discarded");
    require(delay.creatures[0].digestion.empty(), "Digested packets remain in queue");

    config.basal_cost = 1;
    EcosystemWorld starving(config, false);
    starving.creatures = {creature(config, 1, {5, 5})};
    starving.creatures[0].energy = 0.01;
    starving.resources = {food(1, FoodKind::FruitA, {5.5, 5})};
    starving.step({forage()});
    require(starving.creatures.empty() && starving.totals.deaths == 1, "Pending digestion rescued a starving creature");
    near(starving.totals.discarded_energy, 1, "Death did not discard pending food energy");
    near(starving.totals.metabolism, 0.01, "Terminal costs should debit only available energy");
    require(std::count_if(starving.events.begin(), starving.events.end(), [](const EcoEvent& event) { return event.type == "extinction"; }) == 1, "Extinction event missing");
    starving.step();
    require(starving.events.empty(), "Extinction event repeated indefinitely");
}

void cooperative_pods()
{
    const auto config = fixture_config();
    EcosystemWorld solo(config, false), pair(config, false);
    solo.creatures = {creature(config, 1, {4.4, 5})};
    pair.creatures = {creature(config, 1, {4.4, 5}), creature(config, 2, {5.6, 5}, pi)};
    solo.resources = pair.resources = {food(1, FoodKind::Pod, {5, 5})};
    for (int i = 0; i < 25; ++i) pair.step({forage(), forage()});
    require(pair.resources[0].pod_state == PodState::Open, "Two workers did not open a pod in 2.5 seconds");
    near(pair.resources[0].stock, 12, "Opening created or consumed pod biomass");
    near(pair.totals.consumed_biomass, 0, "Newly opened pod was edible in the opening step");
    near(pair.resources[0].opened_at, 2.5, "Wrong pod opening boundary");
    for (int i = 0; i < 99; ++i) solo.step({forage()});
    require(solo.resources[0].pod_state == PodState::Closed, "Solo pod opened too early");
    solo.step({forage()});
    require(solo.resources[0].pod_state == PodState::Open, "Solo pod did not open after 10 seconds");
    near(solo.creatures[0].pod_work, 10, "Solo work accounting wrong");
    near(pair.creatures[0].pod_work + pair.creatures[1].pod_work, 5, "Shared work accounting wrong");
    pair.step({forage(), forage()});
    near(pair.totals.consumed_biomass, 0.2, "Open pod was not shared fairly");
    near(pair.creatures[0].eaten[3], pair.creatures[1].eaten[3], "Pod access privileges one worker");

    EcosystemWorld abandoned(config, false);
    abandoned.creatures = {creature(config, 1, {4.4, 5})};
    abandoned.resources = {food(1, FoodKind::Pod, {5, 5})};
    abandoned.step({forage()});
    abandoned.step({forage()});
    abandoned.step({{}});
    near(abandoned.resources[0].progress, 0.1, "Abandoned pod progress did not decay");

    auto refill_config = config;
    refill_config.pod_open_duration = 0.2;
    EcosystemWorld refill(refill_config, false);
    refill.resources = {food(1, FoodKind::Pod, {5, 5}, 1)};
    refill.resources[0].regrowth = 5;
    refill.resources[0].pod_state = PodState::Open;
    refill.step();
    refill.step();
    near(refill.resources[0].stock, 1, "Open pod spoiled too early");
    refill.step();
    require(refill.resources[0].pod_state == PodState::Refilling, "Expired pod did not enter refill phase");
    near(refill.resources[0].stock, 0.5, "Wrong pod refill amount");
    near(refill.totals.spoiled_biomass, 1, "Spoiled biomass accounting wrong");
    refill.step();
    require(refill.resources[0].pod_state == PodState::Closed, "Refilled pod did not become workable");
    near(refill.resources[0].stock + refill.totals.consumed_biomass + refill.totals.spoiled_biomass,
        1 + refill.totals.regrown_biomass, "Pod lifecycle creates biomass");
}

void weather_shelter_and_costs()
{
    auto config = fixture_config();
    config.calm_duration = config.warning_duration = config.storm_duration = 0.2;
    config.basal_cost = 0.2;
    config.storm_cost = 0.8;
    EcosystemWorld world(config, false);
    world.terrain[5 * config.width + 6] = Terrain::Shelter;
    world.creatures = {creature(config, 1, {5.5, 5.5}), creature(config, 2, {6.5, 5.5})};
    world.resources = {food(1, FoodKind::Graze, {7, 5}, 8)};
    world.resources[0].stock = 0;
    world.resources[0].regrowth = 1;
    require(world.weather() == WeatherPhase::Calm, "World should begin in calm phase");
    world.step({{}, {}});
    world.step({{}, {}});
    require(world.weather() == WeatherPhase::Warning, "Warning boundary wrong");
    near(world.storm_cue(), 0, "Warning cue does not begin at zero");
    world.step({{}, {}});
    near(world.storm_cue(), 0.5, "Warning cue does not ramp");
    world.step({{}, {}});
    require(world.weather() == WeatherPhase::Storm, "Storm boundary wrong");
    world.step({{}, {}});
    world.step({{}, {}});
    require(world.weather() == WeatherPhase::Calm, "Cycle did not restart");
    near(world.creatures[1].energy - world.creatures[0].energy, 0.16, "Shelter did not remove exactly the exposure cost");
    near(world.totals.metabolism, 0.24, "Shelter incorrectly removed ordinary metabolism");
    near(world.totals.exposure, 0.16, "Storm exposure accounting wrong");
    near(world.resources[0].stock, 0.4, "Resources replenished during a storm");

    auto no_storm_config = config;
    no_storm_config.storms_enabled = false;
    no_storm_config.basal_cost = 0;
    EcosystemWorld no_storm(no_storm_config, false);
    no_storm.creatures = {creature(no_storm_config, 1, {5.5, 5.5})};
    no_storm.resources = {food(1, FoodKind::Graze, {7, 5}, 8)};
    no_storm.resources[0].stock = 0;
    no_storm.resources[0].regrowth = 1;
    for (int i = 0; i < 6; ++i) {
        require(no_storm.weather() == WeatherPhase::Calm && no_storm.storm_cue() == 0,
            "Disabled storms changed phase or activated the retained cue sensor");
        no_storm.step({{}});
    }
    near(no_storm.totals.exposure, 0, "Disabled storm still charged exposure");
    near(no_storm.resources[0].stock, 0.6, "Disabled storm still paused food regrowth");

    auto effort_config = fixture_config();
    effort_config.movement_cost = 0.12;
    effort_config.forage_cost = 0.3;
    effort_config.call_cost = 0.05;
    effort_config.communication = false;
    EcosystemWorld effort(effort_config, false);
    effort.terrain[5 * config.width + 6] = Terrain::Wall;
    effort.terrain[5 * config.width + 5] = Terrain::Rough;
    effort.creatures = {creature(effort_config, 1, {5.75, 5.5})};
    effort.step({{1, 0, 0, 1, 1}});
    near(effort.creatures[0].position.x, 5.75, "Blocked movement penetrated wall");
    near(effort.totals.movement, 0.024, "Blocked movement on rough terrain was not charged");
    near(effort.totals.foraging, 0.03, "Unsuccessful foraging was free");
    near(effort.totals.calling, 0, "Disabled communication still cost energy");
    near(effort.creatures[0].action.call, 0, "Disabled communication still emitted a call");
}

void swept_collisions_and_visibility()
{
    auto config = fixture_config();
    config.max_speed = 50;
    EcosystemWorld wall(config, false);
    for (std::size_t y = 0; y < config.height; ++y) wall.terrain[y * config.width + 6] = Terrain::Wall;
    wall.creatures = {creature(config, 1, {4, 5.5})};
    wall.step({{1, 0, 0, 0, 0}});
    require(wall.creatures[0].position.x <= 5.75 + 1e-8 && wall.traversable(wall.creatures[0].position), "Fast movement tunneled through a wall");
    require(!wall.line_of_sight({4, 5.5}, {8, 5.5}), "Vision penetrates a wall");
    require(wall.line_of_sight({4, 5.5}, {5.5, 7}), "Wall-free sightline was blocked");
    require(!wall.traversable({6 - config.radius / 2, 5.5}), "Body radius is ignored against walls");
    require(!wall.traversable({-1, 5}), "Out-of-world body is traversable");

    EcosystemWorld edge(config, false);
    edge.terrain[5 * config.width + 4] = Terrain::Wall;
    require(!edge.line_of_sight({5, 4.5}, {5, 6.5}), "Vision slips along an occluding wall boundary");
    require(!edge.line_of_sight({5, 6.5}, {5, 4.5}), "Wall-boundary occlusion depends on ray direction");

    config.max_speed = 20;
    EcosystemWorld pair(config, false);
    pair.creatures = {creature(config, 1, {4, 5}), creature(config, 2, {6, 5}, pi)};
    auto reversed = pair;
    std::reverse(reversed.creatures.begin(), reversed.creatures.end());
    pair.step({{1, 0, 0, 0, 0}, {1, 0, 0, 0, 0}});
    reversed.step({{1, 0, 0, 0, 0}, {1, 0, 0, 0, 0}});
    require(pair.creatures[0].position.x < pair.creatures[1].position.x, "Creatures tunneled through each other");
    require(length(pair.creatures[0].position - pair.creatures[1].position) >= 2 * config.radius - 1e-8, "Creature collision produced overlap");
    for (const auto& c : pair.creatures) {
        near(c.position.x, by_id(reversed, c.id).position.x, "Movement depends on vector order");
        near(c.position.y, by_id(reversed, c.id).position.y, "Movement depends on vector order");
    }
    near(5 - pair.creatures[0].position.x, pair.creatures[1].position.x - 5, "Head-on collision favored a creature");

    EcosystemWorld queue(config, false);
    queue.creatures = {creature(config, 1, {4, 5}), creature(config, 2, {4.6, 5}), creature(config, 3, {5.2, 5})};
    queue.step({{1, 0, 0, 0, 0}, {1, 0, 0, 0, 0}, {}});
    for (std::size_t i = 0; i < queue.creatures.size(); ++i) for (std::size_t j = i + 1; j < queue.creatures.size(); ++j)
        require(length(queue.creatures[i].position - queue.creatures[j].position) >= 2 * config.radius - 1e-8, "Stopped creature was penetrated by its follower");
    near(queue.creatures[2].position.x, 5.2, "Stationary body was pushed");

    EcosystemWorld overlap(config, false);
    overlap.creatures = {creature(config, 1, {5, 5}), creature(config, 2, {5, 5})};
    bool rejected = false;
    try { overlap.step({{}, {}}); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Invalid overlapping initial occupancy was silently accepted");
}

void reproduction_and_capacity()
{
    auto config = fixture_config();
    config.reproduction = true;
    config.maturity_age = 0;
    config.max_population = 2;
    EcosystemWorld world(config, false);
    auto parent = creature(config, 1, {5, 5});
    parent.energy = 150;
    parent.digestion = {{10, 3, FoodKind::FruitA}};
    auto neurons = parent.brain.neurons();
    for (auto& neuron : neurons) { neuron.potential = 0.5; neuron.spiked = true; neuron.refractory_remaining = 0.02; }
    parent.brain = Brain::from_components(config.brain, neurons, {});
    world.creatures = {parent};
    world.next_creature_id = 2;
    world.step({{}});
    require(world.creatures.size() == 2 && world.totals.births == 1, "Eligible creature did not reproduce");
    near(by_id(world, 1).energy, 75, "Wrong parent birth expenditure");
    near(by_id(world, 2).energy, 50, "Wrong offspring initial energy");
    near(world.totals.reproduction_overhead, 25, "Birth creates free energy");
    require(by_id(world, 2).parent_id == 1 && by_id(world, 2).generation == 1, "Offspring lineage is missing");
    require(by_id(world, 2).digestion.empty() && by_id(world, 2).age == 0 && by_id(world, 2).spikes == 0, "Offspring inherited adult runtime state");
    for (const auto& neuron : by_id(world, 2).brain.neurons()) require(neuron.potential == 0 && !neuron.spiked && neuron.refractory_remaining == 0, "Offspring inherited neural memories");
    near(by_id(world, 1).brain.neurons()[0].potential, 0.5, "Birth reset parent's brain");
    require(world.traversable(by_id(world, 2).position), "Offspring intersects terrain");
    require(length(by_id(world, 1).position - by_id(world, 2).position) > 2 * config.radius, "Offspring overlaps parent");
    require(world.capacity_limited, "Population cap did not flag the run");
    const auto full_at = world.step_index;
    world.step({{}, {}});
    require(world.step_index == full_at + 1 && world.totals.births == 1,
        "Full population must keep advancing without births");

    auto priority_config = config;
    priority_config.max_population = 3;
    EcosystemWorld priority(priority_config, false);
    priority.creatures = {creature(priority_config, 1, {3, 5}),
        creature(priority_config, 2, {7, 5}), creature(priority_config, 3, {5, 8})};
    priority.creatures[0].energy = 160;
    priority.creatures[1].energy = 190;
    priority.creatures[2].energy = 10;
    priority.next_creature_id = 4;
    priority.capacity_limited = true;
    priority.step({{}, {}, {}});
    require(priority.totals.births == 0 && priority.step_index == 1,
        "Full population allowed a birth or stopped time");
    near(by_id(priority, 1).energy, 160, "Blocked birth charged energy");
    near(by_id(priority, 2).energy, 190, "Blocked birth charged energy");
    priority.creatures[2].energy = 0;
    auto priority_reversed = priority;
    std::reverse(priority_reversed.creatures.begin(), priority_reversed.creatures.end());
    priority.step({{}, {}, {}});
    priority_reversed.step({{}, {}, {}});
    require(priority.totals.deaths == 1 && priority.totals.births == 1 && priority.creatures.size() == 3,
        "Death at capacity must free a birth slot in the same step");
    require(by_id(priority, 4).parent_id == 2 && by_id(priority_reversed, 4).parent_id == 2,
        "Highest-energy eligible parent must receive the available slot regardless of vector order");
    for (auto& creature : priority.creatures) creature.energy = 100;
    priority.creatures.back().energy = 0;
    priority.step({{}, {}, {}});
    require(!priority.capacity_limited && priority.creatures.size() == 2,
        "Capacity flag remained latched after population decreased");

    config.max_population = 10;
    EcosystemWorld enclosed(config, false);
    std::fill(enclosed.terrain.begin(), enclosed.terrain.end(), Terrain::Wall);
    enclosed.terrain[5 * config.width + 5] = Terrain::Ground;
    parent = creature(config, 1, {5.5, 5.5});
    parent.energy = 150;
    enclosed.creatures = {parent};
    enclosed.next_creature_id = 2;
    enclosed.step({{}});
    require(enclosed.creatures.size() == 1 && enclosed.totals.births == 0, "Offspring spawned through a wall or into overlap");
    near(enclosed.creatures[0].energy, 150, "Blocked birth spent energy");

    config.reproduction_cost = config.reproduction_threshold;
    EcosystemWorld terminal_birth(config, false);
    terminal_birth.creatures = {creature(config, 1, {5, 5})};
    terminal_birth.creatures[0].energy = config.reproduction_threshold;
    terminal_birth.next_creature_id = 2;
    terminal_birth.step({{}});
    require(terminal_birth.creatures.size() == 1 && terminal_birth.creatures[0].id == 2 && terminal_birth.totals.deaths == 1,
        "Parent exhausted by reproduction remained alive at zero energy");

    config.reproduction_cost = 75;
    EcosystemWorld parents(config, false);
    parents.creatures = {creature(config, 1, {4, 5}), creature(config, 2, {6, 5})};
    for (auto& c : parents.creatures) c.energy = 150;
    parents.next_creature_id = 3;
    auto reversed = parents;
    std::reverse(reversed.creatures.begin(), reversed.creatures.end());
    parents.step({{}, {}});
    reversed.step({{}, {}});
    require(parents.totals.births == 2 && reversed.totals.births == 2, "Separated eligible parents did not both reproduce");
    for (const auto& c : parents.creatures) {
        const auto& same = by_id(reversed, c.id);
        require(c.parent_id == same.parent_id, "Reproductive competition depends on creature vector order");
        near(c.position.x, same.position.x, "Birth placement depends on creature vector order");
        near(c.position.y, same.position.y, "Birth placement depends on creature vector order");
        require(c.brain.synapses().size() == same.brain.synapses().size(), "Mutation stream depends on creature vector order");
    }

    for (bool stable : {false, true}) {
        std::size_t exact = 0, slight = 0, strong = 0;
        for (std::uint64_t seed = 1; seed <= 256; ++seed) {
            auto inheritance_config = fixture_config();
            inheritance_config.seed = seed;
            inheritance_config.reproduction = true;
            inheritance_config.maturity_age = 0;
            inheritance_config.max_population = 2;
            inheritance_config.mutation.stable = stable;
            EcosystemWorld inheritance(inheritance_config, false);
            inheritance.creatures = {creature(inheritance_config, 1, {5, 5})};
            inheritance.creatures[0].genome_id = 1;
            inheritance.creatures[0].energy = inheritance_config.reproduction_threshold;
            inheritance.next_creature_id = 2;
            auto expected_rng = inheritance.mutation_rng;
            const double choice = expected_rng.uniform(0, 1);
            auto expected_brain = inheritance.creatures[0].brain;
            if (choice >= 0.25) expected_brain.mutate(choice < 0.75
                ? detail::slight_mutation(inheritance_config.mutation)
                : detail::strong_mutation(inheritance_config.mutation), expected_rng);
            expected_brain.reset_state();
            inheritance.step({{}});
            const auto& child = by_id(inheritance, 2);
            if (choice < 0.25) {
                require(child.genome_id == 1, "Exact offspring lost the parent genome ID");
                ++exact;
            }
            else {
                require(child.genome_id == child.id, "Mutated offspring did not receive a new genome ID");
                if (choice < 0.75) ++slight;
                else ++strong;
            }
            std::ostringstream actual_state, expected_state;
            child.brain.save_state(actual_state); expected_brain.save_state(expected_state);
            require(actual_state.str() == expected_state.str(), "Birth used the wrong mutation preset or failed to reset activity");
        }
        require(exact >= 40 && exact <= 88 && slight >= 96 && slight <= 160 && strong >= 40 && strong <= 88,
            "Birth inheritance is not approximately 25% copy / 50% slight / 25% strong across deterministic seeds");
    }
}

void rejects_invalid_configuration()
{
    const auto rejects = [](EcosystemConfig config) {
        try { config.validate(); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    EcosystemConfig config;
    config.brain.background_event_rate_hz = -1;
    require(rejects(config), "Negative neural noise rate accepted");
    config = EcosystemConfig{};
    config.mutation.add_synapse_probability = 3;
    require(rejects(config), "Mutation probability above one accepted");
    config = EcosystemConfig{};
    config.movement_cost = std::numeric_limits<double>::quiet_NaN();
    require(rejects(config), "NaN energetic cost accepted");
    config = EcosystemConfig{};
    config.width = 257;
    require(rejects(config), "Unsupported map dimension accepted");
    config = EcosystemConfig{};
    config.brain.max_delay_steps = 4097;
    require(rejects(config), "Uncheckpointable neural delay accepted");
    config = EcosystemConfig{};
    config.dt = 0.03;
    require(rejects(config), "Fractional neural substep accepted");
}

void seeded_maps_and_brains()
{
    EcosystemConfig config;
    config.width = 32;
    config.height = 24;
    config.shelters = 3;
    config.grazing_patches = 24;
    config.fruit_patches = 12;
    config.pods = 4;
    config.initial_creatures = 3;
    config.reproduction = false;
    EcosystemWorld a(config);
    config.initial_creatures = 11;
    EcosystemWorld b(config);
    require(a.terrain == b.terrain && a.resources.size() == b.resources.size() && a.fruit_a_rich == b.fruit_a_rich,
        "Population size altered terrain/resources/food assignment");
    for (std::size_t i = 0; i < a.resources.size(); ++i) {
        require(a.resources[i].position.x == b.resources[i].position.x && a.resources[i].position.y == b.resources[i].position.y
            && a.resources[i].kind == b.resources[i].kind && a.resources[i].energy_per_unit == b.resources[i].energy_per_unit,
            "Population size altered a resource");
        const auto expected_energy = a.resources[i].shelter_food ? config.shelter_food_energy : a.resources[i].kind == FoodKind::Graze ? config.graze_energy
            : a.resources[i].kind == FoodKind::Pod ? config.pod_energy
            : ((a.resources[i].kind == FoodKind::FruitA) == a.fruit_a_rich ? config.rich_fruit_energy : config.poor_fruit_energy);
        near(a.resources[i].energy_per_unit, expected_energy, "Generated food ignored configured energy density");
        require(a.traversable(a.resources[i].position) && (a.sheltered(a.resources[i].position) == a.resources[i].shelter_food), "Food was placed in a wall or shelter");
    }
    require(a.map_rng.next_u64() == b.map_rng.next_u64(), "Founder creation consumed map randomness");
    for (std::size_t i = 0; i < a.creatures.size(); ++i) {
        require(a.creatures[i].position.x == b.creatures[i].position.x && a.creatures[i].position.y == b.creatures[i].position.y
            && a.creatures[i].heading == b.creatures[i].heading, "Founder placement prefix changes with population");
        require(a.creatures[i].brain.synapses().size() == b.creatures[i].brain.synapses().size(), "Founder genome changes with population");
        for (std::size_t n = 0; n < a.creatures[i].brain.neurons().size(); ++n) near(a.creatures[i].brain.neurons()[n].threshold,
            b.creatures[i].brain.neurons()[n].threshold, "Founder genome changes with population");
    }
    require(a.creatures[0].brain.neurons().data() != a.creatures[1].brain.neurons().data(), "Creatures share a brain instance");
    require(a.creatures[0].brain.neurons()[0].threshold != a.creatures[1].brain.neurons()[0].threshold, "Founders have cloned rather than individually seeded brains");

    // Flood-fill all traversable cells: every food patch, shelter and spawn must
    // be reachable with the actual fixed body radius, not just center-point LOS.
    std::vector<unsigned char> seen(a.terrain.size(), 0);
    const auto start = static_cast<std::size_t>(a.creatures[0].position.y) * a.config.width + static_cast<std::size_t>(a.creatures[0].position.x);
    std::vector<std::size_t> queue{start};
    seen[start] = 1;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto cell = queue[head];
        const auto visit = [&](std::size_t other) {
            const Vec2 position{static_cast<double>(other % a.config.width) + 0.5, static_cast<double>(other / a.config.width) + 0.5};
            if (!seen[other] && a.traversable(position)) { seen[other] = 1; queue.push_back(other); }
        };
        if (cell % a.config.width > 0) visit(cell - 1);
        if (cell % a.config.width + 1 < a.config.width) visit(cell + 1);
        if (cell >= a.config.width) visit(cell - a.config.width);
        if (cell + a.config.width < a.terrain.size()) visit(cell + a.config.width);
    }
    for (std::size_t cell = 0; cell < a.terrain.size(); ++cell) if (a.terrain[cell] != Terrain::Wall) require(seen[cell] != 0, "Generated map contains an unreachable open region");
}

void long_running_accounts()
{
    auto config = fixture_config();
    config.digestion_delay = 0.2;
    config.basal_cost = 0.2;
    config.forage_cost = 0.3;
    config.storm_cost = 0.8;
    config.calm_duration = 0.5;
    config.warning_duration = 0.3;
    config.storm_duration = 0.4;
    EcosystemWorld world(config, false);
    world.creatures = {creature(config, 1, {4.4, 5}), creature(config, 2, {5.6, 5}, pi)};
    world.resources = {food(1, FoodKind::FruitA, {5, 5}, 1)};
    world.resources[0].regrowth = 0.7;
    for (int i = 0; i < 2000 && !world.creatures.empty(); ++i) {
        world.step(std::vector<EcoAction>(world.creatures.size(), forage()));
        near(world.resources[0].stock + world.totals.consumed_biomass + world.totals.spoiled_biomass,
            1 + world.totals.regrown_biomass, "Long-run biomass conservation failed", 1e-6);
        double reserves = 0, pending = 0;
        for (const auto& c : world.creatures) {
            reserves += c.energy;
            for (const auto& packet : c.digestion) pending += packet.energy;
            require(c.digestion.size() <= 4, "Digestion queue grows without bound");
        }
        const double costs = world.totals.metabolism + world.totals.movement + world.totals.turning + world.totals.foraging
            + world.totals.calling + world.totals.neural + world.totals.exposure + world.totals.reproduction_overhead;
        near(reserves + pending + costs + world.totals.discarded_energy,
            180 + world.totals.consumed_biomass * 10, "Long-run energy conservation failed", 1e-6);
        require(world.events.size() < 20, "Event history accumulated across steps");
    }
}

} // namespace

int main()
{
    try {
        fair_food_and_delayed_energy();
        cooperative_pods();
        weather_shelter_and_costs();
        swept_collisions_and_visibility();
        reproduction_and_capacity();
        rejects_invalid_configuration();
        seeded_maps_and_brains();
        long_running_accounts();
        std::cout << "Ecosystem mechanics tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
