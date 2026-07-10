#pragma once

#include "neuroevo/environment.hpp"
#include "neuroevo/nsga2.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace neuroevo {

enum class ObjectiveSet {
    Basic,
    Extended,
};

struct ObjectiveConfig {
    ObjectiveSet objective_set = ObjectiveSet::Basic;
    double target_foods_per_trial = 3.0;
    double target_spike_rate = 0.02;
    double synapse_budget = 64.0;
    double neuron_budget = 64.0;
};

struct GenomeComplexity {
    std::size_t neuron_count = 0;
    std::size_t hidden_node_count = 0;
    std::size_t enabled_synapses = 0;
    std::size_t disabled_synapses = 0;
};

struct EvaluationMetrics {
    double raw_fitness = 0.0;
    double raw_reward = 0.0;
    double raw_penalty = 0.0;
    double raw_foods_collected = 0.0;
    double raw_occluded_foods_collected = 0.0;
    double raw_spikes = 0.0;
    double raw_steps_used = 0.0;
    double mean_spike_rate = 0.0;
    double task_score_norm = 0.0;
    double spike_energy_norm = 0.0;
    double synapse_count_norm = 0.0;
    double neuron_count_norm = 0.0;
    double time_cost_norm = 1.0;
    double scalar_display_score = 0.0;
    GenomeComplexity complexity;
};

ObjectiveSet parse_objective_set(const std::string& value);
std::string to_string(ObjectiveSet objective_set);

std::vector<ObjectiveDescriptor> make_objective_descriptors(ObjectiveSet objective_set);
std::vector<double> make_objective_vector(const EvaluationMetrics& metrics, ObjectiveSet objective_set);

EvaluationMetrics make_evaluation_metrics(
    const EvaluationResult& evaluation,
    GenomeComplexity complexity,
    const ObjectiveConfig& objective_config,
    const EnvironmentConfig& environment_config);

} // namespace neuroevo
