#pragma once

#include "neuroevo/brain.hpp"
#include "neuroevo/random.hpp"
#include "neuroevo/vector2.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace neuroevo {

struct EnvironmentConfig {
    double width = 1.0;
    double height = 1.0;
    double target_radius = 0.075;
    double min_target_distance = 0.25;
    double max_speed = 0.80;
    double motor_gain = 4.0;
    double env_dt = 0.08;
    std::size_t episode_steps = 600;
    std::size_t brain_steps_per_env_step = 4;
    double food_reward = 10.0;
    double progress_reward_scale = 3.0;
    double final_distance_penalty = 0.5;
    double spike_penalty = 0.0008;
    double synapse_penalty = 0.002;
    double neuron_penalty = 0.001;
};

struct TrajectoryPoint {
    std::size_t step = 0;
    Vec2 position;
    Vec2 target;
    double distance = 0.0;
    double motor_x = 0.0;
    double motor_y = 0.0;
    std::size_t cumulative_spikes = 0;
    std::size_t foods_collected = 0;
};

struct EvaluationResult {
    double fitness = 0.0;
    double reward = 0.0;
    double penalty = 0.0;
    double spikes = 0.0;
    double foods_collected = 0.0;
    std::vector<TrajectoryPoint> trajectory;
};

class Environment {
public:
    explicit Environment(EnvironmentConfig config = {});

    EvaluationResult evaluate(const Brain& genome, Random& rng, bool record_trajectory = false) const;
    const EnvironmentConfig& config() const noexcept { return config_; }

private:
    EnvironmentConfig config_;

    Vec2 random_position(Random& rng) const;
    Vec2 random_target_away_from(Vec2 position, Random& rng) const;
    std::vector<double> sense(Vec2 position, Vec2 target) const;
};

void write_trajectory_csv(const std::string& path, const std::vector<TrajectoryPoint>& trajectory);

} // namespace neuroevo
