#include "neuroevo/config.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>

namespace neuroevo {
namespace {
constexpr double epsilon = 1e-10;
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

} // namespace

void EcosystemConfig::set_predation(bool enabled)
{
    predation = enabled;
    brain.input_count = enabled ? eco_predation_input_count
        : extended_senses ? eco_input_count : eco_legacy_input_count;
    brain.output_count = enabled ? eco_predation_output_count : eco_output_count;
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
        positive(nursery_food_energy, "Nursery food energy");
        positive(nursery_food_capacity, "Nursery food capacity");
        positive(nursery_food_regrowth, "Nursery food regrowth", true);
        if (nursery_food_relocates && (nursery_food_patches<1 || nursery_food_patches>(nursery_size-4)*(nursery_size-4)))
            throw std::invalid_argument("Nursery food patch count must fit the nursery interior");
    }
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
    positive(nursery_food_decay, "Nursery food decay", true);
    if (nursery_frontier && nursery_food_decay >= ingestion_rate)
        throw std::invalid_argument("Nursery food decay must be slower than ingestion");
    positive(graze_decay, "Graze decay", true);
    positive(fruit_decay, "Fruit decay", true);
    positive(shelter_food_decay, "Shelter food decay", true);
    if(shelter_food_decay>=ingestion_rate)throw std::invalid_argument("Shelter food decay must be slower than ingestion");
    positive(shelter_food_energy, "Shelter food energy");
    positive(shelter_food_capacity, "Shelter food capacity");
    positive(shelter_food_regrowth, "Shelter food regrowth", true);
    if (outdoor_food_relocates && (graze_decay >= ingestion_rate || fruit_decay >= ingestion_rate))
        throw std::invalid_argument("Food decay must be slower than ingestion");
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
    for (const auto value : {health_per_mass, body_energy_per_mass, attack_range, attack_degrees,
             attack_damage, attack_cost, healing_cost, meat_energy, meat_decay}) positive(value, "Predation parameter");
    for (const auto value : {healing_rate, mutation.mass_mutation_sigma, mutation.carnivory_mutation_sigma})
        positive(value, "Body mutation/healing parameter", true);
    for (const auto value : {founder_carnivory, mutation.mass_mutation_probability, mutation.carnivory_mutation_probability}) {
        positive(value, "Trait/probability", true);
        if (value > 1) throw std::invalid_argument("Traits and mutation probabilities must be in [0,1]");
    }
    positive(founder_mass, "Founder mass");
    if (founder_mass < eco_min_mass || founder_mass > eco_max_mass || attack_degrees > 360)
        throw std::invalid_argument("Mass must be 0.5..2; attack arc must be <=360 degrees");
    positive(attack_base_fraction, "Base attack fraction");
    positive(carnivore_basal_fraction, "Carnivore basal fraction");
    if (carnivore_basal_fraction > 1) throw std::invalid_argument("Carnivore basal fraction must be in (0,1]");
    if (attack_base_fraction > 1) throw std::invalid_argument("Base attack fraction must be in (0,1]");
    positive(carcass_recovery, "Carcass recovery");
    if (carcass_recovery >= 1) throw std::invalid_argument("Carcass recovery must be strictly less than one");
    if (predation && !extended_senses)
        throw std::invalid_argument("Predation requires extended senses");
    const auto inputs = predation ? eco_predation_input_count : extended_senses ? eco_input_count : eco_legacy_input_count;
    if (brain.input_count != inputs || brain.output_count != (predation ? eco_predation_output_count : eco_output_count)) throw std::invalid_argument("Ecological brains require the selected local sensor and motor layout");
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
    if (!std::isfinite(brain.reset_potential)) throw std::invalid_argument("Brain reset potential must be finite");
    probability(brain.initial_connection_probability, "Brain connection probability");
    probability(brain.max_bias_fraction_of_threshold, "Maximum neural bias fraction");
    probability(brain.motor_trace_decay, "Motor trace decay");
    if (brain.max_bias_fraction_of_threshold >= 1 || brain.motor_trace_decay >= 1 || brain.initial_background_sensitivity > 2) throw std::invalid_argument("Bias fraction and trace decay must be below 1; background sensitivity cannot exceed 2");
    for (const auto value : {mutation.weight_sigma, mutation.bias_sigma, mutation.threshold_sigma, mutation.position_sigma,
             mutation.background_sensitivity_sigma}) positive(value, "A mutation standard deviation", true);
    for (const auto value : {mutation.add_synapse_probability, mutation.add_neuron_probability,
             mutation.add_reciprocal_motif_probability,
             mutation.remove_synapse_probability, mutation.remove_neuron_probability, mutation.rewire_synapse_probability,
             mutation.mutate_weight_probability, mutation.mutate_neuron_probability}) probability(value, "Mutation probability");
    if (mutation.max_hidden_neurons < (sparse_ancestor && controller == ControllerKind::Spiking ? 2+eco_sectors : brain.hidden_count) || mutation.max_hidden_neurons > 10000)
        throw std::invalid_argument("Mutation hidden-neuron limit must include the initial brain and be at most 10000");
    if (!std::isfinite(mutation.hidden_bias_min) || mutation.hidden_bias_min > 0)
        throw std::invalid_argument("Hidden bias minimum must be finite and nonpositive");
    positive(mutation.background_sensitivity_min, "Minimum background sensitivity", true);
    positive(mutation.background_sensitivity_max, "Maximum background sensitivity", true);
    if (mutation.background_sensitivity_min > mutation.background_sensitivity_max || mutation.background_sensitivity_max > 2)
        throw std::invalid_argument("Mutation sensitivity bounds are reversed or outside [0,2]");
    for (const auto value : {mutation.copy_probability, mutation.slight_probability,
            mutation.disconnected_neuron_prune_probability}) probability(value, "Inheritance probability");
    if (mutation.copy_probability + mutation.slight_probability > 1)
        throw std::invalid_argument("Copy and slight inheritance probabilities must sum to at most one");
    if (!std::isfinite(mutation.structural_edit_probability) || (mutation.structural_edit_probability < 0 && mutation.structural_edit_probability != -1)
        || mutation.structural_edit_probability > 1)
        throw std::invalid_argument("Structural edit probability must be -1 (automatic) or in [0,1]");
    positive(mutation.local_weight_limit_multiplier, "Local weight limit", true);
    if (mutation.local_edit_limit > 10000) throw std::invalid_argument("Local edit limit exceeds 10000");
    for (const auto& profile : {mutation.slight, mutation.strong}) {
        probability(profile.structural_probability, "Birth structural probability");
        for (const auto scale : {profile.sigma_scale, profile.operator_scale, profile.weight_limit,
                profile.body_sigma_scale, profile.body_probability_scale})
            positive(scale, "Birth mutation scale", true);
        if (profile.local_edits > 10000) throw std::invalid_argument("Birth local edit limit exceeds 10000");
    }
    if (controller != ControllerKind::Spiking && controller != ControllerKind::Reactive && controller != ControllerKind::Random) throw std::invalid_argument("Invalid controller kind");
}

} // namespace neuroevo
