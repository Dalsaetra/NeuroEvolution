#include "fixtures.hpp"
#include "../src/ecosystem_spatial.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace neuroevo;

static std::string state(const EcosystemWorld& world)
{
    std::ostringstream out;
    world.save_checkpoint(out);
    // Events are not part of a checkpoint; compare these and neural observations too.
    write_ecosystem_frame(out, world, true, true, true);
    return out.str();
}

int main()
{
    try {
        for (auto model : {NeuronModel::Lif, NeuronModel::FilteredLif, NeuronModel::Izhikevich}) {
            auto config = controlled_config();
            config.set_predation(model != NeuronModel::Lif);
            config.width = config.height = 32;
            config.shelters = 2;
            config.initial_creatures = 64;
            config.max_population = 100;
            config.brain.neuron_model = model;
            config.brain.dt = model == NeuronModel::Izhikevich ? 0.001 : 0.005;
            config.sparse_ancestor = false;
            config.maturity_age = 0;
            config.reproduction_threshold = 100;
            config.founder_energy = 240;
            config.reproduction_cooldown = 0.5;
            config.storms_enabled = true;
            config.calm_duration = 1;
            config.warning_duration = 1;
            config.storm_duration = 1;
            EcosystemWorld original(config);
            // Exercise baseline controllers' private random streams as well.
            original.creatures[0].controller = ControllerKind::Random;
            original.creatures[1].controller = ControllerKind::Reactive;
            original.creatures[2].energy = -1; // Guaranteed death during the first step.
            original.spatial_index = false;
            for (int workers : {1, 2, 4, 8, 12}) {
                auto serial = original;
                auto parallel = original;
                parallel.worker_threads = workers;
                parallel.spatial_index = true;
                for (int step = 0; step < 45; ++step) {
                    serial.step();
                    parallel.step();
                    if (serial.events.size() != parallel.events.size())
                        throw std::runtime_error("Parallel event count changed");
                    for (std::size_t i = 0; i < serial.events.size(); ++i) {
                        const auto& a = serial.events[i];
                        const auto& b = parallel.events[i];
                        if (a.time != b.time || a.type != b.type || a.creature != b.creature
                            || a.other != b.other || a.resource != b.resource || a.amount != b.amount)
                            throw std::runtime_error("Parallel event order or content changed");
                    }
                    if ((step % 10 == 0 || step == 44) && state(serial) != state(parallel))
                        throw std::runtime_error("Parallel/spatial simulation changed exact state or recording");
                    if (step == 20) {
                        std::ostringstream checkpoint;
                        parallel.save_checkpoint(checkpoint);
                        std::istringstream input(checkpoint.str());
                        parallel = EcosystemWorld::load_checkpoint(input);
                        parallel.worker_threads = workers;
                    }
                }
                if (!serial.totals.births || !serial.totals.deaths)
                    throw std::runtime_error("Equivalence fixture must exercise births and deaths");
            }
        }
        // Dense, simultaneous movement with repeated collision rejection.
        auto config = controlled_config();
        config.set_predation(true);
        config.initial_creatures = 64;
        config.width = config.height = 32;
        config.shelters = 2;
        EcosystemWorld serial(config), indexed = serial;
        // Pack an open area tightly to force path crossings and rejection chains.
        std::fill(serial.terrain.begin(), serial.terrain.end(), Terrain::Ground);
        for (std::size_t i = 0; i < serial.creatures.size(); ++i) {
            serial.creatures[i].position = {5 + (2 * config.radius + 0.02) * (i % 8),
                                           5 + (2 * config.radius + 0.02) * (i / 8)};
        }
        indexed = serial;
        serial.spatial_index = false;
        Random random(435);
        for (int step = 0; step < 100; ++step) {
            std::vector<EcoAction> actions(serial.creatures.size());
            for (auto& action : actions) {
                action.forward = random.uniform(0, 1);
                action.left = random.uniform(0, 1);
                action.right = random.uniform(0, 1);
                action.attack = random.uniform(0, 1);
            }
            serial.step(actions);
            indexed.step(actions);
            if ((step % 10 == 0 || step == 99) && state(serial) != state(indexed))
                throw std::runtime_error("Spatial movement changed exact state");
        }
        // An exception in a worker must reach the caller instead of terminating.
        indexed = EcosystemWorld(config);
        indexed.worker_threads = 4;
        auto bad_brain = indexed.config.brain;
        bad_brain.input_count = 1;
        indexed.creatures[0].brain = Brain(bad_brain);
        bool caught = false;
        try { indexed.step(); } catch (const std::invalid_argument&) { caught = true; }
        if (!caught) throw std::runtime_error("Worker exception was lost");

        // Broad-phase range boundaries and stable candidate ordering, including
        // negative coordinates and exact radius boundaries.
        std::vector<Vec2> positions{{-1, 0}, {1, 0}, {0, 1}, {0, -1}, {0, 0}};
        for (int i = 0; i < 200; ++i)
            positions.push_back({random.uniform(-20, 20), random.uniform(-20, 20)});
        const detail::CreatureIndex index(positions);
        for (auto center : positions) for (double radius : {0.0, 0.4, 1.0, 8.0}) {
            const auto nearby = index.nearby(center, radius);
            if (!std::is_sorted(nearby.begin(), nearby.end())) throw std::runtime_error("Unstable spatial order");
            for (std::size_t i = 0; i < positions.size(); ++i)
                if (length(positions[i] - center) <= radius
                    && !std::binary_search(nearby.begin(), nearby.end(), i))
                    throw std::runtime_error("Spatial index excluded an exact neighbour");
        }
        std::cout << "Exact parallel/spatial state, events, brain recordings, resume and exceptions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
