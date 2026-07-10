#include "neuroevo/neat.hpp"

#include "neuroevo/nsga2.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace neuroevo {
namespace {

bool node_id_less(const NodeGene& lhs, const NodeGene& rhs)
{
    return lhs.id < rhs.id;
}

bool connection_innovation_less(const ConnectionGene& lhs, const ConnectionGene& rhs)
{
    return lhs.innovation_id < rhs.innovation_id;
}

double random_weight(Random& rng)
{
    const double sign = rng.chance(0.82) ? 1.0 : -1.0;
    return sign * std::exp(rng.normal(-0.25, 0.55));
}

bool is_directional_fov_shape(const BrainConfig& config)
{
    const std::size_t auxiliary_count = (config.has_clock_input ? 1 : 0)
        + (config.has_episode_start_input ? 1 : 0);
    const std::size_t sensory_count = config.sensory_input_count > 0
        ? config.sensory_input_count
        : config.input_count - std::min(config.input_count, auxiliary_count);
    return sensory_count == 4 && config.output_count == 3;
}

bool is_clock_input(const BrainConfig& config, std::size_t node_id)
{
    return config.has_clock_input
        && node_id == config.clock_input_index
        && node_id < config.input_count;
}

bool is_episode_start_input(const BrainConfig& config, std::size_t node_id)
{
    return config.has_episode_start_input
        && node_id == config.episode_start_input_index
        && node_id < config.input_count;
}

bool is_auxiliary_input(const BrainConfig& config, std::size_t node_id)
{
    return is_clock_input(config, node_id) || is_episode_start_input(config, node_id);
}

bool is_directional_fov_seed_pair(
    const BrainConfig& config,
    const NodeGene& source,
    const NodeGene& target)
{
    if (source.kind != NodeKind::Input || target.kind != NodeKind::Output) {
        return false;
    }

    const std::size_t output = target.id - config.input_count;
    return (source.id == 0 && output == 0)
        || (source.id == 3 && output == 0)
        || (source.id == 1 && output == 1)
        || (source.id == 2 && output == 2);
}

double directional_fov_seed_weight(const BrainConfig& config, const NodeGene&, const NodeGene& target)
{
    const std::size_t output = target.id - config.input_count;
    return config.seed_input_output_weight * (output == 1 || output == 2 ? 2.0 : 1.0);
}

const NodeGene& require_node(const Genome& genome, std::size_t node_id)
{
    const NodeGene* node = genome.find_node(node_id);
    if (node == nullptr) {
        throw std::invalid_argument("Genome connection references a missing node");
    }
    return *node;
}

void add_node_if_missing(
    std::map<std::size_t, NodeGene>& nodes_by_id,
    const Genome& preferred,
    const Genome& fallback,
    std::size_t node_id)
{
    if (nodes_by_id.find(node_id) != nodes_by_id.end()) {
        return;
    }

    if (const NodeGene* node = preferred.find_node(node_id)) {
        nodes_by_id.emplace(node_id, *node);
        return;
    }
    if (const NodeGene* node = fallback.find_node(node_id)) {
        nodes_by_id.emplace(node_id, *node);
        return;
    }

    throw std::invalid_argument("Cannot inherit connection endpoint without a node gene");
}

std::map<std::size_t, ConnectionGene> connection_map(const Genome& genome)
{
    std::map<std::size_t, ConnectionGene> result;
    for (const auto& connection : genome.connections) {
        result[connection.innovation_id] = connection;
    }
    return result;
}

bool is_better_parent(const Genome& lhs, const Genome& rhs, Random& rng)
{
    const int comparison = compare_nsga2(
        lhs.pareto_rank,
        lhs.crowding_distance,
        lhs.metrics.task_score_norm,
        rhs.pareto_rank,
        rhs.crowding_distance,
        rhs.metrics.task_score_norm);
    if (comparison == 0) {
        return rng.chance(0.5);
    }
    return comparison < 0;
}

void reset_genome_metadata(Genome& genome)
{
    genome.id = 0;
    genome.species_id = 0;
    genome.age = 0;
    genome.metrics = {};
    genome.objectives.clear();
    genome.pareto_rank = std::numeric_limits<std::size_t>::max();
    genome.crowding_distance = 0.0;
    genome.scalar_display_score = 0.0;
}

ConnectionGene* find_connection(Genome& genome, std::size_t source_node_id, std::size_t target_node_id)
{
    const auto found = std::find_if(genome.connections.begin(), genome.connections.end(), [&](const ConnectionGene& connection) {
        return connection.source_node_id == source_node_id && connection.target_node_id == target_node_id;
    });
    return found == genome.connections.end() ? nullptr : &*found;
}

bool has_enabled_outgoing(const Genome& genome, std::size_t source_node_id)
{
    return std::any_of(genome.connections.begin(), genome.connections.end(), [&](const ConnectionGene& connection) {
        return connection.enabled && connection.source_node_id == source_node_id;
    });
}

bool has_enabled_incoming(const Genome& genome, std::size_t target_node_id)
{
    return std::any_of(genome.connections.begin(), genome.connections.end(), [&](const ConnectionGene& connection) {
        return connection.enabled && connection.target_node_id == target_node_id;
    });
}

const NodeGene* first_node_of_kind(const Genome& genome, NodeKind kind)
{
    const auto found = std::find_if(genome.nodes.begin(), genome.nodes.end(), [kind](const NodeGene& node) {
        return node.kind == kind;
    });
    return found == genome.nodes.end() ? nullptr : &*found;
}

const NodeGene* output_node(const Genome& genome, const BrainConfig& brain_config, std::size_t output_index)
{
    return genome.find_node(brain_config.input_count + output_index);
}

double repair_connection_weight(
    const BrainConfig& brain_config,
    const NodeGene& source,
    const NodeGene& target,
    Random& rng)
{
    if (is_directional_fov_shape(brain_config) && is_directional_fov_seed_pair(brain_config, source, target)) {
        return directional_fov_seed_weight(brain_config, source, target);
    }
    return std::clamp(random_weight(rng), -6.0, 6.0);
}

void ensure_connection(
    Genome& genome,
    const BrainConfig& brain_config,
    const NodeGene& source,
    const NodeGene& target,
    InnovationTracker& innovation_tracker,
    Random& rng)
{
    if (source.id == target.id
        || source.kind == NodeKind::Output
        || target.kind == NodeKind::Input
        || (is_auxiliary_input(brain_config, source.id) && target.kind == NodeKind::Output)) {
        return;
    }

    if (ConnectionGene* existing = find_connection(genome, source.id, target.id)) {
        existing->enabled = true;
        if (existing->weight == 0.0) {
            existing->weight = repair_connection_weight(brain_config, source, target, rng);
        }
        return;
    }

    genome.connections.push_back({
        innovation_tracker.get_connection_innovation(source.id, target.id),
        source.id,
        target.id,
        repair_connection_weight(brain_config, source, target, rng),
        true,
        1,
    });
}

} // namespace

