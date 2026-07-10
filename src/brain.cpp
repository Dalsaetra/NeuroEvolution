#include "neuroevo/brain.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace neuroevo {
namespace {

bool is_directional_fov_shape(const BrainConfig& config)
{
    const std::size_t auxiliary_count = (config.has_clock_input ? 1 : 0)
        + (config.has_episode_start_input ? 1 : 0);
    const std::size_t sensory_count = config.sensory_input_count > 0
        ? config.sensory_input_count
        : config.input_count - std::min(config.input_count, auxiliary_count);
    return sensory_count == 4 && config.output_count == 3;
}

bool is_clock_input(const BrainConfig& config, std::size_t node_id)
{
    return config.has_clock_input
        && node_id == config.clock_input_index
        && node_id < config.input_count;
}

bool is_episode_start_input(const BrainConfig& config, std::size_t node_id)
{
    return config.has_episode_start_input
        && node_id == config.episode_start_input_index
        && node_id < config.input_count;
}

bool is_auxiliary_input(const BrainConfig& config, std::size_t node_id)
{
    return is_clock_input(config, node_id) || is_episode_start_input(config, node_id);
}

bool is_directional_fov_seed_pair(const Brain& brain, std::size_t pre, std::size_t post)
{
    const std::size_t output = post - brain.config().input_count - brain.config().hidden_count;
    return (pre == 0 && output == 0)
        || (pre == 3 && output == 0)
        || (pre == 1 && output == 1)
        || (pre == 2 && output == 2);
}

double directional_fov_seed_weight(const Brain& brain, std::size_t, std::size_t post)
{
    const std::size_t output = post - brain.config().input_count - brain.config().hidden_count;
    return brain.config().seed_input_output_weight * (output == 1 || output == 2 ? 2.0 : 1.0);
}

double random_synapse_weight(Random& rng)
{
    const double sign = rng.chance(0.82) ? 1.0 : -1.0;
    return sign * std::exp(rng.normal(-0.25, 0.55));
}

} // namespace

double clamp_subthreshold_bias(
    double bias,
    double threshold,
    double membrane_tau,
    double subthreshold_fraction,
    double lower_bound)
{
    const double safe_tau = std::max(0.001, membrane_tau);
    const double safe_fraction = std::clamp(subthreshold_fraction, 0.0, 0.999999);
    const double upper_bound = safe_fraction * std::max(0.2, threshold) / safe_tau;
    return std::clamp(bias, lower_bound, upper_bound);
}

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
            neuron.bias = 0.0;
        } else {
            neuron.position = {rng.uniform(0.25, 0.75), rng.uniform(0.05, 0.95)};
            neuron.bias = rng.normal(0.0, 0.05);
        }
        neuron.threshold = std::max(0.2, config.threshold + rng.normal(0.0, 0.08));
        neuron.background_sensitivity = std::clamp(
            config.initial_background_sensitivity
                + rng.normal(0.0, config.initial_background_sensitivity_sigma),
            0.0,
            2.0);
        if (!brain.is_input(i) && !brain.is_output(i)) {
            neuron.bias = clamp_subthreshold_bias(
                neuron.bias,
                neuron.threshold,
                config.membrane_tau,
                config.max_bias_fraction_of_threshold,
                -15.0);
        }
    }

    const std::size_t total = brain.total_neurons();
    for (std::size_t pre = 0; pre < total; ++pre) {
        if (brain.is_output(pre)) {
            continue;
        }
        for (std::size_t post = config.input_count; post < total; ++post) {
            if (pre == post
                || (is_auxiliary_input(config, pre) && brain.is_output(post))
                || rng.chance(1.0 - config.initial_connection_probability)) {
                continue;
            }
            Brain::Synapse synapse;
            synapse.pre = pre;
            synapse.post = post;
            synapse.weight = random_synapse_weight(rng);
            synapse.delay_steps = brain.compute_delay_steps(brain.neurons_[pre].position, brain.neurons_[post].position);
            brain.synapses_.push_back(synapse);
        }
    }

    if (config.seed_input_output_synapses) {
        const bool structured_directional_seed = is_directional_fov_shape(config);
        for (std::size_t pre = 0; pre < config.input_count; ++pre) {
            if (is_auxiliary_input(config, pre)) {
                continue;
            }
            for (std::size_t post = brain.first_output_index(); post < total; ++post) {
                if (structured_directional_seed && !is_directional_fov_seed_pair(brain, pre, post)) {
                    continue;
                }
                if (brain.synapse_exists(pre, post)) {
                    continue;
                }
                Brain::Synapse synapse;
                synapse.pre = pre;
                synapse.post = post;
                synapse.weight = structured_directional_seed
                    ? directional_fov_seed_weight(brain, pre, post)
                    : config.seed_input_output_weight;
                synapse.delay_steps = brain.compute_delay_steps(brain.neurons_[pre].position, brain.neurons_[post].position);
                brain.synapses_.push_back(synapse);
            }
        }
    }

    brain.ensure_io_connectivity(rng);
    brain.rebuild_runtime_state();
    return brain;
}

