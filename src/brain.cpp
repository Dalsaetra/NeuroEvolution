#include "neuroevo/brain.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace neuroevo {

Brain::Brain(BrainConfig config) : config_(config)
{
    neurons_.resize(total_neurons());
    rebuild_runtime_state();
}

Brain Brain::random(BrainConfig config, Random& rng)
{
    Brain brain(config);

    for (std::size_t i = 0; i < brain.neurons_.size(); ++i) {
        auto& neuron = brain.neurons_[i];
        if (brain.is_input(i)) {
            const double t = config.input_count <= 1 ? 0.5 : static_cast<double>(i) / static_cast<double>(config.input_count - 1);
            neuron.position = {0.05, 0.1 + 0.8 * t};
            neuron.bias = 0.0;
        } else if (brain.is_output(i)) {
            const std::size_t local = i - brain.first_output_index();
            const double t = config.output_count <= 1 ? 0.5 : static_cast<double>(local) / static_cast<double>(config.output_count - 1);
            neuron.position = {0.95, 0.1 + 0.8 * t};
            neuron.bias = rng.normal(0.0, 0.03);
        } else {
            neuron.position = {rng.uniform(0.25, 0.75), rng.uniform(0.05, 0.95)};
            neuron.bias = rng.normal(0.0, 0.05);
        }
        neuron.threshold = std::max(0.2, config.threshold + rng.normal(0.0, 0.08));
    }

    const std::size_t total = brain.total_neurons();
    for (std::size_t pre = 0; pre < total; ++pre) {
        if (brain.is_output(pre)) {
            continue;
        }
        for (std::size_t post = config.input_count; post < total; ++post) {
            if (pre == post || rng.chance(1.0 - config.initial_connection_probability)) {
                continue;
            }
            const double sign = rng.chance(0.82) ? 1.0 : -1.0;
            const double magnitude = std::exp(rng.normal(-0.25, 0.55));
            Brain::Synapse synapse;
            synapse.pre = pre;
            synapse.post = post;
            synapse.weight = sign * magnitude;
            synapse.delay_steps = brain.compute_delay_steps(brain.neurons_[pre].position, brain.neurons_[post].position);
            brain.synapses_.push_back(synapse);
        }
    }

    brain.rebuild_runtime_state();
    return brain;
}

void Brain::reset_state()
{
    for (auto& neuron : neurons_) {
        neuron.potential = 0.0;
        neuron.refractory_remaining = 0.0;
        neuron.spiked = false;
    }
    for (auto& buffer : current_buffers_) {
        std::fill(buffer.begin(), buffer.end(), 0.0);
    }
    std::fill(motor_traces_.begin(), motor_traces_.end(), 0.0);
    buffer_cursor_ = 0;
}

BrainStepResult Brain::step(const std::vector<double>& inputs)
{
    if (inputs.size() != config_.input_count) {
        throw std::invalid_argument("Brain::step input count does not match BrainConfig::input_count");
    }

    BrainStepResult result;
    result.motor_outputs.assign(config_.output_count, 0.0);

    for (std::size_t i = 0; i < neurons_.size(); ++i) {
        auto& neuron = neurons_[i];
        neuron.spiked = false;

        double current = current_buffers_[i][buffer_cursor_];
        current_buffers_[i][buffer_cursor_] = 0.0;
        current += neuron.bias;

        if (is_input(i)) {
            current += std::clamp(inputs[i], 0.0, 1.0) * config_.input_gain;
        }

        if (neuron.refractory_remaining > 0.0) {
            neuron.refractory_remaining = std::max(0.0, neuron.refractory_remaining - config_.dt);
            neuron.potential = config_.reset_potential;
            continue;
        }

        const double leak = -neuron.potential / config_.membrane_tau;
        neuron.potential += config_.dt * (leak + current);

        if (neuron.potential >= neuron.threshold) {
            neuron.spiked = true;
            neuron.potential = config_.reset_potential;
            neuron.refractory_remaining = config_.refractory_time;
            ++result.spikes;
        }
    }

    for (std::size_t pre = 0; pre < neurons_.size(); ++pre) {
        if (!neurons_[pre].spiked) {
            continue;
        }
        for (const std::size_t synapse_index : outgoing_[pre]) {
            const Synapse& synapse = synapses_[synapse_index];
            const std::size_t target_cursor = (buffer_cursor_ + synapse.delay_steps) % current_buffers_[synapse.post].size();
            current_buffers_[synapse.post][target_cursor] += synapse.weight;
        }
    }

    for (std::size_t i = 0; i < config_.output_count; ++i) {
        const std::size_t neuron_index = first_output_index() + i;
        motor_traces_[i] *= config_.motor_trace_decay;
        if (neurons_[neuron_index].spiked) {
            motor_traces_[i] += 1.0;
        }
        const double normalized_potential = std::clamp(
            neurons_[neuron_index].potential / std::max(0.001, neurons_[neuron_index].threshold),
            0.0,
            1.0);
        result.motor_outputs[i] = motor_traces_[i] + 0.5 * normalized_potential;
    }

    buffer_cursor_ = (buffer_cursor_ + 1) % current_buffers_.front().size();
    return result;
}

