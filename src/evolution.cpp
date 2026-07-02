#include "neuroevo/evolution.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace neuroevo {
namespace {

double max_hidden_bias(const Brain& brain)
{
    const auto& config = brain.config();
    const auto& neurons = brain.neurons();
    double max_bias = -std::numeric_limits<double>::infinity();
    for (std::size_t i = config.input_count; i < config.input_count + config.hidden_count; ++i) {
        max_bias = std::max(max_bias, neurons[i].bias);
    }
    return max_bias;
}

bool has_clock_candidate(const Brain& brain)
{
    const auto& config = brain.config();
    const auto& neurons = brain.neurons();
    for (std::size_t i = config.input_count; i < config.input_count + config.hidden_count; ++i) {
        if (neurons[i].bias * config.membrane_tau >= neurons[i].threshold) {
            return true;
        }
    }
    return false;
}

bool better_evaluation(const EvaluationResult& lhs, const EvaluationResult& rhs)
{
    if (lhs.foods_collected != rhs.foods_collected) {
        return lhs.foods_collected > rhs.foods_collected;
    }
    return lhs.fitness > rhs.fitness;
}

} // namespace

EvolutionRunner::EvolutionRunner(EvolutionConfig config)
    : config_(config), rng_(config.seed), environment_(config.environment)
{
    const SensorimotorSpec spec = sensorimotor_spec(config_.environment.sensorimotor_regime);
    config_.brain.input_count = spec.input_count;
    config_.brain.output_count = spec.output_count;

    if (config_.population_size == 0) {
        throw std::invalid_argument("population_size must be greater than zero");
    }
    if (config_.elite_count > config_.population_size) {
        throw std::invalid_argument("elite_count cannot exceed population_size");
    }
    config_.tournament_size = std::max<std::size_t>(1, config_.tournament_size);
    config_.trials_per_genome = std::max<std::size_t>(1, config_.trials_per_genome);
    config_.recorded_trajectory_trials = std::max<std::size_t>(1, config_.recorded_trajectory_trials);
}

EvolutionResult EvolutionRunner::run(const std::string& output_dir)
{
    std::vector<Brain> population;
    population.reserve(config_.population_size);
    for (std::size_t i = 0; i < config_.population_size; ++i) {
        population.push_back(Brain::random(config_.brain, rng_));
    }

    EvolutionResult result;
    EvaluationResult best_evaluation_seen;
    bool has_best_seen = false;

    for (std::size_t generation = 0; generation < config_.generations; ++generation) {
        std::vector<ScoredGenome> scored = evaluate_population(population);
        std::sort(scored.begin(), scored.end(), [](const ScoredGenome& lhs, const ScoredGenome& rhs) {
            return better_evaluation(lhs.evaluation, rhs.evaluation);
        });

        result.stats.push_back(summarize(generation, scored));

        if (!has_best_seen || better_evaluation(scored.front().evaluation, best_evaluation_seen)) {
            best_evaluation_seen = scored.front().evaluation;
            result.best_brain = scored.front().brain;
            result.best_evaluation = scored.front().evaluation;
            has_best_seen = true;
        }

        std::vector<Brain> next_population;
        next_population.reserve(config_.population_size);
        for (std::size_t i = 0; i < config_.elite_count; ++i) {
            next_population.push_back(scored[i].brain);
        }

        while (next_population.size() < config_.population_size) {
            Brain child = scored[select_parent(scored)].brain;
            child.mutate(config_.mutation, rng_);
            next_population.push_back(child);
        }

        population = std::move(next_population);
    }

    EvaluationResult best_recorded_life;
    bool has_recorded_life = false;
    for (std::size_t trial = 0; trial < config_.recorded_trajectory_trials; ++trial) {
        EvaluationResult candidate = environment_.evaluate(result.best_brain, rng_, true);
        const bool better_food = candidate.foods_collected > best_recorded_life.foods_collected;
        const bool tied_food_better_fitness = candidate.foods_collected == best_recorded_life.foods_collected
            && candidate.fitness > best_recorded_life.fitness;
        if (!has_recorded_life || better_food || tied_food_better_fitness) {
            best_recorded_life = std::move(candidate);
            has_recorded_life = true;
        }
    }
    result.best_evaluation = std::move(best_recorded_life);

    if (!output_dir.empty()) {
        std::filesystem::create_directories(output_dir);
        write_run_metadata_csv((std::filesystem::path(output_dir) / "metadata.csv").string(), config_);
        write_generation_stats_csv((std::filesystem::path(output_dir) / "stats.csv").string(), result.stats);
        write_trajectory_csv((std::filesystem::path(output_dir) / "best_trajectory.csv").string(), result.best_evaluation.trajectory);
        write_brain_activity_csv((std::filesystem::path(output_dir) / "brain_activity.csv").string(), result.best_evaluation.brain_activity);
        write_brain_synapses_csv((std::filesystem::path(output_dir) / "brain_synapses.csv").string(), result.best_evaluation.brain_synapses);
        write_synapse_events_csv((std::filesystem::path(output_dir) / "synapse_events.csv").string(), result.best_evaluation.synapse_events);
    }

    return result;
}

