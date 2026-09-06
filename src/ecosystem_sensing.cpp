#include "neuroevo/ecosystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace neuroevo {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double epsilon = 1e-9;
constexpr std::size_t hearing_offset = eco_sectors * eco_sector_channels;
constexpr std::size_t contact_offset = hearing_offset + 4;
constexpr std::size_t body_offset = contact_offset + 4;

double unit(double value)
{
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

double bearing(Vec2 delta, double heading)
{
    return std::remainder(std::atan2(delta.y, delta.x) - heading, 2.0 * pi);
}

// Sector zero is the rightmost sector, and sector four is the leftmost.
std::size_t sector_at(double angle, double half_fov)
{
    const double coordinate = (angle + half_fov) / (2.0 * half_fov);
    return std::min(eco_sectors - 1,
        static_cast<std::size_t>(std::max(0.0, coordinate) * eco_sectors));
}

// Body-relative quadrants: front, left, back, right.
std::size_t quadrant_at(double angle)
{
    double positive = std::fmod(angle + pi / 4.0 + 2.0 * pi, 2.0 * pi);
    if (positive < 0.0) positive += 2.0 * pi;
    return std::min<std::size_t>(3, static_cast<std::size_t>(positive / (pi / 2.0)));
}

struct Box { double x0, y0, x1, y1; };

Vec2 closest_point(Vec2 point, const Box& box)
{
    return {std::clamp(point.x, box.x0, box.x1), std::clamp(point.y, box.y0, box.y1)};
}

// Exact ray/rectangle entry distance, including zero-width world boundaries.
double ray_entry(Vec2 origin, double angle, const Box& box)
{
    const Vec2 direction{std::cos(angle), std::sin(angle)};
    double entry = 0.0;
    double exit = std::numeric_limits<double>::infinity();
    const auto clip = [&](double position, double velocity, double low, double high) {
        if (std::abs(velocity) < epsilon) return position >= low && position <= high;
        const double a = (low - position) / velocity;
        const double b = (high - position) / velocity;
        entry = std::max(entry, std::min(a, b));
        exit = std::min(exit, std::max(a, b));
        return entry <= exit + epsilon;
    };
    if (!clip(origin.x, direction.x, box.x0, box.x1)
        || !clip(origin.y, direction.y, box.y0, box.y1)) {
        return std::numeric_limits<double>::infinity();
    }
    return exit >= 0.0 ? entry : std::numeric_limits<double>::infinity();
}

double obstacle_distance(Vec2 origin, double heading, double low_angle, double high_angle, const Box& box)
{
    const Vec2 delta = closest_point(origin, box) - origin;
    const double angle = bearing(delta, heading);
    double result = std::numeric_limits<double>::infinity();
    if (length(delta) < epsilon || (angle >= low_angle - epsilon && angle <= high_angle + epsilon)) {
        result = length(delta);
    }
    result = std::min(result, ray_entry(origin, heading + low_angle, box));
    return std::min(result, ray_entry(origin, heading + high_angle, box));
}

EcoAction baseline_action(const std::vector<double>& inputs, const EcosystemConfig& config,
    ControllerKind kind, Random& rng, double carnivory)
{
    EcoAction action;
    const double previous_turn = inputs[body_offset + 2] - inputs[body_offset + 3];
    double turn = std::clamp(0.88 * previous_turn + rng.normal(0.0, 0.20), -1.0, 1.0);
    action.forward = 0.8;

    if (kind == ControllerKind::Random) {
        action.forage = rng.chance(0.35) ? 1.0 : 0.0;
        action.attack = config.predation && rng.chance(0.05) ? 1.0 : 0.0;
        action.call = config.communication && rng.chance(0.03) ? 0.5 : 0.0;
    } else {
        double best_score = -1.0;
        std::size_t target_sector = eco_sectors;
        double target_distance = 0;
        bool target_pod = false;
        for (std::size_t sector = 0; sector < eco_sectors; ++sector) {
            const std::size_t offset = sector * eco_sector_channels;
            // Compare independently visible plant and meat targets using only local senses.
            for (bool meat : {false, true}) {
                if (meat && !config.predation) continue;
                const double efficiency = config.predation ? (meat ? carnivory : 1-carnivory) : 1;
                const double present = meat ? inputs[eco_meat_offset + sector] : inputs[offset + 1];
                const double stock = meat ? inputs[eco_meat_offset + 2 * eco_sectors + sector] : inputs[offset + 7];
                if (efficiency <= 0 || present <= 0 || stock <= 0) continue;
                const auto nearest_type = [&](std::size_t channel) {
                    return inputs[offset + channel] > 0 && (!config.typed_food_proximity
                        || std::abs(inputs[offset + channel] - inputs[offset + 2]) < epsilon);
                };
                const bool refilling = !meat && nearest_type(6)
                    && inputs[offset + 8] == 0.0 && inputs[offset + 9] == 0.0;
                if (refilling) continue;
                const double proximity = meat ? inputs[eco_meat_offset + eco_sectors + sector] : inputs[offset + 2];
                const double distance = (1.0 - proximity) * config.vision_range;
                const double value = !meat && nearest_type(3) ? 1.0 : 1.5;
                const bool pod = !meat && inputs[offset + 8] > 0.0;
                const double score = efficiency * value * stock / (0.5 + distance + (pod ? 2.0 : 0.0));
                if (score > best_score) {
                    best_score = score; target_sector = sector; target_distance = distance; target_pod = pod;
                }
            }
        }
        if (target_sector < eco_sectors) {
            const double angle = (static_cast<double>(target_sector) + 0.5
                - static_cast<double>(eco_sectors) / 2.0)
                * config.fov_degrees * pi / (180.0 * static_cast<double>(eco_sectors));
            turn = config.max_turn_rate > 0.0
                ? std::clamp(angle / (config.max_turn_rate * 0.4), -1.0, 1.0) : 0.0;
            if (target_distance <= config.interaction_range
                && std::abs(angle) <= config.interaction_degrees * pi / 360.0) {
                action.forward = 0.0;
                action.forage = 1.0;
                action.call = config.communication && target_pod ? 0.5 : 0.0;
            }
        }
    }

    const double forward_obstacle = inputs[(eco_sectors / 2) * eco_sector_channels];
    const double obstacle_distance_ahead = (1.0 - forward_obstacle) * config.vision_range;
    if ((forward_obstacle > 0.0 && obstacle_distance_ahead < config.radius + 0.4)
        || inputs[contact_offset] > 0.0) {
        const double right_obstacle = inputs[0];
        const double left_obstacle = inputs[(eco_sectors - 1) * eco_sector_channels];
        turn = std::abs(left_obstacle - right_obstacle) < epsilon
            ? (rng.chance(0.5) ? 1.0 : -1.0)
            : (left_obstacle < right_obstacle ? 1.0 : -1.0);
        action.forward = 0.0;
    }
    action.left = std::max(turn, 0.0);
    action.right = std::max(-turn, 0.0);
    return action;
}

} // namespace

