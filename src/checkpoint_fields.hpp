#pragma once

#include "neuroevo/ecosystem.hpp"
#include <cmath>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace neuroevo::checkpoint {

template<class T> void write_value(std::ostream& s, const T& v)
{
    if constexpr (std::is_enum_v<T>) s << static_cast<int>(v) << ' ';
    else s << v << ' ';
}
template<class T> void read_value(std::istream& s, T& v)
{
    if constexpr (std::is_enum_v<T>) { int n = 0; s >> n; v = static_cast<T>(n); }
    else s >> v;
    if (!s) throw std::runtime_error("Truncated or invalid checkpoint");
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(v)) throw std::runtime_error("Non-finite checkpoint value");
    }
}
template<class... T> void write(std::ostream& s, const T&... v)
{
    (write_value(s, v), ...); s << '\n';
    if (!s) throw std::runtime_error("Failed to write checkpoint");
}
template<class... T> void read(std::istream& s, T&... v) { (read_value(s, v), ...); }
template<class T> auto brain_fields(T& c) {
    return std::tie(c.input_count,c.sensory_input_count,c.hidden_count,c.output_count,c.dt,
        c.membrane_tau,c.threshold,c.reset_potential,c.refractory_time,c.input_gain,c.synaptic_gain,
        c.seed_input_output_synapses,c.seed_input_output_weight,c.has_clock_input,c.clock_input_index,
        c.has_episode_start_input,c.episode_start_input_index,c.background_activity_enabled,
        c.background_event_rate_hz,c.background_event_current,c.initial_background_sensitivity,
        c.initial_background_sensitivity_sigma,c.max_bias_fraction_of_threshold,c.motor_trace_decay,
        c.conduction_speed,c.initial_connection_probability,c.max_delay_steps);
}
template<class T> auto legacy_mutation_fields(T& c) {
    return std::tie(c.weight_sigma,c.bias_sigma,c.threshold_sigma,c.position_sigma,c.hidden_bias_min,
        c.hidden_bias_max,c.hidden_bias_jump_min_magnitude,c.hidden_bias_jump_probability,
        c.background_sensitivity_sigma,c.background_sensitivity_min,c.background_sensitivity_max,
        c.add_synapse_probability,c.add_reciprocal_motif_probability,c.remove_synapse_probability,
        c.mutate_weight_probability,c.mutate_neuron_probability,c.mutate_clock_threshold_probability,
        c.clock_threshold_sigma,c.clock_threshold_min,c.clock_threshold_max);
}

