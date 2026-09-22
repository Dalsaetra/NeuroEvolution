#pragma once

#include "neuroevo/brain.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace neuroevo::detail {

// Immutable broad phase. Return candidates in original creature order so sensory
// sums and geometric tie breaking retain their exact floating-point order.
class CreatureIndex {
public:
    explicit CreatureIndex(const std::vector<Vec2>& positions) : positions_(positions), order_(positions.size())
    {
        std::iota(order_.begin(), order_.end(), 0);
        std::sort(order_.begin(), order_.end(), [&](auto a, auto b) {
            return positions_[a].x < positions_[b].x;
        });
    }

    std::vector<std::size_t> nearby(Vec2 center, double radius) const
    {
        // Broad-phase bounds are deliberately wider than the exact predicates.
        radius += 1e-9;
        const double low = std::nextafter(center.x - radius, -INFINITY);
        const double high = std::nextafter(center.x + radius, INFINITY);
        auto it = std::lower_bound(order_.begin(), order_.end(), low,
            [&](auto i, double x) { return positions_[i].x < x; });
        std::vector<std::size_t> result;
        for (; it != order_.end() && positions_[*it].x <= high; ++it)
            if (std::abs(positions_[*it].y - center.y) <= radius) result.push_back(*it);
        std::sort(result.begin(), result.end());
        return result;
    }

private:
    const std::vector<Vec2>& positions_;
    std::vector<std::size_t> order_;
};

} // namespace neuroevo::detail
