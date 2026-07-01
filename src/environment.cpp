#include "neuroevo/environment.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace neuroevo {

Environment::Environment(EnvironmentConfig config) : config_(config) {}

EvaluationResult Environment::evaluate(const Brain& genome, Random& rng, bool record_trajectory) const
{
    Brain brain = genome;
    brain.reset_state();

    EvaluationResult result;
    Vec2 position = random_position(rng);
    Vec2 target = random_target_away_from(position, rng);
    double previous_distance = length(target - position);
    const double world_diagonal = length({config_.width, config_.height});

    for (std::size_t step = 0; step < config_.episode_steps; ++step) {
        std::vector<double> sensors = sense(position, target);

        std::vector<double> motors(brain.config().output_count, 0.0);
        for (std::size_t substep = 0; substep < config_.brain_steps_per_env_step; ++substep) {
            BrainStepResult step_result = brain.step(sensors);
            result.spikes += step_result.spikes;
            motors = step_result.motor_outputs;
        }

        const double left = motors.size() > 0 ? motors[0] : 0.0;
        const double right = motors.size() > 1 ? motors[1] : 0.0;
        const double down = motors.size() > 2 ? motors[2] : 0.0;
        const double up = motors.size() > 3 ? motors[3] : 0.0;

        Vec2 command{
            std::tanh(config_.motor_gain * (right - left)),
            std::tanh(config_.motor_gain * (up - down)),
        };
        Vec2 velocity = clamp_length(command, 1.0) * config_.max_speed;
        position = clamp_to_bounds(position + velocity * config_.env_dt, config_.width, config_.height);

        const double distance = length(target - position);
        const double progress = previous_distance - distance;
        result.reward += config_.progress_reward_scale * progress;

        double recorded_distance = distance;
        if (distance <= config_.target_radius) {
            result.foods_collected += 1.0;
            result.reward += config_.food_reward;
            target = random_target_away_from(position, rng);
            previous_distance = length(target - position);
            recorded_distance = previous_distance;
        } else {
            previous_distance = distance;
        }

        if (record_trajectory) {
            result.trajectory.push_back({
                step,
                position,
                target,
                recorded_distance,
                velocity.x,
                velocity.y,
                static_cast<std::size_t>(result.spikes),
                static_cast<std::size_t>(result.foods_collected),
            });
        }
    }

    const double final_distance = previous_distance / std::max(0.001, world_diagonal);
    const BrainStats stats = brain.stats();
    result.penalty = config_.final_distance_penalty * final_distance
        + config_.spike_penalty * static_cast<double>(result.spikes)
        + config_.synapse_penalty * static_cast<double>(stats.synapse_count)
        + config_.neuron_penalty * static_cast<double>(stats.neuron_count);
    result.fitness = result.reward - result.penalty;

    return result;
}

Vec2 Environment::random_position(Random& rng) const
{
    return {rng.uniform(0.0, config_.width), rng.uniform(0.0, config_.height)};
}

Vec2 Environment::random_target_away_from(Vec2 position, Random& rng) const
{
    constexpr std::size_t max_attempts = 64;
    for (std::size_t attempt = 0; attempt < max_attempts; ++attempt) {
        Vec2 candidate = random_position(rng);
        if (length(candidate - position) >= config_.min_target_distance) {
            return candidate;
        }
    }
    return random_position(rng);
}

std::vector<double> Environment::sense(Vec2 position, Vec2 target) const
{
    const Vec2 delta = target - position;
    const double distance = length(delta);
    const double normalized_dx = distance > 0.001 ? std::clamp(delta.x / distance, -1.0, 1.0) : 0.0;
    const double normalized_dy = distance > 0.001 ? std::clamp(delta.y / distance, -1.0, 1.0) : 0.0;
    const double normalized_distance = std::clamp(distance / std::max(0.001, length({config_.width, config_.height})), 0.0, 1.0);

    return {
        std::max(0.0, normalized_dx),
        std::max(0.0, -normalized_dx),
        std::max(0.0, normalized_dy),
        std::max(0.0, -normalized_dy),
        normalized_distance,
    };
}

void write_trajectory_csv(const std::string& path, const std::vector<TrajectoryPoint>& trajectory)
{
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Could not open trajectory CSV for writing: " + path);
    }

    output << "step,x,y,target_x,target_y,distance,motor_x,motor_y,cumulative_spikes,foods_collected\n";
    output << std::setprecision(10);
    for (const auto& point : trajectory) {
        output << point.step << ','
               << point.position.x << ','
               << point.position.y << ','
               << point.target.x << ','
               << point.target.y << ','
               << point.distance << ','
               << point.motor_x << ','
               << point.motor_y << ','
               << point.cumulative_spikes << ','
               << point.foods_collected << '\n';
    }
}

} // namespace neuroevo