Brain Genome::to_brain(BrainConfig config) const
{
    std::vector<NodeGene> inputs;
    std::vector<NodeGene> hidden;
    std::vector<NodeGene> outputs;
    for (const auto& node : nodes) {
        switch (node.kind) {
        case NodeKind::Input:
            inputs.push_back(node);
            break;
        case NodeKind::Hidden:
            hidden.push_back(node);
            break;
        case NodeKind::Output:
            outputs.push_back(node);
            break;
        }
    }

    std::sort(inputs.begin(), inputs.end(), node_id_less);
    std::sort(hidden.begin(), hidden.end(), node_id_less);
    std::sort(outputs.begin(), outputs.end(), node_id_less);

    if (inputs.size() != config.input_count) {
        throw std::invalid_argument("Genome input node count does not match BrainConfig");
    }
    if (outputs.size() != config.output_count) {
        throw std::invalid_argument("Genome output node count does not match BrainConfig");
    }

    config.hidden_count = hidden.size();
    std::vector<Brain::Neuron> neurons(config.input_count + config.hidden_count + config.output_count);
    std::map<std::size_t, std::size_t> index_by_node_id;

    auto write_neuron = [&](const NodeGene& gene, std::size_t index) {
        Brain::Neuron neuron;
        neuron.position = gene.position;
        neuron.bias = gene.kind == NodeKind::Hidden ? gene.bias : 0.0;
        neuron.threshold = std::max(0.2, gene.threshold);
        neuron.background_sensitivity = gene.background_sensitivity;
        if (gene.kind == NodeKind::Hidden) {
            neuron.bias = clamp_subthreshold_bias(
                neuron.bias,
                neuron.threshold,
                config.membrane_tau,
                config.max_bias_fraction_of_threshold,
                -15.0);
        }
        neurons[index] = neuron;
        index_by_node_id[gene.id] = index;
    };

    for (std::size_t i = 0; i < inputs.size(); ++i) {
        write_neuron(inputs[i], i);
    }
    for (std::size_t i = 0; i < hidden.size(); ++i) {
        write_neuron(hidden[i], config.input_count + i);
    }
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        write_neuron(outputs[i], config.input_count + config.hidden_count + i);
    }

    std::vector<ConnectionGene> sorted_connections = connections;
    std::sort(sorted_connections.begin(), sorted_connections.end(), connection_innovation_less);

    std::vector<Brain::Synapse> synapses;
    synapses.reserve(enabled_connection_count());
    for (const auto& connection : sorted_connections) {
        if (!connection.enabled) {
            continue;
        }

        const auto source = index_by_node_id.find(connection.source_node_id);
        const auto target = index_by_node_id.find(connection.target_node_id);
        if (source == index_by_node_id.end() || target == index_by_node_id.end()) {
            throw std::invalid_argument("Genome connection endpoint could not be compiled");
        }

        Brain::Synapse synapse;
        synapse.pre = source->second;
        synapse.post = target->second;
        synapse.weight = connection.weight;
        synapse.delay_steps = connection.delay_steps;
        synapses.push_back(synapse);
    }

    return Brain::from_components(config, std::move(neurons), std::move(synapses));
}

