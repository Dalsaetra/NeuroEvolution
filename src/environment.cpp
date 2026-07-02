#include "neuroevo/environment.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace neuroevo {
namespace {

constexpr double pi = 3.14159265358979323846;

} // namespace

Environment::Environment(EnvironmentConfig config) : config_(config) {}

EvaluationResult Environment::evaluate(const Brain& genome, Random& rng, bool record_trajectory) const
{
    Brain brain = genome;
    brain.reset_state();

    EvaluationResult result;
    AgentState agent{random_position(rng), rng.uniform(-pi, pi)};
    Vec2 target = random_target_away_from(agent.position, rng);
    double previous_distance = length(target - agent.position);
    double closest_distance_to_target = previous_distance;
    const double world_diagonal = length({config_.width, config_.height});
    const double fov_radians = config_.fov_degrees * pi / 180.0;
    if (config_.sensorimotor_regime == SensorimotorRegimeKind::DirectionalFov) {
        const Vec2 initial_delta = target - agent.position;
        const double target_heading = std::atan2(initial_delta.y, initial_delta.x);
        agent.heading_radians = normalize_angle(target_heading + rng.uniform(-0.25 * fov_radians, 0.25 * fov_radians));
    }

    if (record_trajectory) {
        const auto& synapses = brain.synapses();
        result.brain_synapses.reserve(synapses.size());
        for (std::size_t i = 0; i < synapses.size(); ++i) {
            const auto& synapse = synapses[i];
            result.brain_synapses.push_back({
                i,
                synapse.pre,
                synapse.post,
                synapse.weight,
                synapse.delay_steps,
            });
        }
    }

    for (std::size_t step = 0; step < config_.episode_steps; ++step) {
        const SensoryState sensory = sense_target(
            config_.sensorimotor_regime,
            agent,
            target,
            world_diagonal,
            fov_radians);

        std::vector<double> motors(brain.config().output_count, 0.0);
        std::vector<bool> neuron_spiked;
        std::vector<bool> synapse_fired;
        if (record_trajectory) {
            neuron_spiked.assign(brain.neurons().size(), false);
            synapse_fired.assign(brain.synapses().size(), false);
        }
        for (std::size_t substep = 0; substep < config_.brain_steps_per_env_step; ++substep) {
            BrainStepResult step_result = brain.step(sensory.inputs);
            result.spikes += step_result.spikes;
            motors = step_result.motor_outputs;
            if (record_trajectory) {
                const auto& neurons = brain.neurons();
                for (std::size_t i = 0; i < neurons.size(); ++i) {
                    neuron_spiked[i] = neuron_spiked[i] || neurons[i].spiked;
                }
                const auto& synapses = brain.synapses();
                for (std::size_t i = 0; i < synapses.size(); ++i) {
                    synapse_fired[i] = synapse_fired[i] || neurons[synapses[i].pre].spiked;
                }
            }
        }

        const MotorCommand command = decode_motor_command(
            config_.sensorimotor_regime,
            motors,
            agent.heading_radians,
            config_.max_speed,
            config_.max_turn_rate,
            config_.motor_gain,
            config_.env_dt);
        agent.heading_radians = command.heading_radians;
        agent.position = clamp_to_bounds(agent.position + command.velocity * config_.env_dt, config_.width, config_.height);

        const double distance = length(target - agent.position);
        const double progress = previous_distance - distance;
        result.reward += config_.progress_reward_scale * progress;
        const double improvement = std::max(0.0, closest_distance_to_target - distance);
        if (improvement > 0.0) {
            result.reward += config_.distance_improvement_reward_scale * (improvement / std::max(0.001, world_diagonal));
            closest_distance_to_target = distance;
        }
        if (sensory.target_visible) {
            const double half_fov = std::max(0.001, fov_radians * 0.5);
            const double centered = 1.0 - std::clamp(std::abs(sensory.target_bearing) / half_fov, 0.0, 1.0);
            const double speed_gate = std::clamp(command.speed_command, 0.0, 1.0);
            result.reward += config_.visibility_reward_scale * centered * speed_gate;
        }
        result.penalty += config_.turn_penalty * std::abs(command.turn_command);
        result.penalty += config_.inactivity_penalty * (1.0 - std::clamp(command.speed_command, 0.0, 1.0));

        double recorded_distance = distance;
        if (distance <= config_.target_radius) {
            result.foods_collected += 1.0;
            result.reward += config_.food_reward;
            target = random_target_away_from(agent.position, rng);
            previous_distance = length(target - agent.position);
            closest_distance_to_target = previous_distance;
            recorded_distance = previous_distance;
        } else {
            previous_distance = distance;
        }

        if (record_trajectory) {
            result.trajectory.push_back({
                step,
                agent.position,
                target,
                recorded_distance,
                command.velocity.x,
                command.velocity.y,
                agent.heading_radians,
                command.speed_command,
                command.turn_command,
                sensory.target_visible,
                sensory.target_bearing,
                static_cast<std::size_t>(result.spikes),
                static_cast<std::size_t>(result.foods_collected),
            });

            for (std::size_t i = 0; i < synapse_fired.size(); ++i) {
                if (synapse_fired[i]) {
                    result.synapse_events.push_back({step, i});
                }
            }

            const auto& neurons = brain.neurons();
            const BrainConfig& brain_config = brain.config();
            for (std::size_t i = 0; i < neurons.size(); ++i) {
                const auto& neuron = neurons[i];
                std::string neuron_type = "output";
                if (i < brain_config.input_count) {
                    neuron_type = "input";
                } else if (i < brain_config.input_count + brain_config.hidden_count) {
                    neuron_type = "hidden";
                }

                const double activation = neuron_spiked[i]
                    ? 1.0
                    : std::clamp(neuron.potential / std::max(0.001, neuron.threshold), 0.0, 1.0);

                result.brain_activity.push_back({
                    step,
                    i,
                    neuron_type,
                    neuron.position,
                    neuron.bias,
                    neuron.potential,
                    neuron.threshold,
                    activation,
                    neuron_spiked[i],
                });
            }
        }
    }

    const double final_distance = previous_distance / std::max(0.001, world_diagonal);
    const BrainStats stats = brain.stats();
    result.penalty = config_.final_distance_penalty * final_distance
        + result.penalty
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

    output << "step,x,y,target_x,target_y,distance,motor_x,motor_y,heading,speed_command,turn_command,"
              "target_visible,target_bearing,cumulative_spikes,foods_collected\n";
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
               << point.heading << ','
               << point.speed_command << ','
               << point.turn_command << ','
               << (point.target_visible ? 1 : 0) << ','
               << point.target_bearing << ','
               << point.cumulative_spikes << ','
               << point.foods_collected << '\n';
    }
}

