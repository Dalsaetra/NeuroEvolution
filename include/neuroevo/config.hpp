#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace neuroevo {

// Tune new runs here, then rebuild. Checkpoints preserve their own settings.
enum class Terrain { Ground, Rough, Wall, Shelter };
enum class FoodKind { Graze, FruitA, FruitB, Pod, Meat };
enum class FoodDistribution { Scattered, FieldsAndTrees };
const char* food_distribution_name(FoodDistribution preset);
struct FoodSourceConfig {
    std::size_t fields = 3, fruit_trees = 18, pod_trees = 3;
    std::size_t fruit_sites = 8, pod_sites = 5;
    double field_radius = 12, field_spacing = 1.5, source_gap = 5;
    double field_energy = 25, field_capacity = 3, field_regrowth = 0.04;
    double tree_radius = 3;
    // Total biomass/second per tree, shared by its sites; fruit ripens in batches.
    double fruit_production = 0.4, pod_production = 0.12;
};
enum class PodState { Closed, Open, Refilling };
enum class ControllerKind { Spiking, Reactive, Random };
enum class NeuronModel { Lif, Izhikevich, FilteredLif };
const char* neuron_model_name(NeuronModel model);
struct IzhikevichParameters {
    double a = 0.02, b = 0.2, c = -65.0, d = 8.0; // Regular spiking.
    void validate() const;
};
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
constexpr std::size_t eco_plant_offset = eco_health_offset + 2;
constexpr std::size_t eco_reproduction_offset = eco_plant_offset + eco_sectors;
constexpr std::size_t eco_predation_input_count = eco_reproduction_offset + 2;
constexpr std::size_t eco_predation_output_count = 6;
constexpr double eco_min_mass = 0.5, eco_max_mass = 2.0;

struct BrainConfig {
    NeuronModel neuron_model = NeuronModel::Lif;
    IzhikevichParameters izhikevich_defaults;
    // Select before constructing a brain. This also selects neural timing;
    // it does not convert an existing genome or its active state.
    void select_model(NeuronModel model);
    static BrainConfig izhikevich();
    static BrainConfig filtered_lif(double timestep = 0.005);
    // Exponential synaptic current; global/inherited, not intrinsically mutated.
    double synaptic_tau = 0.10;
    void validate_model() const;
    std::size_t synaptic_pulse_steps() const;
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
    // Opt-in probability within an Izhikevich neuron-parameter edit. Only a/d
    // change; b/c remain inherited. Zero keeps intrinsic parameters fixed.
    double izhikevich_intrinsic_probability = 0.0;
    double izhikevich_log_sigma = 0.05;
    // Copy / slight / strong inheritance; strong is the remaining probability.
    // New runs evolve between the standard mixture (1) and the upper knot (2).
    // The checkpoint carries the floor so historical runs retain their policy.
    bool meta_mutation_enabled = true;
    double meta_mutation_probability = 0.10, meta_mutation_sigma = 0.10;
    double min_mutation_scale = 1.0;
    static constexpr double max_mutation_scale = 2.0;
    std::array<double,3> inheritance_probabilities(double scale) const; // copy, slight, strong
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
    double allocation_mutation_probability = 0.2, allocation_mutation_sigma = 0.1;
    double mass_mutation_probability = 0.2, mass_mutation_sigma = 0.24;
    double carnivory_mutation_probability = 0.2, carnivory_mutation_sigma = 0.4;

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
    // Relative choice weight within the structural mutation budget, not an
    // independent per-neuron probability. Adds at most one hidden-neuron loop.
    double add_autapse_probability = 0.10;
    double remove_synapse_probability = 0.30;
    double rewire_synapse_probability = 0.40;
    // Remove one hidden neuron and all incident edges; sensor/motor slots survive.
    double remove_neuron_probability = 0.06;
    double mutate_weight_probability = 0.22;
    double mutate_neuron_probability = 0.16;
};

struct EcosystemConfig {
    // Body, diet and predation
    double carnivore_basal_fraction = 0.5; // Basal-rate multiplier at full carnivory.
    bool predation = true;
    bool shelter_predation_damage = false; // Allow damage to targets in ordinary shelters; nursery stays protected.
    double founder_mass = 1.0, founder_carnivory = 0.4;
    double health_per_mass = 20, body_energy_per_mass = 60;
    double attack_range = 1.6, attack_degrees = 60, attack_damage = 35, attack_cost = 0.01;
    double attack_base_fraction = 0.15; // Fraction of full attack damage at zero carnivory.
    // Mass is relative to a reference body of mass 1. Historical checkpoints disable these rules.
    bool mass_allometry = true;
    double ingestion_mass_exponent = 0.8;
    double pod_mass_exponent = 2.0 / 3.0, attack_mass_exponent = 2.0 / 3.0;
    double metabolism_mass_exponent = 0.75, energy_mass_exponent = 1.0;
    double speed_mass_exponent = 0.25, acceleration_mass_exponent = -0.5;
    double max_acceleration = 3.0; // Speed units/second at mass 1; also limits braking.
    double healing_rate = 0.1, healing_cost = 10.0;
    double meat_energy = 60, meat_decay = 0.0002, carcass_recovery = 0.95;
    double nursery_meat_decay = 0.005; // Biomass/second inside nursery; meat_decay applies outside.

