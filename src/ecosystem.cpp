#include "neuroevo/ecosystem.hpp"
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

void positive(double value, const char* name, bool zero_allowed = false)
{
    if (!std::isfinite(value) || (zero_allowed ? value < 0 : value <= 0)) {
        throw std::invalid_argument(std::string(name) + (zero_allowed ? " must be finite and nonnegative" : " must be finite and positive"));
    }
}

void probability(double value, const char* name)
{
    if (!std::isfinite(value) || value < 0 || value > 1) throw std::invalid_argument(std::string(name) + " must be in [0,1]");
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
    for (double* value : {&action.forward, &action.left, &action.right, &action.forage, &action.call}) {
        if (!std::isfinite(*value)) throw std::invalid_argument("Creature actions must be finite");
        *value = std::clamp(*value, 0.0, 1.0);
    }
    if (!communication) action.call = 0;
    return action;
}

} // namespace

EcosystemConfig::EcosystemConfig()
{
    brain.input_count = eco_input_count;
    brain.sensory_input_count = eco_input_count;
    brain.output_count = eco_output_count;
    brain.hidden_count = 16;
    brain.seed_input_output_synapses = false;
    brain.initial_connection_probability = 0.08;
    brain.calibrated_io = true;
    brain.conduction_speed = 6.0;
    brain.max_delay_steps = 8;
    // Sparse local sensory activity needs stronger delivered currents than the
    // earlier dense target-vector scaffold to initiate varied motor activity.
    brain.synaptic_gain = 32.0;
    // Ecosystem reproduction needs enough variation to explore new behavior
    // while retaining the sparse ancestor's useful feeding circuit.
    mutation.weight_sigma = 0.30;
    mutation.stable = true;
    mutation.bias_sigma = 0.50;
    mutation.threshold_sigma = 0.06;
    mutation.position_sigma = 0.05;
    mutation.background_sensitivity_sigma = 0.12;
    mutation.mutate_weight_probability = 0.22;
    mutation.mutate_neuron_probability = 0.16;
    mutation.add_synapse_probability = 0.40;
    mutation.add_neuron_probability = 0.12;
    mutation.add_reciprocal_motif_probability = 0.12;
}