template<class T> auto calibrated_brain_fields(T& c) {
    return std::tie(c.calibrated_io,c.sensory_rate_hz,c.motor_rate_tau,c.motor_reference_hz);
}
template<class T> auto interface_config_fields(T& c) {
    return std::tie(c.extended_senses,c.actuator_tau,c.archive_eval_trials,c.archive_eval_seed,c.archive_eval_seconds);
}
template<class T> auto newborn_trial_fields(T& t) {
    return std::tie(t.seed,t.offspring,t.descendant_births,t.mature_offspring,t.elapsed,t.focal_age,
        t.food_energy,t.operating_energy,t.first_birth,t.second_birth,t.matured,t.focal_alive,t.capacity_limited);
}
template<class T> auto mutation_fields(T& c) {
    return std::tuple_cat(legacy_mutation_fields(c),std::tie(c.add_neuron_probability,c.max_hidden_neurons));
}
template<class T> auto world_config_fields(T& c) {
    return std::tie(c.width,c.height,c.initial_creatures,c.max_population,c.shelters,c.grazing_patches,
        c.fruit_patches,c.pods,c.seed,c.dt,c.radius,c.max_speed,c.max_turn_rate,c.vision_range,c.fov_degrees,
        c.hearing_range,c.interaction_range,c.interaction_degrees,c.energy_capacity,c.founder_energy,
        c.basal_cost,c.movement_cost,c.turn_cost,c.forage_cost,c.call_cost,c.neuron_cost,c.synapse_cost,
        c.spike_cost,c.rough_multiplier,c.ingestion_rate,c.digestion_delay,c.graze_capacity,c.fruit_capacity,
        c.pod_capacity,c.graze_regrowth,c.fruit_regrowth,c.pod_regrowth,c.pod_work,c.pod_decay,
        c.pod_open_duration,c.calm_duration,c.warning_duration,c.storm_duration,c.storm_cost,c.phase_offset,
        c.maturity_age,c.reproduction_threshold,c.reproduction_cost,c.offspring_energy,
        c.reproduction_cooldown,c.motor_gain,c.reproduction,c.communication,c.food_assignment,c.controller);
}
template<class T> auto food_energy_config_fields(T& c) {
    return std::tie(c.graze_energy,c.poor_fruit_energy,c.rich_fruit_energy,c.pod_energy);
}
template<class T> auto predation_config_fields(T& c) {
    return std::tie(c.predation,c.founder_mass,c.founder_carnivory,c.health_per_mass,c.body_energy_per_mass,
        c.attack_range,c.attack_degrees,c.attack_damage,c.attack_cost,c.healing_rate,c.healing_cost,
        c.meat_energy,c.meat_decay,c.carcass_recovery,c.mass_mutation_probability,c.mass_mutation_sigma,
        c.carnivory_mutation_probability,c.carnivory_mutation_sigma);
}
template<class T> auto predation_total_fields(T& t) {
    return std::tie(t.attacking,t.healing,t.body_construction,t.external_body_energy,t.carcass_energy,
        t.meat_spoiled_energy,t.damage,t.predation_deaths);
}
template<class T> auto total_fields(T& t) {
    return std::tie(t.births,t.deaths,t.maturations,t.spikes,t.pods_opened,t.consumed_biomass,
        t.regrown_biomass,t.spoiled_biomass,t.energy_gained,t.metabolism,t.movement,t.turning,t.foraging,
        t.calling,t.neural,t.exposure,t.reproduction_overhead,t.discarded_energy);
}

template<class T> auto legacy_establishment_config_fields(T& c) {
    return std::tie(c.establishment,c.immigration_auto_stop,c.immigration_floor,c.immigration_batch,
        c.archive_capacity,c.withdrawal_cycles,c.immigration_interval,c.archive_min_energy,c.archive_min_age);
}
template<class T> auto v3_establishment_config_fields(T& c) {
    return std::tuple_cat(legacy_establishment_config_fields(c),
        std::tie(c.archive_min_feeding_bouts,c.archive_min_efficiency));
}
template<class T> auto v4_establishment_config_fields(T& c) {
    return std::tuple_cat(v3_establishment_config_fields(c),std::tie(c.storms_enabled));
}
template<class T> auto establishment_config_fields(T& c) {
    return std::tuple_cat(v4_establishment_config_fields(c),std::tie(c.archive_tournament_size));
}
template<class T> auto legacy_establishment_total_fields(T& t) {
    return std::tie(t.immigrants,t.immigrant_mutations,t.immigrant_clones,t.immigrant_random,
        t.archive_fallbacks,t.natural_spiking_breeders,t.mature_offspring,t.immigrant_energy);
}
template<class T> auto establishment_total_fields(T& t) {
    return std::tuple_cat(legacy_establishment_total_fields(t),
        std::tie(t.immigrant_slight_mutations,t.immigrant_strong_mutations,t.archive_empty_checks,
            t.founder_births,t.immigrant_births,t.descendant_births,t.births_first_100s));
}
template<class T> auto archive_trial_fields(T& t) {
    return std::tie(t.creature_id,t.feeding_bouts,t.offspring,t.energy_gained,t.energy_spent,t.age,t.observed_at,t.finished);
}
template<class Tuple> void write_tuple(std::ostream& s, Tuple t) {
    std::apply([&s](const auto&... v) { write(s,v...); },t);
}
template<class Tuple> void read_tuple(std::istream& s, Tuple t) {
    std::apply([&s](auto&... v) { read(s,v...); },t);
}
inline std::size_t count(std::istream& s, std::size_t maximum) {
    std::size_t n = 0; read(s,n);
    if (n > maximum) throw std::runtime_error("Checkpoint collection exceeds supported size");
    return n;
}
inline void marker(std::istream& s, const char* expected) {
    std::string got; read(s,got);
    if (got != expected) throw std::runtime_error(std::string("Expected checkpoint marker ") + expected);
}
} // namespace neuroevo::checkpoint
