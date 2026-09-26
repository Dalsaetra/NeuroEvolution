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

void IzhikevichParameters::validate() const
{
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) || !std::isfinite(d)
        || a < 0.001 || a > 0.2 || b < 0 || b > 0.3 || c < -80 || c > -40 || d < 0 || d > 20)
        throw std::invalid_argument("Izhikevich parameters require a=0.001..0.2, b=0..0.3, c=-80..-40, d=0..20");
}

void BrainConfig::select_model(NeuronModel model)
{
    if (model == NeuronModel::FilteredLif || neuron_model == NeuronModel::FilteredLif) {
        membrane_tau = model == NeuronModel::FilteredLif ? 0.05 : 0.10;
        refractory_time = model == NeuronModel::FilteredLif ? 0.01 : 0.04;
        synaptic_gain = model == NeuronModel::FilteredLif ? 12.0 : 32.0;
    }
    neuron_model = model;
    dt = model == NeuronModel::Izhikevich ? 0.001 : model == NeuronModel::FilteredLif ? 0.005 : 0.02;
    max_delay_steps = static_cast<std::size_t>(std::lround(0.160/dt));
    validate_model();
}

const char* neuron_model_name(NeuronModel model)
{
    switch(model) {
    case NeuronModel::Lif: return "lif";
    case NeuronModel::Izhikevich: return "izhikevich";
    case NeuronModel::FilteredLif: return "filtered-lif";
    }
    throw std::invalid_argument("Unknown neuron model");
}

BrainConfig BrainConfig::filtered_lif(double timestep)
{
    BrainConfig result;
    result.select_model(NeuronModel::FilteredLif);
    result.dt=timestep;
    result.validate_model();
    result.max_delay_steps=static_cast<std::size_t>(std::ceil(0.160/timestep));
    return result;
}

BrainConfig BrainConfig::izhikevich()
{
    BrainConfig result;
    result.select_model(NeuronModel::Izhikevich);
    return result;
}

void BrainConfig::validate_model() const
{
    if (neuron_model != NeuronModel::Lif && neuron_model != NeuronModel::Izhikevich && neuron_model != NeuronModel::FilteredLif)
        throw std::invalid_argument("Unknown neuron model");
    if (neuron_model == NeuronModel::FilteredLif) {
        if (!std::isfinite(dt) || dt < 0.00005 || dt > 0.0050000001 || !calibrated_io)
            throw std::invalid_argument("Filtered LIF requires calibrated IO and a timestep from 0.05 to 5 ms; use 5 or 2 ms");
        positive(membrane_tau,"Filtered LIF membrane time constant");
        positive(synaptic_tau,"Filtered LIF synaptic time constant");
        positive(refractory_time,"Filtered LIF refractory time",true);
        if (membrane_tau < dt || synaptic_tau < dt || std::abs(refractory_time/dt-std::round(refractory_time/dt))>1e-8)
            throw std::invalid_argument("Filtered LIF time constants must be >= dt and refractory time must be a whole number of steps");
    }
    izhikevich_defaults.validate();
    if (neuron_model == NeuronModel::Izhikevich
        && (!std::isfinite(dt) || dt <= 0 || dt > 0.0010000001 || !calibrated_io))
        throw std::invalid_argument("Izhikevich brains require calibrated IO and a timestep <= 1 ms; use BrainConfig::izhikevich()");
    if (neuron_model == NeuronModel::Izhikevich
        && (0.001/dt > 1000 || std::abs(0.001/dt-std::round(0.001/dt))>1e-8))
        throw std::invalid_argument("Izhikevich timestep must divide the fixed 1 ms synaptic pulse (at most 1000 subdivisions)");
}

std::size_t BrainConfig::synaptic_pulse_steps() const
{
    return neuron_model==NeuronModel::Izhikevich ? static_cast<std::size_t>(std::lround(0.001/dt)) : 1;
}

