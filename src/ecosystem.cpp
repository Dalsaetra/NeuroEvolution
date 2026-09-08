#include "neuroevo/ecosystem.hpp"
#include "ecosystem_terrain.hpp"
#include "ecosystem_mutation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace neuroevo {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double epsilon = 1e-10;

std::uint64_t mix(std::uint64_t value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
double squared(Vec2 value) { return dot(value, value); }
double angle(double value) { return std::remainder(value, 2 * pi); }

template <typename T> void shuffle(std::vector<T>& values, Random& rng)
{
    for (std::size_t i = values.size(); i > 1; --i) {
        std::swap(values[i - 1], values[rng.uniform_index(i)]);
    }
}

double phase_time(const EcosystemWorld& world)
{
    const auto& cfg = world.config;
    const double cycle = cfg.calm_duration + cfg.warning_duration + cfg.storm_duration;
    double phase = std::fmod(world.time() + cfg.phase_offset, cycle);
    if (phase < 0) phase += cycle;
    return phase;
}

double point_segment_distance_squared(Vec2 point, Vec2 a, Vec2 b)
{
    const Vec2 delta = b - a;
    const double denominator = squared(delta);
    const double t = denominator == 0 ? 0 : std::clamp(dot(point - a, delta) / denominator, 0.0, 1.0);
    return squared(point - (a + delta * t));
}

// Inclusive slab test: rays that graze a wall corner do not see through it.
bool segment_box(Vec2 a, Vec2 b, double left, double bottom, double right, double top)
{
    double minimum = 0, maximum = 1;
    const double origins[] = {a.x, a.y};
    const double directions[] = {b.x - a.x, b.y - a.y};
    const double lower[] = {left, bottom}, upper[] = {right, top};
    for (int axis = 0; axis < 2; ++axis) {
        if (std::abs(directions[axis]) < 1e-15) {
            if (origins[axis] < lower[axis] || origins[axis] > upper[axis]) return false;
        } else {
            double first = (lower[axis] - origins[axis]) / directions[axis];
            double last = (upper[axis] - origins[axis]) / directions[axis];
            if (first > last) std::swap(first, last);
            minimum = std::max(minimum, first);
            maximum = std::min(maximum, last);
            if (minimum > maximum) return false;
        }
    }
    return true;
}

double segment_box_distance_squared(Vec2 a, Vec2 b, double x, double y)
{
    if (segment_box(a, b, x, y, x + 1, y + 1)) return 0;
    const auto point_box = [x, y](Vec2 point) {
        return squared(point - Vec2{std::clamp(point.x, x, x + 1), std::clamp(point.y, y, y + 1)});
    };
    double distance = std::min(point_box(a), point_box(b));
    for (const auto corner : {Vec2{x, y}, Vec2{x + 1, y}, Vec2{x, y + 1}, Vec2{x + 1, y + 1}}) {
        distance = std::min(distance, point_segment_distance_squared(corner, a, b));
    }
    return distance;
}

// Exact swept circle against grid squares; small movement substeps additionally
// make conservative creature/creature blocking unobtrusive at contact.
bool motion_clear(const EcosystemWorld& world, Vec2 a, Vec2 b)
{
    const double radius = world.config.radius;
    if (!world.traversable(b)) return false;
    const int low_x = std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x) - radius)));
    const int high_x = std::min(static_cast<int>(world.config.width) - 1, static_cast<int>(std::floor(std::max(a.x, b.x) + radius)));
    const int low_y = std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y) - radius)));
    const int high_y = std::min(static_cast<int>(world.config.height) - 1, static_cast<int>(std::floor(std::max(a.y, b.y) + radius)));
    for (int y = low_y; y <= high_y; ++y) {
        for (int x = low_x; x <= high_x; ++x) {
            if (world.terrain[static_cast<std::size_t>(y) * world.config.width + static_cast<std::size_t>(x)] == Terrain::Wall
                && segment_box_distance_squared(a, b, x, y) < radius * radius - epsilon) return false;
        }
    }
    return true;
}

bool paths_collide(Vec2 a, Vec2 a_end, Vec2 b, Vec2 b_end, double diameter)
{
    const Vec2 relative_start = a - b;
    const Vec2 relative_velocity = (a_end - a) - (b_end - b);
    const double speed_squared = squared(relative_velocity);
    const double closest = speed_squared == 0 ? 0
        : std::clamp(-dot(relative_start, relative_velocity) / speed_squared, 0.0, 1.0);
    return squared(relative_start + relative_velocity * closest) < diameter * diameter - epsilon;
}

bool connected(const EcosystemWorld& world)
{
    const auto first = std::find_if(world.terrain.begin(), world.terrain.end(), [](Terrain t) { return t != Terrain::Wall; });
    if (first == world.terrain.end()) return false;
    std::vector<unsigned char> visited(world.terrain.size(), 0);
    std::vector<std::size_t> queue{static_cast<std::size_t>(first - world.terrain.begin())};
    visited[queue[0]] = 1;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::size_t cell = queue[head], x = cell % world.config.width, y = cell / world.config.width;
        const auto visit = [&](std::size_t next) {
            if (!visited[next] && world.terrain[next] != Terrain::Wall) {
                visited[next] = 1;
                queue.push_back(next);
            }
        };
        if (x > 0) visit(cell - 1);
        if (x + 1 < world.config.width) visit(cell + 1);
        if (y > 0) visit(cell - world.config.width);
        if (y + 1 < world.config.height) visit(cell + world.config.width);
    }
    return queue.size() == static_cast<std::size_t>(std::count_if(world.terrain.begin(), world.terrain.end(), [](Terrain t) { return t != Terrain::Wall; }));
}

EcoAction bounded_action(EcoAction action, bool communication)
{
    for (double* value : {&action.forward, &action.left, &action.right, &action.forage, &action.call, &action.attack}) {
        if (!std::isfinite(*value)) throw std::invalid_argument("Creature actions must be finite");
        *value = std::clamp(*value, 0.0, 1.0);
    }
    if (!communication) action.call = 0;
    return action;
}

} // namespace

