#pragma once

#include "neuroevo/vector2.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace neuroevo {

enum class SensorimotorRegimeKind {
    TargetVector,
    DirectionalFov,
};

struct AgentState {
    Vec2 position;
    double heading_radians = 0.0;
};

struct SensorimotorSpec {
    SensorimotorRegimeKind kind = SensorimotorRegimeKind::DirectionalFov;
    std::string name;
    std::size_t input_count = 0;
    std::size_t output_count = 0;
    std::vector<std::string> input_labels;
    std::vector<std::string> output_labels;
};

struct SensoryState {
    std::vector<double> inputs;
    bool target_visible = true;
    double target_bearing = 0.0;
    double target_distance = 0.0;
};

struct MotorCommand {
    Vec2 velocity;
    double heading_radians = 0.0;
    double speed_command = 0.0;
    double turn_command = 0.0;
};

SensorimotorSpec sensorimotor_spec(SensorimotorRegimeKind kind);
std::string to_string(SensorimotorRegimeKind kind);
SensorimotorRegimeKind parse_sensorimotor_regime(const std::string& value);
double normalize_angle(double radians);

SensoryState sense_target(
    SensorimotorRegimeKind kind,
    AgentState agent,
    Vec2 target,
    double world_diagonal,
    double fov_radians);

MotorCommand decode_motor_command(
    SensorimotorRegimeKind kind,
    const std::vector<double>& motors,
    double heading_radians,
    double max_speed,
    double max_turn_rate,
    double motor_gain,
    double env_dt);

} // namespace neuroevo