GenomeComplexity Genome::complexity() const noexcept
{
    return {nodes.size(), hidden_node_count(), enabled_connection_count(), disabled_connection_count()};
}

std::size_t Genome::enabled_connection_count() const noexcept
{
    return static_cast<std::size_t>(std::count_if(connections.begin(), connections.end(), [](const ConnectionGene& connection) {
        return connection.enabled;
    }));
}

std::size_t Genome::disabled_connection_count() const noexcept
{
    return connections.size() - enabled_connection_count();
}

std::size_t Genome::hidden_node_count() const noexcept
{
    return static_cast<std::size_t>(std::count_if(nodes.begin(), nodes.end(), [](const NodeGene& node) {
        return node.kind == NodeKind::Hidden;
    }));
}

bool Genome::has_connection(std::size_t source_node_id, std::size_t target_node_id) const noexcept
{
    return std::any_of(connections.begin(), connections.end(), [&](const ConnectionGene& connection) {
        return connection.source_node_id == source_node_id && connection.target_node_id == target_node_id;
    });
}

const NodeGene* Genome::find_node(std::size_t node_id) const noexcept
{
    const auto found = std::find_if(nodes.begin(), nodes.end(), [node_id](const NodeGene& node) {
        return node.id == node_id;
    });
    return found == nodes.end() ? nullptr : &*found;
}

InnovationTracker::InnovationTracker(std::size_t next_node_id) : next_node_id_(next_node_id) {}

std::size_t InnovationTracker::create_node_id()
{
    return next_node_id_++;
}

std::size_t InnovationTracker::get_connection_innovation(std::size_t source_node_id, std::size_t target_node_id)
{
    const auto key = std::make_pair(source_node_id, target_node_id);
    const auto found = connection_innovations_.find(key);
    if (found != connection_innovations_.end()) {
        return found->second;
    }

    const std::size_t innovation_id = next_innovation_id_++;
    connection_innovations_[key] = innovation_id;
    return innovation_id;
}

