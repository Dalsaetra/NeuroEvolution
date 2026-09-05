#pragma once

#include "neuroevo/brain.hpp"

#include <algorithm>

namespace neuroevo::detail {

inline MutationConfig slight_mutation(MutationConfig mutation)
{
    mutation.weight_sigma *= 0.45;
    mutation.bias_sigma *= 0.45;
    mutation.threshold_sigma *= 0.45;
    mutation.position_sigma *= 0.45;
    mutation.background_sensitivity_sigma *= 0.45;
    mutation.mutate_weight_probability *= 0.65;
    mutation.mutate_neuron_probability *= 0.65;
    mutation.add_synapse_probability *= 0.65;
    mutation.add_neuron_probability *= 0.65;
    mutation.add_reciprocal_motif_probability *= 0.65;
    mutation.remove_synapse_probability *= 0.65;
    mutation.remove_neuron_probability *= 0.65;
    return mutation;
}

inline MutationConfig strong_mutation(MutationConfig mutation)
{
    // Strong offspring and archive exploration use broad changes.
    mutation.stable = false;
    mutation.weight_sigma *= 1.75;
    mutation.bias_sigma *= 1.75;
    mutation.threshold_sigma *= 1.75;
    mutation.position_sigma *= 1.75;
    mutation.background_sensitivity_sigma *= 1.75;
    if (mutation.mutate_weight_probability > 0) mutation.mutate_weight_probability = std::min(1.0, std::max(0.45, mutation.mutate_weight_probability * 2));
    if (mutation.mutate_neuron_probability > 0) mutation.mutate_neuron_probability = std::min(1.0, std::max(0.35, mutation.mutate_neuron_probability * 2));
    if (mutation.add_synapse_probability > 0) mutation.add_synapse_probability = std::min(1.0, std::max(0.75, mutation.add_synapse_probability * 2));
    if (mutation.add_neuron_probability > 0) mutation.add_neuron_probability = std::min(1.0, std::max(0.35, mutation.add_neuron_probability * 2));
    if (mutation.add_reciprocal_motif_probability > 0) mutation.add_reciprocal_motif_probability = std::min(1.0, std::max(0.30, mutation.add_reciprocal_motif_probability * 2));
    if (mutation.remove_synapse_probability > 0) mutation.remove_synapse_probability = std::min(1.0, std::max(0.10, mutation.remove_synapse_probability * 2));
    mutation.remove_neuron_probability = std::min(1.0, mutation.remove_neuron_probability * 2);
    return mutation;
}

} // namespace neuroevo::detail
