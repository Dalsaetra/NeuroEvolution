#pragma once

#include <algorithm>
#include <cmath>

namespace neuroevo {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2& operator+=(Vec2 other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vec2& operator-=(Vec2 other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vec2& operator*=(double scalar)
    {
        x *= scalar;
        y *= scalar;
        return *this;
    }
};

inline Vec2 operator+(Vec2 lhs, Vec2 rhs)
{
    lhs += rhs;
    return lhs;
}

inline Vec2 operator-(Vec2 lhs, Vec2 rhs)
{
    lhs -= rhs;
    return lhs;
}

inline Vec2 operator*(Vec2 lhs, double scalar)
{
    lhs *= scalar;
    return lhs;
}

inline Vec2 operator*(double scalar, Vec2 rhs)
{
    rhs *= scalar;
    return rhs;
}

inline double length(Vec2 value)
{
    return std::sqrt(value.x * value.x + value.y * value.y);
}

inline Vec2 clamp_to_bounds(Vec2 value, double width, double height)
{
    return {std::clamp(value.x, 0.0, width), std::clamp(value.y, 0.0, height)};
}

inline Vec2 clamp_length(Vec2 value, double max_length)
{
    const double current_length = length(value);
    if (current_length <= max_length || current_length == 0.0) {
        return value;
    }
    return value * (max_length / current_length);
}

} // namespace neuroevo