SplitInnovation InnovationTracker::get_split_innovation(
    std::size_t connection_innovation_id,
    std::size_t source_node_id,
    std::size_t target_node_id)
{
    const auto found = split_innovations_.find(connection_innovation_id);
    if (found != split_innovations_.end()) {
        return found->second;
    }

    SplitInnovation split;
    split.node_id = create_node_id();
    split.source_to_new_innovation_id = get_connection_innovation(source_node_id, split.node_id);
    split.new_to_target_innovation_id = get_connection_innovation(split.node_id, target_node_id);
    split_innovations_[connection_innovation_id] = split;
    return split;
}

double compatibility_distance(
    const Genome& lhs,
    const Genome& rhs,
    const SpeciationConfig& config)
{
    std::vector<ConnectionGene> left = lhs.connections;
    std::vector<ConnectionGene> right = rhs.connections;
    std::sort(left.begin(), left.end(), connection_innovation_less);
    std::sort(right.begin(), right.end(), connection_innovation_less);

    const std::size_t left_max = left.empty() ? 0 : left.back().innovation_id;
    const std::size_t right_max = right.empty() ? 0 : right.back().innovation_id;

    std::size_t i = 0;
    std::size_t j = 0;
    std::size_t excess = 0;
    std::size_t disjoint = 0;
    std::size_t matching = 0;
    double weight_difference = 0.0;

    while (i < left.size() && j < right.size()) {
        const auto& left_gene = left[i];
        const auto& right_gene = right[j];
        if (left_gene.innovation_id == right_gene.innovation_id) {
            ++matching;
            weight_difference += std::abs(left_gene.weight - right_gene.weight);
            ++i;
            ++j;
        } else if (left_gene.innovation_id < right_gene.innovation_id) {
            if (left_gene.innovation_id > right_max) {
                ++excess;
            } else {
                ++disjoint;
            }
            ++i;
        } else {
            if (right_gene.innovation_id > left_max) {
                ++excess;
            } else {
                ++disjoint;
            }
            ++j;
        }
    }

    while (i < left.size()) {
        if (left[i].innovation_id > right_max) {
            ++excess;
        } else {
            ++disjoint;
        }
        ++i;
    }
    while (j < right.size()) {
        if (right[j].innovation_id > left_max) {
            ++excess;
        } else {
            ++disjoint;
        }
        ++j;
    }

    const double normalizer = static_cast<double>(std::max<std::size_t>(1, std::max(left.size(), right.size())));
    const double average_weight_difference = matching == 0 ? 0.0 : weight_difference / static_cast<double>(matching);
    return config.c1 * static_cast<double>(excess) / normalizer
        + config.c2 * static_cast<double>(disjoint) / normalizer
        + config.c3 * average_weight_difference;
}

Speciator::Speciator(SpeciationConfig config) : config_(config) {}

void Speciator::assign_species(std::vector<Genome>& genomes)
{
    for (auto& species : species_) {
        species.members.clear();
    }

    for (std::size_t genome_index = 0; genome_index < genomes.size(); ++genome_index) {
        Genome& genome = genomes[genome_index];
        bool assigned = false;
        for (auto& species : species_) {
            if (compatibility_distance(genome, species.representative, config_) <= config_.compatibility_threshold) {
                species.members.push_back(genome_index);
                genome.species_id = species.id;
                assigned = true;
                break;
            }
        }

        if (!assigned) {
            Species species;
            species.id = next_species_id_++;
            species.representative = genome;
            species.members.push_back(genome_index);
            genome.species_id = species.id;
            species_.push_back(std::move(species));
        }
    }

    std::vector<Species> active_species;
    active_species.reserve(species_.size());
    for (auto& species : species_) {
        if (species.members.empty()) {
            continue;
        }

        ++species.age;
        double best_task_score = -std::numeric_limits<double>::infinity();
        for (const std::size_t member_index : species.members) {
            best_task_score = std::max(best_task_score, genomes[member_index].metrics.task_score_norm);
        }

        if (best_task_score > species.best_historical_task_score) {
            species.best_historical_task_score = best_task_score;
            species.stagnation_counter = 0;
        } else {
            ++species.stagnation_counter;
        }

        species.representative = genomes[species.members.front()];
        active_species.push_back(std::move(species));
    }

    species_ = std::move(active_species);
}