std::vector<EvolutionRunner::ScoredGenome> EvolutionRunner::evaluate_population(const std::vector<Brain>& population)
{
    std::vector<ScoredGenome> scored;
    scored.reserve(population.size());

    for (const Brain& brain : population) {
        EvaluationResult aggregate;
        for (std::size_t trial = 0; trial < config_.trials_per_genome; ++trial) {
            EvaluationResult trial_result = environment_.evaluate(brain, rng_, false);
            aggregate.fitness += trial_result.fitness;
            aggregate.reward += trial_result.reward;
            aggregate.penalty += trial_result.penalty;
            aggregate.spikes += trial_result.spikes;
            aggregate.foods_collected += trial_result.foods_collected;
        }

        const double trials = static_cast<double>(config_.trials_per_genome);
        aggregate.fitness /= trials;
        aggregate.reward /= trials;
        aggregate.penalty /= trials;
        aggregate.spikes /= trials;
        aggregate.foods_collected /= trials;
        scored.push_back({brain, aggregate});
    }

    return scored;
}

GenerationStats EvolutionRunner::summarize(std::size_t generation, const std::vector<ScoredGenome>& scored) const
{
    GenerationStats stats;
    stats.generation = generation;
    stats.best_fitness = scored.front().evaluation.fitness;
    stats.best_reward = scored.front().evaluation.reward;
    stats.best_penalty = scored.front().evaluation.penalty;
    stats.best_spikes = scored.front().evaluation.spikes;
    stats.best_synapses = scored.front().brain.stats().synapse_count;
    stats.best_foods_collected = scored.front().evaluation.foods_collected;
    stats.best_max_hidden_bias = max_hidden_bias(scored.front().brain);

    for (const auto& item : scored) {
        stats.mean_fitness += item.evaluation.fitness;
        stats.mean_reward += item.evaluation.reward;
        stats.mean_penalty += item.evaluation.penalty;
        stats.mean_spikes += static_cast<double>(item.evaluation.spikes);
        stats.mean_synapses += static_cast<double>(item.brain.stats().synapse_count);
        stats.mean_foods_collected += static_cast<double>(item.evaluation.foods_collected);
        stats.mean_max_hidden_bias += max_hidden_bias(item.brain);
        if (has_clock_candidate(item.brain)) {
            ++stats.clock_candidate_genomes;
        }
    }

    const double count = static_cast<double>(scored.size());
    stats.mean_fitness /= count;
    stats.mean_reward /= count;
    stats.mean_penalty /= count;
    stats.mean_spikes /= count;
    stats.mean_synapses /= count;
    stats.mean_foods_collected /= count;
    stats.mean_max_hidden_bias /= count;

    return stats;
}

std::size_t EvolutionRunner::select_parent(const std::vector<ScoredGenome>& scored)
{
    std::size_t best_index = rng_.uniform_index(scored.size());
    for (std::size_t i = 1; i < config_.tournament_size; ++i) {
        const std::size_t candidate = rng_.uniform_index(scored.size());
        if (better_evaluation(scored[candidate].evaluation, scored[best_index].evaluation)) {
            best_index = candidate;
        }
    }
    return best_index;
}

