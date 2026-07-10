#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>

namespace neuroevo {

class Random {
public:
    explicit Random(std::uint64_t seed = 7) : engine_(seed) {}

    double uniform(double min, double max)
    {
        std::uniform_real_distribution<double> dist(min, max);
        return dist(engine_);
    }

    std::size_t uniform_index(std::size_t upper_exclusive)
    {
        std::uniform_int_distribution<std::size_t> dist(0, upper_exclusive - 1);
        return dist(engine_);
    }

    double normal(double mean, double stddev)
    {
        if (stddev <= 0.0) {
            return mean;
        }
        std::normal_distribution<double> dist(mean, stddev);
        return dist(engine_);
    }

    bool chance(double probability)
    {
        const double clipped = std::clamp(probability, 0.0, 1.0);
        return uniform(0.0, 1.0) < clipped;
    }

    std::uint64_t next_u64()
    {
        return engine_();
    }

private:
    std::mt19937_64 engine_;
};

} // namespace neuroevo