void EcosystemConfig::set_predation(bool enabled)
{
    predation = enabled;
    if (!enabled) funded_reproduction=false;
    brain.input_count = enabled ? eco_predation_input_count
        : extended_senses ? eco_input_count : eco_legacy_input_count;
    brain.output_count = enabled ? eco_predation_output_count : eco_output_count;
}

std::array<double,3> MutationConfig::inheritance_probabilities(double scale) const
{
    if (!meta_mutation_enabled) return {copy_probability,slight_probability,1-copy_probability-slight_probability};
    const double x=std::clamp(scale,min_mutation_scale,max_mutation_scale);
    if (x<1) {
        // Historical checkpoints allowed scales down to 0.5. Keep that fixed
        // interpolation anchor, independent of the configured clamping floor.
        const double t=(x-0.5)/0.5;
        return {0.75-0.25*t,0.25+0.20*t,0.05*t};
    }
    const double t=(x-1)/(max_mutation_scale-1);
    return {0.5*(1-t),0.45+0.05*t,0.05+0.45*t};
}

double EcosystemConfig::max_energy(double mass) const
{
    if (!predation || !mass_scaled_energy_capacity) return energy_capacity;
    if (mass_allometry) return energy_capacity * std::pow(mass, energy_mass_exponent);
    return energy_capacity * (reproduction_cost + body_energy_per_mass * mass)
        / (reproduction_cost + body_energy_per_mass);
}

