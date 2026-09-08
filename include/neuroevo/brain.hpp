#pragma once

#include "neuroevo/config.hpp"
#include "neuroevo/random.hpp"
#include "neuroevo/vector2.hpp"

#include <cstddef>
#include <iosfwd>
#include <vector>

namespace neuroevo {

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
    using InputGroups = std::vector<std::vector<std::size_t>>;
    struct Neuron {
        Vec2 position;
        double potential = 0.0;
        double bias = 0.0;
        double threshold = 1.0;
        double background_sensitivity = 0.0;
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
    BrainStepResult step(const std::vector<double>& inputs, Random* rng = nullptr);
    // Optional partition of all inputs for category-balanced connection growth.
    void mutate(const MutationConfig& config, Random& rng, const InputGroups& input_groups = {});
    // Remove at most one hidden neuron missing an incoming or outgoing edge.
    bool remove_disconnected_hidden_neuron(Random& rng);

    const BrainConfig& config() const noexcept { return config_; }
    const std::vector<Neuron>& neurons() const noexcept { return neurons_; }
    const std::vector<Synapse>& synapses() const noexcept { return synapses_; }
    BrainStats stats() const noexcept;

    // Includes delayed currents, refractory periods, and motor traces, not just a genome.
    void save_state(std::ostream& stream) const;
    static Brain load_state(std::istream& stream);

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
    void add_random_synapse(Random& rng, bool weak = false, const InputGroups& input_groups = {});
    void rewire_random_synapse(Random& rng, const InputGroups& input_groups);
    void add_random_neuron(Random& rng, bool weak = false);
    void remove_random_neuron(Random& rng);
    std::vector<std::size_t> disconnected_hidden_neurons() const;
    void remove_hidden_neuron(std::size_t index);
    void add_reciprocal_motif(Random& rng, bool weak = false);
    void ensure_io_connectivity(Random& rng);
};

double clamp_subthreshold_bias(
    double bias,
    double threshold,
    double membrane_tau,
    double subthreshold_fraction,
    double lower_bound);

} // namespace neuroevo
