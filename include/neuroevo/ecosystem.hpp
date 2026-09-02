#pragma once

#include "neuroevo/brain.hpp"
#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_set>
#include <vector>

namespace neuroevo {

enum class Terrain { Ground, Rough, Wall, Shelter };
enum class FoodKind { Graze, FruitA, FruitB, Pod };
enum class PodState { Closed, Open, Refilling };
enum class ControllerKind { Spiking, Reactive, Random };
enum class WeatherPhase { Calm, Warning, Storm };
constexpr std::size_t eco_sectors = 5;
constexpr std::size_t eco_sector_channels = 14;
constexpr std::size_t eco_input_count = eco_sectors * eco_sector_channels + 17;
constexpr std::size_t eco_output_count = 5;

struct EcosystemConfig {
    std::size_t width = 48, height = 48, initial_creatures = 24, max_population = 256;
    std::size_t shelters = 6, grazing_patches = 80, fruit_patches = 32, pods = 8;
    std::uint64_t seed = 7;
    double dt = 0.10, radius = 0.25, max_speed = 1.5, max_turn_rate = 3.141592653589793;
    double vision_range = 6.0, fov_degrees = 150.0, hearing_range = 6.0;
    double interaction_range = 0.8, interaction_degrees = 120.0;
    double energy_capacity = 200, founder_energy = 90, basal_cost = 0.20;
    double movement_cost = 0.12, turn_cost = 0.02, forage_cost = 0.30, call_cost = 0.05;
    double neuron_cost = 0.0001, synapse_cost = 0.00001, spike_cost = 0.00001;
    double rough_multiplier = 2.0, ingestion_rate = 1.0, digestion_delay = 3.0;
    double graze_capacity = 8, fruit_capacity = 12, pod_capacity = 12;
    double graze_regrowth = 0.02, fruit_regrowth = 0.01, pod_regrowth = 0.08;
    double pod_work = 10, pod_decay = 1, pod_open_duration = 30;
    double calm_duration = 150, warning_duration = 30, storm_duration = 60;
    double storm_cost = 0.8, phase_offset = 0;
    double maturity_age = 120, reproduction_threshold = 150, reproduction_cost = 75;
    double offspring_energy = 45, reproduction_cooldown = 120;
    double motor_gain = 8;
    bool reproduction = true, communication = true;
    // -1 chooses the assignment from the map RNG, 0 makes A rich, 1 makes B rich.
    int food_assignment = -1;
    ControllerKind controller = ControllerKind::Spiking;
    BrainConfig brain;
    MutationConfig mutation;
    EcosystemConfig();
    void validate() const;
};

struct EcoAction {
    double forward = 0, left = 0, right = 0, forage = 0, call = 0;
};
struct DigestivePacket { double due = 0, energy = 0; FoodKind kind = FoodKind::Graze; };
struct EcoResource {
    std::uint64_t id = 0;
    FoodKind kind = FoodKind::Graze;
    Vec2 position;
    double stock = 0, capacity = 0, regrowth = 0, energy_per_unit = 0;
    PodState pod_state = PodState::Closed;
    double progress = 0, opened_at = 0;
};
struct EcoCreature {
    std::uint64_t id = 0, parent_id = 0, generation = 0;
    Vec2 position;
    double heading = 0, energy = 0, age = 0, last_birth = -1e9;
    double speed = 0, turn = 0, ingestion_pulse = 0, digestion_pulse = 0;
    EcoAction action;
    Brain brain;
    Random neural_rng;
    ControllerKind controller = ControllerKind::Spiking;
    std::vector<DigestivePacket> digestion;
    std::array<double, 4> eaten{};
    std::uint64_t spikes = 0, step_spikes = 0, offspring = 0;
    double energy_gained = 0, energy_spent = 0, pod_work = 0, exposed_time = 0;
    bool matured = false;
};
struct EcoEvent {
    double time = 0;
    std::string type;
    std::uint64_t creature = 0, other = 0, resource = 0;
    double amount = 0;
};
struct EcoTotals {
    std::uint64_t births = 0, deaths = 0, maturations = 0, spikes = 0, pods_opened = 0;
    double consumed_biomass = 0, regrown_biomass = 0, spoiled_biomass = 0;
    double energy_gained = 0, metabolism = 0, movement = 0, turning = 0;
    double foraging = 0, calling = 0, neural = 0, exposure = 0;
    double reproduction_overhead = 0, discarded_energy = 0;
};

// Public state enables explicit controlled experiments and full-state checkpoints.
// Normal runs mutate the world through step(); controllers only receive local sensing.
class EcosystemWorld {
public:
    explicit EcosystemWorld(EcosystemConfig config = {}, bool generate = true);
    EcosystemConfig config;
    std::vector<Terrain> terrain;
    std::vector<EcoResource> resources;
    std::vector<EcoCreature> creatures;
    std::vector<EcoEvent> events; // Events produced by the most recent step only.
    EcoTotals totals;
    Random map_rng, mutation_rng, conflict_rng;
    std::uint64_t step_index = 0, next_creature_id = 1;
    bool fruit_a_rich = true, capacity_limited = false;

    double time() const;
    WeatherPhase weather() const;
    double storm_cue() const;
    Terrain terrain_at(Vec2 position) const;
    bool sheltered(Vec2 position) const;
    bool traversable(Vec2 position) const;
    bool line_of_sight(Vec2 from, Vec2 to) const;
    void generate_world();
    // Empty actions means each living creature runs its own controller/brain.
    // Supplied actions must match the population at the beginning of the step.
    void step(const std::vector<EcoAction>& actions = {});
    std::vector<double> observe(std::size_t creature_index) const;
    EcoAction control(std::size_t creature_index);
    void save_checkpoint(std::ostream& stream) const;
    static EcosystemWorld load_checkpoint(std::istream& stream);
};

std::vector<std::string> ecosystem_input_labels();
const char* to_string(Terrain value);
const char* to_string(FoodKind value);
const char* to_string(PodState value);
const char* to_string(ControllerKind value);
const char* to_string(WeatherPhase value);
ControllerKind parse_controller(const std::string& value);

// Streaming JSON Lines replay, independent of rendering and optional brain detail.
void write_ecosystem_metadata(std::ostream& stream, const EcosystemWorld& world);
void write_ecosystem_frame(std::ostream& stream, const EcosystemWorld& world, bool record_brains,
    std::unordered_set<std::uint64_t>* known_brains = nullptr,
    const std::vector<EcoEvent>* recorded_events = nullptr);
void write_ecosystem_stats_header(std::ostream& stream);
void write_ecosystem_stats(std::ostream& stream, const EcosystemWorld& world);

} // namespace neuroevo
