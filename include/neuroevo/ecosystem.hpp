#pragma once

#include "neuroevo/brain.hpp"
#include <array>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace neuroevo {

enum class Terrain { Ground, Rough, Wall, Shelter };
enum class FoodKind { Graze, FruitA, FruitB, Pod, Meat };
enum class PodState { Closed, Open, Refilling };
enum class ControllerKind { Spiking, Reactive, Random };
enum class WeatherPhase { Calm, Warning, Storm };
// Values 0..4 are checkpoint-stable. Strong mutation was appended in v3.
enum class CreatureOrigin { Founder, Birth, ArchiveMutation, ArchiveClone, RandomImmigrant, ArchiveStrongMutation };
constexpr std::size_t eco_sectors = 5;
constexpr std::size_t eco_sector_channels = 14;
constexpr std::size_t eco_legacy_input_count = eco_sectors * eco_sector_channels + 17;
constexpr std::size_t eco_depleted_offset = eco_legacy_input_count;
constexpr std::size_t eco_shelter_offset = eco_depleted_offset + eco_sectors;
constexpr std::size_t eco_input_count = eco_shelter_offset + eco_sectors;
constexpr std::size_t eco_output_count = 5;
// Append sensory groups; all pre-predation input/output indices stay stable.
constexpr std::size_t eco_meat_offset = eco_input_count;
constexpr std::size_t eco_other_mass_offset = eco_meat_offset + 3 * eco_sectors;
constexpr std::size_t eco_other_health_offset = eco_other_mass_offset + eco_sectors;
constexpr std::size_t eco_health_offset = eco_other_health_offset + eco_sectors;
constexpr std::size_t eco_predation_input_count = eco_health_offset + 2;
constexpr std::size_t eco_predation_output_count = 6;
constexpr double eco_min_mass = 0.5, eco_max_mass = 2.0;

// Together with Brain these are the inherited genome. Health/actions are state.
struct BodyGenes {
    double mass = 1.0, carnivory = 0.0;
};

struct EcosystemConfig {
    bool predation = false; // Enabled by default in new nursery-frontier runs.
    double founder_mass = 1.0, founder_carnivory = 0.0;
    double health_per_mass = 20, body_energy_per_mass = 30;
    double attack_range = 0.8, attack_degrees = 60, attack_damage = 5, attack_cost = 2;
    double healing_rate = 0.1, healing_cost = 2;
    double meat_energy = 20, meat_decay = 0.0025, carcass_recovery = 0.8;
    double mass_mutation_probability = 0.2, mass_mutation_sigma = 0.12;
    double carnivory_mutation_probability = 0.2, carnivory_mutation_sigma = 0.08;
    bool nursery_frontier = false;
    std::size_t nursery_size = 16;
    std::size_t nursery_exit_width = 4;
    std::size_t shelter_size = 6;
    std::size_t nursery_food_patches = 16;
    bool nursery_food_relocates = true;
    double nursery_food_decay = 0.005; // Biomass per second, including during storms.
    double nursery_food_energy = 30.0, nursery_food_capacity = 16, nursery_food_regrowth = 0.02;
    std::size_t width = 48, height = 48, initial_creatures = 24, max_population = 128;
    std::size_t shelters = 12, grazing_patches = 80, fruit_patches = 32, pods = 8;
    std::uint64_t seed = 7;
    double dt = 0.10, radius = 0.25, max_speed = 1.5, max_turn_rate = 3.141592653589793;
    double vision_range = 12.0, fov_degrees = 150.0, hearing_range = 6.0;
    double interaction_range = 0.8, interaction_degrees = 120.0;
    double energy_capacity = 200, founder_energy = 90, basal_cost = 0.20;
    double movement_cost = 0.12, turn_cost = 0.02, forage_cost = 0.30, call_cost = 0.05;
    double neuron_cost = 0.0001, synapse_cost = 0.00001, spike_cost = 0.00001;
    double rough_multiplier = 2.0, ingestion_rate = 1.0, digestion_delay = 3.0;
    double graze_capacity = 8, fruit_capacity = 12, pod_capacity = 12;
    double graze_energy = 2.5, poor_fruit_energy = 5, rich_fruit_energy = 12.5, pod_energy = 15;
    double graze_regrowth = 0.02, fruit_regrowth = 0.01, pod_regrowth = 0.08;
    bool outdoor_food_relocates = true;
    double graze_decay = 0.005, fruit_decay = 0.005; // biomass per second
    double shelter_food_energy = 1, shelter_food_capacity = 2, shelter_food_regrowth = 0.6;
    double pod_work = 10, pod_decay = 1, pod_open_duration = 30;
    double calm_duration = 150, warning_duration = 30, storm_duration = 60;
    double storm_cost = 3.0, phase_offset = 0;
    double maturity_age = 120, reproduction_threshold = 150, reproduction_cost = 75;
    double offspring_energy = 50, reproduction_cooldown = 120;
    double motor_gain = 1.0, actuator_tau = 0.30;
    bool extended_senses = true;
    bool reproduction = true, communication = true, storms_enabled = true;
    bool establishment = false, immigration_auto_stop = true;
    // Zero floor means half the founder population, with a minimum of one.
    std::size_t immigration_floor = 0, immigration_batch = 2, archive_capacity = 16;
    std::size_t archive_tournament_size = 3;
    std::size_t withdrawal_cycles = 3;
    double immigration_interval = 5, archive_min_energy = 37.5, archive_min_age = 60;
    std::size_t archive_min_feeding_bouts = 3;
    double archive_min_efficiency = 0.60;
    std::size_t archive_eval_trials = 5, archive_eval_seed = 17071;
    double archive_eval_seconds = 600;
    // -1 chooses the assignment from the map RNG, 0 makes A rich, 1 makes B rich.
    int food_assignment = -1;
    ControllerKind controller = ControllerKind::Spiking;
    BrainConfig brain;
    MutationConfig mutation;
    EcosystemConfig();
    void set_predation(bool enabled);
    void validate() const;
};