Genome make_minimal_genome(
    const BrainConfig& brain_config,
    InnovationTracker& innovation_tracker,
    Random& rng,
    std::size_t initial_hidden_count)
{
    Genome genome;
    genome.nodes.reserve(brain_config.input_count + initial_hidden_count + brain_config.output_count);

    for (std::size_t i = 0; i < brain_config.input_count; ++i) {
        const double t = brain_config.input_count <= 1 ? 0.5 : static_cast<double>(i) / static_cast<double>(brain_config.input_count - 1);
        genome.nodes.push_back({
            i,
            NodeKind::Input,
            {0.05, 0.1 + 0.8 * t},
            0.0,
            std::max(0.2, brain_config.threshold + rng.normal(0.0, 0.08)),
            std::clamp(
                brain_config.initial_background_sensitivity
                    + rng.normal(0.0, brain_config.initial_background_sensitivity_sigma),
                0.0,
                2.0),
        });
    }

    for (std::size_t i = 0; i < brain_config.output_count; ++i) {
        const std::size_t id = brain_config.input_count + i;
        const double t = brain_config.output_count <= 1 ? 0.5 : static_cast<double>(i) / static_cast<double>(brain_config.output_count - 1);
        genome.nodes.push_back({
            id,
            NodeKind::Output,
            {0.95, 0.1 + 0.8 * t},
            0.0,
            std::max(0.2, brain_config.threshold + rng.normal(0.0, 0.08)),
            std::clamp(
                brain_config.initial_background_sensitivity
                    + rng.normal(0.0, brain_config.initial_background_sensitivity_sigma),
                0.0,
                2.0),
        });
    }

    for (std::size_t i = 0; i < initial_hidden_count; ++i) {
        genome.nodes.push_back({
            innovation_tracker.create_node_id(),
            NodeKind::Hidden,
            {rng.uniform(0.25, 0.75), rng.uniform(0.05, 0.95)},
            rng.normal(0.0, 0.05),
            std::max(0.2, brain_config.threshold + rng.normal(0.0, 0.08)),
            std::clamp(
                brain_config.initial_background_sensitivity
                    + rng.normal(0.0, brain_config.initial_background_sensitivity_sigma),
                0.0,
                2.0),
        });
    }

    const bool structured_directional_seed = brain_config.seed_input_output_synapses
        && is_directional_fov_shape(brain_config);

    for (const auto& source : genome.nodes) {
        if (source.kind == NodeKind::Output) {
            continue;
        }
        for (const auto& target : genome.nodes) {
            if (target.kind == NodeKind::Input
                || source.id == target.id
                || (is_auxiliary_input(brain_config, source.id) && target.kind == NodeKind::Output)) {
                continue;
            }

            bool create_connection = false;
            if (structured_directional_seed && is_directional_fov_seed_pair(brain_config, source, target)) {
                create_connection = true;
            } else if (brain_config.seed_input_output_synapses
                && !structured_directional_seed
                && source.kind == NodeKind::Input
                && target.kind == NodeKind::Output) {
                create_connection = true;
            } else if (!brain_config.seed_input_output_synapses || source.kind == NodeKind::Hidden || target.kind == NodeKind::Hidden) {
                create_connection = rng.chance(brain_config.initial_connection_probability);
            }

            if (!create_connection) {
                continue;
            }

            double base_weight = random_weight(rng);
            if (source.kind == NodeKind::Input && target.kind == NodeKind::Output) {
                base_weight = structured_directional_seed
                    ? directional_fov_seed_weight(brain_config, source, target)
                    : brain_config.seed_input_output_weight;
            }
            genome.connections.push_back({
                innovation_tracker.get_connection_innovation(source.id, target.id),
                source.id,
                target.id,
                std::clamp(base_weight + rng.normal(0.0, 0.10), -6.0, 6.0),
                true,
                1,
            });
        }
    }

    repair_io_connectivity(genome, brain_config, innovation_tracker, rng);
    sort_genes(genome);
    return genome;
}

void sort_genes(Genome& genome)
{
    std::sort(genome.nodes.begin(), genome.nodes.end(), node_id_less);
    std::sort(genome.connections.begin(), genome.connections.end(), connection_innovation_less);
}

