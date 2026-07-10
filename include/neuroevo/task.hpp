#pragma once

#include "neuroevo/random.hpp"
#include "neuroevo/vector2.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace neuroevo {

enum class TaskRegimeKind {
    FoodSeeking,
    CueOcclusion,
};

enum class TaskPhase {
    Visible,
    Cue,
    Occluded,
    Revealed,
};

struct TaskConfig {
    TaskRegimeKind regime = TaskRegimeKind::FoodSeeking;
    std::size_t cue_visible_steps = 40;
    std::size_t occlusion_min_steps = 80;
    std::size_t occlusion_max_steps = 160;
    double reveal_distance = 0.12;
};

struct TaskWorldConfig {
    double width = 1.0;
    double height = 1.0;
    double target_radius = 0.075;
    double min_target_distance = 0.25;
    double food_reward = 10.0;
};

struct TaskObservation {
    Vec2 target;
    bool sensory_available = true;
    TaskPhase phase = TaskPhase::Visible;
};

struct TaskStepResult {
    double reward = 0.0;
    double foods_collected = 0.0;
    double occluded_foods_collected = 0.0;
    bool target_changed = false;
};

class TaskScenario {
public:
    virtual ~TaskScenario() = default;

    virtual void reset(Vec2 agent_position, Random& rng) = 0;
    virtual TaskObservation observe(Vec2 agent_position, std::size_t step) const = 0;
    virtual TaskStepResult advance(Vec2 agent_position, std::size_t step, Random& rng) = 0;
};

std::unique_ptr<TaskScenario> make_task_scenario(
    const TaskConfig& task_config,
    const TaskWorldConfig& world_config);

TaskRegimeKind parse_task_regime(const std::string& value);
std::string to_string(TaskRegimeKind regime);
std::string to_string(TaskPhase phase);

} // namespace neuroevo