std::vector<double> EcosystemWorld::observe(std::size_t creature_index) const
{
    const EcoCreature& self = creatures.at(creature_index);
    std::vector<double> inputs(config.brain.input_count, 0.0);
    const double half_fov = config.fov_degrees * pi / 360.0;
    const double sector_width = 2.0 * half_fov / static_cast<double>(eco_sectors);
    std::array<double, eco_sectors> food_distances, creature_distances;
    std::array<double, eco_sectors> food_angles, creature_angles, meat_distances, meat_angles;
    meat_distances.fill(std::numeric_limits<double>::infinity());
    meat_angles.fill(std::numeric_limits<double>::infinity());
    food_distances.fill(std::numeric_limits<double>::infinity());
    creature_distances.fill(std::numeric_limits<double>::infinity());
    food_angles.fill(std::numeric_limits<double>::infinity());
    creature_angles.fill(std::numeric_limits<double>::infinity());

    const auto sense_obstacle = [&](const Box& box) {
        const Vec2 closest_delta = closest_point(self.position, box) - self.position;
        const double distance = length(closest_delta);
        const double contact_margin = std::max(1e-5, 0.2 * config.radius);
        if (distance <= config.radius + contact_margin) {
            inputs[contact_offset + quadrant_at(bearing(closest_delta, self.heading))] = 1.0;
        }
        if (distance > config.vision_range) return;
        for (std::size_t sector = 0; sector < eco_sectors; ++sector) {
            const double low = -half_fov + static_cast<double>(sector) * sector_width;
            const double visible_distance = obstacle_distance(
                self.position, self.heading, low, low + sector_width, box);
            inputs[sector * eco_sector_channels] = std::max(inputs[sector * eco_sector_channels],
                unit(1.0 - visible_distance / config.vision_range));
        }
    };

    // Taking the nearest wall point in a sector already accounts for occlusion:
    // any wall hiding it lies closer along the same direction in that sector.
    const double extent = std::max(config.vision_range, config.radius + 1e-5);
    const int x0 = std::max(0, static_cast<int>(std::floor(self.position.x - extent)));
    const int y0 = std::max(0, static_cast<int>(std::floor(self.position.y - extent)));
    const int x1 = std::min(static_cast<int>(config.width) - 1,
        static_cast<int>(std::floor(self.position.x + extent)));
    const int y1 = std::min(static_cast<int>(config.height) - 1,
        static_cast<int>(std::floor(self.position.y + extent)));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (terrain[static_cast<std::size_t>(y) * config.width + static_cast<std::size_t>(x)] == Terrain::Wall) {
                sense_obstacle({static_cast<double>(x), static_cast<double>(y),
                    static_cast<double>(x + 1), static_cast<double>(y + 1)});
            }
            if (config.extended_senses
                && terrain[static_cast<std::size_t>(y) * config.width + static_cast<std::size_t>(x)] == Terrain::Shelter) {
                const Vec2 target{static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5};
                const Vec2 delta = target - self.position;
                const double distance = length(delta), angle = bearing(delta, self.heading);
                if (distance <= config.vision_range && std::abs(angle) <= half_fov + epsilon
                    && line_of_sight(self.position, target)) {
                    auto& cue = inputs[eco_shelter_offset + sector_at(angle, half_fov)];
                    cue = std::max(cue, unit(1.0 - distance / config.vision_range));
                }
            }
        }
    }
    const double width = static_cast<double>(config.width);
    const double height = static_cast<double>(config.height);
    sense_obstacle({0.0, 0.0, 0.0, height});
    sense_obstacle({width, 0.0, width, height});
    sense_obstacle({0.0, 0.0, width, 0.0});
    sense_obstacle({0.0, height, width, height});

    for (const EcoResource& resource : resources) {
        const Vec2 delta = resource.position - self.position;
        const double distance = length(delta);
        const double angle = bearing(delta, self.heading);
        if (distance > config.vision_range || std::abs(angle) > half_fov + epsilon
            || !line_of_sight(self.position, resource.position)) continue;
        const std::size_t sector = sector_at(angle, half_fov);
        if (resource.kind == FoodKind::Meat) {
            if (!config.predation || resource.stock <= epsilon) continue;
            if (distance > meat_distances[sector] + epsilon
                || (std::abs(distance - meat_distances[sector]) <= epsilon && angle >= meat_angles[sector])) continue;
            meat_distances[sector] = distance; meat_angles[sector] = angle;
            inputs[eco_meat_offset + sector] = 1;
            inputs[eco_meat_offset + eco_sectors + sector] = unit(1 - distance / config.vision_range);
            const double reference = config.carcass_recovery
                * (config.body_energy_per_mass * eco_max_mass + config.energy_capacity) / config.meat_energy;
            inputs[eco_meat_offset + 2 * eco_sectors + sector] = unit(resource.stock / reference);
            continue;
        }
        if (config.extended_senses && (resource.stock <= epsilon
            || (resource.kind == FoodKind::Pod && resource.pod_state == PodState::Refilling))) {
            auto& depleted = inputs[eco_depleted_offset + sector];
            depleted = std::max(depleted, unit(1.0 - distance / config.vision_range));
            continue;
        }
        const std::size_t offset = sector * eco_sector_channels;
        if (config.typed_food_proximity && resource.stock > epsilon
            && !(resource.kind == FoodKind::Pod && resource.pod_state == PodState::Refilling)) {
            auto& proximity = inputs[offset + 3 + static_cast<std::size_t>(resource.kind)];
            proximity = std::max(proximity, unit(1.0 - distance / config.vision_range));
        }
        if (distance > food_distances[sector] + epsilon
            || (std::abs(distance - food_distances[sector]) <= epsilon && angle >= food_angles[sector])) continue;
        food_distances[sector] = distance;
        food_angles[sector] = angle;
        if (!config.typed_food_proximity)
            std::fill(inputs.begin() + static_cast<std::ptrdiff_t>(offset + 3),
                inputs.begin() + static_cast<std::ptrdiff_t>(offset + 7), 0.0);
        inputs[offset + 8] = inputs[offset + 9] = 0;
        inputs[offset + 1] = 1.0;
        inputs[offset + 2] = unit(1.0 - distance / config.vision_range);
        if (!config.typed_food_proximity) inputs[offset + 3 + static_cast<std::size_t>(resource.kind)] = 1.0;
        inputs[offset + 7] = resource.capacity > 0.0 ? unit(resource.stock / resource.capacity) : 0.0;
        if (resource.kind == FoodKind::Pod) {
            inputs[offset + 8] = resource.pod_state == PodState::Closed ? 1.0 : 0.0;
            inputs[offset + 9] = resource.pod_state == PodState::Open ? 1.0 : 0.0;
        }
    }

    for (std::size_t index = 0; index < creatures.size(); ++index) {
        if (index == creature_index) continue;
        const EcoCreature& other = creatures[index];
        const Vec2 delta = other.position - self.position;
        const double distance = length(delta);
        const double angle = bearing(delta, self.heading);
        const double contact_margin = std::max(1e-5, 0.2 * config.radius);
        if (distance <= 2.0 * config.radius + contact_margin) {
            inputs[contact_offset + quadrant_at(angle)] = 1.0;
        }
        if (distance > std::max(config.vision_range, config.hearing_range)
            || !line_of_sight(self.position, other.position)) continue;
        if (config.communication && distance <= config.hearing_range) {
            const std::size_t hearing = hearing_offset + quadrant_at(angle);
            inputs[hearing] = unit(inputs[hearing]
                + unit(other.action.call) * unit(1.0 - distance / config.hearing_range));
        }
        if (distance > config.vision_range || std::abs(angle) > half_fov + epsilon) continue;
        const std::size_t sector = sector_at(angle, half_fov);
        if (distance > creature_distances[sector] + epsilon
            || (std::abs(distance - creature_distances[sector]) <= epsilon && angle >= creature_angles[sector])) continue;
        creature_distances[sector] = distance;
        creature_angles[sector] = angle;
        const std::size_t offset = sector * eco_sector_channels;
        if (config.predation) {
            inputs[eco_other_mass_offset + sector] = unit(other.body.mass / eco_max_mass);
            inputs[eco_other_health_offset + sector] = unit(other.health / max_health(other));
        }
        inputs[offset + 10] = 1.0;
        inputs[offset + 11] = unit(1.0 - distance / config.vision_range);
        inputs[offset + 12] = unit(other.action.forage);
        inputs[offset + 13] = config.communication ? unit(other.action.call) : 0.0;
    }

    if (config.predation) {
        inputs[eco_health_offset] = unit(self.health / max_health(self));
        inputs[eco_health_offset + 1] = unit(self.damage_pulse / max_health(self));
    }
    inputs[body_offset] = unit(self.energy / config.energy_capacity);
    inputs[body_offset + 1] = config.max_speed > 0.0 ? unit(std::abs(self.speed) / maximum_speed(self)) : 0.0;
    inputs[body_offset + 2] = config.max_turn_rate > 0.0 ? unit(self.turn / config.max_turn_rate) : 0.0;
    inputs[body_offset + 3] = config.max_turn_rate > 0.0 ? unit(-self.turn / config.max_turn_rate) : 0.0;
    inputs[body_offset + 4] = sheltered(self.position) ? 1.0 : 0.0;
    inputs[eco_unsheltered_input] = 1.0 - inputs[body_offset + 4];
    inputs[body_offset + 5] = unit(storm_cue());
    inputs[body_offset + 6] = unit(self.ingestion_pulse / (config.ingestion_rate * config.dt));
    const double nutrition_scale = config.extended_senses
        ? std::max({config.graze_energy, config.poor_fruit_energy, config.rich_fruit_energy, config.pod_energy, config.predation ? config.meat_energy : 0.0}) : 12.0;
    inputs[body_offset + 7] = unit(self.digestion_pulse / (nutrition_scale * config.ingestion_rate * config.dt));
    inputs[body_offset + 8] = self.age < 0.2 - epsilon ? 1.0 : 0.0;
    return inputs;
}

