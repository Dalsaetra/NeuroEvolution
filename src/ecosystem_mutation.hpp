#pragma once
#include "neuroevo/config.hpp"
#include <initializer_list>

namespace neuroevo::detail {
inline MutationConfig birth_mutation(MutationConfig mutation, const BirthMutationProfile& profile)
{
    mutation.structural_edit_probability = profile.structural_probability;
    mutation.local_edit_limit = profile.local_edits;
    mutation.local_weight_limit_multiplier = profile.weight_limit;
    for (auto* sigma : {&mutation.weight_sigma, &mutation.bias_sigma, &mutation.threshold_sigma,
            &mutation.position_sigma, &mutation.background_sensitivity_sigma}) *sigma *= profile.sigma_scale;
    for (auto* probability : {&mutation.mutate_weight_probability, &mutation.mutate_neuron_probability,
            &mutation.add_synapse_probability, &mutation.add_neuron_probability,
            &mutation.add_reciprocal_motif_probability, &mutation.remove_synapse_probability,
            &mutation.rewire_synapse_probability, &mutation.remove_neuron_probability})
        *probability *= profile.operator_scale;
    if (mutation.balance_structural_pairs) {
        const auto balance = [](double& add, double& remove) {
            if (add > 0 && remove > 0) add = remove = (add + remove) * 0.5;
        };
        balance(mutation.add_synapse_probability, mutation.remove_synapse_probability);
        balance(mutation.add_neuron_probability, mutation.remove_neuron_probability);
    }
    if (!mutation.allow_birth_motifs) mutation.add_reciprocal_motif_probability = 0;
    return mutation;
}
inline MutationConfig slight_mutation(const MutationConfig& mutation)
{
    return birth_mutation(mutation, mutation.slight);
}
inline MutationConfig strong_mutation(const MutationConfig& mutation)
{
    return birth_mutation(mutation, mutation.strong);
}
} // namespace neuroevo::detail