void write_run_metadata_csv(const std::string& path, const EvolutionConfig& config)
{
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Could not open run metadata CSV for writing: " + path);
    }

    output << "key,value\n";
    output << std::setprecision(10);
    output << "population_size," << config.population_size << '\n';
    output << "generations," << config.generations << '\n';
    output << "elite_count," << config.elite_count << '\n';
    output << "tournament_size," << config.tournament_size << '\n';
    output << "trials_per_genome," << config.trials_per_genome << '\n';
    output << "recorded_trajectory_trials," << config.recorded_trajectory_trials << '\n';
    output << "seed," << config.seed << '\n';
    output << "sensorimotor_regime," << to_string(config.environment.sensorimotor_regime) << '\n';
    output << "brain_input_count," << config.brain.input_count << '\n';
    output << "brain_hidden_count," << config.brain.hidden_count << '\n';
    output << "brain_output_count," << config.brain.output_count << '\n';
    output << "brain_synaptic_gain," << config.brain.synaptic_gain << '\n';
    output << "brain_seed_input_output_synapses," << (config.brain.seed_input_output_synapses ? 1 : 0) << '\n';
    output << "brain_seed_input_output_weight," << config.brain.seed_input_output_weight << '\n';
    output << "mutation_bias_sigma," << config.mutation.bias_sigma << '\n';
    output << "mutation_hidden_bias_min," << config.mutation.hidden_bias_min << '\n';
    output << "mutation_hidden_bias_max," << config.mutation.hidden_bias_max << '\n';
    output << "mutation_hidden_bias_jump_min_magnitude," << config.mutation.hidden_bias_jump_min_magnitude << '\n';
    output << "mutation_hidden_bias_jump_probability," << config.mutation.hidden_bias_jump_probability << '\n';
    output << "environment_width," << config.environment.width << '\n';
    output << "environment_height," << config.environment.height << '\n';
    output << "environment_target_radius," << config.environment.target_radius << '\n';
    output << "environment_min_target_distance," << config.environment.min_target_distance << '\n';
    output << "environment_max_speed," << config.environment.max_speed << '\n';
    output << "environment_max_turn_rate," << config.environment.max_turn_rate << '\n';
    output << "environment_motor_gain," << config.environment.motor_gain << '\n';
    output << "environment_fov_degrees," << config.environment.fov_degrees << '\n';
    output << "environment_dt," << config.environment.env_dt << '\n';
    output << "environment_episode_steps," << config.environment.episode_steps << '\n';
    output << "environment_brain_steps_per_env_step," << config.environment.brain_steps_per_env_step << '\n';
    output << "environment_food_reward," << config.environment.food_reward << '\n';
    output << "environment_progress_reward_scale," << config.environment.progress_reward_scale << '\n';
    output << "environment_distance_improvement_reward_scale," << config.environment.distance_improvement_reward_scale << '\n';
    output << "environment_visibility_reward_scale," << config.environment.visibility_reward_scale << '\n';
    output << "environment_final_distance_penalty," << config.environment.final_distance_penalty << '\n';
    output << "environment_spike_penalty," << config.environment.spike_penalty << '\n';
    output << "environment_synapse_penalty," << config.environment.synapse_penalty << '\n';
    output << "environment_neuron_penalty," << config.environment.neuron_penalty << '\n';
    output << "environment_turn_penalty," << config.environment.turn_penalty << '\n';
    output << "environment_inactivity_penalty," << config.environment.inactivity_penalty << '\n';
}

void write_generation_stats_csv(const std::string& path, const std::vector<GenerationStats>& stats)
{
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Could not open generation stats CSV for writing: " + path);
    }

    output << "generation,best_fitness,mean_fitness,best_reward,mean_reward,best_penalty,mean_penalty,"
              "best_spikes,mean_spikes,best_synapses,mean_synapses,best_foods_collected,mean_foods_collected,"
              "best_max_hidden_bias,mean_max_hidden_bias,clock_candidate_genomes\n";
    output << std::setprecision(10);
    for (const auto& row : stats) {
        output << row.generation << ','
               << row.best_fitness << ','
               << row.mean_fitness << ','
               << row.best_reward << ','
               << row.mean_reward << ','
               << row.best_penalty << ','
               << row.mean_penalty << ','
               << row.best_spikes << ','
               << row.mean_spikes << ','
               << row.best_synapses << ','
               << row.mean_synapses << ','
               << row.best_foods_collected << ','
               << row.mean_foods_collected << ','
               << row.best_max_hidden_bias << ','
               << row.mean_max_hidden_bias << ','
               << row.clock_candidate_genomes << '\n';
    }
}

} // namespace neuroevo
