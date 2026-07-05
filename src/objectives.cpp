#include "neuroevo/objectives.hpp"

#include <algorithm>
#include <stdexcept>

namespace neuroevo {
namespace {

double safe_positive(double value, double fallback)
{
    return value > 0.0 ? value : fallback;
}

} // namespace

ObjectiveSet parse_objective_set(const std::string& value)
{
    if (value == "basic") {
        return ObjectiveSet::Basic;
    }
    if (value == "extended") {
        return ObjectiveSet::Extended;
    }
    throw std::invalid_argument("Unknown objective set: " + value);
}

std::string to_string(ObjectiveSet objective_set)
{
    switch (objective_set) {
    case ObjectiveSet::Basic:
        return "basic";
    case ObjectiveSet::Extended:
        return "extended";
    }
    throw std::invalid_argument("Unknown objective set");
}

std::vector<ObjectiveDescriptor> make_objective_descriptors(ObjectiveSet objective_set)
{
    std::vector<ObjectiveDescriptor> objectives{
        {"task_score_norm", ObjectiveDirection::Maximize},
        {"spike_energy_norm", ObjectiveDirection::Minimize},
        {"synapse_count_norm", ObjectiveDirection::Minimize},
        {"time_cost_norm", ObjectiveDirection::Minimize},
    };
    if (objective_set == ObjectiveSet::Extended) {
        objectives.push_back({"neuron_count_norm", ObjectiveDirection::Minimize});
    }
    return objectives;
}

std::vector<double> make_objective_vector(const EvaluationMetrics& metrics, ObjectiveSet objective_set)
{
    std::vector<double> values{
        metrics.task_score_norm,
        metrics.spike_energy_norm,
        metrics.synapse_count_norm,
        metrics.time_cost_norm,
    };
    if (objective_set == ObjectiveSet::Extended) {
        values.push_back(metrics.neuron_count_norm);
    }
    return values;
}

EvaluationMetrics make_evaluation_metrics(
    const EvaluationResult& evaluation,
    GenomeComplexity complexity,
    const ObjectiveConfig& objective_config,
    const EnvironmentConfig& environment_config)
{
    EvaluationMetrics metrics;
    metrics.raw_fitness = evaluation.fitness;
    metrics.raw_reward = evaluation.reward;
    metrics.raw_penalty = evaluation.penalty;
    metrics.raw_foods_collected = evaluation.foods_collected;
    metrics.raw_spikes = evaluation.spikes;
    metrics.raw_steps_used = static_cast<double>(environment_config.episode_steps);
    metrics.complexity = complexity;

    const double target_foods = safe_positive(objective_config.target_foods_per_trial, 1.0);
    const double food_component = std::clamp(evaluation.foods_collected / target_foods, 0.0, 1.0);
    const double non_food_reward = std::max(0.0, evaluation.reward - evaluation.foods_collected * environment_config.food_reward);
    const double shaping_denominator = safe_positive(environment_config.food_reward * target_foods, 1.0);
    const double shaping_component = std::clamp(non_food_reward / shaping_denominator, 0.0, 1.0);
    metrics.task_score_norm = std::clamp(food_component + 0.1 * shaping_component, 0.0, 1.0);

    const double brain_steps = safe_positive(
        static_cast<double>(environment_config.episode_steps * environment_config.brain_steps_per_env_step),
        1.0);
    const double neuron_count = safe_positive(static_cast<double>(complexity.neuron_count), 1.0);
    metrics.mean_spike_rate = evaluation.spikes / (neuron_count * brain_steps);
    metrics.spike_energy_norm = std::max(
        0.0,
        metrics.mean_spike_rate / safe_positive(objective_config.target_spike_rate, 1.0) - 1.0);

    metrics.synapse_count_norm = std::max(
        0.0,
        static_cast<double>(complexity.enabled_synapses) / safe_positive(objective_config.synapse_budget, 1.0) - 1.0);
    metrics.neuron_count_norm = std::max(
        0.0,
        static_cast<double>(complexity.neuron_count) / safe_positive(objective_config.neuron_budget, 1.0) - 1.0);

    // The current food-seeking environment has no terminal success state, so every
    // trial consumes the full episode. Keeping this explicit makes the objective
    // contract ready for future tasks with completion times.
    metrics.time_cost_norm = 1.0;

    metrics.scalar_display_score = metrics.task_score_norm
        - 0.10 * metrics.spike_energy_norm
        - 0.05 * metrics.synapse_count_norm
        - 0.01 * metrics.time_cost_norm;

    return metrics;
}

} // namespace neuroevo
