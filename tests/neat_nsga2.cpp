#include "neuroevo/neat.hpp"
#include "neuroevo/nsga2.hpp"
#include "neuroevo/objectives.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

bool close(double lhs, double rhs, double epsilon = 1e-9)
{
    return std::abs(lhs - rhs) <= epsilon;
}

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

bool has_index(const std::vector<std::size_t>& values, std::size_t target)
{
    return std::find(values.begin(), values.end(), target) != values.end();
}

neuroevo::NodeGene node(std::size_t id, neuroevo::NodeKind kind)
{
    return {id, kind, {0.5, 0.5}, 0.0, 1.0};
}

neuroevo::ConnectionGene connection(
    std::size_t innovation,
    std::size_t source,
    std::size_t target,
    double weight,
    bool enabled = true)
{
    return {innovation, source, target, weight, enabled, 1};
}

} // namespace

int main()
{
    {
        neuroevo::EnvironmentConfig env;
        env.episode_steps = 10;
        env.brain_steps_per_env_step = 2;
        env.food_reward = 10.0;

        neuroevo::ObjectiveConfig objectives;
        objectives.target_foods_per_trial = 3.0;
        objectives.target_spike_rate = 0.10;
        objectives.synapse_budget = 12.0;
        objectives.neuron_budget = 20.0;

        neuroevo::EvaluationResult evaluation;
        evaluation.foods_collected = 1.5;
        evaluation.reward = 20.0;
        evaluation.penalty = 1.0;
        evaluation.fitness = 19.0;
        evaluation.spikes = 20.0;

        const neuroevo::GenomeComplexity complexity{10, 2, 6, 1};
        const auto metrics = neuroevo::make_evaluation_metrics(evaluation, complexity, objectives, env);

        if (!close(metrics.task_score_norm, 0.5 + 0.1 * (5.0 / 30.0))) {
            return fail("Unexpected normalized task score");
        }
        if (!close(metrics.mean_spike_rate, 0.10)) {
            return fail("Unexpected mean spike rate");
        }
        if (!close(metrics.spike_energy_norm, 0.0)) {
            return fail("At-budget spike activity should not create normalized energy cost");
        }
        if (!close(metrics.synapse_count_norm, 0.0)) {
            return fail("Below-budget synapse count should not create normalized structural cost");
        }
        if (!close(metrics.neuron_count_norm, 0.0)) {
            return fail("Below-budget neuron count should not create normalized structural cost");
        }

        const neuroevo::GenomeComplexity above_budget_complexity{25, 2, 18, 1};
        evaluation.spikes = 100.0;
        const auto above_budget_metrics = neuroevo::make_evaluation_metrics(
            evaluation,
            above_budget_complexity,
            objectives,
            env);
        if (!close(above_budget_metrics.mean_spike_rate, 0.20)) {
            return fail("Unexpected above-budget mean spike rate");
        }
        if (!close(above_budget_metrics.spike_energy_norm, 1.0)) {
            return fail("Spike energy should measure excess over the target rate");
        }
        if (!close(above_budget_metrics.synapse_count_norm, 0.5)) {
            return fail("Synapse cost should measure excess over the synapse budget");
        }
        if (!close(above_budget_metrics.neuron_count_norm, 0.25)) {
            return fail("Neuron cost should measure excess over the neuron budget");
        }
    }

    {
        const std::vector<neuroevo::ObjectiveDescriptor> objectives{
            {"score", neuroevo::ObjectiveDirection::Maximize},
            {"cost", neuroevo::ObjectiveDirection::Minimize},
        };
        const std::vector<std::vector<double>> values{
            {1.0, 1.0},
            {2.0, 2.0},
            {2.0, 1.0},
            {0.0, 0.0},
        };

        if (!neuroevo::dominates(values[2], values[0], objectives)) {
            return fail("Expected genome 2 to dominate genome 0");
        }
        if (neuroevo::dominates(values[0], values[2], objectives)) {
            return fail("Dominance direction is wrong");
        }

        const auto fronts = neuroevo::non_dominated_sort(values, objectives);
        if (fronts.empty() || fronts[0].size() != 2 || !has_index(fronts[0], 2) || !has_index(fronts[0], 3)) {
            return fail("Unexpected first Pareto front");
        }
    }

    {
        const std::vector<neuroevo::ObjectiveDescriptor> objectives{
            {"a", neuroevo::ObjectiveDirection::Minimize},
            {"b", neuroevo::ObjectiveDirection::Minimize},
        };
        const std::vector<std::vector<double>> values{
            {0.0, 0.0},
            {0.5, 0.5},
            {1.0, 1.0},
        };
        const std::vector<std::size_t> front{0, 1, 2};
        const auto distances = neuroevo::compute_crowding_distance(values, front, objectives);
        if (!std::isinf(distances[0]) || !std::isinf(distances[2]) || !close(distances[1], 2.0)) {
            return fail("Unexpected crowding distance behavior");
        }
    }

    {
        neuroevo::Genome parent1;
        parent1.nodes = {node(0, neuroevo::NodeKind::Input), node(1, neuroevo::NodeKind::Output), node(2, neuroevo::NodeKind::Hidden)};
        parent1.connections = {connection(1, 0, 1, 1.0), connection(2, 0, 2, 0.5)};
        parent1.pareto_rank = 0;
        parent1.crowding_distance = 2.0;
        parent1.metrics.task_score_norm = 1.0;

        neuroevo::Genome parent2;
        parent2.nodes = {node(0, neuroevo::NodeKind::Input), node(1, neuroevo::NodeKind::Output), node(3, neuroevo::NodeKind::Hidden)};
        parent2.connections = {connection(1, 0, 1, 2.0), connection(3, 3, 1, 0.25)};
        parent2.pareto_rank = 1;
        parent2.metrics.task_score_norm = 0.5;

        neuroevo::Random rng(42);
        const auto child = neuroevo::crossover_genomes(parent1, parent2, {}, rng);
        std::set<std::size_t> innovations;
        for (const auto& gene : child.connections) {
            innovations.insert(gene.innovation_id);
            if (child.find_node(gene.source_node_id) == nullptr || child.find_node(gene.target_node_id) == nullptr) {
                return fail("Crossover produced a connection with a missing endpoint");
            }
        }
        if (!innovations.count(1) || !innovations.count(2) || innovations.count(3)) {
            return fail("Crossover did not align/inherit genes by innovation as expected");
        }
    }

    {
        neuroevo::BrainConfig brain;
        brain.input_count = 1;
        brain.output_count = 1;
        brain.seed_input_output_synapses = true;
        neuroevo::InnovationTracker tracker(2);
        neuroevo::Random rng(7);
        auto genome = neuroevo::make_minimal_genome(brain, tracker, rng);

        if (!neuroevo::mutate_add_node(genome, tracker, rng)) {
            return fail("Add-node mutation failed on an enabled connection");
        }
        if (genome.hidden_node_count() != 1 || genome.connections.size() != 3) {
            return fail("Add-node mutation did not split into one hidden node and three connection genes");
        }
        if (genome.enabled_connection_count() != 2 || genome.disabled_connection_count() != 1) {
            return fail("Add-node mutation did not disable old connection and enable split connections");
        }
    }

    {
        neuroevo::BrainConfig brain;
        brain.input_count = 4;
        brain.output_count = 3;
        brain.seed_input_output_synapses = true;
        brain.initial_connection_probability = 0.0;

        neuroevo::InnovationTracker tracker(7);
        neuroevo::Random rng(5);
        const auto genome = neuroevo::make_minimal_genome(brain, tracker, rng);
        std::set<std::pair<std::size_t, std::size_t>> endpoints;
        for (const auto& gene : genome.connections) {
            endpoints.insert({gene.source_node_id, gene.target_node_id});
        }

        const std::set<std::pair<std::size_t, std::size_t>> expected{
            {0, 4},
            {3, 4},
            {1, 5},
            {2, 6},
        };
        if (endpoints != expected) {
            return fail("Directional-FOV minimal genome should use structured sensory-to-motor seed wiring");
        }
    }

    {
        neuroevo::BrainConfig brain;
        brain.input_count = 5;
        brain.output_count = 3;
        brain.has_clock_input = true;
        brain.clock_input_index = 4;
        brain.seed_input_output_synapses = true;
        brain.initial_connection_probability = 0.0;

        neuroevo::InnovationTracker tracker(8);
        neuroevo::Random rng(5);
        auto genome = neuroevo::make_minimal_genome(brain, tracker, rng);
        std::set<std::pair<std::size_t, std::size_t>> endpoints;
        for (const auto& gene : genome.connections) {
            if (gene.enabled) {
                endpoints.insert({gene.source_node_id, gene.target_node_id});
            }
        }

        const std::set<std::pair<std::size_t, std::size_t>> expected{
            {0, 5},
            {3, 5},
            {1, 6},
            {2, 7},
        };
        if (endpoints != expected) {
            return fail("Clock-enabled directional-FOV genome should seed sensory-motor wiring without clock outputs");
        }

        for (auto& gene : genome.connections) {
            gene.enabled = false;
        }
        neuroevo::repair_io_connectivity(genome, brain, tracker, rng);

        for (const auto& node : genome.nodes) {
            if (node.kind == neuroevo::NodeKind::Input && node.id != brain.clock_input_index) {
                bool has_outgoing = false;
                for (const auto& gene : genome.connections) {
                    has_outgoing = has_outgoing || (gene.enabled && gene.source_node_id == node.id);
                }
                if (!has_outgoing) {
                    return fail("I/O repair left an input without enabled outgoing connection");
                }
            }
            if (node.kind == neuroevo::NodeKind::Input && node.id == brain.clock_input_index) {
                for (const auto& gene : genome.connections) {
                    if (gene.enabled && gene.source_node_id == node.id) {
                        return fail("I/O repair should not force clock input connections");
                    }
                }
            }
            if (node.kind == neuroevo::NodeKind::Output) {
                bool has_incoming = false;
                for (const auto& gene : genome.connections) {
                    has_incoming = has_incoming || (gene.enabled && gene.target_node_id == node.id);
                }
                if (!has_incoming) {
                    return fail("I/O repair left an output without enabled incoming connection");
                }
            }
        }

        const auto old_clock_node = std::find_if(
            genome.nodes.begin(),
            genome.nodes.end(),
            [&](const neuroevo::NodeGene& node) { return node.id == brain.clock_input_index; });
        if (old_clock_node == genome.nodes.end()) {
            return fail("Clock input node is missing from clock-enabled genome");
        }
        const double old_clock_threshold = old_clock_node->threshold;
        neuroevo::NeatMutationConfig mutation;
        mutation.mutate_weight_probability = 0.0;
        mutation.mutate_node_probability = 1.0;
        mutation.clock_threshold_sigma = 0.5;
        mutation.add_connection_probability = 0.0;
        mutation.add_node_probability = 0.0;
        mutation.enable_disable_probability = 0.0;
        neuroevo::mutate_genome(genome, brain, mutation, tracker, rng);
        const auto clock_node = std::find_if(
            genome.nodes.begin(),
            genome.nodes.end(),
            [&](const neuroevo::NodeGene& node) { return node.id == brain.clock_input_index; });
        if (clock_node == genome.nodes.end() || close(clock_node->threshold, old_clock_threshold)) {
            return fail("Clock input threshold should be evolvable");
        }
    }

    {
        neuroevo::BrainConfig brain;
        brain.input_count = 2;
        brain.output_count = 1;
        brain.seed_input_output_synapses = false;
        brain.initial_connection_probability = 0.0;
        neuroevo::InnovationTracker tracker(3);
        neuroevo::Random rng(11);
        auto genome = neuroevo::make_minimal_genome(brain, tracker, rng, 1);

        for (int i = 0; i < 16; ++i) {
            neuroevo::mutate_add_connection(genome, brain, {}, tracker, rng);
        }

        std::set<std::pair<std::size_t, std::size_t>> endpoints;
        for (const auto& gene : genome.connections) {
            const auto inserted = endpoints.insert({gene.source_node_id, gene.target_node_id});
            if (!inserted.second) {
                return fail("Add-connection mutation created a duplicate connection");
            }
        }
    }

    {
        neuroevo::Genome a;
        a.nodes = {node(0, neuroevo::NodeKind::Input), node(1, neuroevo::NodeKind::Output)};
        a.connections = {connection(1, 0, 1, 1.0), connection(2, 0, 1, 0.5)};

        neuroevo::Genome b;
        b.nodes = a.nodes;
        b.connections = {connection(1, 0, 1, 1.5), connection(3, 0, 1, 0.25)};

        neuroevo::SpeciationConfig config;
        config.c1 = 1.0;
        config.c2 = 1.0;
        config.c3 = 0.4;
        if (!close(neuroevo::compatibility_distance(a, b, config), 1.2)) {
            return fail("Unexpected compatibility distance");
        }

        std::vector<neuroevo::Genome> population{a, b};
        neuroevo::Speciator high_threshold({2.0, 1.0, 1.0, 0.4});
        high_threshold.assign_species(population);
        if (high_threshold.species().size() != 1 || population[0].species_id == 0 || population[1].species_id == 0) {
            return fail("High compatibility threshold should group genomes into one species");
        }

        neuroevo::Speciator low_threshold({0.1, 1.0, 1.0, 0.4});
        low_threshold.assign_species(population);
        if (low_threshold.species().size() != 2) {
            return fail("Low compatibility threshold should split genomes into two species");
        }
    }

    {
        neuroevo::BrainConfig brain;
        brain.input_count = 1;
        brain.output_count = 1;
        brain.seed_input_output_synapses = true;

        neuroevo::InnovationTracker tracker1(2);
        neuroevo::InnovationTracker tracker2(2);
        neuroevo::Random rng1(123);
        neuroevo::Random rng2(123);
        auto genome1 = neuroevo::make_minimal_genome(brain, tracker1, rng1);
        auto genome2 = neuroevo::make_minimal_genome(brain, tracker2, rng2);

        neuroevo::mutate_add_node(genome1, tracker1, rng1);
        neuroevo::mutate_add_node(genome2, tracker2, rng2);
        if (genome1.nodes.size() != genome2.nodes.size() || genome1.connections.size() != genome2.connections.size()) {
            return fail("Deterministic add-node mutation changed genome sizes");
        }
        for (std::size_t i = 0; i < genome1.connections.size(); ++i) {
            if (genome1.connections[i].innovation_id != genome2.connections[i].innovation_id
                || genome1.connections[i].source_node_id != genome2.connections[i].source_node_id
                || genome1.connections[i].target_node_id != genome2.connections[i].target_node_id) {
                return fail("Add-node mutation is not deterministic for a fixed seed");
            }
        }
    }

    return 0;
}
