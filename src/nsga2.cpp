#include "neuroevo/nsga2.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace neuroevo {
namespace {

void validate_objective_vector(
    const std::vector<double>& values,
    const std::vector<ObjectiveDescriptor>& objectives)
{
    if (values.size() != objectives.size()) {
        throw std::invalid_argument("Objective value count does not match objective descriptor count");
    }
}

double objective_delta(double lhs, double rhs, ObjectiveDirection direction)
{
    return direction == ObjectiveDirection::Maximize ? lhs - rhs : rhs - lhs;
}

double task_score_at(const std::vector<double>& task_scores, std::size_t index)
{
    return index < task_scores.size() ? task_scores[index] : 0.0;
}

} // namespace

bool dominates(
    const std::vector<double>& lhs,
    const std::vector<double>& rhs,
    const std::vector<ObjectiveDescriptor>& objectives)
{
    validate_objective_vector(lhs, objectives);
    validate_objective_vector(rhs, objectives);

    bool strictly_better = false;
    for (std::size_t i = 0; i < objectives.size(); ++i) {
        const double delta = objective_delta(lhs[i], rhs[i], objectives[i].direction);
        if (delta < 0.0) {
            return false;
        }
        if (delta > 0.0) {
            strictly_better = true;
        }
    }
    return strictly_better;
}

std::vector<std::vector<std::size_t>> non_dominated_sort(
    const std::vector<std::vector<double>>& objective_values,
    const std::vector<ObjectiveDescriptor>& objectives)
{
    const std::size_t count = objective_values.size();
    if (count == 0) {
        return {};
    }

    for (const auto& values : objective_values) {
        validate_objective_vector(values, objectives);
    }

    std::vector<std::vector<std::size_t>> dominated_by_index(count);
    std::vector<std::size_t> domination_counts(count, 0);
    std::vector<std::size_t> first_front;
    first_front.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = 0; j < count; ++j) {
            if (i == j) {
                continue;
            }
            if (dominates(objective_values[i], objective_values[j], objectives)) {
                dominated_by_index[i].push_back(j);
            } else if (dominates(objective_values[j], objective_values[i], objectives)) {
                ++domination_counts[i];
            }
        }

        if (domination_counts[i] == 0) {
            first_front.push_back(i);
        }
    }

    std::vector<std::vector<std::size_t>> fronts;
    fronts.push_back(std::move(first_front));

    for (std::size_t front_index = 0; front_index < fronts.size(); ++front_index) {
        std::vector<std::size_t> next_front;
        for (const std::size_t index : fronts[front_index]) {
            for (const std::size_t dominated : dominated_by_index[index]) {
                if (domination_counts[dominated] == 0) {
                    continue;
                }
                --domination_counts[dominated];
                if (domination_counts[dominated] == 0) {
                    next_front.push_back(dominated);
                }
            }
        }
        if (!next_front.empty()) {
            fronts.push_back(std::move(next_front));
        }
    }

    return fronts;
}

std::vector<double> compute_crowding_distance(
    const std::vector<std::vector<double>>& objective_values,
    const std::vector<std::size_t>& front,
    const std::vector<ObjectiveDescriptor>& objectives)
{
    std::vector<double> distances(objective_values.size(), 0.0);
    if (front.empty()) {
        return distances;
    }

    for (const std::size_t index : front) {
        if (index >= objective_values.size()) {
            throw std::out_of_range("Crowding front index is out of range");
        }
        validate_objective_vector(objective_values[index], objectives);
    }

    if (front.size() <= 2) {
        for (const std::size_t index : front) {
            distances[index] = std::numeric_limits<double>::infinity();
        }
        return distances;
    }

    std::vector<std::size_t> sorted = front;
    for (std::size_t objective_index = 0; objective_index < objectives.size(); ++objective_index) {
        std::sort(sorted.begin(), sorted.end(), [&](std::size_t lhs, std::size_t rhs) {
            return objective_values[lhs][objective_index] < objective_values[rhs][objective_index];
        });

        const double min_value = objective_values[sorted.front()][objective_index];
        const double max_value = objective_values[sorted.back()][objective_index];
        distances[sorted.front()] = std::numeric_limits<double>::infinity();
        distances[sorted.back()] = std::numeric_limits<double>::infinity();

        const double range = max_value - min_value;
        if (range <= 0.0) {
            continue;
        }

        for (std::size_t i = 1; i + 1 < sorted.size(); ++i) {
            if (std::isinf(distances[sorted[i]])) {
                continue;
            }
            const double previous = objective_values[sorted[i - 1]][objective_index];
            const double next = objective_values[sorted[i + 1]][objective_index];
            distances[sorted[i]] += (next - previous) / range;
        }
    }

    return distances;
}

int compare_nsga2(
    std::size_t lhs_rank,
    double lhs_crowding_distance,
    double lhs_task_score,
    std::size_t rhs_rank,
    double rhs_crowding_distance,
    double rhs_task_score)
{
    if (lhs_rank != rhs_rank) {
        return lhs_rank < rhs_rank ? -1 : 1;
    }
    if (lhs_crowding_distance != rhs_crowding_distance) {
        return lhs_crowding_distance > rhs_crowding_distance ? -1 : 1;
    }
    if (lhs_task_score != rhs_task_score) {
        return lhs_task_score > rhs_task_score ? -1 : 1;
    }
    return 0;
}

std::size_t nsga2_tournament_select(
    const std::vector<std::size_t>& pareto_ranks,
    const std::vector<double>& crowding_distances,
    const std::vector<double>& task_scores,
    std::size_t tournament_size,
    Random& rng)
{
    if (pareto_ranks.empty()) {
        throw std::invalid_argument("Cannot select from an empty population");
    }
    if (crowding_distances.size() != pareto_ranks.size()) {
        throw std::invalid_argument("Crowding distance count does not match rank count");
    }

    tournament_size = std::max<std::size_t>(1, tournament_size);
    std::size_t best = rng.uniform_index(pareto_ranks.size());

    for (std::size_t i = 1; i < tournament_size; ++i) {
        const std::size_t candidate = rng.uniform_index(pareto_ranks.size());
        const int comparison = compare_nsga2(
            pareto_ranks[candidate],
            crowding_distances[candidate],
            task_score_at(task_scores, candidate),
            pareto_ranks[best],
            crowding_distances[best],
            task_score_at(task_scores, best));
        if (comparison < 0 || (comparison == 0 && rng.chance(0.5))) {
            best = candidate;
        }
    }

    return best;
}

std::vector<std::size_t> nsga2_survival_select(
    const std::vector<std::size_t>& pareto_ranks,
    const std::vector<double>& crowding_distances,
    const std::vector<double>& task_scores,
    std::size_t population_size)
{
    if (pareto_ranks.size() != crowding_distances.size()) {
        throw std::invalid_argument("Crowding distance count does not match rank count");
    }

    std::vector<std::size_t> indices(pareto_ranks.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::stable_sort(indices.begin(), indices.end(), [&](std::size_t lhs, std::size_t rhs) {
        const int comparison = compare_nsga2(
            pareto_ranks[lhs],
            crowding_distances[lhs],
            task_score_at(task_scores, lhs),
            pareto_ranks[rhs],
            crowding_distances[rhs],
            task_score_at(task_scores, rhs));
        if (comparison != 0) {
            return comparison < 0;
        }
        return lhs < rhs;
    });

    if (indices.size() > population_size) {
        indices.resize(population_size);
    }
    return indices;
}

} // namespace neuroevo
