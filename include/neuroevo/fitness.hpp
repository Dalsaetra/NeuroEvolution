#pragma once

#include "neuroevo/sensorimotor.hpp"

#include <memory>
#include <string>

namespace neuroevo {

enum class FitnessRegimeKind {
    Shaped,
    Sparse,
};

struct FitnessConfig {
    FitnessRegimeKind regime = FitnessRegimeKind::Shaped;
    double progress_reward_scale = 3.0;
    double distance_improvement_reward_scale = 8.0;
    double visibility_reward_scale = 0.001;
    double final_distance_penalty = 0.5;
};

struct FitnessStepContext {
    double task_reward = 0.0;
    double progress = 0.0;
    double distance = 0.0;
    double closest_distance = 0.0;
    double world_diagonal = 1.0;
    double half_fov_radians = 1.0;
    SensoryState sensory;
    MotorCommand command;
};

struct FitnessStepResult {
    double reward = 0.0;
    double closest_distance = 0.0;
};

class FitnessRegime {
public:
    virtual ~FitnessRegime() = default;
    virtual FitnessStepResult score_step(const FitnessStepContext& context) const = 0;
    virtual double terminal_penalty(double normalized_final_distance) const = 0;
};

std::unique_ptr<FitnessRegime> make_fitness_regime(const FitnessConfig& config);
FitnessRegimeKind parse_fitness_regime(const std::string& value);
std::string to_string(FitnessRegimeKind regime);

} // namespace neuroevo
