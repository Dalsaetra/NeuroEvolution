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
        << "  --generations N   Number of generations to evolve (default: 40)\n"
        << "  --population N    Population size (default: 64)\n"
        << "  --steps N         Episode steps per trial (default: 600)\n"
        << "  --trials N        Trials per genome (default: 3)\n"
        << "  --record-trials N Replay trials used to choose best_trajectory.csv (default: 16)\n"
        << "  --sensorimotor X  Sensorimotor regime: directional-fov or target-vector (default: directional-fov)\n"
        << "  --fov-degrees N   Directional FOV width in degrees (default: 120)\n"
        << "  --turn-rate N     Max turn rate in radians per simulated second (default: pi)\n"
        << "  --turn-penalty N  Fitness penalty per step for turning intensity (default: 0.001)\n"
        << "  --inactivity-penalty N  Fitness penalty per step for not walking (default: 0.0005)\n"
        << "  --hidden-bias-jump N  Probability that a hidden-neuron mutation jumps bias to large magnitude (default: 0.08)\n"
        << "  --hidden-bias-jump-min N  Minimum absolute bias for hidden jump mutations (default: 8)\n"
        << "  --synaptic-gain N  Gain applied to delivered synaptic spike current (default: 8)\n"
        << "  --seed-io-weight N  Initial direct sensory-to-motor synapse weight (default: 1.6)\n"
        << "  --distance-reward N  Reward scale for each new closest approach to the current food (default: 8)\n"
        << "  --visibility-reward N  Speed-gated per-step reward scale for keeping visible food centered (default: 0.001)\n"
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
            if (arg == "--generations") {
                config.generations = parse_size_arg(value, arg);
            } else if (arg == "--population") {
                config.population_size = parse_size_arg(value, arg);
            } else if (arg == "--steps") {
                config.environment.episode_steps = parse_size_arg(value, arg);
            } else if (arg == "--trials") {
                config.trials_per_genome = parse_size_arg(value, arg);
            } else if (arg == "--record-trials") {
                config.recorded_trajectory_trials = parse_size_arg(value, arg);
            } else if (arg == "--sensorimotor") {
                config.environment.sensorimotor_regime = neuroevo::parse_sensorimotor_regime(value);
            } else if (arg == "--fov-degrees") {
                config.environment.fov_degrees = parse_double_arg(value, arg);
            } else if (arg == "--turn-rate") {
                config.environment.max_turn_rate = parse_double_arg(value, arg);
            } else if (arg == "--turn-penalty") {
                config.environment.turn_penalty = parse_double_arg(value, arg);
            } else if (arg == "--inactivity-penalty") {
                config.environment.inactivity_penalty = parse_double_arg(value, arg);
            } else if (arg == "--hidden-bias-jump") {
                config.mutation.hidden_bias_jump_probability = parse_double_arg(value, arg);
            } else if (arg == "--hidden-bias-jump-min") {
                config.mutation.hidden_bias_jump_min_magnitude = parse_double_arg(value, arg);
            } else if (arg == "--synaptic-gain") {
                config.brain.synaptic_gain = parse_double_arg(value, arg);
            } else if (arg == "--seed-io-weight") {
                config.brain.seed_input_output_weight = parse_double_arg(value, arg);
            } else if (arg == "--distance-reward") {
                config.environment.distance_improvement_reward_scale = parse_double_arg(value, arg);
            } else if (arg == "--visibility-reward") {
                config.environment.visibility_reward_scale = parse_double_arg(value, arg);
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
                  << "Final-generation selected fitness: " << final.best_fitness << "\n"
                  << "Mean fitness: " << final.mean_fitness << "\n"
                  << "Final-generation selected foods collected: " << final.best_foods_collected << "\n"
                  << "Best recorded life foods collected: " << result.best_evaluation.foods_collected << "\n"
                  << "Best recorded life fitness: " << result.best_evaluation.fitness << "\n"
                  << "Wrote metadata to: " << (std::filesystem::path(output_dir) / "metadata.csv") << "\n"
                  << "Wrote stats to: " << (std::filesystem::path(output_dir) / "stats.csv") << "\n"
                  << "Wrote trajectory to: " << (std::filesystem::path(output_dir) / "best_trajectory.csv") << "\n"
                  << "Wrote brain activity to: " << (std::filesystem::path(output_dir) / "brain_activity.csv") << "\n"
                  << "Wrote brain synapses to: " << (std::filesystem::path(output_dir) / "brain_synapses.csv") << "\n"
                  << "Wrote synapse events to: " << (std::filesystem::path(output_dir) / "synapse_events.csv") << "\n";
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
