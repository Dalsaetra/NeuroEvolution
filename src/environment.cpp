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

std::vector<double> brain_inputs_with_auxiliary_channels(
    const SensoryState& sensory,
    const BrainConfig& brain_config,
    const EnvironmentConfig& environment_config,
    bool episode_start_active)
{
    std::vector<double> inputs = sensory.inputs;
    if (environment_config.clock_input_enabled) {
        inputs.push_back(std::clamp(environment_config.clock_input_value, 0.0, 1.0));
    }
    if (environment_config.episode_start_input_enabled) {
        inputs.push_back(episode_start_active ? 1.0 : 0.0);
    }

    if (inputs.size() != brain_config.input_count) {
        throw std::invalid_argument("Environment sensory input count does not match BrainConfig::input_count");
    }

    return inputs;
}

} // namespace

Environment::Environment(EnvironmentConfig config) : config_(config) {}

EvaluationResult Environment::evaluate(const Brain& genome, Random& rng, bool record_trajectory) const
{
    Brain brain = genome;
    brain.reset_state();
    Random neural_rng(rng.next_u64());

    EvaluationResult result;
    AgentState agent{{rng.uniform(0.0, config_.width), rng.uniform(0.0, config_.height)}, rng.uniform(-pi, pi)};
    const TaskWorldConfig task_world{
        config_.width,
        config_.height,
        config_.target_radius,
        config_.min_target_distance,
        config_.food_reward,
    };
    std::unique_ptr<TaskScenario> task = make_task_scenario(config_.task, task_world);
    std::unique_ptr<FitnessRegime> fitness = make_fitness_regime(config_.fitness);
    task->reset(agent.position, rng);
    TaskObservation task_observation = task->observe(agent.position, 0);
    Vec2 target = task_observation.target;
    double previous_distance = length(target - agent.position);
    double closest_distance_to_target = previous_distance;
    double accumulated_turn_effort = 0.0;
    double accumulated_stillness = 0.0;
    const double world_diagonal = length({config_.width, config_.height});
    const double fov_radians = config_.fov_degrees * pi / 180.0;
    if (config_.sensorimotor_regime == SensorimotorRegimeKind::DirectionalFov) {
        const Vec2 initial_delta = target - agent.position;
        const double target_heading = std::atan2(initial_delta.y, initial_delta.x);
        const double max_initial_offset = 0.5 * fov_radians * std::clamp(config_.initial_heading_fov_fraction, 0.0, 1.0);
        agent.heading_radians = normalize_angle(target_heading + rng.uniform(-max_initial_offset, max_initial_offset));
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
        task_observation = task->observe(agent.position, step);
        target = task_observation.target;
        const SensoryState sensory = sense_target(
            config_.sensorimotor_regime,
            agent,
            target,
            world_diagonal,
            fov_radians,
            task_observation.sensory_available);

        std::vector<double> motors(brain.config().output_count, 0.0);
        std::vector<bool> neuron_spiked;
        std::vector<bool> synapse_fired;
        if (record_trajectory) {
            neuron_spiked.assign(brain.neurons().size(), false);
            synapse_fired.assign(brain.synapses().size(), false);
        }
        for (std::size_t substep = 0; substep < config_.brain_steps_per_env_step; ++substep) {
            const std::size_t brain_step = step * config_.brain_steps_per_env_step + substep;
            const bool episode_start_active = brain_step < config_.episode_start_pulse_brain_steps;
            const std::vector<double> brain_inputs = brain_inputs_with_auxiliary_channels(
                sensory,
                brain.config(),
                config_,
                episode_start_active);
            BrainStepResult step_result = brain.step(brain_inputs, &neural_rng);
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
        const TaskStepResult task_step = task->advance(agent.position, step, rng);
        const FitnessStepResult fitness_step = fitness->score_step({
            task_step.reward,
            progress,
            distance,
            closest_distance_to_target,
            world_diagonal,
            fov_radians * 0.5,
            sensory,
            command,
        });
        result.reward += fitness_step.reward;
        closest_distance_to_target = fitness_step.closest_distance;
        result.foods_collected += task_step.foods_collected;
        result.occluded_foods_collected += task_step.occluded_foods_collected;
        accumulated_turn_effort += std::abs(command.turn_command);
        accumulated_stillness += 1.0 - std::clamp(command.speed_command, 0.0, 1.0);

        double recorded_distance = distance;
        TaskObservation recorded_observation = task_observation;
        if (task_step.target_changed) {
            recorded_observation = task->observe(agent.position, step + 1);
            target = recorded_observation.target;
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
                recorded_observation.sensory_available,
                sensory.target_bearing,
                to_string(recorded_observation.phase),
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
                    neuron.background_sensitivity,
                    activation,
                    neuron_spiked[i],
                });
            }
        }
    }

    const double final_distance = previous_distance / std::max(0.001, world_diagonal);
    const BrainStats stats = brain.stats();
    const double turn_budget = config_.turn_budget_per_step * static_cast<double>(config_.episode_steps);
    const double inactivity_budget = config_.inactivity_budget_per_step * static_cast<double>(config_.episode_steps);
    const double brain_steps = static_cast<double>(config_.episode_steps * config_.brain_steps_per_env_step);
    const double spike_budget = config_.spike_budget_per_neuron_per_brain_step
        * static_cast<double>(stats.neuron_count)
        * brain_steps;
    const double synapse_excess = std::max(0.0, static_cast<double>(stats.synapse_count) - config_.synapse_budget);
    const double neuron_excess = std::max(0.0, static_cast<double>(stats.neuron_count) - config_.neuron_budget);
    result.penalty = fitness->terminal_penalty(final_distance)
        + config_.turn_penalty * std::max(0.0, accumulated_turn_effort - turn_budget)
        + config_.inactivity_penalty * std::max(0.0, accumulated_stillness - inactivity_budget)
        + config_.spike_penalty * std::max(0.0, static_cast<double>(result.spikes) - spike_budget)
        + config_.synapse_penalty * synapse_excess
        + config_.neuron_penalty * neuron_excess;
    result.fitness = result.reward - result.penalty;

    return result;
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
              "target_visible,target_sensory_available,target_bearing,task_phase,cumulative_spikes,foods_collected\n";
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
               << (point.target_sensory_available ? 1 : 0) << ','
               << point.target_bearing << ','
               << point.task_phase << ','
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

    output << "step,neuron_index,neuron_type,brain_x,brain_y,bias,potential,threshold,background_sensitivity,activation,spiked\n";
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
               << point.background_sensitivity << ','
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