Brain Brain::from_components(
    BrainConfig config,
    std::vector<Neuron> neurons,
    std::vector<Synapse> synapses)
{
    Brain brain(config);
    if (neurons.size() != brain.total_neurons()) {
        throw std::invalid_argument("Brain::from_components neuron count does not match BrainConfig");
    }

    brain.neurons_ = std::move(neurons);
    brain.synapses_ = std::move(synapses);
    for (auto& synapse : brain.synapses_) {
        if (synapse.pre >= brain.neurons_.size() || synapse.post >= brain.neurons_.size()) {
            throw std::invalid_argument("Brain::from_components synapse endpoint is out of range");
        }
        synapse.delay_steps = brain.compute_delay_steps(
            brain.neurons_[synapse.pre].position,
            brain.neurons_[synapse.post].position);
    }
    for (std::size_t i = brain.first_hidden_index(); i < brain.first_output_index(); ++i) {
        brain.neurons_[i].bias = clamp_subthreshold_bias(
            brain.neurons_[i].bias,
            brain.neurons_[i].threshold,
            config.membrane_tau,
            config.max_bias_fraction_of_threshold,
            -15.0);
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

BrainStepResult Brain::step(const std::vector<double>& inputs, Random* rng)
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

        if (rng != nullptr && config_.background_activity_enabled) {
            const double event_probability = std::clamp(config_.background_event_rate_hz * config_.dt, 0.0, 1.0);
            if (rng->chance(event_probability)) {
                current += config_.background_event_current * neuron.background_sensitivity;
            }
        }

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
            current_buffers_[synapse.post][target_cursor] += synapse.weight * config_.synaptic_gain;
        }
    }

    for (std::size_t i = 0; i < config_.output_count; ++i) {
        const std::size_t neuron_index = first_output_index() + i;
        motor_traces_[i] *= config_.motor_trace_decay;
        if (neurons_[neuron_index].spiked) {
            motor_traces_[i] += 1.0;
        }
        result.motor_outputs[i] = motor_traces_[i];
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
            if (!is_output(i)) {
                if (rng.chance(config.hidden_bias_jump_probability)) {
                    const double magnitude = rng.uniform(
                        std::min(config.hidden_bias_jump_min_magnitude, config.hidden_bias_max),
                        config.hidden_bias_max);
                    neuron.bias = rng.chance(0.5) ? magnitude : -magnitude;
                } else {
                    neuron.bias += rng.normal(0.0, config.bias_sigma);
                }
            } else {
                neuron.bias = 0.0;
            }
            neuron.threshold = std::clamp(neuron.threshold + rng.normal(0.0, config.threshold_sigma), 0.2, 3.0);
            if (!is_output(i)) {
                neuron.bias = clamp_subthreshold_bias(
                    neuron.bias,
                    neuron.threshold,
                    config_.membrane_tau,
                    config_.max_bias_fraction_of_threshold,
                    config.hidden_bias_min);
                neuron.position.x = std::clamp(neuron.position.x + rng.normal(0.0, config.position_sigma), 0.05, 0.95);
                neuron.position.y = std::clamp(neuron.position.y + rng.normal(0.0, config.position_sigma), 0.05, 0.95);
            }
        }
    }

    for (auto& neuron : neurons_) {
        if (rng.chance(config.mutate_neuron_probability)) {
            neuron.background_sensitivity = std::clamp(
                neuron.background_sensitivity + rng.normal(0.0, config.background_sensitivity_sigma),
                config.background_sensitivity_min,
                config.background_sensitivity_max);
        }
    }

    if (!synapses_.empty() && rng.chance(config.remove_synapse_probability)) {
        const std::size_t index = rng.uniform_index(synapses_.size());
        synapses_.erase(synapses_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    if (rng.chance(config.add_synapse_probability)) {
        add_random_synapse(rng);
    }
    if (rng.chance(config.add_reciprocal_motif_probability)) {
        add_reciprocal_motif(rng);
    }

    if (config_.has_clock_input
        && config_.clock_input_index < config_.input_count
        && rng.chance(config.mutate_clock_threshold_probability)) {
        auto& clock = neurons_[config_.clock_input_index];
        clock.threshold = std::clamp(
            clock.threshold + rng.normal(0.0, config.clock_threshold_sigma),
            config.clock_threshold_min,
            config.clock_threshold_max);
    }

    ensure_io_connectivity(rng);

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
        if (pre == post
            || is_output(pre)
            || (is_auxiliary_input(config_, pre) && is_output(post))
            || synapse_exists(pre, post)) {
            continue;
        }

        Synapse synapse;
        synapse.pre = pre;
        synapse.post = post;
        synapse.weight = random_synapse_weight(rng);
        synapse.delay_steps = compute_delay_steps(neurons_[pre].position, neurons_[post].position);
        synapses_.push_back(synapse);
        return;
    }
}