void EcosystemConfig::validate() const
{
    if (shelter_size < 1 || std::min(width,height) < 5 || shelter_size > std::min(width,height)-4)
        throw std::invalid_argument("Shelter size must be 1..min(width,height)-4");
    if (nursery_frontier) {
        if (nursery_size < 16 || nursery_size > std::min(width, height)
            || std::min(width, height) - nursery_size < 24)
            throw std::invalid_argument("Nursery frontier needs a nursery >=16 cells and at least 24 extra map cells per dimension");
        if (nursery_exit_width > nursery_size - 2)
            throw std::invalid_argument("Nursery exit width must be 0..nursery_size-2");
        if (establishment || archive_eval_trials != 0)
            throw std::invalid_argument("Nursery frontier does not permit archive evaluation or immigration");
        positive(nursery_food_energy, "Nursery food energy");
        positive(nursery_food_capacity, "Nursery food capacity");
        positive(nursery_food_regrowth, "Nursery food regrowth", true);
    }
    if (archive_capacity < 4 || archive_capacity > 256 || immigration_batch == 0 || immigration_batch > 256)
        throw std::invalid_argument("Archive capacity must be 4..256; immigration batch must be 1..256");
    if (archive_tournament_size == 0 || archive_tournament_size > archive_capacity)
        throw std::invalid_argument("Archive tournament size must be 1..archive capacity");
    positive(immigration_interval, "Immigration interval");
    positive(archive_min_energy, "Archive minimum food energy");
    positive(archive_min_age, "Archive minimum age", true);
    positive(archive_min_efficiency, "Archive minimum food-to-cost ratio", true);
    if (archive_eval_trials > 32) throw std::invalid_argument("Archive evaluation trials must be 0..32");
    positive(archive_eval_seconds, "Archive evaluation duration");
    if (archive_eval_seconds > 100000 || archive_eval_seconds / dt > 1000000)
        throw std::invalid_argument("Archive evaluation exceeds one million steps or 100000 seconds");
    if (archive_min_feeding_bouts == 0 || archive_min_feeding_bouts > 1000000)
        throw std::invalid_argument("Archive minimum feeding bouts must be 1..1000000");
    if (withdrawal_cycles == 0 || withdrawal_cycles > 10000)
        throw std::invalid_argument("Withdrawal cycles must be 1..10000");
    if (establishment && (max_population < 2 || immigration_floor >= max_population))
        throw std::invalid_argument("Immigration floor must be below a population cap of at least two");
    if (width < 5 || height < 5 || width > 256 || height > 256) throw std::invalid_argument("World dimensions must each be between 5 and 256 (v1 connectivity and collision limits)");
    if (max_population == 0 || initial_creatures > max_population) throw std::invalid_argument("Initial population must not exceed a positive population cap");
    if (grazing_patches > width * height || fruit_patches > width * height || pods > width * height || shelters > width * height) throw std::invalid_argument("Too many resources or shelters for this map");
    positive(dt, "World timestep");
    positive(radius, "Creature radius");
    if (radius >= 0.5) throw std::invalid_argument("Creature radius must be less than half a terrain cell");
    positive(max_speed, "Maximum speed", true);
    positive(max_turn_rate, "Maximum turn rate", true);
    positive(vision_range, "Vision range");
    positive(hearing_range, "Hearing range");
    positive(fov_degrees, "Vision arc");
    positive(interaction_degrees, "Interaction arc");
    if (fov_degrees > 360 || interaction_degrees > 360) throw std::invalid_argument("Sensory and interaction arcs cannot exceed 360 degrees");
    positive(interaction_range, "Interaction range");
    positive(energy_capacity, "Energy capacity");
    positive(founder_energy, "Founder energy");
    if (founder_energy > energy_capacity) throw std::invalid_argument("Founder energy exceeds capacity");
    for (const auto value : {basal_cost, movement_cost, turn_cost, forage_cost, call_cost, neuron_cost, synapse_cost, spike_cost,
             graze_regrowth, fruit_regrowth, pod_regrowth, pod_decay, storm_cost, maturity_age, reproduction_cooldown, digestion_delay}) positive(value, "An energy cost, duration or regrowth rate", true);
    for (const auto value : {graze_energy, poor_fruit_energy, rich_fruit_energy, pod_energy})
        positive(value, "Food energy density");
    if (rich_fruit_energy <= poor_fruit_energy)
        throw std::invalid_argument("Rich fruit energy must exceed poor fruit energy");
    positive(rough_multiplier, "Rough ground multiplier");
    if (rough_multiplier < 1) throw std::invalid_argument("Rough ground must not reduce movement cost");
    positive(ingestion_rate, "Ingestion rate");
    positive(graze_capacity, "Grazing capacity");
    positive(fruit_capacity, "Fruit capacity");
    positive(pod_capacity, "Pod capacity");
    positive(pod_work, "Pod opening work");
    positive(pod_open_duration, "Pod open duration");
    positive(calm_duration, "Calm duration", true);
    positive(warning_duration, "Warning duration", true);
    positive(storm_duration, "Storm duration", true);
    positive(calm_duration + warning_duration + storm_duration, "Weather cycle duration");
    if (!std::isfinite(phase_offset)) throw std::invalid_argument("Weather phase offset must be finite");
    positive(reproduction_threshold, "Reproduction threshold");
    positive(reproduction_cost, "Reproduction cost");
    positive(offspring_energy, "Offspring energy");
    if (reproduction_threshold > energy_capacity || reproduction_cost > reproduction_threshold || offspring_energy > reproduction_cost) throw std::invalid_argument("Reproduction requires offspring energy <= birth cost <= threshold <= capacity");
    positive(motor_gain, "Motor gain");
    positive(actuator_tau, "Actuator time constant", true);
    if (food_assignment < -1 || food_assignment > 1) throw std::invalid_argument("Food assignment must be -1 (seeded), 0 (A rich), or 1 (B rich)");
    const auto inputs = extended_senses ? eco_input_count : eco_legacy_input_count;
    if (brain.input_count != inputs || brain.output_count != eco_output_count || brain.sensory_input_count != inputs || brain.has_clock_input || brain.has_episode_start_input) throw std::invalid_argument("Ecological brains require the selected local sensor and motor layout without clock inputs");
    positive(brain.sensory_rate_hz, "Sensory spike rate");
    positive(brain.motor_rate_tau, "Motor rate time constant");
    positive(brain.motor_reference_hz, "Motor reference spike rate");
    if (brain.calibrated_io && brain.sensory_rate_hz * brain.dt > 1)
        throw std::invalid_argument("Sensory spike rate exceeds brain sampling rate");
    positive(brain.dt, "Brain timestep");
    const double neural_steps = dt / brain.dt;
    if (neural_steps < 1 - epsilon || neural_steps > 10000 || std::abs(neural_steps - std::round(neural_steps)) > 1e-8) throw std::invalid_argument("World timestep must be an integer multiple of brain timestep (at most 10000 neural updates)");
    if (brain.max_delay_steps == 0 || brain.max_delay_steps > 4096 || brain.hidden_count > 10000) throw std::invalid_argument("Invalid brain delay or hidden-neuron count");
    const auto neurons = brain.input_count + brain.hidden_count + brain.output_count;
    if (neurons * (brain.max_delay_steps + 1) > 5000000 || neurons * (brain.hidden_count + brain.output_count) > 1000000) throw std::invalid_argument("Brain exceeds the v1 runtime buffer or synapse limits");
    positive(brain.membrane_tau, "Membrane time constant");
    positive(brain.threshold, "Neuron threshold");
    positive(brain.conduction_speed, "Neural conduction speed");
    for (const auto value : {brain.refractory_time, brain.input_gain, brain.synaptic_gain, brain.background_event_rate_hz,
             brain.background_event_current, brain.initial_background_sensitivity, brain.initial_background_sensitivity_sigma}) positive(value, "A neural rate, gain, duration or sensitivity", true);
    if (!std::isfinite(brain.reset_potential) || !std::isfinite(brain.seed_input_output_weight)) throw std::invalid_argument("Brain reset potential and seed weight must be finite");
    probability(brain.initial_connection_probability, "Brain connection probability");
    probability(brain.max_bias_fraction_of_threshold, "Maximum neural bias fraction");
    probability(brain.motor_trace_decay, "Motor trace decay");
    if (brain.max_bias_fraction_of_threshold >= 1 || brain.motor_trace_decay >= 1 || brain.initial_background_sensitivity > 2) throw std::invalid_argument("Bias fraction and trace decay must be below 1; background sensitivity cannot exceed 2");
    for (const auto value : {mutation.weight_sigma, mutation.bias_sigma, mutation.threshold_sigma, mutation.position_sigma,
             mutation.background_sensitivity_sigma, mutation.clock_threshold_sigma, mutation.hidden_bias_jump_min_magnitude}) positive(value, "A mutation standard deviation or jump magnitude", true);
    for (const auto value : {mutation.hidden_bias_jump_probability, mutation.add_synapse_probability, mutation.add_neuron_probability,
             mutation.add_reciprocal_motif_probability,
             mutation.remove_synapse_probability, mutation.mutate_weight_probability, mutation.mutate_neuron_probability,
             mutation.mutate_clock_threshold_probability}) probability(value, "Mutation probability");
    if (mutation.max_hidden_neurons < brain.hidden_count || mutation.max_hidden_neurons > 10000)
        throw std::invalid_argument("Mutation hidden-neuron limit must include the initial brain and be at most 10000");
    if (!std::isfinite(mutation.hidden_bias_min) || !std::isfinite(mutation.hidden_bias_max)
        || mutation.hidden_bias_min > 0 || mutation.hidden_bias_max < 0) throw std::invalid_argument("Hidden bias bounds must be finite and contain zero");
    positive(mutation.background_sensitivity_min, "Minimum background sensitivity", true);
    positive(mutation.background_sensitivity_max, "Maximum background sensitivity", true);
    positive(mutation.clock_threshold_min, "Minimum clock threshold");
    positive(mutation.clock_threshold_max, "Maximum clock threshold");
    if (mutation.background_sensitivity_min > mutation.background_sensitivity_max || mutation.background_sensitivity_max > 2
        || mutation.clock_threshold_min > mutation.clock_threshold_max) throw std::invalid_argument("Mutation bounds are reversed or outside allowed sensitivity range");
    if (controller != ControllerKind::Spiking && controller != ControllerKind::Reactive && controller != ControllerKind::Random) throw std::invalid_argument("Invalid controller kind");
}

