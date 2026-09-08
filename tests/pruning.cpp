#include "fixtures.hpp"
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
    mutation.rewire_synapse_probability = 0;
    mutation.remove_neuron_probability = 0;
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
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        auto mutation = exact_inheritance();
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

void budget_contract()
{
    BrainConfig config;
    config.input_count = 3; config.hidden_count = 12; config.output_count = 2;
    Random initial(171);
    const auto parent = Brain::random(config, initial);
    for (bool strong : {false, true}) {
        EcosystemConfig ecosystem = neuroevo::controlled_config();
        auto mutation = strong ? detail::strong_mutation(ecosystem.mutation)
            : detail::slight_mutation(ecosystem.mutation);
        require(mutation.add_reciprocal_motif_probability == 0,
            "Budgeted presets must use local edits without unpaired motif growth");
        require(mutation.add_synapse_probability == mutation.remove_synapse_probability
            && mutation.add_neuron_probability == mutation.remove_neuron_probability,
            "Structural add/remove probabilities are asymmetric");
        std::size_t add_edges=0, remove_edges=0, add_nodes=0, remove_nodes=0, weight_only=0, rewires=0;
        Random rng(932);
        for (int trial=0; trial<10000; ++trial) {
            auto child=parent;
            child.mutate(mutation,rng);
            const auto before=parent.config().hidden_count, after=child.config().hidden_count;
            if (after>before) { require(after==before+1,"More than one node added"); ++add_nodes; }
            else if (after<before) { require(after+1==before,"More than one node removed"); ++remove_nodes; }
            else if (child.synapses().size()>parent.synapses().size()) {
                require(child.synapses().size()==parent.synapses().size()+1,"More than one edge added"); ++add_edges;
            } else if (child.synapses().size()<parent.synapses().size()) {
                require(child.synapses().size()+1==parent.synapses().size(),"More than one edge removed"); ++remove_edges;
            } else {
                std::size_t weights=0, neurons=0, routes=0;
                for (std::size_t i=0;i<child.synapses().size();++i) {
                    weights += child.synapses()[i].weight != parent.synapses()[i].weight;
                    routes += child.synapses()[i].pre != parent.synapses()[i].pre
                        || child.synapses()[i].post != parent.synapses()[i].post;
                }
                for (std::size_t i=0;i<child.neurons().size();++i) {
                    const auto& a=parent.neurons()[i]; const auto& b=child.neurons()[i];
                    neurons += a.threshold!=b.threshold || a.bias!=b.bias || a.background_sensitivity!=b.background_sensitivity
                        || a.position.x!=b.position.x || a.position.y!=b.position.y;
                }
                require(weights+neurons <= (strong?4u:2u),"Local edit count exceeded preset limit");
                require(weights==0 || neurons==0,"Budgeted offspring mixed parameter families");
                if (routes) {
                    require(routes==1 && weights==0 && neurons==0,"Rewiring exceeded the structural edit budget");
                    ++rewires;
                }
                weight_only += weights>0 && neurons==0;
            }
        }
        const auto structural=add_edges+remove_edges+add_nodes+remove_nodes+rewires;
        require(structural>(strong?4800u:2800u) && structural<(strong?5200u:3200u),"Wrong structural budget");
        const double structural_weight = mutation.add_synapse_probability + mutation.remove_synapse_probability
            + mutation.add_neuron_probability + mutation.remove_neuron_probability
            + mutation.add_reciprocal_motif_probability + mutation.rewire_synapse_probability;
        const double rewire_probability = mutation.structural_edit_probability
            * mutation.rewire_synapse_probability / structural_weight;
        const double expected_rewires = 10000 * rewire_probability;
        const double tolerance = 5 * std::sqrt(10000 * rewire_probability * (1 - rewire_probability));
        require(std::abs(double(rewires) - expected_rewires) < tolerance,"Wrong rewiring share within structural budget");
        require(weight_only>(strong?3900u:5500u),"Parameter batches did not favor weight edits");
        require(std::abs(double(add_edges)-double(remove_edges))<180
            && std::abs(double(add_nodes)-double(remove_nodes))<100,"Observed structural choices are asymmetric");

        mutation.structural_edit_probability=1;
        mutation.add_synapse_probability=mutation.remove_synapse_probability=0;
        mutation.rewire_synapse_probability=0;
        mutation.add_neuron_probability=mutation.remove_neuron_probability=1;
        mutation.max_hidden_neurons=config.hidden_count;
        std::size_t unchanged=0;
        for (int trial=0;trial<1000;++trial) {
            auto child=parent; child.mutate(mutation,rng);
            unchanged += child.config().hidden_count==config.hidden_count;
        }
        require(unchanged>400 && unchanged<600,"Unavailable growth was redistributed into pruning");
    }
}

void disconnected_contract()
{
    BrainConfig config;
    config.input_count = 1;
    config.hidden_count = 3;
    config.output_count = 1;
    std::vector<Brain::Neuron> neurons(5);
    // Hidden 1 lacks output, hidden 2 lacks input; hidden 3 is connected.
    const auto parent = Brain::from_components(config, neurons, {{0,1,1,1},{2,4,2,1},{0,3,3,1},{3,4,4,1}});
    for (unsigned seed=0; seed<128; ++seed) {
        Random rng(seed);
        auto growth = exact_inheritance(); growth.add_synapse_probability = 1;
        auto child = parent; child.mutate(growth,rng);
        require(child.synapses().size()==5 && child.synapses().back().pre==1 && child.synapses().back().post==2,
            "Growth must repair both disconnected hidden endpoints first");
        require(!child.remove_disconnected_hidden_neuron(rng),"Cleanup removed a connected neuron");
        auto prune = exact_inheritance(); prune.remove_neuron_probability = 1;
        child = parent; child.mutate(prune,rng);
        require(child.config().hidden_count==2 && child.synapses().size()==3,
            "Pruning must prefer a disconnected hidden neuron");
        child = parent;
        require(child.remove_disconnected_hidden_neuron(rng) && child.config().hidden_count==2
            && child.synapses().size()==3,"Cleanup must remove exactly one eligible neuron");
    }
    // A sole isolated neuron cannot connect to itself: repair one side first.
    config.hidden_count = 1;
    const auto isolated = Brain::from_components(config, std::vector<Brain::Neuron>(3), {});
    {
        Random rng(17); auto child = isolated;
        auto growth = exact_inheritance(); growth.add_synapse_probability=1;
        child.mutate(growth,rng); child.mutate(growth,rng);
        require(child.synapses().size()==2 && !child.remove_disconnected_hidden_neuron(rng),
            "Successive growth must repair both sides of an isolated neuron without self-loops");
    }
}

int main()
{
    try {
        pruning_contract();
        disconnected_contract();
        budget_contract();
        std::cout << "pruning contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
