#include "neuroevo/evolution.hpp"

#include <cmath>
#include <iostream>

int main()
{
    neuroevo::EvolutionConfig config;
    config.population_size = 8;
    config.generations = 3;
    config.elite_count = 2;
    config.trials_per_genome = 1;
    config.environment.episode_steps = 40;
    config.environment.brain_steps_per_env_step = 2;
    config.environment.min_target_distance = 0.20;
    config.seed = 123;

    neuroevo::EvolutionRunner runner(config);
    const neuroevo::EvolutionResult result = runner.run();

    if (result.stats.size() != config.generations) {
        std::cerr << "Expected one stats row per generation\n";
        return 1;
    }

    if (!std::isfinite(result.stats.back().best_fitness)) {
        std::cerr << "Best fitness is not finite\n";
        return 1;
    }

    if (result.best_brain.stats().neuron_count == 0) {
        std::cerr << "Best brain has no neurons\n";
        return 1;
    }

    return 0;
}
