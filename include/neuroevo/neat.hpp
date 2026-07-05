#pragma once

#include "neuroevo/brain.hpp"
#include "neuroevo/objectives.hpp"
#include "neuroevo/random.hpp"

#include <cstddef>
#include <limits>
#include <map>
#include <vector>

namespace neuroevo {

enum class NodeKind {
    Input,
    Hidden,
    Output,
};

struct NodeGene {
    std::size_t id = 0;
    NodeKind kind = NodeKind::Hidden;
    Vec2 position;
    double bias = 0.0;
    double threshold = 1.0;
};

struct ConnectionGene {
    std::size_t innovation_id = 0;
    std::size_t source_node_id = 0;
    std::size_t target_node_id = 0;
    double weight = 0.0;
    bool enabled = true;
    std::size_t delay_steps = 1;
};

struct Genome {
    std::size_t id = 0;
    std::size_t species_id = 0;
    std::size_t age = 0;
    std::vector<NodeGene> nodes;
    std::vector<ConnectionGene> connections;
    EvaluationMetrics metrics;
    std::vector<double> objectives;
    std::size_t pareto_rank = std::numeric_limits<std::size_t>::max();
    double crowding_distance = 0.0;
    double scalar_display_score = 0.0;

    Brain to_brain(BrainConfig config) const;
    GenomeComplexity complexity() const noexcept;
    std::size_t enabled_connection_count() const noexcept;
    std::size_t disabled_connection_count() const noexcept;
    std::size_t hidden_node_count() const noexcept;
    bool has_connection(std::size_t source_node_id, std::size_t target_node_id) const noexcept;
    const NodeGene* find_node(std::size_t node_id) const noexcept;
};

struct SplitInnovation {
    std::size_t node_id = 0;
    std::size_t source_to_new_innovation_id = 0;
    std::size_t new_to_target_innovation_id = 0;
};

class InnovationTracker {
public:
    explicit InnovationTracker(std::size_t next_node_id = 0);

    std::size_t create_node_id();
    std::size_t get_connection_innovation(std::size_t source_node_id, std::size_t target_node_id);
    SplitInnovation get_split_innovation(
        std::size_t connection_innovation_id,
        std::size_t source_node_id,
        std::size_t target_node_id);

private:
    std::size_t next_node_id_ = 0;
    std::size_t next_innovation_id_ = 1;
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> connection_innovations_;
    std::map<std::size_t, SplitInnovation> split_innovations_;
};

struct NeatMutationConfig {
    double mutate_weight_probability = 0.80;
    double weight_sigma = 0.20;
    double weight_reset_probability = 0.08;
    double mutate_node_probability = 0.12;
    double bias_sigma = 0.35;
    double threshold_sigma = 0.03;
    double clock_threshold_sigma = 0.08;
    double clock_threshold_min = 0.2;
    double clock_threshold_max = 5.0;
    double position_sigma = 0.03;
    double hidden_bias_min = -15.0;
    double hidden_bias_max = 15.0;
    double hidden_bias_jump_min_magnitude = 8.0;
    double hidden_bias_jump_probability = 0.04;
    double add_node_probability = 0.03;
    double add_connection_probability = 0.16;
    double enable_disable_probability = 0.02;
    double disabled_gene_inheritance_probability = 0.75;
    double crossover_probability = 0.75;
};

struct SpeciationConfig {
    double compatibility_threshold = 0.15;
    double c1 = 1.0;
    double c2 = 1.0;
    double c3 = 0.4;
};

struct Species {
    std::size_t id = 0;
    Genome representative;
    std::vector<std::size_t> members;
    std::size_t age = 0;
    double best_historical_task_score = -std::numeric_limits<double>::infinity();
    std::size_t stagnation_counter = 0;
};

double compatibility_distance(
    const Genome& lhs,
    const Genome& rhs,
    const SpeciationConfig& config);

class Speciator {
public:
    explicit Speciator(SpeciationConfig config = {});

    void assign_species(std::vector<Genome>& genomes);
    const std::vector<Species>& species() const noexcept { return species_; }
    const SpeciationConfig& config() const noexcept { return config_; }

private:
    SpeciationConfig config_;
    std::size_t next_species_id_ = 1;
    std::vector<Species> species_;
};

Genome make_minimal_genome(
    const BrainConfig& brain_config,
    InnovationTracker& innovation_tracker,
    Random& rng,
    std::size_t initial_hidden_count = 0);

void sort_genes(Genome& genome);
void mutate_genome(
    Genome& genome,
    const BrainConfig& brain_config,
    const NeatMutationConfig& mutation_config,
    InnovationTracker& innovation_tracker,
    Random& rng);

bool mutate_add_node(Genome& genome, InnovationTracker& innovation_tracker, Random& rng);
bool mutate_add_connection(
    Genome& genome,
    const BrainConfig& brain_config,
    const NeatMutationConfig& mutation_config,
    InnovationTracker& innovation_tracker,
    Random& rng);
void repair_io_connectivity(
    Genome& genome,
    const BrainConfig& brain_config,
    InnovationTracker& innovation_tracker,
    Random& rng);

Genome crossover_genomes(
    const Genome& lhs,
    const Genome& rhs,
    const NeatMutationConfig& mutation_config,
    Random& rng);

} // namespace neuroevo
