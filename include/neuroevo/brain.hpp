#pragma once

#include "neuroevo/random.hpp"
#include "neuroevo/vector2.hpp"

#include <cstddef>
#include <vector>

namespace neuroevo {

struct BrainConfig {
    std::size_t input_count = 5;
    std::size_t hidden_count = 16;
    std::size_t output_count = 4;
    double dt = 0.02;
    double membrane_tau = 0.10;
    double threshold = 1.0;
    double reset_potential = 0.0;
    double refractory_time = 0.04;
    double input_gain = 25.0;
    double synaptic_gain = 8.0;
    bool seed_input_output_synapses = true;
    double seed_input_output_weight = 3.0;
    bool has_clock_input = false;
    std::size_t clock_input_index = 0;
    double motor_trace_decay = 0.82;
    double conduction_speed = 1.5;
    double initial_connection_probability = 0.25;
    std::size_t max_delay_steps = 24;
};

struct MutationConfig {
    double weight_sigma = 0.20;
    double bias_sigma = 0.35;
    double threshold_sigma = 0.03;
    double position_sigma = 0.03;
    double hidden_bias_min = -15.0;
    double hidden_bias_max = 15.0;
    double hidden_bias_jump_min_magnitude = 8.0;
    double hidden_bias_jump_probability = 0.08;
    double add_synapse_probability = 0.24;
    double remove_synapse_probability = 0.04;
    double mutate_weight_probability = 0.12;
    double mutate_neuron_probability = 0.08;
    double mutate_clock_threshold_probability = 0.08;
    double clock_threshold_sigma = 0.08;
    double clock_threshold_min = 0.2;
    double clock_threshold_max = 5.0;
};

struct BrainStepResult {
    std::vector<double> motor_outputs;
    std::size_t spikes = 0;
};

struct BrainStats {
    std::size_t neuron_count = 0;
    std::size_t synapse_count = 0;
};

class Brain {
public:
    struct Neuron {
        Vec2 position;
        double potential = 0.0;
        double bias = 0.0;
        double threshold = 1.0;
        double refractory_remaining = 0.0;
        bool spiked = false;
    };

    struct Synapse {
        std::size_t pre = 0;
        std::size_t post = 0;
        double weight = 0.0;
        std::size_t delay_steps = 1;
    };

    Brain() = default;
    explicit Brain(BrainConfig config);

    static Brain random(BrainConfig config, Random& rng);
    static Brain from_components(
        BrainConfig config,
        std::vector<Neuron> neurons,
        std::vector<Synapse> synapses);

    void reset_state();
    BrainStepResult step(const std::vector<double>& inputs);
    void mutate(const MutationConfig& config, Random& rng);

    const BrainConfig& config() const noexcept { return config_; }
    const std::vector<Neuron>& neurons() const noexcept { return neurons_; }
    const std::vector<Synapse>& synapses() const noexcept { return synapses_; }
    BrainStats stats() const noexcept;

private:
    BrainConfig config_;
    std::vector<Neuron> neurons_;
    std::vector<Synapse> synapses_;
    std::vector<std::vector<std::size_t>> outgoing_;
    std::vector<std::vector<double>> current_buffers_;
    std::vector<double> motor_traces_;
    std::size_t buffer_cursor_ = 0;

    std::size_t first_hidden_index() const noexcept { return config_.input_count; }
    std::size_t first_output_index() const noexcept { return config_.input_count + config_.hidden_count; }
    std::size_t total_neurons() const noexcept { return config_.input_count + config_.hidden_count + config_.output_count; }

    bool is_input(std::size_t index) const noexcept;
    bool is_output(std::size_t index) const noexcept;
    bool synapse_exists(std::size_t pre, std::size_t post) const noexcept;
    std::size_t compute_delay_steps(Vec2 pre, Vec2 post) const noexcept;
    void rebuild_runtime_state();
    void add_random_synapse(Random& rng);
    void ensure_io_connectivity(Random& rng);
};

} // namespace neuroevo
