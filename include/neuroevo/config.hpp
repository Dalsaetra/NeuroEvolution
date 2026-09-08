#pragma once

#include <cstddef>
#include <cstdint>

namespace neuroevo {

// Tune new runs here, then rebuild. Checkpoints preserve their own settings.
enum class Terrain { Ground, Rough, Wall, Shelter };
enum class FoodKind { Graze, FruitA, FruitB, Pod, Meat };
enum class PodState { Closed, Open, Refilling };
enum class ControllerKind { Spiking, Reactive, Random };
enum class WeatherPhase { Calm, Warning, Storm };
enum class CreatureOrigin { Founder, Birth };
constexpr std::size_t eco_sectors = 3;
constexpr std::size_t eco_sector_channels = 14;
constexpr std::size_t eco_unsheltered_input = eco_sectors * eco_sector_channels + 17;
constexpr std::size_t eco_legacy_input_count = eco_unsheltered_input + 1;
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

struct BrainConfig {
    std::size_t input_count = eco_predation_input_count;
    std::size_t hidden_count = 16; // Random founders only; the sparse ancestor has five.
    std::size_t output_count = eco_predation_output_count;
    double dt = 0.02;
    double membrane_tau = 0.10;
    double threshold = 1.0;
    double reset_potential = 0.0;
    double refractory_time = 0.04;
    double input_gain = 25.0;
    double synaptic_gain = 32.0;
    bool background_activity_enabled = true;
    double background_event_rate_hz = 2.0;
    double background_event_current = 25.0;
    double initial_background_sensitivity = 0.10;
    double initial_background_sensitivity_sigma = 0.03;
    double max_bias_fraction_of_threshold = 0.95;
    double motor_trace_decay = 0.82;
    double conduction_speed = 6.0;
    double initial_connection_probability = 0.08; // Random founders only.
    std::size_t max_delay_steps = 8;
    // Rate encoding and decoding for the nursery sensor/motor interface.
    bool calibrated_io = true;
    double sensory_rate_hz = 20.0;
    double motor_rate_tau = 0.20;
    double motor_reference_hz = 10.0;
};

// Scales applied to the base mutation settings for a single birth.
struct BirthMutationProfile {
    double sigma_scale;
    double operator_scale;
    double structural_probability;
    std::size_t local_edits;
    double weight_limit;
    double body_sigma_scale;
    double body_probability_scale;
};

struct MutationConfig {
    // Copy / slight / strong inheritance; strong is the remaining probability.
    double copy_probability = 0.50;
    double slight_probability = 0.45;
    double disconnected_neuron_prune_probability = 0.25;
    BirthMutationProfile slight{
        0.75, // sigma_scale
        0.9,  // operator_scale
        0.30, // structural_probability
        2,    // local_edits
        1.0,  // weight_limit
        0.45, // body_sigma_scale
        0.65  // body_probability_scale
    };
    BirthMutationProfile strong{
        1.75, // sigma_scale
        1.0,  // operator_scale
        0.50, // structural_probability
        4,    // local_edits
        2.0,  // weight_limit
        1.75, // body_sigma_scale
        2.0   // body_probability_scale
    };
    bool balance_structural_pairs = true;
    bool allow_birth_motifs = false;
    double mass_mutation_probability = 0.2, mass_mutation_sigma = 0.12;
    double carnivory_mutation_probability = 0.2, carnivory_mutation_sigma = 0.24;

    // Direct Brain::mutate controls. Births derive these from the profiles above.
    // A structural budget of -1 uses the sum of operator probabilities.
    double structural_edit_probability = -1.0;
    std::size_t local_edit_limit = 2;
    double local_weight_limit_multiplier = 1.0;
    double weight_sigma = 1.0;
    double bias_sigma = 0.75;
    double threshold_sigma = 0.4;
    double position_sigma = 0.3;
    double hidden_bias_min = -15.0;
    double background_sensitivity_sigma = 0.3;
    double background_sensitivity_min = 0.0;
    double background_sensitivity_max = 2.0;
    double add_synapse_probability = 0.40;
    // Adds a hidden neuron as a side branch of an existing connection. Keeping
    // the original connection makes topology growth locally heritable.
    double add_neuron_probability = 0.12;
    std::size_t max_hidden_neurons = 128;
    double add_reciprocal_motif_probability = 0.12;
    double remove_synapse_probability = 0.30;
    double rewire_synapse_probability = 0.40;
    // Remove one hidden neuron and all incident edges; sensor/motor slots survive.
    double remove_neuron_probability = 0.06;
    double mutate_weight_probability = 0.22;
    double mutate_neuron_probability = 0.16;
};

struct EcosystemConfig {
    // Body, diet and predation
    double carnivore_basal_fraction = 0.2; // Basal-rate multiplier at full carnivory.
    bool predation = true;
    double founder_mass = 1.0, founder_carnivory = 0.0;
    double health_per_mass = 20, body_energy_per_mass = 30;
    double attack_range = 0.8, attack_degrees = 60, attack_damage = 35, attack_cost = 2;
    double attack_base_fraction = 0.15; // Fraction of full attack damage at zero carnivory.
    double healing_rate = 0.1, healing_cost = 2;
    double meat_energy = 50, meat_decay = 0.0025, carcass_recovery = 0.95;

