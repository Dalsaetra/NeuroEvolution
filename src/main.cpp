#include "neuroevo/evolution.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::size_t parse_size_arg(const char* value, const std::string& name)
{
    try {
        return static_cast<std::size_t>(std::stoull(value));
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid value for " + name + ": " + value);
    }
}

std::uint64_t parse_seed_arg(const char* value, const std::string& name)
{
    try {
        return static_cast<std::uint64_t>(std::stoull(value));
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid value for " + name + ": " + value);
    }
}

double parse_double_arg(const char* value, const std::string& name)
{
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid value for " + name + ": " + value);
    }
}

void print_help(const char* executable)
{
    std::cout
        << "Usage: " << executable << " [options]\n\n"
        << "Options:\n"
        << "  --ea-mode X       Evolution mode: scalar or neat-nsga2 (default: scalar)\n"
        << "  --generations N   Number of generations to evolve (default: 40)\n"
        << "  --population N    Population size (default: 64)\n"
        << "  --steps N         Episode steps per trial (default: 600)\n"
        << "  --trials N        Trials per genome (default: 3)\n"
        << "  --record-trials N Replay trials used to choose best_trajectory.csv (default: 16)\n"
        << "  --record-pareto-front N  NEAT Pareto-front solutions to replay and save (default: 6, 0 disables)\n"
        << "  --record-pareto-trials N Replay trials per saved Pareto solution (default: 4)\n"
        << "  --sensorimotor X  Sensorimotor regime: directional-fov or target-vector (default: directional-fov)\n"
        << "  --fov-degrees N   Directional FOV width in degrees (default: 120)\n"
        << "  --initial-heading-fov-frac N  Initial target-bearing range as fraction of half-FOV (default: 0.9)\n"
        << "  --clock-input N   Add tonic clock input neuron: 1 enabled, 0 disabled (default: 1)\n"
        << "  --clock-input-value N  Tonic clock input value clamped to [0, 1] (default: 1)\n"
        << "  --turn-rate N     Max turn rate in radians per simulated second (default: pi)\n"
        << "  --turn-penalty N  Fitness penalty per excess turning intensity (default: 0.0001)\n"
        << "  --inactivity-penalty N  Fitness penalty per step for not walking (default: 0.0005)\n"
        << "  --turn-budget N  Average absolute turn command per step before penalty (default: 0.25)\n"
        << "  --inactivity-budget N  Average stillness per step before penalty (default: 0.5)\n"
        << "  --spike-budget-rate N  Spikes per neuron per brain step before spike penalty (default: 0.02)\n"
        << "  --structural-synapse-budget N  Enabled synapses before scalar structural penalty (default: 64)\n"
        << "  --structural-neuron-budget N  Neurons before scalar structural penalty (default: 64)\n"
        << "  --hidden-bias-jump N  Probability that a hidden-neuron mutation jumps bias to large magnitude (default: 0.08)\n"
        << "  --hidden-bias-jump-min N  Minimum absolute bias for hidden jump mutations (default: 8)\n"
        << "  --mutate-clock-threshold-prob N  Scalar-mode clock input threshold mutation probability (default: 0.08)\n"
        << "  --clock-threshold-sigma N  Clock input threshold mutation sigma (default: 0.08)\n"
        << "  --clock-threshold-min N  Minimum clock input threshold (default: 0.2)\n"
        << "  --clock-threshold-max N  Maximum clock input threshold (default: 5)\n"
        << "  --synaptic-gain N  Gain applied to delivered synaptic spike current (default: 8)\n"
        << "  --seed-io-weight N  Initial direct sensory-to-motor synapse weight (default: 3)\n"
        << "  --distance-reward N  Reward scale for each new closest approach to the current food (default: 8)\n"
        << "  --visibility-reward N  Speed-gated per-step reward scale for keeping visible food centered (default: 0.001)\n"
        << "  --compat-threshold N  NEAT species compatibility threshold (default: 0.15)\n"
        << "  --species-c1 N    NEAT excess-gene compatibility coefficient (default: 1)\n"
        << "  --species-c2 N    NEAT disjoint-gene compatibility coefficient (default: 1)\n"
        << "  --species-c3 N    NEAT weight-difference compatibility coefficient (default: 0.4)\n"
        << "  --mutate-weight-prob N  NEAT per-connection weight mutation probability (default: 0.8)\n"
        << "  --mutate-add-node-prob N  NEAT add-node mutation probability (default: 0.03)\n"
        << "  --mutate-add-conn-prob N  NEAT add-connection mutation probability (default: 0.08)\n"
        << "  --mutate-enable-disable-prob N  NEAT connection toggle mutation probability (default: 0.02)\n"
        << "  --interspecies-mate-prob N  NEAT probability of mating outside species (default: 0.02)\n"
        << "  --target-foods N  Target foods per trial for task-score normalization (default: 3)\n"
        << "  --target-spike-rate N  Target mean spikes per neuron per brain step (default: 0.02)\n"
        << "  --synapse-budget N  Synapse-count normalization budget (default: 64)\n"
        << "  --neuron-budget N  Neuron-count normalization budget (default: 64)\n"
        << "  --objective-set X  Objective set: basic or extended (default: basic)\n"
        << "  --seed N          Random seed (default: 7)\n"
        << "  --out DIR         Output directory for stats.csv and best_trajectory.csv (default: runs/latest)\n"
        << "  --help            Show this help text\n";
}

} // namespace