void EcosystemConfig::validate() const
{
    brain.validate_model();
    if (food_distribution != FoodDistribution::Scattered && food_distribution != FoodDistribution::FieldsAndTrees)
        throw std::invalid_argument("Invalid food distribution preset");
    const auto& fs=food_sources;
    if (background_food_patches > 1000000) throw std::invalid_argument("Too many background food patches");
    positive(background_food_energy,"Background food energy");
    if (fs.fields > 100 || fs.fruit_trees > 1000 || fs.pod_trees > 1000
        || fs.fruit_sites < 1 || fs.fruit_sites > 32 || fs.pod_sites < 1 || fs.pod_sites > 32)
        throw std::invalid_argument("Invalid food source or tree site count");
    positive(fs.field_radius,"Field radius");positive(fs.field_spacing,"Field spacing");
    positive(fs.source_gap,"Food source gap",true);positive(fs.tree_radius,"Tree radius");
    positive(fs.field_energy,"Field energy");positive(fs.field_capacity,"Field capacity");
    positive(fs.field_regrowth,"Field regrowth",true);
    positive(fs.fruit_production,"Fruit tree production");positive(fs.pod_production,"Pod tree production");
    if (fs.field_spacing < 1 || fs.field_spacing > 2 || fs.field_radius < fs.field_spacing
        || fs.field_radius > 100 || fs.tree_radius < 2 || fs.tree_radius > 30)
        throw std::invalid_argument("Fields require spacing 1..2 and radius spacing..100; tree radius must be 2..30");
    positive(nursery_food_energy_factor, "Nursery food energy factor");
    positive(nursery_food_reduction_delay, "Nursery food reduction delay", true);
    if (nursery_food_energy_factor > 1)
        throw std::invalid_argument("Nursery food energy factor must be in (0,1]");
    probability(mutation.izhikevich_intrinsic_probability, "Izhikevich intrinsic mutation probability");
    positive(mutation.izhikevich_log_sigma, "Izhikevich mutation log sigma", true);
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
    positive(max_acceleration, "Maximum acceleration");
    for (const auto exponent : {ingestion_mass_exponent, pod_mass_exponent, attack_mass_exponent,
             metabolism_mass_exponent, energy_mass_exponent, speed_mass_exponent, acceleration_mass_exponent})
        if (!std::isfinite(exponent) || exponent < -2 || exponent > 2)
            throw std::invalid_argument("Mass scaling exponents must be finite and between -2 and 2");
    positive(max_turn_rate, "Maximum turn rate", true);
    positive(vision_range, "Vision range");
    positive(hearing_range, "Hearing range");
    positive(fov_degrees, "Vision arc");
    positive(interaction_degrees, "Interaction arc");
    if (fov_degrees > 360 || interaction_degrees > 360) throw std::invalid_argument("Sensory and interaction arcs cannot exceed 360 degrees");
    positive(interaction_range, "Interaction range");
    positive(energy_capacity, "Energy capacity");
    positive(founder_energy, "Founder energy");
    if (founder_energy > max_energy(founder_mass)) throw std::invalid_argument("Founder energy exceeds capacity");
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
    if (offspring_energy > reproduction_cost) throw std::invalid_argument("Offspring energy must not exceed reproduction cost");
    if (!funded_reproduction && (reproduction_threshold > energy_capacity || reproduction_cost > reproduction_threshold))
        throw std::invalid_argument("Threshold reproduction requires birth cost <= threshold <= capacity");
    positive(motor_gain, "Motor gain");
    positive(actuator_tau, "Actuator time constant", true);
    if (food_assignment < -1 || food_assignment > 1) throw std::invalid_argument("Food assignment must be -1 (seeded), 0 (A rich), or 1 (B rich)");
    for (const auto value : {health_per_mass, body_energy_per_mass, attack_range, attack_degrees,
             attack_damage, attack_cost, healing_cost, meat_energy}) positive(value, "Predation parameter");
    positive(nursery_food_respawn_delay, "Nursery food respawn delay", true);
    positive(outdoor_food_respawn_delay, "Outdoor food respawn delay", true);
    if (!std::isfinite(max_energy(eco_min_mass)) || !std::isfinite(max_energy(eco_max_mass))
        || std::min(max_energy(eco_min_mass),max_energy(eco_max_mass))<=0
        || offspring_energy>std::min(max_energy(eco_min_mass),max_energy(eco_max_mass)))
        throw std::invalid_argument("Offspring reserve must fit the minimum body capacity");
    if (!std::isfinite(founder_reproduction_allocation) || founder_reproduction_allocation<0 || founder_reproduction_allocation>1
        || !std::isfinite(mutation.allocation_mutation_probability) || mutation.allocation_mutation_probability<0 || mutation.allocation_mutation_probability>1)
        throw std::invalid_argument("Reproduction allocation and mutation probability must be 0..1");
    positive(mutation.allocation_mutation_sigma,"Allocation mutation sigma",true);
    if(funded_reproduction && !predation) throw std::invalid_argument("Funded reproduction requires body mechanics");
    positive(storm_damage, "Storm damage", true);
    if (storm_health_damage && !predation) throw std::invalid_argument("Storm health damage requires predation/body mechanics");
    positive(meat_decay, "Outside meat decay", true);
    positive(nursery_meat_decay, "Nursery meat decay", true);
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
    if ((brain.input_count != inputs && !(predation && !funded_reproduction && brain.input_count==eco_reproduction_offset)) || brain.output_count != (predation ? eco_predation_output_count : eco_output_count)) throw std::invalid_argument("Ecological brains require the selected local sensor and motor layout");
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
    if (neurons * (brain.max_delay_steps + brain.synaptic_pulse_steps()) > 5000000 || neurons * (brain.hidden_count + brain.output_count) > 1000000) throw std::invalid_argument("Brain exceeds the v1 runtime buffer or synapse limits");
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
             mutation.add_autapse_probability,
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
    if (!std::isfinite(mutation.meta_mutation_probability) || mutation.meta_mutation_probability<0 || mutation.meta_mutation_probability>1)
        throw std::invalid_argument("Meta mutation probability must be 0..1");
    positive(mutation.meta_mutation_sigma, "Meta mutation sigma", true);
    if (!std::isfinite(mutation.min_mutation_scale) || mutation.min_mutation_scale<0.5 || mutation.min_mutation_scale>1)
        throw std::invalid_argument("Minimum mutation scale must be between 0.5 and 1");
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
