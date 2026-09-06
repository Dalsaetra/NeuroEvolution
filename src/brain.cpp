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

        if (config_.calibrated_io && is_input(i)) {
            // A phase accumulator preserves weak signals without a DC dead zone
            // or Poisson sampling noise. Threshold remains sensory sensitivity.
            current_buffers_[i][buffer_cursor_] = 0.0;
            neuron.refractory_remaining = 0.0;
            neuron.potential += std::min(1.0, std::clamp(inputs[i], 0.0, 1.0)
                * config_.sensory_rate_hz * config_.dt / std::max(0.2, neuron.threshold));
            if (neuron.potential >= 1.0) {
                neuron.potential -= 1.0;
                neuron.spiked = true;
                ++result.spikes;
            }
            continue;
        }

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
        const double decay = config_.calibrated_io
            ? std::exp(-config_.dt / config_.motor_rate_tau) : config_.motor_trace_decay;
        motor_traces_[i] *= decay;
        if (neurons_[neuron_index].spiked) {
            motor_traces_[i] += config_.calibrated_io ? (1.0 - decay) / config_.dt : 1.0;
        }
        result.motor_outputs[i] = motor_traces_[i]
            / (config_.calibrated_io ? config_.motor_reference_hz : 1.0);
    }

    buffer_cursor_ = (buffer_cursor_ + 1) % current_buffers_.front().size();
    return result;
}

