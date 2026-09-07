#pragma once

#include "neuroevo/brain.hpp"

#include <algorithm>

namespace neuroevo::detail {

inline MutationConfig budgeted_mutation(MutationConfig mutation, double budget,
    std::size_t edits, double weight_limit)
{
    mutation.stable = true;
    mutation.structural_edit_probability = budget;
    mutation.local_edit_limit = edits;
    mutation.local_weight_limit_multiplier = weight_limit;
    // Respect explicit zeroes; balance each enabled add/remove pair.
    const auto balance = [](double& add, double& remove) {
        if (add > 0 && remove > 0) add = remove = (add + remove) * 0.5;
    };
    balance(mutation.add_synapse_probability, mutation.remove_synapse_probability);
    balance(mutation.add_neuron_probability, mutation.remove_neuron_probability);
    // Unpaired motif growth would bias the structural budget toward additions.
    mutation.add_reciprocal_motif_probability = 0;
    return mutation;
}

inline MutationConfig slight_mutation(MutationConfig mutation)
{
    mutation.weight_sigma *= 0.75;
    mutation.bias_sigma *= 0.75;
    mutation.threshold_sigma *= 0.75;
    mutation.position_sigma *= 0.75;
    mutation.background_sensitivity_sigma *= 0.75;
    mutation.mutate_weight_probability *= 0.9;
    mutation.mutate_neuron_probability *= 0.9;
    mutation.add_synapse_probability *= 0.9;
    mutation.add_neuron_probability *= 0.9;
    mutation.add_reciprocal_motif_probability *= 0.9;
    mutation.remove_synapse_probability *= 0.9;
    mutation.rewire_synapse_probability *= 0.9;
    mutation.remove_neuron_probability *= 0.9;
    return budgeted_mutation(mutation, 0.3, 2, 1.0);
}

inline MutationConfig strong_mutation(MutationConfig mutation)
{
    // Strong offspring explore further, but their edit count is genome-size independent.
    mutation.weight_sigma *= 1.75;
    mutation.bias_sigma *= 1.75;
    mutation.threshold_sigma *= 1.75;
    mutation.position_sigma *= 1.75;
    mutation.background_sensitivity_sigma *= 1.75;
    return budgeted_mutation(mutation, 0.5, 4, 2.0);
}

} // namespace neuroevo::detail