void Brain::add_reciprocal_motif(Random& rng)
{
    if (config_.hidden_count < 2) {
        return;
    }

    const std::size_t first = first_hidden_index() + rng.uniform_index(config_.hidden_count);
    std::size_t second = first;
    for (std::size_t attempt = 0; attempt < 8 && second == first; ++attempt) {
        second = first_hidden_index() + rng.uniform_index(config_.hidden_count);
    }
    if (first == second) {
        return;
    }

    auto add_if_missing = [&](std::size_t pre, std::size_t post) {
        if (synapse_exists(pre, post)) {
            return;
        }
        synapses_.push_back({
            pre,
            post,
            random_synapse_weight(rng),
            compute_delay_steps(neurons_[pre].position, neurons_[post].position),
        });
    };
    add_if_missing(first, second);
    add_if_missing(second, first);
}

void Brain::ensure_io_connectivity(Random& rng)
{
    auto add_synapse = [&](std::size_t pre, std::size_t post) {
        if (pre == post || pre >= total_neurons() || post >= total_neurons() || synapse_exists(pre, post)) {
            return;
        }

        Synapse synapse;
        synapse.pre = pre;
        synapse.post = post;
        synapse.weight = random_synapse_weight(rng);
        if (is_directional_fov_shape(config_) && is_directional_fov_seed_pair(*this, pre, post)) {
            synapse.weight = directional_fov_seed_weight(*this, pre, post);
        }
        synapse.delay_steps = compute_delay_steps(neurons_[pre].position, neurons_[post].position);
        synapses_.push_back(synapse);
    };

    for (std::size_t input = 0; input < config_.input_count; ++input) {
        if (is_auxiliary_input(config_, input)) {
            continue;
        }
        const bool has_outgoing = std::any_of(synapses_.begin(), synapses_.end(), [&](const Synapse& synapse) {
            return synapse.pre == input;
        });
        if (has_outgoing) {
            continue;
        }

        const std::size_t output = first_output_index() + rng.uniform_index(config_.output_count);
        add_synapse(input, output);
    }

    for (std::size_t output = first_output_index(); output < total_neurons(); ++output) {
        const bool has_incoming = std::any_of(synapses_.begin(), synapses_.end(), [&](const Synapse& synapse) {
            return synapse.post == output;
        });
        if (has_incoming) {
            continue;
        }

        std::vector<std::size_t> sensory_inputs;
        sensory_inputs.reserve(config_.input_count);
        for (std::size_t input = 0; input < config_.input_count; ++input) {
            if (!is_auxiliary_input(config_, input)) {
                sensory_inputs.push_back(input);
            }
        }
        if (sensory_inputs.empty()) {
            continue;
        }
        const std::size_t input = sensory_inputs[rng.uniform_index(sensory_inputs.size())];
        add_synapse(input, output);
    }
}

} // namespace neuroevo