EcosystemWorld::EcosystemWorld(EcosystemConfig settings, bool generate)
    : config(std::move(settings)), map_rng(mix(config.seed ^ 0x6d6170ULL)),
      mutation_rng(mix(config.seed ^ 0x6d7574617465ULL)), conflict_rng(mix(config.seed ^ 0x746965ULL))
{
    config.validate();
    terrain.assign(config.width * config.height, Terrain::Ground);
    if (generate) generate_world();
}

double EcosystemWorld::time() const { return static_cast<double>(step_index) * config.dt; }

WeatherPhase EcosystemWorld::weather() const
{
    if (!config.storms_enabled) return WeatherPhase::Calm;
    const double phase = phase_time(*this);
    if (phase < config.calm_duration - epsilon) return WeatherPhase::Calm;
    if (phase < config.calm_duration + config.warning_duration - epsilon) return WeatherPhase::Warning;
    return WeatherPhase::Storm;
}

double EcosystemWorld::storm_cue() const
{
    if (!config.storms_enabled) return 0;
    const auto phase = weather();
    if (phase == WeatherPhase::Calm) return 0;
    if (phase == WeatherPhase::Storm) return 1;
    return std::clamp((phase_time(*this) - config.calm_duration) / config.warning_duration, 0.0, 1.0);
}

Terrain EcosystemWorld::terrain_at(Vec2 position) const
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || position.x < 0 || position.y < 0
        || position.x >= static_cast<double>(config.width) || position.y >= static_cast<double>(config.height)) return Terrain::Wall;
    return terrain[static_cast<std::size_t>(position.y) * config.width + static_cast<std::size_t>(position.x)];
}

bool EcosystemWorld::sheltered(Vec2 position) const { return terrain_at(position) == Terrain::Shelter; }

bool EcosystemWorld::in_nursery(Vec2 p) const
{
    if (!config.nursery_frontier) return false;
    const auto x = (config.width - config.nursery_size) / 2;
    const auto y = (config.height - config.nursery_size) / 2;
    return p.x >= x && p.y >= y && p.x < x + config.nursery_size && p.y < y + config.nursery_size;
}

bool EcosystemWorld::traversable(Vec2 position) const
{
    const double radius = config.radius;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || position.x < radius || position.y < radius
        || position.x > static_cast<double>(config.width) - radius || position.y > static_cast<double>(config.height) - radius) return false;
    const int left = std::max(0, static_cast<int>(std::floor(position.x - radius)));
    const int right = std::min(static_cast<int>(config.width) - 1, static_cast<int>(std::floor(position.x + radius)));
    const int bottom = std::max(0, static_cast<int>(std::floor(position.y - radius)));
    const int top = std::min(static_cast<int>(config.height) - 1, static_cast<int>(std::floor(position.y + radius)));
    for (int y = bottom; y <= top; ++y) {
        for (int x = left; x <= right; ++x) {
            if (terrain[static_cast<std::size_t>(y) * config.width + static_cast<std::size_t>(x)] == Terrain::Wall) {
                const Vec2 closest{std::clamp(position.x, static_cast<double>(x), static_cast<double>(x + 1)), std::clamp(position.y, static_cast<double>(y), static_cast<double>(y + 1))};
                if (squared(position - closest) < radius * radius - epsilon) return false;
            }
        }
    }
    return true;
}

bool EcosystemWorld::line_of_sight(Vec2 from, Vec2 to) const
{
    if (terrain_at(from) == Terrain::Wall || terrain_at(to) == Terrain::Wall) return false;
    const int left = std::max(0, static_cast<int>(std::floor(std::min(from.x, to.x))) - 1);
    const int right = std::min(static_cast<int>(config.width) - 1, static_cast<int>(std::floor(std::max(from.x, to.x))));
    const int bottom = std::max(0, static_cast<int>(std::floor(std::min(from.y, to.y))) - 1);
    const int top = std::min(static_cast<int>(config.height) - 1, static_cast<int>(std::floor(std::max(from.y, to.y))));
    for (int y = bottom; y <= top; ++y) {
        for (int x = left; x <= right; ++x) {
            if (terrain[static_cast<std::size_t>(y) * config.width + static_cast<std::size_t>(x)] == Terrain::Wall
                && segment_box(from, to, x, y, x + 1, y + 1)) return false;
        }
    }
    return true;
}

