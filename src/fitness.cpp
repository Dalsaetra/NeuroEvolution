#include "neuroevo/fitness.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace neuroevo {
namespace {

class ShapedFitness final : public FitnessRegime {
public:
    explicit ShapedFitness(FitnessConfig config) : config_(config) {}

    FitnessStepResult score_step(const FitnessStepContext& context) const override
    {
        FitnessStepResult result;
        result.reward = context.task_reward + config_.progress_reward_scale * context.progress;
        result.closest_distance = context.closest_distance;

        const double improvement = std::max(0.0, context.closest_distance - context.distance);
        if (improvement > 0.0) {
            result.reward += config_.distance_improvement_reward_scale
                * (improvement / std::max(0.001, context.world_diagonal));
            result.closest_distance = context.distance;
        }

        if (context.sensory.target_visible) {
            const double centered = 1.0 - std::clamp(
                std::abs(context.sensory.target_bearing) / std::max(0.001, context.half_fov_radians),
                0.0,
                1.0);
            const double speed_gate = std::clamp(context.command.speed_command, 0.0, 1.0);
            result.reward += config_.visibility_reward_scale * centered * speed_gate;
        }
        return result;
    }

    double terminal_penalty(double normalized_final_distance) const override
    {
        return config_.final_distance_penalty * normalized_final_distance;
    }

private:
    FitnessConfig config_;
};

class SparseFitness final : public FitnessRegime {
public:
    FitnessStepResult score_step(const FitnessStepContext& context) const override
    {
        return {context.task_reward, std::min(context.closest_distance, context.distance)};
    }

    double terminal_penalty(double) const override
    {
        return 0.0;
    }
};

} // namespace

std::unique_ptr<FitnessRegime> make_fitness_regime(const FitnessConfig& config)
{
    switch (config.regime) {
    case FitnessRegimeKind::Shaped:
        return std::make_unique<ShapedFitness>(config);
    case FitnessRegimeKind::Sparse:
        return std::make_unique<SparseFitness>();
    }
    throw std::invalid_argument("Unknown fitness regime");
}

FitnessRegimeKind parse_fitness_regime(const std::string& value)
{
    if (value == "shaped") {
        return FitnessRegimeKind::Shaped;
    }
    if (value == "sparse" || value == "task-only" || value == "task_only") {
        return FitnessRegimeKind::Sparse;
    }
    throw std::invalid_argument("Unknown fitness regime: " + value);
}

std::string to_string(FitnessRegimeKind regime)
{
    switch (regime) {
    case FitnessRegimeKind::Shaped:
        return "shaped";
    case FitnessRegimeKind::Sparse:
        return "sparse";
    }
    throw std::invalid_argument("Unknown fitness regime");
}

} // namespace neuroevo
