#pragma once

#include "neuroevo/random.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace neuroevo {

enum class ObjectiveDirection {
    Minimize,
    Maximize,
};

struct ObjectiveDescriptor {
    std::string name;
    ObjectiveDirection direction = ObjectiveDirection::Minimize;
};

bool dominates(
    const std::vector<double>& lhs,
    const std::vector<double>& rhs,
    const std::vector<ObjectiveDescriptor>& objectives);

std::vector<std::vector<std::size_t>> non_dominated_sort(
    const std::vector<std::vector<double>>& objective_values,
    const std::vector<ObjectiveDescriptor>& objectives);

std::vector<double> compute_crowding_distance(
    const std::vector<std::vector<double>>& objective_values,
    const std::vector<std::size_t>& front,
    const std::vector<ObjectiveDescriptor>& objectives);

int compare_nsga2(
    std::size_t lhs_rank,
    double lhs_crowding_distance,
    double lhs_task_score,
    std::size_t rhs_rank,
    double rhs_crowding_distance,
    double rhs_task_score);

std::size_t nsga2_tournament_select(
    const std::vector<std::size_t>& pareto_ranks,
    const std::vector<double>& crowding_distances,
    const std::vector<double>& task_scores,
    std::size_t tournament_size,
    Random& rng);

std::vector<std::size_t> nsga2_survival_select(
    const std::vector<std::size_t>& pareto_ranks,
    const std::vector<double>& crowding_distances,
    const std::vector<double>& task_scores,
    std::size_t population_size);

} // namespace neuroevo