void EcosystemWorld::generate_world()
{
    config.validate();
    map_rng = Random(mix(config.seed ^ 0x6d6170ULL));
    mutation_rng = Random(mix(config.seed ^ 0x6d7574617465ULL));
    conflict_rng = Random(mix(config.seed ^ 0x746965ULL));
    step_index = 0;
    next_creature_id = 1;
    next_resource_id = 1;
    capacity_limited = false;
    totals = {};
    events.clear();
    creatures.clear();
    resources.clear();
    terrain.assign(config.width * config.height, Terrain::Ground);
    if (config.nursery_frontier) { generate_nursery_frontier(); return; }
    std::vector<std::size_t> interior;
    for (std::size_t y = 0; y < config.height; ++y) {
        for (std::size_t x = 0; x < config.width; ++x) {
            const std::size_t cell = y * config.width + x;
            if (x == 0 || y == 0 || x + 1 == config.width || y + 1 == config.height) terrain[cell] = Terrain::Wall;
            else {
                interior.push_back(cell);
                if (map_rng.chance(0.12)) terrain[cell] = Terrain::Rough;
            }
        }
    }
    // Each obstacle is accepted only if all remaining open cells are connected.
    // This recipe never reads population size: changing it preserves the map.
    const std::size_t required_space = config.grazing_patches + config.fruit_patches + config.pods + config.shelters * config.shelter_size * config.shelter_size;
    if (required_space > interior.size()) throw std::invalid_argument("Resource and shelter counts do not fit in this world");
    for (std::size_t obstacle = 0; obstacle < interior.size() / 45; ++obstacle) {
        const std::size_t anchor = interior[map_rng.uniform_index(interior.size())];
        const std::size_t width = 1 + map_rng.uniform_index(3), height = 1 + map_rng.uniform_index(3);
        std::vector<std::pair<std::size_t, Terrain>> previous;
        for (std::size_t y = anchor / config.width; y < std::min(config.height - 1, anchor / config.width + height); ++y) {
            for (std::size_t x = anchor % config.width; x < std::min(config.width - 1, anchor % config.width + width); ++x) {
                const std::size_t cell = y * config.width + x;
                previous.emplace_back(cell, terrain[cell]);
                terrain[cell] = Terrain::Wall;
            }
        }
        const auto open = static_cast<std::size_t>(std::count_if(terrain.begin(), terrain.end(), [](Terrain t) { return t != Terrain::Wall; }));
        if (open < required_space || !connected(*this)) for (const auto& saved : previous) terrain[saved.first] = saved.second;
    }
    shuffle(interior, map_rng);
    std::vector<Vec2> shelter_centers;
    const auto low=config.shelter_size/2, high=config.shelter_size-1-low;
    // Place connected square shelter floors in existing open regions, preserving
    // at least two open approaches. There is deliberately no food indoors.
    for (const auto cell : interior) {
        if (shelter_centers.size() == config.shelters) break;
        const std::size_t x = cell % config.width, y = cell / config.width;
        if (x < low+1 || y < low+1 || x + high+1 >= config.width || y + high+1 >= config.height) continue;
        const Vec2 center{static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5};
        if (std::any_of(shelter_centers.begin(), shelter_centers.end(), [&](Vec2 previous) { return length(center - previous) < config.shelter_size+2; })) continue;
        bool clear = true;
        for (std::size_t yy = y - low; yy <= y + high; ++yy) for (std::size_t xx = x - low; xx <= x + high; ++xx) if (terrain[yy * config.width + xx] == Terrain::Wall || terrain[yy * config.width + xx] == Terrain::Shelter) clear = false;
        if (!clear) continue;
        int approaches = 0;
        for (const auto neighbour : {cell - (low+1), cell + (high+1), cell - (low+1) * config.width, cell + (high+1) * config.width}) if (terrain[neighbour] != Terrain::Wall) ++approaches;
        if (approaches < 2) continue;
        for (std::size_t yy = y - low; yy <= y + high; ++yy) for (std::size_t xx = x - low; xx <= x + high; ++xx) terrain[yy * config.width + xx] = Terrain::Shelter;
        shelter_centers.push_back(center);
    }
    if (shelter_centers.size() != config.shelters) throw std::invalid_argument("Not enough separate open areas for the requested shelters; enlarge the map or reduce shelters");

    cluster_rough_ground(*this,0.12);
    for (const auto center : shelter_centers) add_shelter_food(center);

    fruit_a_rich = config.food_assignment < 0 ? map_rng.chance(0.5) : config.food_assignment == 0;
    std::vector<unsigned char> occupied(terrain.size(), 0);
    const auto position_of = [&](std::size_t cell) { return Vec2{static_cast<double>(cell % config.width) + 0.5, static_cast<double>(cell / config.width) + 0.5}; };
    const auto available = [&](std::size_t cell) { return !occupied[cell] && terrain[cell] != Terrain::Wall && terrain[cell] != Terrain::Shelter; };
    Random food_age_rng(config.seed ^ 0x666f6f64616765ULL);
    const auto add_resource = [&](std::size_t cell, FoodKind kind) {
        EcoResource resource;
        resource.id = resources.size() + 1;
        resource.position = position_of(cell);
        resource.kind = kind;
        resource.capacity = kind == FoodKind::Graze ? config.graze_capacity : kind == FoodKind::Pod ? config.pod_capacity : config.fruit_capacity;
        resource.stock = resource.capacity;
        if (config.outdoor_food_relocates && kind != FoodKind::Pod)
            resource.stock *= food_age_rng.uniform(0.0, 1.0);
        resource.regrowth = kind == FoodKind::Graze ? config.graze_regrowth : kind == FoodKind::Pod ? config.pod_regrowth : config.fruit_regrowth;
        resource.energy_per_unit = kind == FoodKind::Graze ? config.graze_energy
            : kind == FoodKind::Pod ? config.pod_energy
            : ((kind == FoodKind::FruitA) == fruit_a_rich ? config.rich_fruit_energy : config.poor_fruit_energy);
        resources.push_back(resource);
        occupied[cell] = 1;
    };
    // Reserve pods first so every pod has at least two physical approaches.
    std::size_t pods_placed = 0;
    for (const auto cell : interior) {
        if (pods_placed == config.pods) break;
        if (!available(cell)) continue;
        int approaches = 0;
        for (const auto neighbour : {cell - 1, cell + 1, cell - config.width, cell + config.width}) if (terrain[neighbour] != Terrain::Wall) ++approaches;
        if (approaches >= 2) { add_resource(cell, FoodKind::Pod); ++pods_placed; }
    }
    if (pods_placed != config.pods) throw std::invalid_argument("Not enough accessible pod locations");
    std::size_t grazing_placed = 0;
    for (const auto shelter : shelter_centers) {
        std::size_t nearby = 0;
        for (const auto cell : interior) {
            if (nearby == 4 || grazing_placed == config.grazing_patches) break;
            if (available(cell) && length(position_of(cell) - shelter) < 3.6) {
                add_resource(cell, FoodKind::Graze);
                ++nearby;
                ++grazing_placed;
            }
        }
    }
    for (const auto cell : interior) {
        if (grazing_placed == config.grazing_patches) break;
        if (available(cell)) { add_resource(cell, FoodKind::Graze); ++grazing_placed; }
    }
    std::size_t fruit_placed = 0;
    for (const auto cell : interior) {
        if (fruit_placed == config.fruit_patches) break;
        if (available(cell)) { add_resource(cell, fruit_placed % 2 == 0 ? FoodKind::FruitA : FoodKind::FruitB); ++fruit_placed; }
    }
    if (grazing_placed != config.grazing_patches || fruit_placed != config.fruit_patches) throw std::invalid_argument("Not enough separate food locations");

    // Spawn and genotype streams are isolated from map generation and one
    // another. The first N founders are identical in N- and M-founder worlds.
    Random spawn_rng(mix(config.seed ^ 0x737061776eULL));
    std::vector<std::size_t> spawning;
    for (const auto cell : interior) {
        if (terrain[cell] == Terrain::Wall) continue;
        bool wide = true;
        for (const auto neighbour : {cell - 1, cell + 1, cell - config.width, cell + config.width}) if (terrain[neighbour] == Terrain::Wall) wide = false;
        if (wide) spawning.push_back(cell);
    }
    shuffle(spawning, spawn_rng);
    std::stable_partition(spawning.begin(), spawning.end(), [&](std::size_t cell) {
        const auto p = position_of(cell);
        return std::any_of(resources.begin(), resources.end(), [p](const EcoResource& resource) { return resource.kind == FoodKind::Graze && length(p - resource.position) <= 3; });
    });
    if (config.initial_creatures > spawning.size()) throw std::invalid_argument("Not enough roomy, nonoverlapping spawn cells; enlarge the world or reduce founders");
    for (std::size_t i = 0; i < config.initial_creatures; ++i) {
        EcoCreature creature;
        creature.id = next_creature_id++;
        creature.genome_id = creature.id;
        creature.position = position_of(spawning[i]);
        creature.heading = spawn_rng.uniform(-pi, pi);
        creature.energy = config.founder_energy;
        creature.controller = config.controller;
        Random genome_rng(mix(config.seed ^ mix(creature.id) ^ 0x67656e6f6d65ULL));
        creature.brain = config.sparse_ancestor && config.controller == ControllerKind::Spiking
            ? make_sparse_ancestral_brain(config) : Brain::random(config.brain, genome_rng);
        if (config.sparse_ancestor && config.controller == ControllerKind::Spiking) creature.genome_id=1;
        creature.brain.reset_state();
        creature.neural_rng = Random(mix(config.seed ^ mix(creature.id) ^ 0x6e657572616cULL));
        initialize_body(creature);
        creatures.push_back(std::move(creature));
    }
    if (config.reproduction && creatures.size() >= config.max_population) {
        capacity_limited = true;
        events.push_back({time(), "capacity_limited", 0, 0, 0, static_cast<double>(creatures.size())});
    }
}

