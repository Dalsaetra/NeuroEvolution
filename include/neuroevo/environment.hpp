#pragma once

#include "neuroevo/brain.hpp"
#include "neuroevo/fitness.hpp"
#include "neuroevo/random.hpp"
#include "neuroevo/sensorimotor.hpp"
#include "neuroevo/task.hpp"
#include "neuroevo/vector2.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace neuroevo {

struct EnvironmentConfig {
    SensorimotorRegimeKind sensorimotor_regime = SensorimotorRegimeKind::DirectionalFov;
    TaskConfig task;
    FitnessConfig fitness;
    double width = 1.0;
    double height = 1.0;
    double target_radius = 0.075;
    double min_target_distance = 0.25;
    double max_speed = 0.80;
    double max_turn_rate = 3.14159265358979323846;
    double motor_gain = 8.0;
    double fov_degrees = 120.0;
    double initial_heading_fov_fraction = 0.90;
    bool clock_input_enabled = false;
    double clock_input_value = 1.0;
    bool episode_start_input_enabled = true;
    std::size_t episode_start_pulse_brain_steps = 2;
    double env_dt = 0.08;
    std::size_t episode_steps = 600;
    std::size_t brain_steps_per_env_step = 4;
    double food_reward = 10.0;
    double spike_penalty = 0.00005;
    double synapse_penalty = 0.002;
    double neuron_penalty = 0.001;
    double turn_penalty = 0.0001;
    double inactivity_penalty = 0.0005;
    double turn_budget_per_step = 0.25;
    double inactivity_budget_per_step = 0.50;
    double spike_budget_per_neuron_per_brain_step = 0.02;
    double synapse_budget = 64.0;
    double neuron_budget = 64.0;
};

struct TrajectoryPoint {
    std::size_t step = 0;
    Vec2 position;
    Vec2 target;
    double distance = 0.0;
    double motor_x = 0.0;
    double motor_y = 0.0;
    double heading = 0.0;
    double speed_command = 0.0;
    double turn_command = 0.0;
    bool target_visible = true;
    bool target_sensory_available = true;
    double target_bearing = 0.0;
    std::string task_phase;
    std::size_t cumulative_spikes = 0;
    std::size_t foods_collected = 0;
};

struct BrainActivityPoint {
    std::size_t step = 0;
    std::size_t neuron_index = 0;
    std::string neuron_type;
    Vec2 position;
    double bias = 0.0;
    double potential = 0.0;
    double threshold = 1.0;
    double background_sensitivity = 0.0;
    double activation = 0.0;
    bool spiked = false;
};

struct BrainSynapsePoint {
    std::size_t synapse_index = 0;
    std::size_t pre = 0;
    std::size_t post = 0;
    double weight = 0.0;
    std::size_t delay_steps = 0;
};

struct SynapseEventPoint {
    std::size_t step = 0;
    std::size_t synapse_index = 0;
};

struct EvaluationResult {
    double fitness = 0.0;
    double reward = 0.0;
    double penalty = 0.0;
    double spikes = 0.0;
    double foods_collected = 0.0;
    double occluded_foods_collected = 0.0;
    std::vector<TrajectoryPoint> trajectory;
    std::vector<BrainActivityPoint> brain_activity;
    std::vector<BrainSynapsePoint> brain_synapses;
    std::vector<SynapseEventPoint> synapse_events;
};

class Environment {
public:
    explicit Environment(EnvironmentConfig config = {});

    EvaluationResult evaluate(const Brain& genome, Random& rng, bool record_trajectory = false) const;
    const EnvironmentConfig& config() const noexcept { return config_; }

private:
    EnvironmentConfig config_;
};

void write_trajectory_csv(const std::string& path, const std::vector<TrajectoryPoint>& trajectory);
void write_brain_activity_csv(const std::string& path, const std::vector<BrainActivityPoint>& activity);
void write_brain_synapses_csv(const std::string& path, const std::vector<BrainSynapsePoint>& synapses);
void write_synapse_events_csv(const std::string& path, const std::vector<SynapseEventPoint>& events);

} // namespace neuroevo
