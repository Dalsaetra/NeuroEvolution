#include "neuroevo/brain.hpp"
#include "../src/ecosystem_mutation.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace neuroevo;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

MutationConfig exact_inheritance()
{
    MutationConfig mutation;
    mutation.mutate_weight_probability = 0;
    mutation.mutate_neuron_probability = 0;
    mutation.add_synapse_probability = 0;
    mutation.add_neuron_probability = 0;
    mutation.add_reciprocal_motif_probability = 0;
    mutation.remove_synapse_probability = 0;
    mutation.remove_neuron_probability = 0;
    mutation.mutate_clock_threshold_probability = 0;
    mutation.hidden_bias_jump_probability = 0;
    return mutation;
}

bool same_genome(const Brain& a, const Brain& b)
{
    if (a.config().input_count != b.config().input_count
        || a.config().hidden_count != b.config().hidden_count
        || a.config().output_count != b.config().output_count
        || a.synapses().size() != b.synapses().size()
        || a.neurons().size() != b.neurons().size()) return false;
    for (std::size_t i = 0; i < a.neurons().size(); ++i) {
        const auto& x = a.neurons()[i];
        const auto& y = b.neurons()[i];
        if (x.position.x != y.position.x || x.position.y != y.position.y
            || x.bias != y.bias || x.threshold != y.threshold
            || x.background_sensitivity != y.background_sensitivity) return false;
    }
    for (std::size_t i = 0; i < a.synapses().size(); ++i) {
        const auto& x = a.synapses()[i];
        const auto& y = b.synapses()[i];
        if (x.pre != y.pre || x.post != y.post || x.weight != y.weight
            || x.delay_steps != y.delay_steps) return false;
    }
    return true;
}

void pruning_contract()
{
    BrainConfig config;
    config.input_count = 2; config.hidden_count = 3; config.output_count = 2;
    config.background_activity_enabled = false;
    std::vector<Brain::Neuron> neurons(7);
    for (std::size_t i = 0; i < neurons.size(); ++i) neurons[i].threshold = 1 + 0.1 * i;
    std::vector<Brain::Synapse> edges;
    for (std::size_t pre = 0; pre < 7; ++pre)
        for (std::size_t post = 2; post < 7; ++post)
            edges.push_back({pre, post, 0.01 * (1 + pre * 7 + post), 1});
    const auto parent = Brain::from_components(config, neurons, edges);
    auto disabled = parent;
    Random disabled_rng(42);
    disabled.mutate(detail::strong_mutation(exact_inheritance()), disabled_rng);
    require(same_genome(parent, disabled), "Strong mutation re-enabled explicitly disabled operators");
    for (bool stable : {false, true}) for (std::uint64_t seed = 0; seed < 32; ++seed) {
        auto mutation = exact_inheritance(); mutation.stable = stable;
        mutation.remove_neuron_probability = 1;
        auto child = parent;
        Random rng(seed);
        child.step({1, 1}, &rng); // Exercise pruning with runtime activity present.
        child.mutate(mutation, rng);
        require(child.config().hidden_count == 2 && child.neurons().size() == 6,
            "Neuron pruning must remove exactly one hidden neuron");
        std::size_t removed = 2;
        while (removed < 5 && child.neurons()[removed].threshold == neurons[removed].threshold) ++removed;
        require(removed < 5, "Pruning removed an input or output instead of a hidden neuron");
        for (std::size_t i = 0; i < child.neurons().size(); ++i)
            require(child.neurons()[i].threshold == neurons[i + (i >= removed)].threshold,
                "Neuron pruning changed surviving neuron parameters or order");
        std::size_t remaining = 0;
        for (const auto& edge : parent.synapses()) {
            if (edge.pre == removed || edge.post == removed) continue;
            const auto& actual = child.synapses().at(remaining++);
            require(actual.pre == edge.pre - (edge.pre > removed)
                && actual.post == edge.post - (edge.post > removed)
                && actual.weight == edge.weight && actual.delay_steps == edge.delay_steps,
                "Neuron pruning failed to preserve and remap a surviving edge");
        }
        require(remaining == child.synapses().size(), "Neuron pruning retained incident edges");
        for (int i = 0; i < 4; ++i) {
            child.mutate(mutation, rng);
            require(child.step({1, 1}, &rng).motor_outputs.size() == 2, "Pruning broke motor interface");
        }
        require(child.config().hidden_count == 0 && child.neurons().size() == 4,
            "Pruning must stop at zero hidden neurons");
        std::ostringstream saved; child.save_state(saved);
        std::istringstream input(saved.str());
        require(same_genome(child, Brain::load_state(input)), "Pruned brain failed checkpoint roundtrip");
        mutation.remove_neuron_probability = 0; mutation.remove_synapse_probability = 1;
        const auto count = child.synapses().size();
        child.mutate(mutation, rng);
        require(child.synapses().size() + 1 == count, "Synapse pruning must remove one edge");
        for (std::size_t i = 0; i < count + 1; ++i) child.mutate(mutation, rng);
        require(child.synapses().empty(), "Pruning must handle empty synapse lists");
    }
    require(parent.neurons().size() == 7 && parent.synapses().size() == edges.size(),
        "Child pruning changed the parent");
}

}

int main()
{
    try {
        pruning_contract();
        std::cout << "pruning contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