void EcosystemWorld::step(const std::vector<EcoAction>& supplied_actions)
{
    if (!supplied_actions.empty() && supplied_actions.size() != creatures.size()) throw std::invalid_argument("Supplied actions must match the starting population");
    if (terrain.size() != config.width * config.height) throw std::runtime_error("Terrain dimensions do not match the world configuration");
    for (std::size_t i = 0; i < creatures.size(); ++i) {
        if (!traversable(creatures[i].position)) throw std::runtime_error("A creature begins the step inside a wall or outside the world");
        if (!std::isfinite(creatures[i].heading) || !std::isfinite(creatures[i].energy)) throw std::runtime_error("Creature heading and energy must be finite");
        if (config.predation && (!std::isfinite(creatures[i].body.mass)
            || creatures[i].body.mass < eco_min_mass || creatures[i].body.mass > eco_max_mass
            || !std::isfinite(creatures[i].body.carnivory) || creatures[i].body.carnivory < 0 || creatures[i].body.carnivory > 1
            || !std::isfinite(creatures[i].health) || creatures[i].health > max_health(creatures[i]) + epsilon))
            throw std::runtime_error("Invalid creature body or health");
        for (std::size_t j = 0; j < i; ++j) {
            if (squared(creatures[i].position - creatures[j].position) < 4 * config.radius * config.radius - epsilon) throw std::runtime_error("Creatures overlap at the start of a step");
        }
    }
    events.clear();
    const std::size_t population = creatures.size();
    const double start = time(), end = start + config.dt;
    const auto initial_weather = weather();
    const bool storm = initial_weather == WeatherPhase::Storm;
    const std::uint64_t tie_seed = conflict_rng.next_u64();

    // 1. All controllers see the same positions, previous actions and feedback.
    // Their only private writes during this stage are neural state and noise.
    std::vector<EcoAction> actions;
    actions.reserve(population);
    for (std::size_t i = 0; i < population; ++i) {
        creatures[i].step_spikes = 0;
        actions.push_back(bounded_action(supplied_actions.empty() ? control(i) : supplied_actions[i], config.communication));
        if (!config.predation) actions.back().attack = 0;
    }
    std::vector<Vec2> beginning(population), displacements(population);
    double largest_displacement = 0;
    for (std::size_t i = 0; i < population; ++i) {
        auto& creature = creatures[i];
        beginning[i] = creature.position;
        creature.action = actions[i];
        creature.ingestion_pulse = creature.digestion_pulse = 0;
        creature.turn = (actions[i].left - actions[i].right) * config.max_turn_rate;
        creature.heading = angle(creature.heading + creature.turn * config.dt);
        const double distance = actions[i].forward * maximum_speed(creature) * (1 - 0.75 * actions[i].forage) * config.dt;
        displacements[i] = Vec2{std::cos(creature.heading), std::sin(creature.heading)} * distance;
        largest_displacement = std::max(largest_displacement, distance);
        totals.spikes += creature.step_spikes;
    }
    // 2. Conservative simultaneous movement. Reject both paths in a collision;
    // repeat after rejection, since stopping somebody may block a follower.
    // Every decision in a pass uses an immutable proposal batch (no ID priority).
    const std::size_t substeps = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(largest_displacement / (config.radius * 0.5))));
    const double diameter = 2 * config.radius;
    std::vector<Vec2> positions(population), proposed(population);
    std::vector<unsigned char> blocked(population), newly_blocked(population);
    for (std::size_t substep = 0; substep < substeps; ++substep) {
        std::fill(blocked.begin(), blocked.end(), 0);
        for (std::size_t i = 0; i < population; ++i) {
            positions[i] = creatures[i].position;
            const Vec2 delta = displacements[i] * (1.0 / static_cast<double>(substeps));
            proposed[i] = positions[i] + delta;
            if (!motion_clear(*this, positions[i], proposed[i])) {
                double low = 0, high = 1;
                for (int iteration = 0; iteration < 16; ++iteration) {
                    const double middle = (low + high) * 0.5;
                    if (motion_clear(*this, positions[i], positions[i] + delta * middle)) low = middle;
                    else high = middle;
                }
                proposed[i] = positions[i] + delta * low;
            }
        }
        bool changed;
        do {
            changed = false;
            std::fill(newly_blocked.begin(), newly_blocked.end(), 0);
            for (std::size_t i = 0; i < population; ++i) {
                for (std::size_t j = i + 1; j < population; ++j) {
                    if (paths_collide(positions[i], proposed[i], positions[j], proposed[j], diameter)) {
                        if (!blocked[i] && squared(proposed[i] - positions[i]) > 0) newly_blocked[i] = 1;
                        if (!blocked[j] && squared(proposed[j] - positions[j]) > 0) newly_blocked[j] = 1;
                    }
                }
            }
            for (std::size_t i = 0; i < population; ++i) if (newly_blocked[i]) {
                blocked[i] = 1;
                proposed[i] = positions[i];
                changed = true;
            }
        } while (changed);
        for (std::size_t i = 0; i < population; ++i) creatures[i].position = proposed[i];
    }
    for (std::size_t i = 0; i < population; ++i) creatures[i].speed = length(creatures[i].position - beginning[i]) / config.dt;

    // Paid, continuous attack effort. Select from an immutable geometry/liveness
    // snapshot, then accumulate hits in stable attacker order before applying damage.
    if (config.predation) {
        std::vector<std::size_t> attackers(population);
        std::iota(attackers.begin(), attackers.end(), 0);
        std::sort(attackers.begin(), attackers.end(), [&](auto a, auto b) { return creatures[a].id < creatures[b].id; });
        std::vector<double> damage(population, 0);
        for (auto& c : creatures) c.damage_pulse = 0;
        for (const auto i : attackers) {
            auto& c = creatures[i];
            if (c.health <= 0 || c.energy <= 0 || c.action.attack <= 0) continue;
            std::size_t target = population;
            double nearest = config.attack_range + epsilon;
            std::uint64_t best_tie = std::numeric_limits<std::uint64_t>::max();
            for (std::size_t j = 0; j < population; ++j) {
                if (i == j || creatures[j].health <= 0) continue;
                const auto delta = creatures[j].position - c.position;
                const double distance = length(delta);
                if (distance > config.attack_range + epsilon || distance > nearest + epsilon
                    || std::abs(angle(std::atan2(delta.y, delta.x) - c.heading)) > config.attack_degrees * pi / 360 + epsilon
                    || !line_of_sight(c.position, creatures[j].position)) continue;
                const auto tie = mix(tie_seed ^ mix(c.id) ^ mix(creatures[j].id));
                if (target == population || distance < nearest - epsilon || tie < best_tie) {
                    target = j; nearest = distance; best_tie = tie;
                }
            }
            const double paid = std::min(c.energy, config.attack_cost * c.action.attack * config.dt);
            c.energy -= paid; c.energy_spent += paid; totals.attacking += paid;
            if (target != population) {
                // Nursery protection follows the target's post-movement position,
                // including attacks across a gate. Effort still costs energy.
                const double diet_strength = config.attack_base_fraction
                    + (1.0 - config.attack_base_fraction) * c.body.carnivory;
                const double hit = in_nursery(creatures[target].position) ? 0.0
                    : config.attack_damage * diet_strength * paid / config.attack_cost;
                damage[target] += hit;
                events.push_back({end, "attack_hit", c.id, creatures[target].id, 0, hit});
            }
        }
        for (std::size_t i = 0; i < population; ++i) {
            auto& c = creatures[i];
            c.damage_pulse = std::min(std::max(0.0, c.health), damage[i]);
            c.health = std::max(0.0, c.health - damage[i]);
            totals.damage += c.damage_pulse;
        }
    }

    // 3. Targets use post-movement geometry and the starting resource state.
    // A seeded ID hash breaks exact distance ties without vector-order priority.
    std::vector<std::vector<std::size_t>> requests(resources.size());
    for (std::size_t i = 0; i < population; ++i) {
        const auto& creature = creatures[i];
        if (creature.action.forage <= 0 || creature.energy <= 0 || (config.predation && creature.health <= 0)) continue;
        // Foraging intent and its energy cost remain, but exposed creatures
        // cannot harvest during the storm interval. Use post-movement shelter.
        if (storm && !sheltered(creature.position)) continue;
        std::size_t target = resources.size();
        double nearest = config.interaction_range + epsilon;
        std::uint64_t best_tie = std::numeric_limits<std::uint64_t>::max();
        for (std::size_t r = 0; r < resources.size(); ++r) {
            if (dietary_efficiency(creature, resources[r].kind) <= 0) continue;
            if (config.extended_senses && (resources[r].stock <= epsilon
                || (resources[r].kind == FoodKind::Pod && resources[r].pod_state == PodState::Refilling))) continue;
            const Vec2 difference = resources[r].position - creature.position;
            const double distance = length(difference);
            if (distance > config.interaction_range + epsilon || distance > nearest + epsilon) continue;
            if (distance > epsilon && std::abs(angle(std::atan2(difference.y, difference.x) - creature.heading)) > config.interaction_degrees * pi / 360 + epsilon) continue;
            if (!line_of_sight(creature.position, resources[r].position)) continue;
            const auto tie = mix(tie_seed ^ mix(creature.id) ^ mix(resources[r].id));
            if (target == resources.size() || distance < nearest - epsilon || tie < best_tie) {
                target = r;
                nearest = distance;
                best_tie = tie;
            }
        }
        if (target != resources.size()) requests[target].push_back(i);
    }
    for (std::size_t r = 0; r < resources.size(); ++r) {
        auto& resource = resources[r];
        auto& consumers = requests[r];
        // Stable ID summation also makes roundoff independent of vector order.
        std::sort(consumers.begin(), consumers.end(), [&](std::size_t a, std::size_t b) { return creatures[a].id < creatures[b].id; });
        if (resource.kind == FoodKind::Pod && resource.pod_state == PodState::Open && start + epsilon >= resource.opened_at + config.pod_open_duration) {
            totals.spoiled_biomass += resource.stock;
            events.push_back({end, "pod_spoiled", 0, 0, resource.id, resource.stock});
            resource.stock = 0;
            resource.progress = 0;
            resource.pod_state = PodState::Refilling;
        }
        if (resource.kind == FoodKind::Pod && resource.pod_state == PodState::Closed) {
            double work = 0;
            for (const auto i : consumers) {
                const double contribution = creatures[i].action.forage * config.dt;
                work += creatures[i].action.forage;
                creatures[i].pod_work += contribution;
                events.push_back({end, "pod_work", creatures[i].id, 0, resource.id, contribution});
            }
            work = std::min(2.0, work);
            resource.progress = std::clamp(resource.progress + (work > 0 ? work * work : -config.pod_decay) * config.dt, 0.0, config.pod_work);
            if (resource.progress + epsilon >= config.pod_work) {
                resource.progress = config.pod_work;
                resource.pod_state = PodState::Open;
                resource.opened_at = end;
                ++totals.pods_opened;
                events.push_back({end, "pod_opened", 0, 0, resource.id, resource.stock});
            }
            continue; // A pod opened now is edible only in the next step.
        }
        if (resource.kind == FoodKind::Pod && resource.pod_state == PodState::Refilling) continue;
        // Nursery forage is delicate: a passing creature can taste it, but
        // harvesting efficiently requires reducing locomotor drive. This gives
        // ingestion feedback time to engage the ancestor's feeding pause.
        const auto demand = [&](std::size_t i) {
            const auto& action = creatures[i].action;
            const double settled = 1.0 - std::clamp(action.forward, 0.0, 1.0);
            const double efficiency = (resource.kind != FoodKind::Meat && in_nursery(resource.position)) ? 0.35 + 0.65 * settled * settled : 1.0;
            // Low-carnivory passers-by can only take small bites. Apply this
            // before shared allocation so they cannot strip a corpse at full speed.
            const double meat_rate = resource.kind == FoodKind::Meat ? creatures[i].body.carnivory : 1.0;
            return action.forage * config.ingestion_rate * config.dt * efficiency * meat_rate;
        };
        double requested = 0;
        for (const auto i : consumers) requested += demand(i);
        const double allocated = std::min(std::max(0.0, resource.stock), requested);
        if (allocated <= 0 || requested <= 0) continue;
        const double share = allocated / requested;
        for (const auto i : consumers) {
            auto& creature = creatures[i];
            const double amount = demand(i) * share;
            creature.ingestion_pulse += amount;
            if (end - creature.last_fed_time > 5.0) ++creature.feeding_bouts;
            creature.last_fed_time = end;
            creature.eaten[static_cast<std::size_t>(resource.kind)] += amount;
            const double raw_energy = amount * resource.energy_per_unit;
            const double digestible = raw_energy * dietary_efficiency(creature, resource.kind);
            totals.discarded_energy += raw_energy - digestible;
            creature.digestion.push_back({end + config.digestion_delay, digestible, resource.kind});
            events.push_back({end, "ingestion", creature.id, 0, resource.id, amount});
        }
        resource.stock = std::max(0.0, resource.stock - allocated);
        totals.consumed_biomass += allocated;
        if (resource.kind == FoodKind::Pod && resource.stock <= epsilon) {
            // Account for any floating point residue instead of creating food.
            totals.spoiled_biomass += resource.stock;
            resource.stock = 0;
            resource.progress = 0;
            resource.pod_state = PodState::Refilling;
        }
    }

    // Relocate only after all feeding allocations: newly placed food cannot be
    // eaten through another creature's stale target in this step.
    for (auto& resource : resources) {
        if (resource.kind == FoodKind::Meat || !in_nursery(resource.position)) continue;
        const double spoiled=std::min(resource.stock,config.nursery_food_decay*config.dt);
        resource.stock-=spoiled;
        totals.spoiled_biomass+=spoiled;
        if (config.nursery_food_relocates && resource.stock<=epsilon
            && relocate_nursery_food(resource,map_rng,true)) {
            totals.regrown_biomass += resource.capacity-resource.stock;
            resource.stock=resource.capacity;
            events.push_back({end,"nursery_food_relocated",0,0,resource.id,resource.stock});
        }
    }

    if(config.shelter_food_decay>0)for(auto& resource:resources) {
        if(!resource.shelter_food)continue;
        const double spoiled=std::min(resource.stock,config.shelter_food_decay*config.dt);
        resource.stock-=spoiled;totals.spoiled_biomass+=spoiled;
        if(resource.stock<=epsilon && relocate_shelter_food(resource)) {
            totals.regrown_biomass+=resource.capacity-resource.stock;
            resource.stock=resource.capacity;
            events.push_back({end,"shelter_food_relocated",0,0,resource.id,resource.stock});
        }
    }

    if (config.outdoor_food_relocates) for (auto& resource : resources) {
        if (resource.kind == FoodKind::Meat || resource.kind == FoodKind::Pod || resource.shelter_food || in_nursery(resource.position)) continue;
        const double decay = resource.kind == FoodKind::Graze ? config.graze_decay : config.fruit_decay;
        const double spoiled = std::min(resource.stock, decay * config.dt);
        resource.stock -= spoiled;
        totals.spoiled_biomass += spoiled;
        if (resource.stock <= epsilon && relocate_outdoor_food(resource)) {
            totals.regrown_biomass += resource.capacity - resource.stock;
            resource.stock = resource.capacity;
            events.push_back({end,"outdoor_food_relocated",0,0,resource.id,resource.stock});
        }
    }

    // Existing corpses decay everywhere, independently of weather/plant policies.
    for (auto& r : resources) if (r.kind == FoodKind::Meat) {
        const double spoiled = std::min(r.stock, config.meat_decay * config.dt);
        r.stock -= spoiled;
        totals.spoiled_biomass += spoiled;
        totals.meat_spoiled_energy += spoiled * r.energy_per_unit;
    }
    resources.erase(std::remove_if(resources.begin(), resources.end(), [](const auto& r) {
        return r.kind == FoodKind::Meat && r.stock <= 0;
    }), resources.end());

    // 4. Due digestive packets arrive at the end boundary, then this interval's
    // energetic costs are charged. Future packets cannot rescue a starving body.
    for (auto& creature : creatures) {
        const bool killed = config.predation && creature.health <= 0;
        std::size_t pending = 0;
        for (const auto& packet : creature.digestion) {
            if (!killed && packet.due <= end + epsilon) {
                const double gain = std::min(packet.energy, std::max(0.0, config.energy_capacity - creature.energy));
                creature.energy += gain;
                creature.energy_gained += gain;
                creature.digestion_pulse += gain;
                totals.energy_gained += gain;
                totals.discarded_energy += packet.energy - gain;
                events.push_back({end, "digestion", creature.id, 0, 0, gain});
            } else creature.digestion[pending++] = packet;
        }
        creature.digestion.resize(pending);
        const auto stats = creature.brain.stats();
        const double rough = terrain_at(creature.position) == Terrain::Rough ? config.rough_multiplier : 1;
        const double diet_metabolism = config.predation
            ? 1.0 - (1.0 - config.carnivore_basal_fraction) * creature.body.carnivory : 1.0;
        const double metabolism = config.basal_cost * (config.predation ? creature.body.mass : 1.0)
            * diet_metabolism * config.dt;
        const double movement = config.movement_cost * creature.action.forward * creature.action.forward * rough * config.dt;
        const double turning = config.turn_cost * std::abs(creature.action.left - creature.action.right) * config.dt;
        const double foraging = config.forage_cost * creature.action.forage * config.dt;
        const double calling = config.call_cost * creature.action.call * config.dt;
        const double neural = (config.neuron_cost * static_cast<double>(stats.neuron_count) + config.synapse_cost * static_cast<double>(stats.synapse_count)) * config.dt + config.spike_cost * static_cast<double>(creature.step_spikes);
        const bool exposed = storm && !sheltered(creature.position);
        const double exposure = exposed ? config.storm_cost * config.dt
            / (config.predation ? creature.body.mass : 1.0) : 0;
        if (exposed) creature.exposed_time += config.dt;
        const double requested_cost = metabolism + movement + turning + foraging + calling + neural + exposure;
        const double paid = std::min(std::max(0.0, creature.energy), requested_cost);
        const double fraction = requested_cost > 0 ? paid / requested_cost : 0;
        creature.energy = std::max(0.0, creature.energy - paid);
        creature.energy_spent += paid;
        totals.metabolism += metabolism * fraction;
        totals.movement += movement * fraction;
        totals.turning += turning * fraction;
        totals.foraging += foraging * fraction;
        totals.calling += calling * fraction;
        totals.neural += neural * fraction;
        totals.exposure += exposure * fraction;
        if (config.predation && !killed && creature.energy > 0 && creature.damage_pulse == 0) {
            const double healed = std::min({max_health(creature) - creature.health,
                config.healing_rate * config.dt, creature.energy / config.healing_cost});
            const double healing_paid = std::max(0.0, healed) * config.healing_cost;
            creature.health += std::max(0.0, healed);
            creature.energy -= healing_paid;
            creature.energy_spent += healing_paid;
            totals.healing += healing_paid;
        }
        creature.age += config.dt;
        if (!killed && creature.energy > 0 && !creature.matured && creature.age + epsilon >= config.maturity_age) {
            creature.matured = true;
            ++totals.maturations;
            if (creature.parent_id != 0) ++totals.mature_offspring;
            events.push_back({end, "maturation", creature.id, creature.parent_id, 0, creature.age});
        }
    }
    remove_dead(end);

    // 5. Birth placement prioritizes energy, breaks ties reproducibly and validates
    // full circles against terrain and every living/born body. Failed birth is free.
    if (config.reproduction) {
        std::vector<std::size_t> parents;
        for (std::size_t i = 0; i < creatures.size(); ++i) if (creatures[i].age + epsilon >= config.maturity_age
            && creatures[i].energy >= config.reproduction_threshold
            && end - creatures[i].last_birth + epsilon >= config.reproduction_cooldown) parents.push_back(i);
        std::sort(parents.begin(), parents.end(), [&](std::size_t a, std::size_t b) {
            if (creatures[a].energy != creatures[b].energy) return creatures[a].energy > creatures[b].energy;
            const auto hash_a = mix(tie_seed ^ creatures[a].id), hash_b = mix(tie_seed ^ creatures[b].id);
            return hash_a == hash_b ? creatures[a].id < creatures[b].id : hash_a < hash_b;
        });
        for (const auto parent_index : parents) {
            if (creatures.size() >= config.max_population) break;
            auto& parent = creatures[parent_index];
            Random placement_rng(mix(tie_seed ^ mix(parent.id) ^ 0x6269727468ULL));
            const double rotation = placement_rng.uniform(-pi, pi);
            Vec2 spawn;
            bool found = false;
            for (int ring = 1; ring <= 3 && !found; ++ring) {
                const double distance = (diameter + 0.05) * ring;
                for (int direction = 0; direction < 24; ++direction) {
                    const double heading = rotation + 2 * pi * static_cast<double>(direction) / 24;
                    const Vec2 candidate = parent.position + Vec2{std::cos(heading), std::sin(heading)} * distance;
                    if (!motion_clear(*this, parent.position, candidate)) continue;
                    const bool occupied = std::any_of(creatures.begin(), creatures.end(), [&](const EcoCreature& other) { return squared(other.position - candidate) < diameter * diameter + epsilon; });
                    if (!occupied) { spawn = candidate; found = true; break; }
                }
            }
            if (!found) continue;
            EcoCreature child;
            child.id = next_creature_id;
            // Preserve successful genomes while retaining mostly local exploration.
            // One draw selects the configured copy / slight / strong mixture.
            const double inheritance = mutation_rng.uniform(0.0, 1.0);
            const bool exact_inheritance = inheritance < config.mutation.copy_probability;
            child.genome_id = exact_inheritance ? (parent.genome_id ? parent.genome_id : parent.id) : child.id;
            child.body = exact_inheritance ? parent.body : inherit_body(parent.body, inheritance >= config.mutation.copy_probability + config.mutation.slight_probability, mutation_rng);
            child.health = max_health(child);
            const double body_cost = config.predation ? config.body_energy_per_mass * child.body.mass : 0.0;
            const double birth_cost = config.reproduction_cost + body_cost;
            if (parent.energy < birth_cost) continue;
            ++next_creature_id;
            child.origin = CreatureOrigin::Birth;
            child.parent_id = parent.id;
            child.generation = parent.generation + 1;
            child.position = spawn;
            child.heading = placement_rng.uniform(-pi, pi);
            child.energy = config.offspring_energy;
            child.controller = parent.controller;
            child.brain = parent.brain;
            if (!exact_inheritance) child.brain.mutate(inheritance < config.mutation.copy_probability + config.mutation.slight_probability
                ? detail::slight_mutation(config.mutation)
                : detail::strong_mutation(config.mutation), mutation_rng, ecosystem_input_groups(config.extended_senses, config.predation));
            // Independent birth cleanup also applies to the copy inheritance case.
            if (mutation_rng.uniform(0.0, 1.0) < config.mutation.disconnected_neuron_prune_probability
                && child.brain.remove_disconnected_hidden_neuron(mutation_rng))
                child.genome_id = child.id;
            child.brain.reset_state();
            child.neural_rng = Random(mix(config.seed ^ mix(child.id) ^ 0x6e657572616cULL));
            parent.energy -= birth_cost;
            parent.energy_spent += birth_cost;
            totals.body_construction += body_cost;
            parent.last_birth = end;
            if (parent.parent_id != 0 && parent.offspring == 0 && parent.controller == ControllerKind::Spiking)
                ++totals.natural_spiking_breeders;
            ++parent.offspring;
            ++totals.births;
            if (parent.parent_id != 0) ++totals.descendant_births;
            else ++totals.founder_births;
            if (end <= 100.0 + epsilon) ++totals.births_first_100s;
            totals.reproduction_overhead += config.reproduction_cost - config.offspring_energy;
            events.push_back({end, "birth", child.id, parent.id, 0, config.offspring_energy});
            creatures.push_back(std::move(child));
        }
        remove_dead(end);
    }
    const bool full = config.reproduction && creatures.size() >= config.max_population;
    if (full != capacity_limited)
        events.push_back({end, full ? "capacity_limited" : "capacity_released", 0, 0, 0,
            static_cast<double>(creatures.size())});
    capacity_limited = full;

    // 6. Regrowth uses the weather at interval start and appears at its end.
    // Open/closed pods do not grow: only refilling pods regenerate biomass.
    for (auto& resource : resources) {
        if (resource.kind == FoodKind::Meat) continue;
        if(resource.shelter_food && config.shelter_food_decay>0)continue;
        if (config.nursery_food_relocates && in_nursery(resource.position)) continue;
        if (config.outdoor_food_relocates && resource.kind != FoodKind::Pod
            && !resource.shelter_food && !in_nursery(resource.position)) continue;
        if (storm && !in_nursery(resource.position) && !resource.shelter_food) continue;
        if (resource.kind == FoodKind::Pod && resource.pod_state != PodState::Refilling) continue;
        const double amount = std::max(0.0, std::min(resource.capacity - resource.stock, resource.regrowth * config.dt));
        resource.stock += amount;
        totals.regrown_biomass += amount;
        if (resource.kind == FoodKind::Pod && resource.stock + epsilon >= resource.capacity) {
            resource.pod_state = PodState::Closed;
            resource.progress = 0;
        }
    }
    ++step_index;
    if (population > 0 && creatures.empty())
        events.push_back({end, "extinction", 0, 0, 0, 0});
    if (weather() != initial_weather) events.push_back({end, std::string("weather_") + to_string(weather()), 0, 0, 0, storm_cue()});
}