EcosystemWorld::EcosystemWorld(EcosystemConfig settings, bool generate)
    : config(std::move(settings)), map_rng(mix(config.seed ^ 0x6d6170ULL)),
      mutation_rng(mix(config.seed ^ 0x6d7574617465ULL)), conflict_rng(mix(config.seed ^ 0x746965ULL))
{
    config.validate();
    terrain.assign(config.width * config.height, Terrain::Ground);
    immigration_rng = Random(mix(config.seed ^ 0x696d6d696772ULL));
    next_immigration_check = config.immigration_interval;
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
    capacity_limited = false;
    totals = {};
    events.clear();
    archive.clear();
    immigration_rng = Random(mix(config.seed ^ 0x696d6d696772ULL));
    immigration_deck = {};
    immigration_deck_cursor = 5;
    next_immigration_check = config.immigration_interval;
    last_immigration_time = 0;
    support_stable_since = -1;
    immigration_withdrawn = false;
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

    fruit_a_rich = config.food_assignment < 0 ? map_rng.chance(0.5) : config.food_assignment == 0;
    std::vector<unsigned char> occupied(terrain.size(), 0);
    const auto position_of = [&](std::size_t cell) { return Vec2{static_cast<double>(cell % config.width) + 0.5, static_cast<double>(cell / config.width) + 0.5}; };
    const auto available = [&](std::size_t cell) { return !occupied[cell] && terrain[cell] != Terrain::Wall && terrain[cell] != Terrain::Shelter; };
    const auto add_resource = [&](std::size_t cell, FoodKind kind) {
        EcoResource resource;
        resource.id = resources.size() + 1;
        resource.position = position_of(cell);
        resource.kind = kind;
        resource.capacity = kind == FoodKind::Graze ? config.graze_capacity : kind == FoodKind::Pod ? config.pod_capacity : config.fruit_capacity;
        resource.stock = resource.capacity;
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
        creature.brain = Brain::random(config.brain, genome_rng);
        creature.brain.reset_state();
        creature.neural_rng = Random(mix(config.seed ^ mix(creature.id) ^ 0x6e657572616cULL));
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
    if (capacity_limited) return;
    if (terrain.size() != config.width * config.height) throw std::runtime_error("Terrain dimensions do not match the world configuration");
    for (std::size_t i = 0; i < creatures.size(); ++i) {
        if (!traversable(creatures[i].position)) throw std::runtime_error("A creature begins the step inside a wall or outside the world");
        if (!std::isfinite(creatures[i].heading) || !std::isfinite(creatures[i].energy)) throw std::runtime_error("Creature heading and energy must be finite");
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
        const double distance = actions[i].forward * config.max_speed * (1 - 0.75 * actions[i].forage) * config.dt;
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

    // 3. Targets use post-movement geometry and the starting resource state.
    // A seeded ID hash breaks exact distance ties without vector-order priority.
    std::vector<std::vector<std::size_t>> requests(resources.size());
    for (std::size_t i = 0; i < population; ++i) {
        const auto& creature = creatures[i];
        if (creature.action.forage <= 0 || creature.energy <= 0) continue;
        std::size_t target = resources.size();
        double nearest = config.interaction_range + epsilon;
        std::uint64_t best_tie = std::numeric_limits<std::uint64_t>::max();
        for (std::size_t r = 0; r < resources.size(); ++r) {
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
            const double efficiency = in_nursery(resource.position) ? 0.35 + 0.65 * settled * settled : 1.0;
            return action.forage * config.ingestion_rate * config.dt * efficiency;
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
            creature.digestion.push_back({end + config.digestion_delay, amount * resource.energy_per_unit, resource.kind});
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

    // 4. Due digestive packets arrive at the end boundary, then this interval's
    // energetic costs are charged. Future packets cannot rescue a starving body.
    for (auto& creature : creatures) {
        std::size_t pending = 0;
        for (const auto& packet : creature.digestion) {
            if (packet.due <= end + epsilon) {
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
        const double metabolism = config.basal_cost * config.dt;
        const double movement = config.movement_cost * creature.action.forward * creature.action.forward * rough * config.dt;
        const double turning = config.turn_cost * std::abs(creature.action.left - creature.action.right) * config.dt;
        const double foraging = config.forage_cost * creature.action.forage * config.dt;
        const double calling = config.call_cost * creature.action.call * config.dt;
        const double neural = (config.neuron_cost * static_cast<double>(stats.neuron_count) + config.synapse_cost * static_cast<double>(stats.synapse_count)) * config.dt + config.spike_cost * static_cast<double>(creature.step_spikes);
        const bool exposed = storm && !sheltered(creature.position);
        const double exposure = exposed ? config.storm_cost * config.dt : 0;
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
        creature.age += config.dt;
        if (creature.energy > 0 && !creature.matured && creature.age + epsilon >= config.maturity_age) {
            creature.matured = true;
            ++totals.maturations;
            if (creature.parent_id != 0) ++totals.mature_offspring;
            events.push_back({end, "maturation", creature.id, creature.parent_id, 0, creature.age});
        }
        if (creature.energy <= 0) {
            consider_archive(creature, true, end);
            double discarded = 0;
            for (const auto& packet : creature.digestion) discarded += packet.energy;
            totals.discarded_energy += discarded;
            ++totals.deaths;
            events.push_back({end, "death", creature.id, creature.parent_id, 0, discarded});
        }
    }
    creatures.erase(std::remove_if(creatures.begin(), creatures.end(), [](const EcoCreature& creature) { return creature.energy <= 0; }), creatures.end());

    // 5. Birth placement uses a shuffled, reproducible parent order and validates
    // full circles against terrain and every living/born body. Failed birth is free.
    if (config.reproduction) {
        std::vector<std::size_t> parents;
        for (std::size_t i = 0; i < creatures.size(); ++i) if (creatures[i].age + epsilon >= config.maturity_age
            && creatures[i].energy >= config.reproduction_threshold
            && end - creatures[i].last_birth + epsilon >= config.reproduction_cooldown) parents.push_back(i);
        std::sort(parents.begin(), parents.end(), [&](std::size_t a, std::size_t b) {
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
            child.id = next_creature_id++;
            // Stable mutations permit more exploration; retain historical birth
            // ratios when continuing a world with the legacy mutation policy.
            const bool exact_inheritance = mutation_rng.chance(config.mutation.stable ? 0.25 : 0.5);
            child.genome_id = exact_inheritance ? (parent.genome_id ? parent.genome_id : parent.id) : child.id;
            child.origin = CreatureOrigin::Birth;
            child.parent_id = parent.id;
            child.generation = parent.generation + 1;
            child.position = spawn;
            child.heading = placement_rng.uniform(-pi, pi);
            child.energy = config.offspring_energy;
            child.controller = parent.controller;
            child.brain = parent.brain;
            if (!exact_inheritance) child.brain.mutate(detail::slight_mutation(config.mutation), mutation_rng);
            child.brain.reset_state();
            child.neural_rng = Random(mix(config.seed ^ mix(child.id) ^ 0x6e657572616cULL));
            parent.energy -= config.reproduction_cost;
            parent.energy_spent += config.reproduction_cost;
            parent.last_birth = end;
            if (parent.parent_id != 0 && parent.offspring == 0 && parent.controller == ControllerKind::Spiking)
                ++totals.natural_spiking_breeders;
            ++parent.offspring;
            ++totals.births;
            if (parent.parent_id != 0) ++totals.descendant_births;
            else if (parent.origin == CreatureOrigin::Founder) ++totals.founder_births;
            else ++totals.immigrant_births;
            if (end <= 100.0 + epsilon) ++totals.births_first_100s;
            totals.reproduction_overhead += config.reproduction_cost - config.offspring_energy;
            events.push_back({end, "birth", child.id, parent.id, 0, config.offspring_energy});
            creatures.push_back(std::move(child));
        }
        // Configurations with birth cost equal to the reserve threshold can
        // exhaust a parent completely. Its offspring survives, but the parent
        // must be removed at this same boundary and loses its pending digestion.
        for (const auto& creature : creatures) if (creature.energy <= 0) {
            consider_archive(creature, true, end);
            double discarded = 0;
            for (const auto& packet : creature.digestion) discarded += packet.energy;
            totals.discarded_energy += discarded;
            ++totals.deaths;
            events.push_back({end, "death", creature.id, creature.parent_id, 0, discarded});
        }
        creatures.erase(std::remove_if(creatures.begin(), creatures.end(), [](const EcoCreature& creature) { return creature.energy <= 0; }), creatures.end());
        if (creatures.size() >= config.max_population) {
            capacity_limited = true;
            events.push_back({end, "capacity_limited", 0, 0, 0, static_cast<double>(creatures.size())});
        }
    }

    // 6. Regrowth uses the weather at interval start and appears at its end.
    // Open/closed pods do not grow: only refilling pods regenerate biomass.
    for (auto& resource : resources) {
        if (storm && !in_nursery(resource.position)) continue;
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
        events.push_back({end, immigration_enabled() ? "population_empty" : "extinction", 0, 0, 0, 0});
    update_establishment();
    if (weather() != initial_weather) events.push_back({end, std::string("weather_") + to_string(weather()), 0, 0, 0, storm_cue()});
}

const char* to_string(Terrain value)
{
    switch (value) { case Terrain::Ground: return "ground"; case Terrain::Rough: return "rough"; case Terrain::Wall: return "wall"; case Terrain::Shelter: return "shelter"; }
    throw std::invalid_argument("Invalid terrain");
}
const char* to_string(FoodKind value)
{
    switch (value) { case FoodKind::Graze: return "graze"; case FoodKind::FruitA: return "fruit-a"; case FoodKind::FruitB: return "fruit-b"; case FoodKind::Pod: return "pod"; }
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

} // namespace neuroevo