struct EcoAction {
    double forward = 0, left = 0, right = 0, forage = 0, call = 0, attack = 0;
};
struct DigestivePacket { double due = 0, energy = 0; FoodKind kind = FoodKind::Graze; };
struct EcoResource {
    std::uint64_t id = 0;
    FoodKind kind = FoodKind::Graze;
    Vec2 position;
    double stock = 0, capacity = 0, regrowth = 0, energy_per_unit = 0;
    bool shelter_food = false; // Low-quality graze; shares the existing graze sensory channel.
    PodState pod_state = PodState::Closed;
    double progress = 0, opened_at = 0;
};
struct EcoCreature {
    BodyGenes body;
    double health = 20, damage_pulse = 0;
    std::uint64_t id = 0, parent_id = 0, generation = 0;
    Vec2 position;
    double heading = 0, energy = 0, age = 0, last_birth = -1e9;
    double speed = 0, turn = 0, ingestion_pulse = 0, digestion_pulse = 0;
    EcoAction action;
    Brain brain;
    Random neural_rng;
    ControllerKind controller = ControllerKind::Spiking;
    std::vector<DigestivePacket> digestion;
    std::array<double, 5> eaten{};
    std::uint64_t spikes = 0, step_spikes = 0, offspring = 0;
    double energy_gained = 0, energy_spent = 0, pod_work = 0, exposed_time = 0;
    bool matured = false;
    CreatureOrigin origin = CreatureOrigin::Founder;
    std::uint64_t genome_id = 0, source_id = 0, feeding_bouts = 0;
    double last_fed_time = -1e9;
};
struct ArchiveTrial {
    std::uint64_t creature_id = 0, feeding_bouts = 0, offspring = 0;
    double energy_gained = 0, energy_spent = 0, age = 0, observed_at = 0;
    bool finished = false;
};
struct GenomeArchiveEntry {
    std::uint64_t genome_id = 0, source_id = 0;
    FoodKind niche = FoodKind::Graze;
    double score = 0;
    Brain genome; // Reset activity; all introduced copies start a new lifetime.
    std::vector<ArchiveTrial> trials; // Up to eight recently observed lifetimes.
};
struct NewbornTrial {
    std::uint64_t seed = 0, offspring = 0, descendant_births = 0, mature_offspring = 0;
    double elapsed = 0, focal_age = 0, food_energy = 0, operating_energy = 0;
    double first_birth = -1, second_birth = -1;
    bool matured = false, focal_alive = false, capacity_limited = false;
};
struct NewbornEvaluation {
    double score = 0;
    std::vector<NewbornTrial> trials;
};
struct EcoEvent {
    double time = 0;
    std::string type;
    std::uint64_t creature = 0, other = 0, resource = 0;
    double amount = 0;
};
struct EcoTotals {
    double attacking = 0, healing = 0, body_construction = 0, external_body_energy = 0;
    double carcass_energy = 0, meat_spoiled_energy = 0, damage = 0;
    std::uint64_t predation_deaths = 0;
    std::uint64_t births = 0, deaths = 0, maturations = 0, spikes = 0, pods_opened = 0;
    double consumed_biomass = 0, regrown_biomass = 0, spoiled_biomass = 0;
    double energy_gained = 0, metabolism = 0, movement = 0, turning = 0;
    double foraging = 0, calling = 0, neural = 0, exposure = 0;
    double reproduction_overhead = 0, discarded_energy = 0;
    std::uint64_t immigrants = 0, immigrant_mutations = 0, immigrant_clones = 0, immigrant_random = 0;
    std::uint64_t archive_fallbacks = 0, natural_spiking_breeders = 0, mature_offspring = 0;
    std::uint64_t immigrant_slight_mutations = 0, immigrant_strong_mutations = 0;
    std::uint64_t archive_empty_checks = 0;
    std::uint64_t founder_births = 0, immigrant_births = 0, descendant_births = 0, births_first_100s = 0;
    double immigrant_energy = 0;
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
    std::uint64_t next_resource_id = 1;
    // capacity_limited reports currently blocked births, not a simulation stop.
    bool fruit_a_rich = true, capacity_limited = false;
    std::vector<GenomeArchiveEntry> archive;
    // Includes rejected candidates; a genome is evaluated once on a fixed suite.
    std::map<std::uint64_t, NewbornEvaluation> newborn_evaluations;
    // Execution controls/diagnostics only: never serialized or used in selection.
    std::size_t evaluation_workers = 1;
    std::uint64_t evaluation_calls = 0;
    double evaluation_wall_seconds = 0;
    Random immigration_rng;
    std::array<CreatureOrigin, 5> immigration_deck{};
    std::size_t immigration_deck_cursor = 5;
    double next_immigration_check = 0, last_immigration_time = 0, support_stable_since = -1;
    bool immigration_withdrawn = false;