EcoAction EcosystemWorld::control(std::size_t creature_index)
{
    EcoCreature& creature = creatures.at(creature_index);
    const std::vector<double> inputs = observe(creature_index);
    creature.step_spikes = 0;
    if (creature.controller != ControllerKind::Spiking) {
        return baseline_action(inputs, config, creature.controller, creature.neural_rng, creature.body.carnivory);
    }
    const BrainConfig& brain_config = creature.brain.config();
    if (brain_config.input_count != config.brain.input_count || brain_config.output_count != config.brain.output_count) {
        throw std::invalid_argument("Ecosystem brain does not match the world's sensory interface");
    }
    const double ratio = config.dt / brain_config.dt;
    if (!std::isfinite(ratio) || ratio < 1.0 || ratio > 10000.0 || std::abs(ratio - std::round(ratio)) > 1e-8) {
        throw std::invalid_argument("World dt must be an integer multiple of each creature's brain dt");
    }
    const std::size_t substeps = static_cast<std::size_t>(std::llround(ratio));
    BrainStepResult result;
    std::vector<double> neural_inputs = inputs;
    for (std::size_t step = 0; step < substeps; ++step) {
        neural_inputs[body_offset + 8] = creature.age + static_cast<double>(step) * brain_config.dt < 0.2 - epsilon ? 1.0 : 0.0;
        result = creature.brain.step(neural_inputs, &creature.neural_rng);
        creature.step_spikes += result.spikes;
    }
    creature.spikes += creature.step_spikes;
    const double normalizer = brain_config.calibrated_io ? std::tanh(config.motor_gain) : 1.0;
    const auto output = [&](std::size_t index) { return unit(std::tanh(config.motor_gain * result.motor_outputs[index]) / normalizer); };
    const double turn = std::clamp(std::tanh(config.motor_gain * (result.motor_outputs[1] - result.motor_outputs[2])) / normalizer, -1.0, 1.0);
    EcoAction action;
    action.forward = output(0);
    action.left = unit(turn);
    action.right = unit(-turn);
    action.attack = config.predation ? output(5) : 0;
    action.forage = output(3);
    action.call = config.communication ? output(4) : 0.0;
    if (config.actuator_tau > 0) {
        const double alpha = -std::expm1(-config.dt / config.actuator_tau);
        const auto smooth = [alpha](double previous, double desired) { return previous + alpha * (desired - previous); };
        action.forward = smooth(creature.action.forward, action.forward);
        action.forage = smooth(creature.action.forage, action.forage);
        action.attack = config.predation ? smooth(creature.action.attack, action.attack) : 0;
        action.call = config.communication ? smooth(creature.action.call, action.call) : 0;
        const double smoothed_turn = smooth(creature.action.left - creature.action.right, turn);
        action.left = unit(smoothed_turn);
        action.right = unit(-smoothed_turn);
    }
    return action;
}

