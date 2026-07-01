#pragma once

#include "neuroevo/brain.hpp"
#include "neuroevo/environment.hpp"
#include "neuroevo/random.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace neuroevo {

struct EvolutionConfig {
    std::size_t population_size = 64;
    std::size_t generations = 40;
    std::size_t elite_count = 4;
    std::size_t tournament_size = 3;
    std::size_t trials_per_genome = 3;
    std::size_t recorded_trajectory_trials = 16;
    std::uint64_t seed = 7;
    BrainConfig brain;
    MutationConfig mutation;
    EnvironmentConfig environment;
};

struct GenerationStats {
    std::size_t generation = 0;
    double best_fitness = 0.0;
    double mean_fitness = 0.0;
    double best_reward = 0.0;
    double mean_reward = 0.0;
    double best_penalty = 0.0;
    double mean_penalty = 0.0;
    double mean_spikes = 0.0;
    double best_spikes = 0.0;
    double mean_synapses = 0.0;
    std::size_t best_synapses = 0;
    double mean_foods_collected = 0.0;
    double best_foods_collected = 0.0;
};

struct EvolutionResult {
    Brain best_brain;
    EvaluationResult best_evaluation;
    std::vector<GenerationStats> stats;
};

class EvolutionRunner {
public:
    explicit EvolutionRunner(EvolutionConfig config);

    EvolutionResult run(const std::string& output_dir = {});
    const EvolutionConfig& config() const noexcept { return config_; }

private:
    struct ScoredGenome {
        Brain brain;
        EvaluationResult evaluation;
    };

    EvolutionConfig config_;
    Random rng_;
    Environment environment_;

    std::vector<ScoredGenome> evaluate_population(const std::vector<Brain>& population);
    GenerationStats summarize(std::size_t generation, const std::vector<ScoredGenome>& scored) const;
    std::size_t select_parent(const std::vector<ScoredGenome>& scored);
};

void write_generation_stats_csv(const std::string& path, const std::vector<GenerationStats>& stats);
void write_run_metadata_csv(const std::string& path, const EvolutionConfig& config);

} // namespace neuroevo
