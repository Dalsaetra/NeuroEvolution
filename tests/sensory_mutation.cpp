#include "neuroevo/ecosystem.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace neuroevo;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

MutationConfig growth_only(bool stable)
{
    MutationConfig mutation;
    mutation.stable = stable;
    mutation.mutate_weight_probability = mutation.mutate_neuron_probability = 0;
    mutation.add_neuron_probability = mutation.add_reciprocal_motif_probability = 0;
    mutation.remove_neuron_probability = mutation.remove_synapse_probability = 0;
    mutation.mutate_clock_threshold_probability = 0;
    mutation.add_synapse_probability = 1;
    return mutation;
}

void category_distribution(bool stable, bool extended)
{
    const auto& groups = ecosystem_input_groups(extended);
    BrainConfig config;
    config.input_count = extended ? eco_input_count : eco_legacy_input_count;
    config.hidden_count = 0; config.output_count = 1;
    const Brain parent(config);
    std::vector<std::size_t> membership(config.input_count, groups.size());
    for (std::size_t group = 0; group < groups.size(); ++group)
        for (auto input : groups[group]) {
            require(input < membership.size() && membership[input] == groups.size(), "Invalid sensory partition");
            membership[input] = group;
        }
    require(std::find(membership.begin(), membership.end(), groups.size()) == membership.end(), "Unclassified input");
    const auto labels = ecosystem_input_labels(extended);
    const auto storm = std::find(labels.begin(), labels.end(), "storm_cue") - labels.begin();
    require(groups[membership[storm]].size() == 1, "Storm must be a singleton category");
    if (extended) {
        const auto shelter = membership[eco_shelter_offset];
        require(groups[shelter].size() == eco_sectors, "Shelter sectors must share one category");
        for (std::size_t sector = 0; sector < eco_sectors; ++sector)
            require(membership[eco_shelter_offset + sector] == shelter, "Shelter category split across sectors");
    }
    std::vector<std::size_t> counts(groups.size()), inputs(config.input_count);
    Random rng(1234);
    const auto mutation = growth_only(stable);
    for (std::size_t trial = 0; trial < groups.size() * 1000; ++trial) {
        auto child = parent;
        child.mutate(mutation, rng, groups);
        require(child.synapses().size() == 1, "Growth failed on an empty sensory graph");
        const auto pre = child.synapses()[0].pre;
        ++counts[membership[pre]]; ++inputs[pre];
    }
    for (auto count : counts) require(count > 800 && count < 1200, "Sensory categories are not equally sampled");
    for (std::size_t group = 0; group < groups.size(); ++group)
        for (auto input : groups[group]) {
            const double expected = double(counts[group]) / groups[group].size();
            require(inputs[input] > expected * 0.65 && inputs[input] < expected * 1.35,
                "Sector/direction sampling within a category is biased");
        }
}

void saturated_categories(bool stable)
{
    BrainConfig config;
    config.input_count = 6; config.hidden_count = 0; config.output_count = 1;
    const Brain::InputGroups groups{{0}, {1, 2, 3, 4, 5}};
    std::vector<Brain::Neuron> neurons(7);
    std::vector<Brain::Synapse> edges{{1, 6, 1, 1}, {2, 6, 1, 1}, {3, 6, 1, 1}, {4, 6, 1, 1}};
    const auto parent = Brain::from_components(config, neurons, edges);
    Random rng(5678);
    std::size_t singleton = 0;
    for (int trial = 0; trial < 4000; ++trial) {
        auto child = parent;
        child.mutate(growth_only(stable), rng, groups);
        require(child.synapses().size() == 5, "Partially saturated graph failed growth");
        singleton += child.synapses().back().pre == 0;
        child.mutate(growth_only(stable), rng, groups);
        require(child.synapses().size() == 6, "Fully occupied category blocked remaining category");
        child.mutate(growth_only(stable), rng, groups);
        require(child.synapses().size() == 6, "Saturated graph created duplicate edges");
    }
    require(singleton > 1800 && singleton < 2200, "Partial saturation biased category choice");
}

int main()
{
    try {
        for (bool stable : {false, true}) {
            for (bool extended : {false, true}) category_distribution(stable, extended);
            saturated_categories(stable);
        }
        std::cout << "Sensory mutation sampling passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