    // Nursery and frontier layout
    bool nursery_frontier = true; // Disable only for controlled mechanics experiments.
    std::size_t nursery_size = 16;
    std::size_t nursery_exit_width = 3;
    std::size_t nursery_food_patches = 16;
    bool nursery_food_relocates = true;
    double nursery_food_decay = 0.005; // Biomass per second, including during storms.
    double nursery_food_energy = 100, nursery_food_capacity = 2, nursery_food_regrowth = 0;
    // Reduce nutrition once per upward population crossing; zero disables it.
    std::size_t nursery_food_population_threshold = 60;
    double nursery_food_energy_factor = 0.8;
    double nursery_food_reduction_delay = 1000; // Simulation seconds between reductions; 0 disables cooldown.

    // World size, population and seed
    std::size_t width = 140, height = 140, initial_creatures = 48, max_population = 500;
    std::size_t shelters = 40, shelter_size = 9;
    std::size_t grazing_patches = 250, fruit_patches = 80, pods = 120;
    std::uint64_t seed = 8;

    // Motion and local senses
    double dt = 0.10, radius = 0.25, max_speed = 1.5, max_turn_rate = 3.141592653589793;
    double vision_range = 12.0, fov_degrees = 150.0, hearing_range = 6.0;
    double interaction_range = 1.0, interaction_degrees = 80.0;

    // Energy budget
    bool mass_scaled_energy_capacity = true; // Allometric reserves; legacy mode scales like child construction cost.
    double max_energy(double mass) const;
    double energy_capacity = 250, founder_energy = 90, basal_cost = 1.0;
    double movement_cost = 0.12, turn_cost = 0.1, forage_cost = 0.30, call_cost = 0.005;
    double neuron_cost = 0.00001, synapse_cost = 0.000001, spike_cost = 0.000001;
    double rough_multiplier = 1.5, ingestion_rate = 1.0, digestion_delay = 3.0;

    // Food nutrition, renewal and decay
    FoodDistribution food_distribution = FoodDistribution::FieldsAndTrees;
    FoodSourceConfig food_sources;
    // Additional scattered graze in fields-and-trees worlds, including ordinary shelters.
    std::size_t background_food_patches = 250;
    double background_food_energy = 10; // Uses same capacity as graze_capacity
    double graze_capacity = 3, fruit_capacity = 4, pod_capacity = 10;
    double graze_energy = 40, poor_fruit_energy = 30, rich_fruit_energy = 70, pod_energy = 120;
    // Optional passive biomass/second, also active with relocation. Pods need refill growth.
    double graze_regrowth = 0, fruit_regrowth = 0, pod_regrowth = 0.08;
    bool outdoor_food_relocates = true;
    // Seconds after graze/fruit depletion before relocation or regrowth can resume.
    double nursery_food_respawn_delay = 1.0, outdoor_food_respawn_delay = 1.0;
    double graze_decay = 0.005, fruit_decay = 0.005; // biomass per second
    double shelter_food_decay = 0.005;
    double shelter_food_energy = 1, shelter_food_capacity = 2, shelter_food_regrowth = 0;
    double pod_work = 10, pod_decay = 1, pod_open_duration = 30;

    // Weather
    double calm_duration = 300, warning_duration = 40, storm_duration = 60;
    double storm_cost = 5.0, phase_offset = 0;
    bool storm_health_damage = true; // Use health damage instead of energy drain; requires predation.
    bool storm_ramp = true; // Triangular intensity; exposed harvest efficiency falls to 50% at peak.
    double storm_damage = 0.5; // Peak health/second independent of mass; health capacity scales with mass.

    // Reproduction
    bool funded_reproduction = true;
    double founder_reproduction_allocation = 0.5;
    double maturity_age = 30, reproduction_threshold = 180, reproduction_cost = 60;
    double offspring_energy = 60, reproduction_cooldown = 20;

    // Neural interface and controllers
    double motor_gain = 1.0, actuator_tau = 0.30;
    bool extended_senses = true;
    bool typed_food_proximity = true;
    bool reproduction = true, communication = true, storms_enabled = true;
    // -1 chooses the assignment from the map RNG, 0 makes A rich, 1 makes B rich.
    int food_assignment = -1;
    ControllerKind controller = ControllerKind::Spiking;
    bool sparse_ancestor = true;
    bool mutate_initial_ancestors = true; // One independent strong brain/body mutation pass per fresh ancestor.
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
    int worker_threads = 0; // Auto: up to eight; one disables parallel controllers.
    std::size_t steps = 4800;
    std::size_t record_every = 500;
    bool record_brains = false;
    bool record_observations = false;
    bool record_brain_graphs = false;
    bool record_routine_events = false;
    double detailed_tail_seconds = 600;
    std::size_t tail_record_every = 10;
};

} // namespace neuroevo
