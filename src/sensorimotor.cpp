#include "neuroevo/sensorimotor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace neuroevo {
namespace {

constexpr double pi = 3.14159265358979323846;

Vec2 heading_vector(double heading_radians)
{
    return {std::cos(heading_radians), std::sin(heading_radians)};
}

double normalized_distance(Vec2 delta, double world_diagonal)
{
    return std::clamp(length(delta) / std::max(0.001, world_diagonal), 0.0, 1.0);
}

} // namespace

SensorimotorSpec sensorimotor_spec(SensorimotorRegimeKind kind)
{
    switch (kind) {
    case SensorimotorRegimeKind::TargetVector:
        return {
            kind,
            "target-vector",
            5,
            4,
            {"target_right", "target_left", "target_up", "target_down", "target_distance"},
            {"move_left", "move_right", "move_down", "move_up"},
        };
    case SensorimotorRegimeKind::DirectionalFov:
        return {
            kind,
            "directional-fov",
            4,
            3,
            {"food_visible", "food_left", "food_right", "food_distance"},
            {"walk_speed", "turn_left", "turn_right"},
        };
    }

    throw std::invalid_argument("Unknown sensorimotor regime");
}

std::string to_string(SensorimotorRegimeKind kind)
{
    return sensorimotor_spec(kind).name;
}

SensorimotorRegimeKind parse_sensorimotor_regime(const std::string& value)
{
    if (value == "target-vector" || value == "target_vector" || value == "allocentric") {
        return SensorimotorRegimeKind::TargetVector;
    }
    if (value == "directional-fov" || value == "directional_fov" || value == "fov") {
        return SensorimotorRegimeKind::DirectionalFov;
    }
    throw std::invalid_argument("Unknown sensorimotor regime: " + value);
}

double normalize_angle(double radians)
{
    while (radians <= -pi) {
        radians += 2.0 * pi;
    }
    while (radians > pi) {
        radians -= 2.0 * pi;
    }
    return radians;
}

SensoryState sense_target(
    SensorimotorRegimeKind kind,
    AgentState agent,
    Vec2 target,
    double world_diagonal,
    double fov_radians)
{
    const Vec2 delta = target - agent.position;
    const double distance = length(delta);
    const double target_angle = distance > 0.001 ? std::atan2(delta.y, delta.x) : agent.heading_radians;
    const double bearing = normalize_angle(target_angle - agent.heading_radians);
    const double distance_input = normalized_distance(delta, world_diagonal);

    switch (kind) {
    case SensorimotorRegimeKind::TargetVector: {
        const double normalized_dx = distance > 0.001 ? std::clamp(delta.x / distance, -1.0, 1.0) : 0.0;
        const double normalized_dy = distance > 0.001 ? std::clamp(delta.y / distance, -1.0, 1.0) : 0.0;
        return {
            {
                std::max(0.0, normalized_dx),
                std::max(0.0, -normalized_dx),
                std::max(0.0, normalized_dy),
                std::max(0.0, -normalized_dy),
                distance_input,
            },
            true,
            bearing,
            distance,
        };
    }
    case SensorimotorRegimeKind::DirectionalFov: {
        const double half_fov = std::max(0.001, fov_radians * 0.5);
        const bool visible = std::abs(bearing) <= half_fov;
        const double normalized_bearing = visible ? std::clamp(bearing / half_fov, -1.0, 1.0) : 0.0;
        return {
            {
                visible ? 1.0 : 0.0,
                visible ? std::max(0.0, normalized_bearing) : 0.0,
                visible ? std::max(0.0, -normalized_bearing) : 0.0,
                visible ? distance_input : 0.0,
            },
            visible,
            bearing,
            distance,
        };
    }
    }

    throw std::invalid_argument("Unknown sensorimotor regime");
}

MotorCommand decode_motor_command(
    SensorimotorRegimeKind kind,
    const std::vector<double>& motors,
    double heading_radians,
    double max_speed,
    double max_turn_rate,
    double motor_gain,
    double env_dt)
{
    switch (kind) {
    case SensorimotorRegimeKind::TargetVector: {
        const double left = motors.size() > 0 ? motors[0] : 0.0;
        const double right = motors.size() > 1 ? motors[1] : 0.0;
        const double down = motors.size() > 2 ? motors[2] : 0.0;
        const double up = motors.size() > 3 ? motors[3] : 0.0;
        Vec2 command{
            std::tanh(motor_gain * (right - left)),
            std::tanh(motor_gain * (up - down)),
        };
        Vec2 velocity = clamp_length(command, 1.0) * max_speed;
        double updated_heading = heading_radians;
        if (length(velocity) > 0.001) {
            updated_heading = std::atan2(velocity.y, velocity.x);
        }
        return {velocity, updated_heading, length(command), 0.0};
    }
    case SensorimotorRegimeKind::DirectionalFov: {
        const double speed_output = motors.size() > 0 ? motors[0] : 0.0;
        const double turn_left = motors.size() > 1 ? motors[1] : 0.0;
        const double turn_right = motors.size() > 2 ? motors[2] : 0.0;
        const double speed_command = std::tanh(motor_gain * speed_output);
        const double turn_command = std::tanh(motor_gain * (turn_left - turn_right));
        const double updated_heading = normalize_angle(heading_radians + turn_command * max_turn_rate * env_dt);
        const Vec2 velocity = heading_vector(updated_heading) * (std::max(0.0, speed_command) * max_speed);
        return {velocity, updated_heading, speed_command, turn_command};
    }
    }

    throw std::invalid_argument("Unknown sensorimotor regime");
}

} // namespace neuroevo