const Brain::InputGroups& ecosystem_input_groups(bool extended, bool predation)
{
    const auto build = [](bool include_extended, bool include_predation) {
        Brain::InputGroups groups;
        for (std::size_t channel = 0; channel < eco_sector_channels; ++channel) {
            std::vector<std::size_t> group;
            for (std::size_t sector = 0; sector < eco_sectors; ++sector)
                group.push_back(sector * eco_sector_channels + channel);
            groups.push_back(std::move(group));
        }
        const auto hearing = eco_sectors * eco_sector_channels;
        groups.push_back({hearing, hearing + 1, hearing + 2, hearing + 3});
        groups.push_back({hearing + 4, hearing + 5, hearing + 6, hearing + 7});
        for (std::size_t input = hearing + 8; input < eco_legacy_input_count; ++input)
            groups.push_back({input});
        if (include_extended) for (const auto offset : {eco_depleted_offset, eco_shelter_offset}) {
            std::vector<std::size_t> group;
            for (std::size_t sector = 0; sector < eco_sectors; ++sector) group.push_back(offset + sector);
            groups.push_back(std::move(group));
        }
        if (include_predation) {
            for (const auto offset : {eco_meat_offset, eco_meat_offset + eco_sectors,
                    eco_meat_offset + 2 * eco_sectors, eco_other_mass_offset, eco_other_health_offset}) {
                std::vector<std::size_t> group;
                for (std::size_t sector = 0; sector < eco_sectors; ++sector) group.push_back(offset + sector);
                groups.push_back(std::move(group));
            }
            groups.push_back({eco_health_offset});
            groups.push_back({eco_health_offset + 1});
        }
        return groups;
    };
    static const auto legacy = build(false, false), current = build(true, false), combat = build(true, true);
    return predation ? combat : extended ? current : legacy;
}