void Brain::mutate(const MutationConfig& config, Random& rng, const InputGroups& input_groups)
{
    if (!input_groups.empty()) {
        std::vector<bool> seen(config_.input_count, false);
        for (const auto& group : input_groups) {
            if (group.empty()) throw std::invalid_argument("Input groups must not be empty");
            for (const auto input : group) {
                if (input >= seen.size() || seen[input])
                    throw std::invalid_argument("Input groups must partition the brain inputs");
                seen[input] = true;
            }
        }
        if (std::find(seen.begin(), seen.end(), false) != seen.end())
            throw std::invalid_argument("Input groups must cover every brain input");
    }
    if (config.stable) {
        mutate_stable(config, rng, input_groups);
        return;
    }
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

    if (config_.hidden_count > 0 && config.remove_neuron_probability > 0
        && rng.chance(config.remove_neuron_probability)) {
        remove_random_neuron(rng);
    }
    if (config_.hidden_count < config.max_hidden_neurons && rng.chance(config.add_neuron_probability)) {
        add_random_neuron(rng);
    }
    if (rng.chance(config.add_synapse_probability)) {
        add_random_synapse(rng, false, input_groups);
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

    // Disconnected inputs and outputs are valid inherited topology. In the
    // ecosystem they allow unused sensory channels to remain dormant until a
    // structural mutation connects them; random initialization is repaired in
    // Brain::random before the first lifetime.
    for (auto& synapse : synapses_) {
        synapse.delay_steps = compute_delay_steps(neurons_[synapse.pre].position, neurons_[synapse.post].position);
    }

    rebuild_runtime_state();
}

void Brain::mutate_stable(const MutationConfig& config, Random& rng, const InputGroups& input_groups)
{
    // One structural operation OR a bounded number of local parameter edits. Probabilities
    // select operators, rather than multiplying the edit count by genome size.
    const double add = config.add_synapse_probability;
    const bool budgeted = config.structural_edit_probability >= 0;
    const double grow = budgeted || config_.hidden_count < config.max_hidden_neurons ? config.add_neuron_probability : 0.0;
    const double motif = config_.hidden_count >= 2 ? config.add_reciprocal_motif_probability : 0.0;
    const double remove = budgeted || !synapses_.empty() ? config.remove_synapse_probability : 0.0;
    const double prune = budgeted || config_.hidden_count > 0 ? config.remove_neuron_probability : 0.0;
    const double structural = add + grow + motif + remove + prune;
    bool moved = false;
    if (structural > 0 && rng.chance(budgeted ? config.structural_edit_probability : std::min(1.0, structural))) {
        const double choice = rng.uniform(0.0, structural);
        if (choice < add) add_random_synapse(rng, true, input_groups);
        else if (choice < add + grow) {
            if (config_.hidden_count < config.max_hidden_neurons) add_random_neuron(rng, true);
        }
        else if (choice < add + grow + motif) add_reciprocal_motif(rng, true);
        else if (choice < add + grow + motif + remove) {
            if (!synapses_.empty())
                synapses_.erase(synapses_.begin() + static_cast<std::ptrdiff_t>(rng.uniform_index(synapses_.size())));
        }
        else remove_random_neuron(rng);
    } else {
        const double weights = synapses_.empty() ? 0.0 : config.mutate_weight_probability * (budgeted ? 4.0 : 1.0);
        const bool sensory_edits = budgeted && !input_groups.empty();
        const double neurons = config_.hidden_count + config_.output_count > 0 || sensory_edits ? config.mutate_neuron_probability : 0.0;
        const double total = weights + neurons;
        // Budgeted offspring choose one parameter family for the entire batch.
        // At ecosystem defaults, roughly 85% of parameter batches edit weights only.
        const bool weight_batch = budgeted && total > 0 && rng.chance(weights / total);
        for (std::size_t edit = 0; edit < config.local_edit_limit && total > 0; ++edit) {
            if (!rng.chance(std::min(1.0, 8.0 * total))) continue;
            if (budgeted ? weight_batch : rng.uniform(0.0, total) < weights) {
                auto& edge = synapses_[rng.uniform_index(synapses_.size())];
                // Small connections should not receive perturbations as large as
                // established strong pathways. A floor permits growth from zero.
                const double sigma = std::min(config.weight_sigma,
                    config.local_weight_limit_multiplier * (0.1 * std::abs(edge.weight) + 0.01));
                edge.weight = std::clamp(edge.weight + rng.normal(0.0, sigma), -6.0, 6.0);
            } else {
                // Sensory threshold is a rate sensitivity in calibrated mode.
                // Select categories first so sector-rich senses do not dominate.
                if (sensory_edits && (config_.hidden_count + config_.output_count == 0 || rng.chance(0.25))) {
                    const auto& group = input_groups[rng.uniform_index(input_groups.size())];
                    auto& neuron = neurons_[group[rng.uniform_index(group.size())]];
                    const double sigma = std::min(config.threshold_sigma, 0.05 * neuron.threshold);
                    neuron.threshold = std::clamp(neuron.threshold + rng.normal(0.0, sigma), 0.2, 5.0);
                    continue;
                }
                const auto i = config_.input_count + rng.uniform_index(config_.hidden_count + config_.output_count);
                auto& neuron = neurons_[i];
                const double property = rng.uniform(0.0, 1.0);
                if (property < 0.4 || (is_output(i) && property >= 0.95)) {
                    neuron.threshold = std::clamp(neuron.threshold + rng.normal(0.0, config.threshold_sigma), 0.2, 3.0);
                    if (!is_output(i)) neuron.bias = clamp_subthreshold_bias(neuron.bias, neuron.threshold,
                        config_.membrane_tau, config_.max_bias_fraction_of_threshold, config.hidden_bias_min);
                } else if (property < 0.7 && !is_output(i)) {
                    neuron.bias = clamp_subthreshold_bias(neuron.bias + rng.normal(0.0, config.bias_sigma),
                        neuron.threshold, config_.membrane_tau, config_.max_bias_fraction_of_threshold, config.hidden_bias_min);
                } else if (property < 0.95 || is_output(i)) {
                    neuron.background_sensitivity = std::clamp(neuron.background_sensitivity
                        + rng.normal(0.0, config.background_sensitivity_sigma),
                        config.background_sensitivity_min, config.background_sensitivity_max);
                } else {
                    // Timing changes are rare and affect just one spatial axis.
                    auto& axis = rng.chance(0.5) ? neuron.position.x : neuron.position.y;
                    axis = std::clamp(axis + rng.normal(0.0, config.position_sigma), 0.05, 0.95);
                    moved = true;
                }
            }
        }
    }
    if (moved) for (auto& edge : synapses_)
        edge.delay_steps = compute_delay_steps(neurons_[edge.pre].position, neurons_[edge.post].position);
    rebuild_runtime_state();
}

void Brain::insert_sensory_input(std::size_t index)
{
    if (index > config_.input_count) throw std::invalid_argument("Invalid sensory insertion index");
    Neuron sensor;
    sensor.position = {0.05, 0.5};
    neurons_.insert(neurons_.begin() + index, sensor);
    current_buffers_.insert(current_buffers_.begin() + index,
        std::vector<double>(config_.max_delay_steps + 1, 0.0));
    ++config_.input_count;
    if (config_.sensory_input_count > 0) ++config_.sensory_input_count;
    if (config_.has_clock_input && config_.clock_input_index >= index) ++config_.clock_input_index;
    if (config_.has_episode_start_input && config_.episode_start_input_index >= index) ++config_.episode_start_input_index;
    outgoing_.assign(total_neurons(), {});
    for (std::size_t i=0;i<synapses_.size();++i) {
        auto& edge=synapses_[i];
        if (edge.pre>=index) ++edge.pre;
        if (edge.post>=index) ++edge.post;
        outgoing_[edge.pre].push_back(i);
    }
}

void Brain::remove_random_neuron(Random& rng)
{
    if (config_.hidden_count == 0) return;
    const auto index = first_hidden_index() + rng.uniform_index(config_.hidden_count);
    synapses_.erase(std::remove_if(synapses_.begin(), synapses_.end(), [index](const Synapse& edge) {
        return edge.pre == index || edge.post == index;
    }), synapses_.end());
    neurons_.erase(neurons_.begin() + static_cast<std::ptrdiff_t>(index));
    --config_.hidden_count;
    for (auto& edge : synapses_) {
        if (edge.pre > index) --edge.pre;
        if (edge.post > index) --edge.post;
    }
    // The caller rebuilds adjacency, delayed-current buffers and motor state.
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

void Brain::add_random_synapse(Random& rng, bool weak, const InputGroups& input_groups)
{
    if (config_.hidden_count + config_.output_count == 0) return;
    // Precompute legal sensory targets so rejection does not overweight groups
    // with more sector/direction variants or more unoccupied edges.
    InputGroups targets(config_.input_count), available_groups;
    if (!input_groups.empty()) {
        std::vector<std::vector<bool>> connected(config_.input_count, std::vector<bool>(total_neurons(), false));
        for (const auto& edge : synapses_) if (is_input(edge.pre)) connected[edge.pre][edge.post] = true;
        for (std::size_t pre = 0; pre < config_.input_count; ++pre)
            for (std::size_t post = config_.input_count; post < total_neurons(); ++post)
                if (!(is_auxiliary_input(config_, pre) && is_output(post)) && !connected[pre][post])
                    targets[pre].push_back(post);
        for (const auto& group : input_groups) {
            std::vector<std::size_t> available;
            for (const auto pre : group) if (!targets[pre].empty()) available.push_back(pre);
            if (!available.empty()) available_groups.push_back(std::move(available));
        }
    }
    constexpr std::size_t max_attempts = 64;
    for (std::size_t attempt = 0; attempt < max_attempts; ++attempt) {
        std::size_t pre = rng.uniform_index(total_neurons());
        std::size_t post;
        if (is_input(pre) && !input_groups.empty()) {
            if (available_groups.empty()) continue;
            const auto& group = available_groups[rng.uniform_index(available_groups.size())];
            pre = group[rng.uniform_index(group.size())];
            post = targets[pre][rng.uniform_index(targets[pre].size())];
        } else post = config_.input_count + rng.uniform_index(total_neurons() - config_.input_count);
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
        if (weak) synapse.weight = std::copysign(std::min(6.0, 0.5 * 32.0 / config_.synaptic_gain), synapse.weight);
        synapse.delay_steps = compute_delay_steps(neurons_[pre].position, neurons_[post].position);
        synapses_.push_back(synapse);
        return;
    }
}

void Brain::add_random_neuron(Random& rng, bool weak)
{
    const std::size_t insertion = first_output_index();
    std::size_t pre = 0;
    std::size_t post = insertion;
    double inherited_weight = random_synapse_weight(rng);

    if (!synapses_.empty()) {
        const auto& selected = synapses_[rng.uniform_index(synapses_.size())];
        pre = selected.pre;
        post = selected.post;
        inherited_weight = selected.weight;
    } else {
        const std::size_t possible_pre = config_.input_count + config_.hidden_count;
        pre = rng.uniform_index(possible_pre);
        post = insertion + rng.uniform_index(config_.output_count);
    }

    const Vec2 midpoint = (neurons_[pre].position + neurons_[post].position) * 0.5;
    Neuron neuron;
    neuron.position = {
        std::clamp(midpoint.x + rng.normal(0.0, 0.04), 0.05, 0.95),
        std::clamp(midpoint.y + rng.normal(0.0, 0.04), 0.05, 0.95),
    };
    neuron.threshold = config_.threshold;
    neuron.background_sensitivity = std::clamp(
        config_.initial_background_sensitivity
            + rng.normal(0.0, config_.initial_background_sensitivity_sigma),
        0.0,
        2.0);
    if (weak) neuron.background_sensitivity = 0.0;

    neurons_.insert(neurons_.begin() + static_cast<std::ptrdiff_t>(insertion), neuron);
    for (auto& synapse : synapses_) {
        if (synapse.pre >= insertion) ++synapse.pre;
        if (synapse.post >= insertion) ++synapse.post;
    }
    if (pre >= insertion) ++pre;
    if (post >= insertion) ++post;
    ++config_.hidden_count;

    // Preserve the selected connection and add a weaker parallel path through
    // the new neuron. This makes node growth useful without erasing the
    // parent's behavior in a single mutation.
    const double drive = std::clamp(std::max(0.75, std::abs(inherited_weight)), 0.75, 6.0);
    const double limit = weak ? std::min(6.0, 0.5 * 32.0 / config_.synaptic_gain) : 6.0;
    const double branch = std::clamp(inherited_weight * 0.5, -limit, limit);
    synapses_.push_back({pre, insertion, drive,
        compute_delay_steps(neurons_[pre].position, neurons_[insertion].position)});
    synapses_.push_back({insertion, post, branch,
        compute_delay_steps(neurons_[insertion].position, neurons_[post].position)});
}

void Brain::add_reciprocal_motif(Random& rng, bool weak)
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
            weak ? std::copysign(std::min(6.0, 0.5 * 32.0 / config_.synaptic_gain), random_synapse_weight(rng)) : random_synapse_weight(rng),
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