    // Nursery and frontier layout
    bool nursery_frontier = true; // Disable only for controlled mechanics experiments.
    std::size_t nursery_size = 16;
    std::size_t nursery_exit_width = 3;
    std::size_t shelter_size = 8;
    std::size_t nursery_food_patches = 16;
    bool nursery_food_relocates = true;
    double nursery_food_decay = 0.005; // Biomass per second, including during storms.
    double nursery_food_energy = 20, nursery_food_capacity = 2, nursery_food_regrowth = 0.02;

    // World size, population and seed
    std::size_t width = 80, height = 80, initial_creatures = 24, max_population = 200;
    std::size_t shelters = 20, grazing_patches = 300, fruit_patches = 80, pods = 120;
    std::uint64_t seed = 7;

    // Motion and local senses
    double dt = 0.10, radius = 0.25, max_speed = 1.5, max_turn_rate = 3.141592653589793;
    double vision_range = 12.0, fov_degrees = 150.0, hearing_range = 6.0;
    double interaction_range = 0.8, interaction_degrees = 80.0;

    // Energy budget
    double energy_capacity = 200, founder_energy = 90, basal_cost = 0.20;
    double movement_cost = 0.12, turn_cost = 0.02, forage_cost = 0.30, call_cost = 0.05;
    double neuron_cost = 0.0001, synapse_cost = 0.00001, spike_cost = 0.00001;
    double rough_multiplier = 2.0, ingestion_rate = 1.0, digestion_delay = 3.0;

    // Food nutrition, renewal and decay
    double graze_capacity = 3, fruit_capacity = 5, pod_capacity = 10;
    double graze_energy = 60, poor_fruit_energy = 45, rich_fruit_energy = 90, pod_energy = 120;
    double graze_regrowth = 0.02, fruit_regrowth = 0.01, pod_regrowth = 0.08;
    bool outdoor_food_relocates = true;
    double graze_decay = 0.005, fruit_decay = 0.005; // biomass per second
    double shelter_food_decay = 0.005;
    double shelter_food_energy = 1, shelter_food_capacity = 2, shelter_food_regrowth = 0.6;
    double pod_work = 10, pod_decay = 1, pod_open_duration = 30;

    // Weather
    double calm_duration = 180, warning_duration = 40, storm_duration = 60;
    double storm_cost = 7.0, phase_offset = 0;

    // Reproduction
    double maturity_age = 30, reproduction_threshold = 150, reproduction_cost = 60;
    double offspring_energy = 60, reproduction_cooldown = 30;

    // Neural interface and controllers
    double motor_gain = 1.0, actuator_tau = 0.30;
    bool extended_senses = true;
    bool typed_food_proximity = true;
    bool reproduction = true, communication = true, storms_enabled = true;
    // -1 chooses the assignment from the map RNG, 0 makes A rich, 1 makes B rich.
    int food_assignment = -1;
    ControllerKind controller = ControllerKind::Spiking;
    bool sparse_ancestor = true;
    BrainConfig brain;
    MutationConfig mutation;
    // Derived interface dimensions only; no hidden tuning overrides.
    EcosystemConfig() { set_predation(predation); }
    void set_predation(bool enabled);
    void validate() const;
};

// Execution and recording defaults. These apply to each invocation, including
// resume; biological configuration and RNG state come from the checkpoint.
struct RunConfig {
    std::size_t steps = 4800;
    std::size_t record_every = 10;
    bool record_brains = true;
    bool record_observations = true;
    bool record_brain_graphs = true;
    bool record_routine_events = true;
    double detailed_tail_seconds = 0;
    std::size_t tail_record_every = 10;
};

} // namespace neuroevo
