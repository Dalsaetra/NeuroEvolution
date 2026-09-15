#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

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

    void save_state(std::ostream& stream) const { stream << engine_ << '\n'; }
    static std::size_t checkpoint_word_count()
    {
        static const std::size_t count = [] {
            std::ostringstream saved;
            saved << std::mt19937_64{};
            std::istringstream input(saved.str());
            std::string word;
            std::size_t words = 0;
            while (input >> word) ++words;
            return words;
        }();
        return count;
    }
    void load_state(std::istream& stream)
    {
        // Each engine occupies its own line. Do not let a runtime with a
        // different MT serialization consume tokens from the following state.
        std::string line;
        if (!std::getline(stream >> std::ws, line))
            throw std::runtime_error("Invalid random generator checkpoint");
        std::istringstream tokens(line);
        std::string word;
        std::size_t words = 0;
        while (tokens >> word) ++words;
        if ((words == 312 || words == 313) && words != checkpoint_word_count())
            throw std::runtime_error("Checkpoint uses a different C++ random-generator format. "
                "Resume with scripts/ecosystem.ps1 to select a compatible build.");
        std::istringstream state(line);
        if (words != checkpoint_word_count() || !(state >> engine_)) {
            throw std::runtime_error("Invalid random generator checkpoint");
        }
    }

private:
    std::mt19937_64 engine_;
};

} // namespace neuroevo