std::vector<std::string> ecosystem_input_labels(bool extended, bool predation, bool typed_food_proximity)
{
    constexpr const char* channels[] = {"obstacle_proximity", "food_present", "food_proximity",
        "food_graze", "food_fruit_a", "food_fruit_b", "food_pod", "food_stock",
        "pod_closed_ready", "pod_open", "creature_present", "creature_proximity",
        "creature_forage", "creature_call"};
    std::vector<std::string> labels;
    labels.reserve(eco_input_count);
    for (std::size_t sector = 0; sector < eco_sectors; ++sector) {
        for (const char* channel : channels) {
            std::string name = channel;
            if (typed_food_proximity && (name == "food_graze" || name == "food_fruit_a"
                || name == "food_fruit_b" || name == "food_pod")) name += "_proximity";
            labels.push_back("vision_" + std::to_string(sector) + "_" + name);
        }
    }
    for (const char* direction : {"front", "left", "back", "right"}) labels.push_back(std::string("hearing_") + direction);
    for (const char* direction : {"front", "left", "back", "right"}) labels.push_back(std::string("contact_") + direction);
    for (const char* channel : {"energy", "speed", "turn_left", "turn_right", "sheltered",
        "storm_cue", "ingestion", "digestion_gain", "episode_start", "unsheltered"}) labels.emplace_back(channel);
    if (extended) {
        for (std::size_t sector = 0; sector < eco_sectors; ++sector)
            labels.push_back("vision_" + std::to_string(sector) + "_depleted_food_proximity");
        for (std::size_t sector = 0; sector < eco_sectors; ++sector)
            labels.push_back("vision_" + std::to_string(sector) + "_shelter_proximity");
    }
    if (predation) {
        for (const char* channel : {"meat_present", "meat_proximity", "meat_amount", "creature_mass", "creature_health"})
            for (std::size_t sector = 0; sector < eco_sectors; ++sector)
                labels.push_back("vision_" + std::to_string(sector) + "_" + channel);
        labels.push_back("health"); labels.push_back("damage");
    }
    return labels;
}

} // namespace neuroevo