const char* to_string(Terrain value)
{
    switch (value) { case Terrain::Ground: return "ground"; case Terrain::Rough: return "rough"; case Terrain::Wall: return "wall"; case Terrain::Shelter: return "shelter"; }
    throw std::invalid_argument("Invalid terrain");
}
const char* to_string(FoodKind value)
{
    switch (value) { case FoodKind::Graze: return "graze"; case FoodKind::FruitA: return "fruit-a"; case FoodKind::FruitB: return "fruit-b"; case FoodKind::Pod: return "pod"; case FoodKind::Meat: return "meat"; }
    throw std::invalid_argument("Invalid food kind");
}
const char* to_string(PodState value)
{
    switch (value) { case PodState::Closed: return "closed"; case PodState::Open: return "open"; case PodState::Refilling: return "refilling"; }
    throw std::invalid_argument("Invalid pod state");
}
const char* to_string(ControllerKind value)
{
    switch (value) { case ControllerKind::Spiking: return "spiking"; case ControllerKind::Reactive: return "reactive"; case ControllerKind::Random: return "random"; }
    throw std::invalid_argument("Invalid controller");
}
const char* to_string(WeatherPhase value)
{
    switch (value) { case WeatherPhase::Calm: return "calm"; case WeatherPhase::Warning: return "warning"; case WeatherPhase::Storm: return "storm"; }
    throw std::invalid_argument("Invalid weather phase");
}
ControllerKind parse_controller(const std::string& value)
{
    if (value == "spiking") return ControllerKind::Spiking;
    if (value == "reactive") return ControllerKind::Reactive;
    if (value == "random") return ControllerKind::Random;
    throw std::invalid_argument("Controller must be spiking, reactive, or random");
}

const char* to_string(CreatureOrigin value)
{
    switch (value) { case CreatureOrigin::Founder: return "founder"; case CreatureOrigin::Birth: return "birth"; }
    throw std::invalid_argument("Invalid creature origin");
}

} // namespace neuroevo
