#include "neuroevo/sensorimotor.hpp"

#include <cmath>
#include <iostream>

int main()
{
    constexpr double pi = 3.14159265358979323846;

    neuroevo::AgentState agent{{0.5, 0.5}, 0.0};
    const neuroevo::Vec2 target_left{0.5, 1.0};
    const neuroevo::SensoryState sensory = neuroevo::sense_target(
        neuroevo::SensorimotorRegimeKind::DirectionalFov,
        agent,
        target_left,
        1.4142135623730951,
        pi);

    if (!sensory.target_visible || sensory.inputs.size() != 4) {
        std::cerr << "Expected visible directional-FOV target inputs\n";
        return 1;
    }
    if (sensory.inputs[1] <= 0.0 || sensory.inputs[2] != 0.0) {
        std::cerr << "Expected positive bearing to activate food_left only\n";
        return 1;
    }

    const neuroevo::MotorCommand left_command = neuroevo::decode_motor_command(
        neuroevo::SensorimotorRegimeKind::DirectionalFov,
        {0.0, 1.0, 0.0},
        0.0,
        1.0,
        1.0,
        4.0,
        1.0);
    if (left_command.turn_command <= 0.0 || left_command.heading_radians <= 0.0) {
        std::cerr << "turn_left output should increase heading in directional-FOV mode\n";
        return 1;
    }

    const neuroevo::MotorCommand right_command = neuroevo::decode_motor_command(
        neuroevo::SensorimotorRegimeKind::DirectionalFov,
        {0.0, 0.0, 1.0},
        0.0,
        1.0,
        1.0,
        4.0,
        1.0);
    if (right_command.turn_command >= 0.0 || right_command.heading_radians >= 0.0) {
        std::cerr << "turn_right output should decrease heading in directional-FOV mode\n";
        return 1;
    }

    return 0;
}