bool mutate_add_node(Genome& genome, InnovationTracker& innovation_tracker, Random& rng)
{
    std::vector<std::size_t> enabled_connection_indices;
    enabled_connection_indices.reserve(genome.connections.size());
    for (std::size_t i = 0; i < genome.connections.size(); ++i) {
        if (genome.connections[i].enabled) {
            enabled_connection_indices.push_back(i);
        }
    }

    if (enabled_connection_indices.empty()) {
        return false;
    }

    ConnectionGene& connection = genome.connections[enabled_connection_indices[rng.uniform_index(enabled_connection_indices.size())]];
    connection.enabled = false;
    const std::size_t old_innovation_id = connection.innovation_id;
    const std::size_t old_source_id = connection.source_node_id;
    const std::size_t old_target_id = connection.target_node_id;
    const double old_weight = connection.weight;

    const NodeGene& source = require_node(genome, old_source_id);
    const NodeGene& target = require_node(genome, old_target_id);
    const SplitInnovation split = innovation_tracker.get_split_innovation(
        old_innovation_id,
        old_source_id,
        old_target_id);

    if (genome.find_node(split.node_id) == nullptr) {
        genome.nodes.push_back({
            split.node_id,
            NodeKind::Hidden,
            {
                std::clamp((source.position.x + target.position.x) * 0.5, 0.05, 0.95),
                std::clamp((source.position.y + target.position.y) * 0.5, 0.05, 0.95),
            },
            0.0,
            std::max(0.2, (source.threshold + target.threshold) * 0.5),
            (source.background_sensitivity + target.background_sensitivity) * 0.5,
        });
    }

    if (!genome.has_connection(old_source_id, split.node_id)) {
        genome.connections.push_back({
            split.source_to_new_innovation_id,
            old_source_id,
            split.node_id,
            1.0,
            true,
            1,
        });
    }

    if (!genome.has_connection(split.node_id, old_target_id)) {
        genome.connections.push_back({
            split.new_to_target_innovation_id,
            split.node_id,
            old_target_id,
            old_weight,
            true,
            1,
        });
    }

    sort_genes(genome);
    return true;
}

bool mutate_add_connection(
    Genome& genome,
    const BrainConfig& brain_config,
    const NeatMutationConfig& mutation_config,
    InnovationTracker& innovation_tracker,
    Random& rng)
{
    (void)brain_config;
    (void)mutation_config;

    std::vector<std::pair<std::size_t, std::size_t>> candidates;
    for (const auto& source : genome.nodes) {
        if (source.kind == NodeKind::Output) {
            continue;
        }
        for (const auto& target : genome.nodes) {
            if (target.kind == NodeKind::Input
                || source.id == target.id
                || (is_auxiliary_input(brain_config, source.id) && target.kind == NodeKind::Output)
                || genome.has_connection(source.id, target.id)) {
                continue;
            }
            candidates.emplace_back(source.id, target.id);
        }
    }

    if (candidates.empty()) {
        return false;
    }

    const auto [source_id, target_id] = candidates[rng.uniform_index(candidates.size())];
    genome.connections.push_back({
        innovation_tracker.get_connection_innovation(source_id, target_id),
        source_id,
        target_id,
        std::clamp(random_weight(rng), -6.0, 6.0),
        true,
        1,
    });
    sort_genes(genome);
    return true;
}

bool mutate_add_reciprocal_motif(
    Genome& genome,
    const BrainConfig& brain_config,
    InnovationTracker& innovation_tracker,
    Random& rng)
{
    std::vector<std::pair<std::size_t, std::size_t>> candidates;
    for (std::size_t i = 0; i < genome.nodes.size(); ++i) {
        if (genome.nodes[i].kind != NodeKind::Hidden) {
            continue;
        }
        for (std::size_t j = i + 1; j < genome.nodes.size(); ++j) {
            if (genome.nodes[j].kind != NodeKind::Hidden) {
                continue;
            }
            const ConnectionGene* forward = nullptr;
            const ConnectionGene* reverse = nullptr;
            for (const auto& connection : genome.connections) {
                if (connection.source_node_id == genome.nodes[i].id && connection.target_node_id == genome.nodes[j].id) {
                    forward = &connection;
                }
                if (connection.source_node_id == genome.nodes[j].id && connection.target_node_id == genome.nodes[i].id) {
                    reverse = &connection;
                }
            }
            if (forward == nullptr || reverse == nullptr || !forward->enabled || !reverse->enabled) {
                candidates.emplace_back(genome.nodes[i].id, genome.nodes[j].id);
            }
        }
    }

    if (candidates.empty()) {
        return false;
    }

    const auto [first_id, second_id] = candidates[rng.uniform_index(candidates.size())];
    const NodeGene& first = require_node(genome, first_id);
    const NodeGene& second = require_node(genome, second_id);
    ensure_connection(genome, brain_config, first, second, innovation_tracker, rng);
    ensure_connection(genome, brain_config, second, first, innovation_tracker, rng);
    sort_genes(genome);
    return true;
}

