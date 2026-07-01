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

void print_help(const char* executable)
{
    std::cout
        << "Usage: " << executable << " [options]\n\n"
        << "Options:\n"
        << "  --generations N   Number of generations to evolve (default: 40)\n"
        << "  --population N    Population size (default: 64)\n"
        << "  --steps N         Episode steps per trial (default: 400)\n"
        << "  --trials N        Trials per genome (default: 3)\n"
        << "  --record-trials N Replay trials used to choose best_trajectory.csv (default: 16)\n"
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
                  << "Best fitness: " << final.best_fitness << "\n"
                  << "Mean fitness: " << final.mean_fitness << "\n"
                  << "Best foods collected: " << final.best_foods_collected << "\n"
                  << "Wrote metadata to: " << (std::filesystem::path(output_dir) / "metadata.csv") << "\n"
                  << "Wrote stats to: " << (std::filesystem::path(output_dir) / "stats.csv") << "\n"
                  << "Wrote trajectory to: " << (std::filesystem::path(output_dir) / "best_trajectory.csv") << "\n";
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
