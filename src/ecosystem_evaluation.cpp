#include "neuroevo/ecosystem.hpp"

#include <algorithm>
#include <atomic>
#include <future>
#include <cmath>
#include <stdexcept>

namespace neuroevo {

NewbornEvaluation EcosystemWorld::evaluate_newborn(const Brain& genome) const
{
    if (genome.config().input_count != config.brain.input_count
        || genome.config().output_count != eco_output_count || genome.config().dt != config.brain.dt)
        throw std::invalid_argument("Newborn evaluation requires a compatible ecosystem genome");
    if (evaluation_workers == 0 || evaluation_workers > 32)
        throw std::invalid_argument("Evaluation worker count must be 1..32");
    const auto run_trial = [&](std::size_t trial) {

        EcosystemConfig settings = config;
        // No live-world state or RNG is consumed. Every candidate sees the same
        // maps, placements, headings, weather phases and neural random streams.
        settings.seed = static_cast<std::uint64_t>(config.archive_eval_seed) + 104729ULL * trial;
        settings.initial_creatures = 0;
        settings.max_population = std::min<std::size_t>(32, std::max<std::size_t>(2, config.max_population));
        settings.establishment = false;
        settings.immigration_floor = 0;
        settings.archive_eval_trials = 0;
        settings.reproduction = true;
        settings.controller = ControllerKind::Spiking;
        settings.brain = genome.config();
        settings.phase_offset = (static_cast<double>(trial) + 0.5)
            / static_cast<double>(config.archive_eval_trials)
            * (settings.calm_duration + settings.warning_duration + settings.storm_duration);
        // Descendants test this genome's lineage, without mutation confounds.
        settings.mutation.mutate_weight_probability = 0;
        settings.mutation.mutate_neuron_probability = 0;
        settings.mutation.add_synapse_probability = 0;
        settings.mutation.add_neuron_probability = 0;
        settings.mutation.add_reciprocal_motif_probability = 0;
        settings.mutation.remove_synapse_probability = 0;
        settings.mutation.mutate_clock_threshold_probability = 0;
        EcosystemWorld world(settings);
        std::vector<Vec2> locations;
        for (std::size_t y = 0; y < settings.height; ++y)
            for (std::size_t x = 0; x < settings.width; ++x) {
                const Vec2 p{static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5};
                if (world.traversable(p)) locations.push_back(p);
            }
        if (locations.empty()) throw std::runtime_error("No valid newborn evaluation placement");
        Random placement(settings.seed ^ 0x706c616365ULL);
        EcoCreature newborn;
        newborn.id = newborn.genome_id = 1;
        newborn.position = locations[placement.uniform_index(locations.size())];
        newborn.heading = placement.uniform(-3.141592653589793, 3.141592653589793);
        newborn.energy = settings.offspring_energy;
        newborn.brain = genome;
        newborn.brain.reset_state();
        newborn.neural_rng = Random(settings.seed ^ 0x6e657572616cULL);
        world.creatures.push_back(std::move(newborn));
        world.next_creature_id = 2;

        NewbornTrial result;
        result.seed = settings.seed;
        const auto steps = static_cast<std::size_t>(std::ceil(config.archive_eval_seconds / settings.dt));
        for (std::size_t step = 0; step < steps && !world.creatures.empty() && !world.capacity_limited; ++step) {
            world.step();
            for (const auto& event : world.events) {
                if (event.type == "birth" && event.other == 1) {
                    ++result.offspring;
                    if (result.first_birth < 0) result.first_birth = event.time;
                    else if (result.second_birth < 0) result.second_birth = event.time;
                }
                if (event.type == "maturation" && event.creature == 1) result.matured = true;
                if (event.type == "death" && event.creature == 1) result.focal_age = world.time();
            }
        }
        result.elapsed = world.time();
        result.focal_alive = std::any_of(world.creatures.begin(), world.creatures.end(),
            [](const EcoCreature& c) { return c.id == 1; });
        if (result.focal_alive) result.focal_age = result.elapsed;
        result.capacity_limited = world.capacity_limited;
        result.descendant_births = world.totals.descendant_births;
        result.mature_offspring = world.totals.mature_offspring;
        result.food_energy = world.totals.energy_gained;
        const auto& t = world.totals;
        result.operating_energy = t.metabolism + t.movement + t.turning + t.foraging + t.calling + t.neural + t.exposure;
        return result;
    };
    NewbornEvaluation evaluation;
    evaluation.trials.resize(config.archive_eval_trials);
    const auto workers = std::min(evaluation_workers, config.archive_eval_trials);
    if (workers <= 1) {
        for (std::size_t trial = 0; trial < evaluation.trials.size(); ++trial)
            evaluation.trials[trial] = run_trial(trial);
    } else {
        // Workers write disjoint slots; RNGs and entire trial worlds are private.
        // Dynamic assignment avoids waiting on a batch containing a long family.
        std::atomic<std::size_t> next{0};
        const auto work = [&]() {
            for (;;) {
                const auto trial = next.fetch_add(1, std::memory_order_relaxed);
                if (trial >= evaluation.trials.size()) return;
                evaluation.trials[trial] = run_trial(trial);
            }
        };
        std::vector<std::future<void>> tasks;
        for (std::size_t worker = 1; worker < workers; ++worker)
            tasks.push_back(std::async(std::launch::async, work));
        work();
        for (auto& task : tasks) task.get();
    }
    // Accumulate in trial order, independent of scheduling, for exact replay.
    for (const auto& result : evaluation.trials) {
        // Breeding descendants dominate; repeated reproduction and mature
        // survival provide intermediate evidence. Use coverage, not terminal
        // net energy: all nonbreeding creatures that starve have the same net
        // loss of their birth reserve, even when their feeding abilities differ.
        evaluation.score += (result.descendant_births > 0 ? 100.0 : 0.0)
            + (result.offspring > 0 ? 10.0 : 0.0) + std::log1p(static_cast<double>(result.offspring))
            + (result.matured ? 0.1 : 0.0)
            + 0.05 * std::min(1.0, result.focal_age / config.archive_eval_seconds)
            + 0.01 * std::clamp(result.food_energy / std::max(1.0, result.operating_energy), 0.0, 2.0);
    }
    if (!evaluation.trials.empty()) evaluation.score /= static_cast<double>(evaluation.trials.size());
    return evaluation;
}

} // namespace neuroevo