void Brain::mutate(const MutationConfig& config, Random& rng)
{
    for (auto& synapse : synapses_) {
        if (rng.chance(config.mutate_weight_probability)) {
            synapse.weight += rng.normal(0.0, config.weight_sigma);
            synapse.weight = std::clamp(synapse.weight, -6.0, 6.0);
        }
    }

    for (std::size_t i = config_.input_count; i < neurons_.size(); ++i) {
        auto& neuron = neurons_[i];
        if (rng.chance(config.mutate_neuron_probability)) {
            neuron.bias = std::clamp(neuron.bias + rng.normal(0.0, config.bias_sigma), -2.0, 2.0);
            neuron.threshold = std::clamp(neuron.threshold + rng.normal(0.0, config.threshold_sigma), 0.2, 3.0);
            if (!is_output(i)) {
                neuron.position.x = std::clamp(neuron.position.x + rng.normal(0.0, config.position_sigma), 0.05, 0.95);
                neuron.position.y = std::clamp(neuron.position.y + rng.normal(0.0, config.position_sigma), 0.05, 0.95);
            }
        }
    }

    if (!synapses_.empty() && rng.chance(config.remove_synapse_probability)) {
        const std::size_t index = rng.uniform_index(synapses_.size());
        synapses_.erase(synapses_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    if (rng.chance(config.add_synapse_probability)) {
        add_random_synapse(rng);
    }

    for (auto& synapse : synapses_) {
        synapse.delay_steps = compute_delay_steps(neurons_[synapse.pre].position, neurons_[synapse.post].position);
    }

    rebuild_runtime_state();
}

BrainStats Brain::stats() const noexcept
{
    return {neurons_.size(), synapses_.size()};
}

bool Brain::is_input(std::size_t index) const noexcept
{
    return index < config_.input_count;
}

bool Brain::is_output(std::size_t index) const noexcept
{
    return index >= first_output_index();
}

bool Brain::synapse_exists(std::size_t pre, std::size_t post) const noexcept
{
    return std::any_of(synapses_.begin(), synapses_.end(), [pre, post](const Synapse& synapse) {
        return synapse.pre == pre && synapse.post == post;
    });
}

std::size_t Brain::compute_delay_steps(Vec2 pre, Vec2 post) const noexcept
{
    const double distance = length(post - pre);
    const double delay_seconds = distance / std::max(0.001, config_.conduction_speed);
    const auto steps = static_cast<std::size_t>(std::ceil(delay_seconds / std::max(0.001, config_.dt)));
    return std::clamp<std::size_t>(std::max<std::size_t>(1, steps), 1, config_.max_delay_steps);
}

void Brain::rebuild_runtime_state()
{
    outgoing_.assign(total_neurons(), {});
    for (std::size_t i = 0; i < synapses_.size(); ++i) {
        if (synapses_[i].pre < outgoing_.size()) {
            outgoing_[synapses_[i].pre].push_back(i);
        }
    }

    current_buffers_.assign(total_neurons(), std::vector<double>(config_.max_delay_steps + 1, 0.0));
    motor_traces_.assign(config_.output_count, 0.0);
    buffer_cursor_ = 0;
}

void Brain::add_random_synapse(Random& rng)
{
    constexpr std::size_t max_attempts = 64;
    for (std::size_t attempt = 0; attempt < max_attempts; ++attempt) {
        const std::size_t pre = rng.uniform_index(total_neurons());
        const std::size_t post = config_.input_count + rng.uniform_index(total_neurons() - config_.input_count);
        if (pre == post || is_output(pre) || synapse_exists(pre, post)) {
            continue;
        }

        const double sign = rng.chance(0.82) ? 1.0 : -1.0;
        Synapse synapse;
        synapse.pre = pre;
        synapse.post = post;
        synapse.weight = sign * std::exp(rng.normal(-0.25, 0.55));
        synapse.delay_steps = compute_delay_steps(neurons_[pre].position, neurons_[post].position);
        synapses_.push_back(synapse);
        return;
    }
}

} // namespace neuroevo
