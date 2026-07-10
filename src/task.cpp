#include "neuroevo/task.hpp"

#include <algorithm>
#include <stdexcept>

namespace neuroevo {
namespace {

class ForagingTask final : public TaskScenario {
public:
    ForagingTask(TaskConfig task_config, TaskWorldConfig world_config)
        : task_config_(task_config), world_config_(world_config)
    {
    }

    void reset(Vec2 agent_position, Random& rng) override
    {
        place_target(agent_position, 0, rng);
    }

    TaskObservation observe(Vec2 agent_position, std::size_t step) const override
    {
        if (task_config_.regime == TaskRegimeKind::FoodSeeking) {
            return {target_, true, TaskPhase::Visible};
        }

        const std::size_t elapsed = step >= target_start_step_ ? step - target_start_step_ : 0;
        if (elapsed < task_config_.cue_visible_steps) {
            return {target_, true, TaskPhase::Cue};
        }

        const bool close_reveal = length(target_ - agent_position) <= task_config_.reveal_distance;
        if (elapsed < task_config_.cue_visible_steps + occlusion_steps_ && !close_reveal) {
            return {target_, false, TaskPhase::Occluded};
        }
        return {target_, true, TaskPhase::Revealed};
    }

    TaskStepResult advance(Vec2 agent_position, std::size_t step, Random& rng) override
    {
        if (length(target_ - agent_position) > world_config_.target_radius) {
            return {};
        }

        const std::size_t elapsed = step >= target_start_step_ ? step - target_start_step_ : 0;
        const bool scheduled_occlusion = task_config_.regime == TaskRegimeKind::CueOcclusion
            && elapsed >= task_config_.cue_visible_steps
            && elapsed < task_config_.cue_visible_steps + occlusion_steps_;
        TaskStepResult result;
        result.reward = world_config_.food_reward;
        result.foods_collected = 1.0;
        result.occluded_foods_collected = scheduled_occlusion ? 1.0 : 0.0;
        result.target_changed = true;
        place_target(agent_position, step + 1, rng);
        return result;
    }

private:
    TaskConfig task_config_;
    TaskWorldConfig world_config_;
    Vec2 target_;
    std::size_t target_start_step_ = 0;
    std::size_t occlusion_steps_ = 0;

    void place_target(Vec2 agent_position, std::size_t start_step, Random& rng)
    {
        constexpr std::size_t max_attempts = 128;
        for (std::size_t attempt = 0; attempt < max_attempts; ++attempt) {
            const Vec2 candidate{
                rng.uniform(0.0, world_config_.width),
                rng.uniform(0.0, world_config_.height),
            };
            if (length(candidate - agent_position) >= world_config_.min_target_distance) {
                target_ = candidate;
                break;
            }
            if (attempt + 1 == max_attempts) {
                target_ = candidate;
            }
        }

        target_start_step_ = start_step;
        const std::size_t min_steps = std::min(task_config_.occlusion_min_steps, task_config_.occlusion_max_steps);
        const std::size_t max_steps = std::max(task_config_.occlusion_min_steps, task_config_.occlusion_max_steps);
        occlusion_steps_ = min_steps;
        if (max_steps > min_steps) {
            occlusion_steps_ += rng.uniform_index(max_steps - min_steps + 1);
        }
    }
};

} // namespace

std::unique_ptr<TaskScenario> make_task_scenario(
    const TaskConfig& task_config,
    const TaskWorldConfig& world_config)
{
    switch (task_config.regime) {
    case TaskRegimeKind::FoodSeeking:
    case TaskRegimeKind::CueOcclusion:
        return std::make_unique<ForagingTask>(task_config, world_config);
    }
    throw std::invalid_argument("Unknown task regime");
}

TaskRegimeKind parse_task_regime(const std::string& value)
{
    if (value == "food-seeking" || value == "food_seeking" || value == "food") {
        return TaskRegimeKind::FoodSeeking;
    }
    if (value == "cue-occlusion" || value == "cue_occlusion" || value == "occlusion") {
        return TaskRegimeKind::CueOcclusion;
    }
    throw std::invalid_argument("Unknown task regime: " + value);
}

std::string to_string(TaskRegimeKind regime)
{
    switch (regime) {
    case TaskRegimeKind::FoodSeeking:
        return "food-seeking";
    case TaskRegimeKind::CueOcclusion:
        return "cue-occlusion";
    }
    throw std::invalid_argument("Unknown task regime");
}

std::string to_string(TaskPhase phase)
{
    switch (phase) {
    case TaskPhase::Visible:
        return "visible";
    case TaskPhase::Cue:
        return "cue";
    case TaskPhase::Occluded:
        return "occluded";
    case TaskPhase::Revealed:
        return "revealed";
    }
    throw std::invalid_argument("Unknown task phase");
}

} // namespace neuroevo
