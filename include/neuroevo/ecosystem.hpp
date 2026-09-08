#pragma once

#include "neuroevo/brain.hpp"
#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_set>
#include <vector>

namespace neuroevo {

// Together with Brain these are the inherited genome. Health/actions are state.
struct BodyGenes {
    double mass = 1.0, carnivory = 0.0;
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
    Vec2 shelter_origin; // Original shelter center, stable across food relocations.
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
    std::uint64_t genome_id = 0, feeding_bouts = 0;
    double last_fed_time = -1e9;
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
    std::uint64_t natural_spiking_breeders = 0, mature_offspring = 0;
    std::uint64_t founder_births = 0, descendant_births = 0, births_first_100s = 0;
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
    bool relocate_shelter_food(EcoResource& resource);
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
    void save_checkpoint(std::ostream& stream) const;
    static EcosystemWorld load_checkpoint(std::istream& stream);
};

std::vector<std::string> ecosystem_input_labels(bool extended = true, bool predation = false,
    bool typed_food_proximity = true);
const Brain::InputGroups& ecosystem_input_groups(bool extended = true, bool predation = false);
// A deliberately small, deterministic founder genome. It uses five hidden
// neurons and a sparse subset of the ecosystem sensors; it remains an ordinary
// spiking Brain and offspring can mutate it through the normal birth path.
Brain make_sparse_ancestral_brain(const EcosystemConfig& config);
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