void write_brain_activity_csv(const std::string& path, const std::vector<BrainActivityPoint>& activity)
{
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Could not open brain activity CSV for writing: " + path);
    }

    output << "step,neuron_index,neuron_type,brain_x,brain_y,bias,potential,threshold,activation,spiked\n";
    output << std::setprecision(10);
    for (const auto& point : activity) {
        output << point.step << ','
               << point.neuron_index << ','
               << point.neuron_type << ','
               << point.position.x << ','
               << point.position.y << ','
               << point.bias << ','
               << point.potential << ','
               << point.threshold << ','
               << point.activation << ','
               << (point.spiked ? 1 : 0) << '\n';
    }
}

void write_brain_synapses_csv(const std::string& path, const std::vector<BrainSynapsePoint>& synapses)
{
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Could not open brain synapses CSV for writing: " + path);
    }

    output << "synapse_index,pre,post,weight,delay_steps\n";
    output << std::setprecision(10);
    for (const auto& synapse : synapses) {
        output << synapse.synapse_index << ','
               << synapse.pre << ','
               << synapse.post << ','
               << synapse.weight << ','
               << synapse.delay_steps << '\n';
    }
}

void write_synapse_events_csv(const std::string& path, const std::vector<SynapseEventPoint>& events)
{
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Could not open synapse events CSV for writing: " + path);
    }

    output << "step,synapse_index\n";
    for (const auto& event : events) {
        output << event.step << ','
               << event.synapse_index << '\n';
    }
}

} // namespace neuroevo