void repair_io_connectivity(
    Genome& genome,
    const BrainConfig& brain_config,
    InnovationTracker& innovation_tracker,
    Random& rng)
{
    for (const auto& node : genome.nodes) {
        if (node.kind != NodeKind::Input
            || is_auxiliary_input(brain_config, node.id)
            || has_enabled_outgoing(genome, node.id)) {
            continue;
        }

        bool repaired = false;
        if (is_directional_fov_shape(brain_config)) {
            for (std::size_t output_index = 0; output_index < brain_config.output_count; ++output_index) {
                const NodeGene* target = output_node(genome, brain_config, output_index);
                if (target != nullptr && is_directional_fov_seed_pair(brain_config, node, *target)) {
                    ensure_connection(genome, brain_config, node, *target, innovation_tracker, rng);
                    repaired = true;
                }
            }
        }

        if (!repaired) {
            if (const NodeGene* target = first_node_of_kind(genome, NodeKind::Output)) {
                ensure_connection(genome, brain_config, node, *target, innovation_tracker, rng);
            }
        }
    }

    for (const auto& node : genome.nodes) {
        if (node.kind != NodeKind::Output || has_enabled_incoming(genome, node.id)) {
            continue;
        }

        bool repaired = false;
        if (is_directional_fov_shape(brain_config)) {
            for (const auto& source : genome.nodes) {
                if (source.kind == NodeKind::Input && is_directional_fov_seed_pair(brain_config, source, node)) {
                    ensure_connection(genome, brain_config, source, node, innovation_tracker, rng);
                    repaired = true;
                    break;
                }
            }
        }

        if (!repaired) {
            const NodeGene* source = nullptr;
            for (const auto& candidate : genome.nodes) {
                if (candidate.kind == NodeKind::Input && !is_auxiliary_input(brain_config, candidate.id)) {
                    source = &candidate;
                    break;
                }
            }
            if (source != nullptr) {
                ensure_connection(genome, brain_config, *source, node, innovation_tracker, rng);
            }
        }
    }

    sort_genes(genome);
}