int main(int argc, char** argv)
{
    neuroevo::EvolutionConfig config;
    std::string output_dir = "runs/latest";

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                print_help(argv[0]);
                return 0;
            }

            if (i + 1 >= argc) {
                throw std::invalid_argument("Missing value after " + arg);
            }

            const char* value = argv[++i];
            if (arg == "--ea-mode") {
                config.ea_mode = neuroevo::parse_ea_mode(value);
            } else if (arg == "--generations") {
                config.generations = parse_size_arg(value, arg);
            } else if (arg == "--population") {
                config.population_size = parse_size_arg(value, arg);
            } else if (arg == "--steps") {
                config.environment.episode_steps = parse_size_arg(value, arg);
            } else if (arg == "--trials") {
                config.trials_per_genome = parse_size_arg(value, arg);
            } else if (arg == "--record-trials") {
                config.recorded_trajectory_trials = parse_size_arg(value, arg);
            } else if (arg == "--record-pareto-front") {
                config.neat.recorded_pareto_front_count = parse_size_arg(value, arg);
            } else if (arg == "--record-pareto-trials") {
                config.neat.recorded_pareto_front_trials = parse_size_arg(value, arg);
            } else if (arg == "--sensorimotor") {
                config.environment.sensorimotor_regime = neuroevo::parse_sensorimotor_regime(value);
            } else if (arg == "--fov-degrees") {
                config.environment.fov_degrees = parse_double_arg(value, arg);
            } else if (arg == "--initial-heading-fov-frac") {
                config.environment.initial_heading_fov_fraction = parse_double_arg(value, arg);
            } else if (arg == "--clock-input") {
                config.environment.clock_input_enabled = parse_size_arg(value, arg) != 0;
            } else if (arg == "--clock-input-value") {
                config.environment.clock_input_value = parse_double_arg(value, arg);
            } else if (arg == "--turn-rate") {
                config.environment.max_turn_rate = parse_double_arg(value, arg);
            } else if (arg == "--turn-penalty") {
                config.environment.turn_penalty = parse_double_arg(value, arg);
            } else if (arg == "--inactivity-penalty") {
                config.environment.inactivity_penalty = parse_double_arg(value, arg);
            } else if (arg == "--turn-budget") {
                config.environment.turn_budget_per_step = parse_double_arg(value, arg);
            } else if (arg == "--inactivity-budget") {
                config.environment.inactivity_budget_per_step = parse_double_arg(value, arg);
            } else if (arg == "--spike-budget-rate") {
                config.environment.spike_budget_per_neuron_per_brain_step = parse_double_arg(value, arg);
            } else if (arg == "--structural-synapse-budget") {
                config.environment.synapse_budget = parse_double_arg(value, arg);
            } else if (arg == "--structural-neuron-budget") {
                config.environment.neuron_budget = parse_double_arg(value, arg);
            } else if (arg == "--hidden-bias-jump") {
                config.mutation.hidden_bias_jump_probability = parse_double_arg(value, arg);
                config.neat.mutation.hidden_bias_jump_probability = config.mutation.hidden_bias_jump_probability;
            } else if (arg == "--hidden-bias-jump-min") {
                config.mutation.hidden_bias_jump_min_magnitude = parse_double_arg(value, arg);
                config.neat.mutation.hidden_bias_jump_min_magnitude = config.mutation.hidden_bias_jump_min_magnitude;
            } else if (arg == "--mutate-clock-threshold-prob") {
                config.mutation.mutate_clock_threshold_probability = parse_double_arg(value, arg);
            } else if (arg == "--clock-threshold-sigma") {
                config.mutation.clock_threshold_sigma = parse_double_arg(value, arg);
                config.neat.mutation.clock_threshold_sigma = config.mutation.clock_threshold_sigma;
            } else if (arg == "--clock-threshold-min") {
                config.mutation.clock_threshold_min = parse_double_arg(value, arg);
                config.neat.mutation.clock_threshold_min = config.mutation.clock_threshold_min;
            } else if (arg == "--clock-threshold-max") {
                config.mutation.clock_threshold_max = parse_double_arg(value, arg);
                config.neat.mutation.clock_threshold_max = config.mutation.clock_threshold_max;
            } else if (arg == "--synaptic-gain") {
                config.brain.synaptic_gain = parse_double_arg(value, arg);
            } else if (arg == "--seed-io-weight") {
                config.brain.seed_input_output_weight = parse_double_arg(value, arg);
            } else if (arg == "--distance-reward") {
                config.environment.distance_improvement_reward_scale = parse_double_arg(value, arg);
            } else if (arg == "--visibility-reward") {
                config.environment.visibility_reward_scale = parse_double_arg(value, arg);
            } else if (arg == "--compat-threshold") {
                config.neat.speciation.compatibility_threshold = parse_double_arg(value, arg);
            } else if (arg == "--species-c1") {
                config.neat.speciation.c1 = parse_double_arg(value, arg);
            } else if (arg == "--species-c2") {
                config.neat.speciation.c2 = parse_double_arg(value, arg);
            } else if (arg == "--species-c3") {
                config.neat.speciation.c3 = parse_double_arg(value, arg);
            } else if (arg == "--mutate-weight-prob") {
                config.neat.mutation.mutate_weight_probability = parse_double_arg(value, arg);
                config.mutation.mutate_weight_probability = config.neat.mutation.mutate_weight_probability;
            } else if (arg == "--mutate-add-node-prob") {
                config.neat.mutation.add_node_probability = parse_double_arg(value, arg);
            } else if (arg == "--mutate-add-conn-prob") {
                config.neat.mutation.add_connection_probability = parse_double_arg(value, arg);
            } else if (arg == "--mutate-enable-disable-prob") {
                config.neat.mutation.enable_disable_probability = parse_double_arg(value, arg);
            } else if (arg == "--interspecies-mate-prob") {
                config.neat.interspecies_mate_probability = parse_double_arg(value, arg);
            } else if (arg == "--target-foods") {
                config.neat.objectives.target_foods_per_trial = parse_double_arg(value, arg);
            } else if (arg == "--target-spike-rate") {
                config.neat.objectives.target_spike_rate = parse_double_arg(value, arg);
            } else if (arg == "--synapse-budget") {
                config.neat.objectives.synapse_budget = parse_double_arg(value, arg);
            } else if (arg == "--neuron-budget") {
                config.neat.objectives.neuron_budget = parse_double_arg(value, arg);
            } else if (arg == "--objective-set") {
                config.neat.objectives.objective_set = neuroevo::parse_objective_set(value);
            } else if (arg == "--seed") {
                config.seed = parse_seed_arg(value, arg);
            } else if (arg == "--out") {
                output_dir = value;
            } else {
                throw std::invalid_argument("Unknown option: " + arg);
            }
        }

        neuroevo::EvolutionRunner runner(config);
        const neuroevo::EvolutionResult result = runner.run(output_dir);

        const auto& final = result.stats.back();
        std::cout << "Finished " << config.generations << " generations\n"
                  << "EA mode: " << neuroevo::to_string(config.ea_mode) << "\n"
                  << "Final-generation selected fitness: " << final.best_fitness << "\n"
                  << "Mean fitness: " << final.mean_fitness << "\n"
                  << "Final-generation selected foods collected: " << final.best_foods_collected << "\n"
                  << "Final-generation best task score: " << final.best_task_score << "\n"
                  << "Final-generation non-dominated genomes: " << final.number_non_dominated << "\n"
                  << "Final-generation species count: " << final.species_count << "\n"
                  << "Best recorded life foods collected: " << result.best_evaluation.foods_collected << "\n"
                  << "Best recorded life fitness: " << result.best_evaluation.fitness << "\n"
                  << "Wrote metadata to: " << (std::filesystem::path(output_dir) / "metadata.csv") << "\n"
                  << "Wrote stats to: " << (std::filesystem::path(output_dir) / "stats.csv") << "\n"
                  << "Wrote trajectory to: " << (std::filesystem::path(output_dir) / "best_trajectory.csv") << "\n"
                  << "Wrote brain activity to: " << (std::filesystem::path(output_dir) / "brain_activity.csv") << "\n"
                  << "Wrote brain synapses to: " << (std::filesystem::path(output_dir) / "brain_synapses.csv") << "\n"
                  << "Wrote synapse events to: " << (std::filesystem::path(output_dir) / "synapse_events.csv") << "\n";
        if (config.ea_mode == neuroevo::EAMode::NeatNsga2 && config.neat.recorded_pareto_front_count > 0) {
            std::cout << "Wrote Pareto front manifest to: " << (std::filesystem::path(output_dir) / "pareto_front.csv") << "\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
