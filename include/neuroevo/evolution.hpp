#pragma once

#include "neuroevo/brain.hpp"
#include "neuroevo/environment.hpp"
#include "neuroevo/neat.hpp"
#include "neuroevo/objectives.hpp"
#include "neuroevo/random.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace neuroevo {

enum class EAMode {
    Scalar,
    NeatNsga2,
};

struct NeatEvolutionConfig {
    NeatMutationConfig mutation;
    SpeciationConfig speciation;
    ObjectiveConfig objectives;
    double interspecies_mate_probability = 0.02;
    std::size_t initial_hidden_count = 0;
    std::size_t recorded_pareto_front_count = 6;
    std::size_t recorded_pareto_front_trials = 4;
};

struct EvolutionConfig {
    EAMode ea_mode = EAMode::Scalar;
    std::size_t population_size = 64;
    std::size_t generations = 40;
    std::size_t elite_count = 4;
    std::size_t tournament_size = 3;
    std::size_t trials_per_genome = 3;
    std::size_t recorded_trajectory_trials = 16;
    std::uint64_t seed = 7;
    BrainConfig brain;
    MutationConfig mutation;
    NeatEvolutionConfig neat;
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
    double mean_occluded_foods_collected = 0.0;
    double best_occluded_foods_collected = 0.0;
    double best_max_hidden_bias = 0.0;
    double mean_max_hidden_bias = 0.0;
    std::size_t clock_candidate_genomes = 0;
    double best_task_score = 0.0;
    double mean_task_score = 0.0;
    std::size_t best_pareto_rank = 0;
    std::size_t number_non_dominated = 0;
    double mean_spike_energy_norm = 0.0;
    double mean_synapse_count_norm = 0.0;
    double mean_time_cost_norm = 0.0;
    double mean_neurons = 0.0;
    double mean_enabled_synapses = 0.0;
    std::size_t species_count = 0;
    std::size_t largest_species_size = 0;
    double mean_crowding_distance = 0.0;
    double best_scalar_display_score = 0.0;
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
    EvolutionResult run_scalar(const std::string& output_dir);
    EvolutionResult run_neat_nsga2(const std::string& output_dir);
    void evaluate_neat_population(std::vector<Genome>& population);
    void assign_nsga2_metadata(std::vector<Genome>& population) const;
    GenerationStats summarize_neat(
        std::size_t generation,
        const std::vector<Genome>& population,
        const std::vector<Species>& species) const;
    void write_neat_pareto_front_recordings(
        const std::string& output_dir,
        const std::vector<Genome>& population);
};

EAMode parse_ea_mode(const std::string& value);
std::string to_string(EAMode mode);

void write_generation_stats_csv(const std::string& path, const std::vector<GenerationStats>& stats);
void write_run_metadata_csv(const std::string& path, const EvolutionConfig& config);

} // namespace neuroevo