void mutate_genome(
    Genome& genome,
    const BrainConfig& brain_config,
    const NeatMutationConfig& mutation_config,
    InnovationTracker& innovation_tracker,
    Random& rng)
{
    for (auto& connection : genome.connections) {
        if (!rng.chance(mutation_config.mutate_weight_probability)) {
            continue;
        }

        if (rng.chance(mutation_config.weight_reset_probability)) {
            connection.weight = random_weight(rng);
        } else {
            connection.weight += rng.normal(0.0, mutation_config.weight_sigma);
        }
        connection.weight = std::clamp(connection.weight, -6.0, 6.0);
    }

    for (auto& node : genome.nodes) {
        if (!rng.chance(mutation_config.mutate_node_probability)) {
            continue;
        }

        node.background_sensitivity = std::clamp(
            node.background_sensitivity + rng.normal(0.0, mutation_config.background_sensitivity_sigma),
            mutation_config.background_sensitivity_min,
            mutation_config.background_sensitivity_max);

        if (node.kind == NodeKind::Input) {
            if (is_clock_input(brain_config, node.id)) {
                node.threshold = std::clamp(
                    node.threshold + rng.normal(0.0, mutation_config.clock_threshold_sigma),
                    mutation_config.clock_threshold_min,
                    mutation_config.clock_threshold_max);
            }
            continue;
        }

        if (node.kind == NodeKind::Hidden) {
            if (rng.chance(mutation_config.hidden_bias_jump_probability)) {
                const double magnitude = rng.uniform(
                    std::min(mutation_config.hidden_bias_jump_min_magnitude, mutation_config.hidden_bias_max),
                    mutation_config.hidden_bias_max);
                node.bias = rng.chance(0.5) ? magnitude : -magnitude;
            } else {
                node.bias = std::clamp(
                    node.bias + rng.normal(0.0, mutation_config.bias_sigma),
                    mutation_config.hidden_bias_min,
                    mutation_config.hidden_bias_max);
            }
            node.position.x = std::clamp(node.position.x + rng.normal(0.0, mutation_config.position_sigma), 0.05, 0.95);
            node.position.y = std::clamp(node.position.y + rng.normal(0.0, mutation_config.position_sigma), 0.05, 0.95);
        } else {
            node.bias = 0.0;
        }

        node.threshold = std::clamp(node.threshold + rng.normal(0.0, mutation_config.threshold_sigma), 0.2, 3.0);
        if (node.kind == NodeKind::Hidden) {
            node.bias = clamp_subthreshold_bias(
                node.bias,
                node.threshold,
                brain_config.membrane_tau,
                brain_config.max_bias_fraction_of_threshold,
                mutation_config.hidden_bias_min);
        }
    }

    if (rng.chance(mutation_config.add_connection_probability)) {
        mutate_add_connection(genome, brain_config, mutation_config, innovation_tracker, rng);
    }
    if (rng.chance(mutation_config.add_node_probability)) {
        mutate_add_node(genome, innovation_tracker, rng);
    }
    if (rng.chance(mutation_config.add_reciprocal_motif_probability)) {
        mutate_add_reciprocal_motif(genome, brain_config, innovation_tracker, rng);
    }
    if (!genome.connections.empty() && rng.chance(mutation_config.enable_disable_probability)) {
        auto& connection = genome.connections[rng.uniform_index(genome.connections.size())];
        connection.enabled = !connection.enabled;
    }

    repair_io_connectivity(genome, brain_config, innovation_tracker, rng);
    sort_genes(genome);
}

Genome crossover_genomes(
    const Genome& lhs,
    const Genome& rhs,
    const NeatMutationConfig& mutation_config,
    Random& rng)
{
    const Genome& fitter = is_better_parent(lhs, rhs, rng) ? lhs : rhs;
    const Genome& other = &fitter == &lhs ? rhs : lhs;

    Genome child;
    const auto fitter_connections = connection_map(fitter);
    const auto other_connections = connection_map(other);

    for (const auto& [innovation_id, fitter_gene] : fitter_connections) {
        ConnectionGene inherited = fitter_gene;
        const auto other_found = other_connections.find(innovation_id);
        const bool matching = other_found != other_connections.end();
        if (matching && rng.chance(0.5)) {
            inherited = other_found->second;
        }

        const bool disabled_in_parent = !fitter_gene.enabled || (matching && !other_found->second.enabled);
        if (disabled_in_parent) {
            inherited.enabled = !rng.chance(mutation_config.disabled_gene_inheritance_probability);
        }

        child.connections.push_back(inherited);
    }

    std::map<std::size_t, NodeGene> nodes_by_id;
    for (const auto& node : fitter.nodes) {
        nodes_by_id[node.id] = node;
    }
    for (const auto& node : other.nodes) {
        if (node.kind != NodeKind::Hidden) {
            nodes_by_id.emplace(node.id, node);
        }
    }

    for (const auto& connection : child.connections) {
        add_node_if_missing(nodes_by_id, fitter, other, connection.source_node_id);
        add_node_if_missing(nodes_by_id, fitter, other, connection.target_node_id);
    }

    child.nodes.reserve(nodes_by_id.size());
    for (const auto& [node_id, node] : nodes_by_id) {
        (void)node_id;
        child.nodes.push_back(node);
    }

    reset_genome_metadata(child);
    sort_genes(child);
    return child;
}

} // namespace neuroevo