    double time() const;
    WeatherPhase weather() const;
    double storm_cue() const;
    Terrain terrain_at(Vec2 position) const;
    bool sheltered(Vec2 position) const;
    bool in_nursery(Vec2 position) const;
    void generate_nursery_frontier();
    bool relocate_nursery_food(EcoResource& resource, Random& rng, bool avoid_creatures);
    bool relocate_outdoor_food(EcoResource& resource);
    void add_shelter_food(Vec2 position);
    bool traversable(Vec2 position) const;
    bool line_of_sight(Vec2 from, Vec2 to) const;
    void generate_world();
    void initialize_body(EcoCreature& creature);
    double max_health(const EcoCreature& creature) const;
    double maximum_speed(const EcoCreature& creature) const;
    double dietary_efficiency(const EcoCreature& creature, FoodKind kind) const;
    BodyGenes inherit_body(const BodyGenes& parent, bool strong, Random& rng) const;
    void remove_dead(double end);
    // Empty actions means each living creature runs its own controller/brain.
    // Supplied actions must match the population at the beginning of the step.
    void step(const std::vector<EcoAction>& actions = {});
    std::vector<double> observe(std::size_t creature_index) const;
    EcoAction control(std::size_t creature_index);
    std::size_t population_floor() const;
    bool immigration_enabled() const;
    void consider_archive(const EcoCreature& creature, bool finished, double observed_at = -1);
    void update_establishment();
    NewbornEvaluation evaluate_newborn(const Brain& genome) const;
    void save_checkpoint(std::ostream& stream) const;
    static EcosystemWorld load_checkpoint(std::istream& stream);
};

std::vector<std::string> ecosystem_input_labels(bool extended = true, bool predation = false);
const Brain::InputGroups& ecosystem_input_groups(bool extended = true, bool predation = false);
// A deliberately small, deterministic founder genome. It uses seven hidden
// neurons and a sparse subset of the ecosystem sensors; it remains an ordinary
// spiking Brain and offspring can mutate it through the normal birth path.
Brain make_sparse_ancestral_brain(const EcosystemConfig& config);
EcosystemConfig nursery_frontier_config();
// Controlled one-founder habitat used to establish that feeding, birth, and
// descendant reproduction work before testing the genome in the harsh world.
EcosystemWorld make_ancestral_nursery(EcosystemConfig config = {});
const char* to_string(Terrain value);
const char* to_string(FoodKind value);
const char* to_string(PodState value);
const char* to_string(ControllerKind value);
const char* to_string(WeatherPhase value);
const char* to_string(CreatureOrigin value);
ControllerKind parse_controller(const std::string& value);

// Streaming JSON Lines replay, independent of rendering and optional brain detail.
void write_ecosystem_metadata(std::ostream& stream, const EcosystemWorld& world, bool record_brain_graphs = true);
void write_ecosystem_frame(std::ostream& stream, const EcosystemWorld& world, bool record_brains,
    bool record_observations = true, bool record_brain_graphs = true,
    std::unordered_set<std::uint64_t>* known_brains = nullptr,
    const std::vector<EcoEvent>* recorded_events = nullptr);
void write_ecosystem_stats_header(std::ostream& stream);
void write_ecosystem_stats(std::ostream& stream, const EcosystemWorld& world);

} // namespace neuroevo
