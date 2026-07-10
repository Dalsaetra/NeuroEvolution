#include "neuroevo/evolution.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace neuroevo {
namespace {

double max_hidden_bias(const Brain& brain)
{
    const auto& config = brain.config();
    const auto& neurons = brain.neurons();
    if (config.hidden_count == 0) {
        return 0.0;
    }

    double max_bias = -std::numeric_limits<double>::infinity();
    for (std::size_t i = config.input_count; i < config.input_count + config.hidden_count; ++i) {
        max_bias = std::max(max_bias, neurons[i].bias);
    }
    return max_bias;
}

double max_hidden_bias(const Genome& genome)
{
    double max_bias = 0.0;
    bool has_hidden = false;
    for (const auto& node : genome.nodes) {
        if (node.kind != NodeKind::Hidden) {
            continue;
        }
        max_bias = has_hidden ? std::max(max_bias, node.bias) : node.bias;
        has_hidden = true;
    }
    return has_hidden ? max_bias : 0.0;
}

bool has_clock_candidate(const Brain& brain)
{
    const auto& config = brain.config();
    const auto& neurons = brain.neurons();
    for (std::size_t i = config.input_count; i < config.input_count + config.hidden_count; ++i) {
        if (neurons[i].bias * config.membrane_tau >= neurons[i].threshold) {
            return true;
        }
    }
    return false;
}

bool has_clock_candidate(const Genome& genome, const BrainConfig& config)
{
    for (const auto& node : genome.nodes) {
        if (node.kind == NodeKind::Hidden && node.bias * config.membrane_tau >= node.threshold) {
            return true;
        }
    }
    return false;
}

double clock_input_threshold(const Genome& genome, const BrainConfig& config)
{
    if (!config.has_clock_input || config.clock_input_index >= config.input_count) {
        return 0.0;
    }
    if (const NodeGene* node = genome.find_node(config.clock_input_index)) {
        return node->threshold;
    }
    return 0.0;
}

bool better_evaluation(const EvaluationResult& lhs, const EvaluationResult& rhs)
{
    if (lhs.foods_collected != rhs.foods_collected) {
        return lhs.foods_collected > rhs.foods_collected;
    }
    return lhs.fitness > rhs.fitness;
}

bool better_genome_for_recording(const Genome& lhs, const Genome& rhs)
{
    if (lhs.metrics.raw_foods_collected != rhs.metrics.raw_foods_collected) {
        return lhs.metrics.raw_foods_collected > rhs.metrics.raw_foods_collected;
    }
    if (lhs.metrics.task_score_norm != rhs.metrics.task_score_norm) {
        return lhs.metrics.task_score_norm > rhs.metrics.task_score_norm;
    }
    const int comparison = compare_nsga2(
        lhs.pareto_rank,
        lhs.crowding_distance,
        lhs.metrics.task_score_norm,
        rhs.pareto_rank,
        rhs.crowding_distance,
        rhs.metrics.task_score_norm);
    if (comparison != 0) {
        return comparison < 0;
    }
    if (lhs.scalar_display_score != rhs.scalar_display_score) {
        return lhs.scalar_display_score > rhs.scalar_display_score;
    }
    return lhs.metrics.raw_fitness > rhs.metrics.raw_fitness;
}

void clear_genome_evaluation(Genome& genome)
{
    genome.species_id = 0;
    genome.age = 0;
    genome.metrics = {};
    genome.objectives.clear();
    genome.pareto_rank = std::numeric_limits<std::size_t>::max();
    genome.crowding_distance = 0.0;
    genome.scalar_display_score = 0.0;
}

std::vector<std::size_t> all_indices(std::size_t count)
{
    std::vector<std::size_t> indices(count);
    std::iota(indices.begin(), indices.end(), 0);
    return indices;
}

std::size_t tournament_select_from_indices(
    const std::vector<Genome>& population,
    const std::vector<std::size_t>& candidates,
    std::size_t tournament_size,
    Random& rng)
{
    if (candidates.empty()) {
        throw std::invalid_argument("Cannot select from an empty parent candidate set");
    }

    tournament_size = std::max<std::size_t>(1, tournament_size);
    std::size_t best = candidates[rng.uniform_index(candidates.size())];
    for (std::size_t i = 1; i < tournament_size; ++i) {
        const std::size_t candidate = candidates[rng.uniform_index(candidates.size())];
        const int comparison = compare_nsga2(
            population[candidate].pareto_rank,
            population[candidate].crowding_distance,
            population[candidate].metrics.task_score_norm,
            population[best].pareto_rank,
            population[best].crowding_distance,
            population[best].metrics.task_score_norm);
        if (comparison < 0 || (comparison == 0 && rng.chance(0.5))) {
            best = candidate;
        }
    }
    return best;
}

std::vector<std::size_t> species_members_or_all(
    const std::vector<Species>& species,
    std::size_t species_id,
    const std::vector<std::size_t>& fallback)
{
    for (const auto& item : species) {
        if (item.id == species_id && !item.members.empty()) {
            return item.members;
        }
    }
    return fallback;
}

std::vector<std::size_t> pareto_ranks(const std::vector<Genome>& population)
{
    std::vector<std::size_t> ranks;
    ranks.reserve(population.size());
    for (const auto& genome : population) {
        ranks.push_back(genome.pareto_rank);
    }
    return ranks;
}

std::vector<double> crowding_distances(const std::vector<Genome>& population)
{
    std::vector<double> distances;
    distances.reserve(population.size());
    for (const auto& genome : population) {
        distances.push_back(genome.crowding_distance);
    }
    return distances;
}

std::vector<double> task_scores(const std::vector<Genome>& population)
{
    std::vector<double> scores;
    scores.reserve(population.size());
    for (const auto& genome : population) {
        scores.push_back(genome.metrics.task_score_norm);
    }
    return scores;
}

void add_unique_index(std::vector<std::size_t>& selected, std::size_t index, std::size_t max_count)
{
    if (selected.size() >= max_count) {
        return;
    }
    if (std::find(selected.begin(), selected.end(), index) == selected.end()) {
        selected.push_back(index);
    }
}

std::vector<std::size_t> select_pareto_front_representatives(
    const std::vector<Genome>& population,
    std::size_t max_count)
{
    if (population.empty() || max_count == 0) {
        return {};
    }

    std::vector<std::size_t> candidates;
    candidates.reserve(population.size());
    for (std::size_t i = 0; i < population.size(); ++i) {
        if (population[i].pareto_rank == 0) {
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) {
        candidates = all_indices(population.size());
    }

    std::vector<std::size_t> selected;
    selected.reserve(std::min(max_count, candidates.size()));

    auto add_best_by = [&](auto better) {
        const auto best = std::max_element(candidates.begin(), candidates.end(), [&](std::size_t lhs, std::size_t rhs) {
            return better(population[rhs], population[lhs]);
        });
        if (best != candidates.end()) {
            add_unique_index(selected, *best, max_count);
        }
    };

    add_best_by([](const Genome& lhs, const Genome& rhs) {
        return lhs.metrics.task_score_norm > rhs.metrics.task_score_norm;
    });
    add_best_by([](const Genome& lhs, const Genome& rhs) {
        return lhs.metrics.spike_energy_norm < rhs.metrics.spike_energy_norm;
    });
    add_best_by([](const Genome& lhs, const Genome& rhs) {
        return lhs.metrics.synapse_count_norm < rhs.metrics.synapse_count_norm;
    });
    add_best_by([](const Genome& lhs, const Genome& rhs) {
        return lhs.metrics.time_cost_norm < rhs.metrics.time_cost_norm;
    });
    add_best_by([](const Genome& lhs, const Genome& rhs) {
        return lhs.metrics.neuron_count_norm < rhs.metrics.neuron_count_norm;
    });

    std::stable_sort(candidates.begin(), candidates.end(), [&](std::size_t lhs, std::size_t rhs) {
        const int comparison = compare_nsga2(
            population[lhs].pareto_rank,
            population[lhs].crowding_distance,
            population[lhs].metrics.task_score_norm,
            population[rhs].pareto_rank,
            population[rhs].crowding_distance,
            population[rhs].metrics.task_score_norm);
        if (comparison != 0) {
            return comparison < 0;
        }
        if (population[lhs].metrics.synapse_count_norm != population[rhs].metrics.synapse_count_norm) {
            return population[lhs].metrics.synapse_count_norm < population[rhs].metrics.synapse_count_norm;
        }
        return population[lhs].id < population[rhs].id;
    });

    for (const std::size_t index : candidates) {
        add_unique_index(selected, index, max_count);
    }
    return selected;
}

void write_solution_metrics_csv(
    const std::filesystem::path& path,
    const Genome& genome,
    const BrainConfig& brain_config,
    const EvaluationResult& recorded)
{
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Could not open solution metrics CSV for writing: " + path.string());
    }

    output << "key,value\n";
    output << std::setprecision(10);
    output << "genome_id," << genome.id << '\n';
    output << "species_id," << genome.species_id << '\n';
    output << "pareto_rank," << genome.pareto_rank << '\n';
    output << "crowding_distance," << genome.crowding_distance << '\n';
    output << "task_score_norm," << genome.metrics.task_score_norm << '\n';
    output << "spike_energy_norm," << genome.metrics.spike_energy_norm << '\n';
    output << "synapse_count_norm," << genome.metrics.synapse_count_norm << '\n';
    output << "neuron_count_norm," << genome.metrics.neuron_count_norm << '\n';
    output << "time_cost_norm," << genome.metrics.time_cost_norm << '\n';
    output << "raw_foods," << genome.metrics.raw_foods_collected << '\n';
    output << "raw_occluded_foods," << genome.metrics.raw_occluded_foods_collected << '\n';
    output << "raw_fitness," << genome.metrics.raw_fitness << '\n';
    output << "raw_reward," << genome.metrics.raw_reward << '\n';
    output << "raw_penalty," << genome.metrics.raw_penalty << '\n';
    output << "raw_spikes," << genome.metrics.raw_spikes << '\n';
    output << "recorded_foods," << recorded.foods_collected << '\n';
    output << "recorded_occluded_foods," << recorded.occluded_foods_collected << '\n';
    output << "recorded_fitness," << recorded.fitness << '\n';
    output << "recorded_spikes," << recorded.spikes << '\n';
    output << "neurons," << genome.nodes.size() << '\n';
    output << "hidden_nodes," << genome.hidden_node_count() << '\n';
    output << "enabled_synapses," << genome.enabled_connection_count() << '\n';
    output << "disabled_synapses," << genome.disabled_connection_count() << '\n';
    output << "clock_input_threshold," << clock_input_threshold(genome, brain_config) << '\n';
}

} // namespace

EAMode parse_ea_mode(const std::string& value)
{
    if (value == "scalar") {
        return EAMode::Scalar;
    }
    if (value == "neat-nsga2" || value == "neat_nsga2" || value == "nsga2" || value == "neat") {
        return EAMode::NeatNsga2;
    }
    throw std::invalid_argument("Unknown EA mode: " + value);
}

std::string to_string(EAMode mode)
{
    switch (mode) {
    case EAMode::Scalar:
        return "scalar";
    case EAMode::NeatNsga2:
        return "neat-nsga2";
    }
    throw std::invalid_argument("Unknown EA mode");
}

EvolutionRunner::EvolutionRunner(EvolutionConfig config)
    : config_(config), rng_(config.seed), environment_(config.environment)
{
    const SensorimotorSpec spec = sensorimotor_spec(config_.environment.sensorimotor_regime);
    config_.brain.sensory_input_count = spec.input_count;
    config_.brain.input_count = spec.input_count
        + (config_.environment.clock_input_enabled ? 1 : 0)
        + (config_.environment.episode_start_input_enabled ? 1 : 0);
    config_.brain.output_count = spec.output_count;
    config_.brain.has_clock_input = config_.environment.clock_input_enabled;
    config_.brain.clock_input_index = config_.environment.clock_input_enabled ? spec.input_count : 0;
    config_.brain.has_episode_start_input = config_.environment.episode_start_input_enabled;
    config_.brain.episode_start_input_index = config_.environment.episode_start_input_enabled
        ? spec.input_count + (config_.environment.clock_input_enabled ? 1 : 0)
        : 0;

    if (config_.population_size == 0) {
        throw std::invalid_argument("population_size must be greater than zero");
    }
    if (config_.elite_count > config_.population_size) {
        throw std::invalid_argument("elite_count cannot exceed population_size");
    }
    config_.tournament_size = std::max<std::size_t>(1, config_.tournament_size);
    config_.trials_per_genome = std::max<std::size_t>(1, config_.trials_per_genome);
    config_.recorded_trajectory_trials = std::max<std::size_t>(1, config_.recorded_trajectory_trials);
    config_.neat.recorded_pareto_front_trials = std::max<std::size_t>(1, config_.neat.recorded_pareto_front_trials);
}

EvolutionResult EvolutionRunner::run(const std::string& output_dir)
{
    switch (config_.ea_mode) {
    case EAMode::Scalar:
        return run_scalar(output_dir);
    case EAMode::NeatNsga2:
        return run_neat_nsga2(output_dir);
    }
    throw std::invalid_argument("Unknown EA mode");
}

EvolutionResult EvolutionRunner::run_scalar(const std::string& output_dir)
{
    std::vector<Brain> population;
    population.reserve(config_.population_size);
    for (std::size_t i = 0; i < config_.population_size; ++i) {
        population.push_back(Brain::random(config_.brain, rng_));
    }

    EvolutionResult result;
    EvaluationResult best_evaluation_seen;
    bool has_best_seen = false;

    for (std::size_t generation = 0; generation < config_.generations; ++generation) {
        std::vector<ScoredGenome> scored = evaluate_population(population);
        std::sort(scored.begin(), scored.end(), [](const ScoredGenome& lhs, const ScoredGenome& rhs) {
            return better_evaluation(lhs.evaluation, rhs.evaluation);
        });

        result.stats.push_back(summarize(generation, scored));

        if (!has_best_seen || better_evaluation(scored.front().evaluation, best_evaluation_seen)) {
            best_evaluation_seen = scored.front().evaluation;
            result.best_brain = scored.front().brain;
            result.best_evaluation = scored.front().evaluation;
            has_best_seen = true;
        }

        std::vector<Brain> next_population;
        next_population.reserve(config_.population_size);
        for (std::size_t i = 0; i < config_.elite_count; ++i) {
            next_population.push_back(scored[i].brain);
        }

        while (next_population.size() < config_.population_size) {
            Brain child = scored[select_parent(scored)].brain;
            child.mutate(config_.mutation, rng_);
            next_population.push_back(child);
        }

        population = std::move(next_population);
    }

    EvaluationResult best_recorded_life;
    bool has_recorded_life = false;
    for (std::size_t trial = 0; trial < config_.recorded_trajectory_trials; ++trial) {
        EvaluationResult candidate = environment_.evaluate(result.best_brain, rng_, true);
        const bool better_food = candidate.foods_collected > best_recorded_life.foods_collected;
        const bool tied_food_better_fitness = candidate.foods_collected == best_recorded_life.foods_collected
            && candidate.fitness > best_recorded_life.fitness;
        if (!has_recorded_life || better_food || tied_food_better_fitness) {
            best_recorded_life = std::move(candidate);
            has_recorded_life = true;
        }
    }
    result.best_evaluation = std::move(best_recorded_life);

    if (!output_dir.empty()) {
        std::filesystem::create_directories(output_dir);
        write_run_metadata_csv((std::filesystem::path(output_dir) / "metadata.csv").string(), config_);
        write_generation_stats_csv((std::filesystem::path(output_dir) / "stats.csv").string(), result.stats);
        write_trajectory_csv((std::filesystem::path(output_dir) / "best_trajectory.csv").string(), result.best_evaluation.trajectory);
        write_brain_activity_csv((std::filesystem::path(output_dir) / "brain_activity.csv").string(), result.best_evaluation.brain_activity);
        write_brain_synapses_csv((std::filesystem::path(output_dir) / "brain_synapses.csv").string(), result.best_evaluation.brain_synapses);
        write_synapse_events_csv((std::filesystem::path(output_dir) / "synapse_events.csv").string(), result.best_evaluation.synapse_events);
    }

    return result;
}

EvolutionResult EvolutionRunner::run_neat_nsga2(const std::string& output_dir)
{
    InnovationTracker innovation_tracker(config_.brain.input_count + config_.brain.output_count);
    Speciator speciator(config_.neat.speciation);

    std::size_t next_genome_id = 1;
    std::vector<Genome> population;
    population.reserve(config_.population_size);
    for (std::size_t i = 0; i < config_.population_size; ++i) {
        Genome genome = make_minimal_genome(
            config_.brain,
            innovation_tracker,
            rng_,
            config_.neat.initial_hidden_count);
        genome.id = next_genome_id++;
        population.push_back(std::move(genome));
    }

    EvolutionResult result;
    Genome best_genome_seen;
    bool has_best_seen = false;

    for (std::size_t generation = 0; generation < config_.generations; ++generation) {
        evaluate_neat_population(population);
        speciator.assign_species(population);
        assign_nsga2_metadata(population);

        const std::vector<std::size_t> global_parent_pool = all_indices(population.size());
        const std::vector<Species> parent_species = speciator.species();
        std::vector<Genome> offspring;
        offspring.reserve(config_.population_size);

        while (offspring.size() < config_.population_size) {
            const std::size_t parent1_index = tournament_select_from_indices(
                population,
                global_parent_pool,
                config_.tournament_size,
                rng_);

            std::vector<std::size_t> mate_pool = rng_.chance(config_.neat.interspecies_mate_probability)
                ? global_parent_pool
                : species_members_or_all(parent_species, population[parent1_index].species_id, global_parent_pool);
            if (mate_pool.empty()) {
                mate_pool = global_parent_pool;
            }

            std::size_t parent2_index = tournament_select_from_indices(
                population,
                mate_pool,
                config_.tournament_size,
                rng_);
            if (mate_pool.size() > 1) {
                for (std::size_t attempt = 0; attempt < 4 && parent2_index == parent1_index; ++attempt) {
                    parent2_index = tournament_select_from_indices(
                        population,
                        mate_pool,
                        config_.tournament_size,
                        rng_);
                }
            }

            Genome child = rng_.chance(config_.neat.mutation.crossover_probability)
                ? crossover_genomes(population[parent1_index], population[parent2_index], config_.neat.mutation, rng_)
                : population[parent1_index];
            mutate_genome(child, config_.brain, config_.neat.mutation, innovation_tracker, rng_);
            clear_genome_evaluation(child);
            child.id = next_genome_id++;
            offspring.push_back(std::move(child));
        }

        evaluate_neat_population(offspring);

        std::vector<Genome> combined;
        combined.reserve(population.size() + offspring.size());
        combined.insert(combined.end(), population.begin(), population.end());
        combined.insert(
            combined.end(),
            std::make_move_iterator(offspring.begin()),
            std::make_move_iterator(offspring.end()));
        assign_nsga2_metadata(combined);

        const std::vector<std::size_t> survivors = nsga2_survival_select(
            pareto_ranks(combined),
            crowding_distances(combined),
            task_scores(combined),
            config_.population_size);

        std::vector<Genome> next_population;
        next_population.reserve(config_.population_size);
        for (const std::size_t survivor_index : survivors) {
            Genome survivor = combined[survivor_index];
            ++survivor.age;
            next_population.push_back(std::move(survivor));
        }

        population = std::move(next_population);
        speciator.assign_species(population);
        assign_nsga2_metadata(population);

        result.stats.push_back(summarize_neat(generation, population, speciator.species()));

        for (const auto& genome : population) {
            if (!has_best_seen || better_genome_for_recording(genome, best_genome_seen)) {
                best_genome_seen = genome;
                has_best_seen = true;
            }
        }
    }

    if (!has_best_seen && !population.empty()) {
        evaluate_neat_population(population);
        assign_nsga2_metadata(population);
        best_genome_seen = population.front();
        has_best_seen = true;
    }

    if (has_best_seen) {
        result.best_brain = best_genome_seen.to_brain(config_.brain);
    }

    EvaluationResult best_recorded_life;
    bool has_recorded_life = false;
    for (std::size_t trial = 0; trial < config_.recorded_trajectory_trials && has_best_seen; ++trial) {
        EvaluationResult candidate = environment_.evaluate(result.best_brain, rng_, true);
        const bool better_food = candidate.foods_collected > best_recorded_life.foods_collected;
        const bool tied_food_better_fitness = candidate.foods_collected == best_recorded_life.foods_collected
            && candidate.fitness > best_recorded_life.fitness;
        if (!has_recorded_life || better_food || tied_food_better_fitness) {
            best_recorded_life = std::move(candidate);
            has_recorded_life = true;
        }
    }
    result.best_evaluation = std::move(best_recorded_life);

    if (!output_dir.empty()) {
        std::filesystem::create_directories(output_dir);
        write_run_metadata_csv((std::filesystem::path(output_dir) / "metadata.csv").string(), config_);
        write_generation_stats_csv((std::filesystem::path(output_dir) / "stats.csv").string(), result.stats);
        write_trajectory_csv((std::filesystem::path(output_dir) / "best_trajectory.csv").string(), result.best_evaluation.trajectory);
        write_brain_activity_csv((std::filesystem::path(output_dir) / "brain_activity.csv").string(), result.best_evaluation.brain_activity);
        write_brain_synapses_csv((std::filesystem::path(output_dir) / "brain_synapses.csv").string(), result.best_evaluation.brain_synapses);
        write_synapse_events_csv((std::filesystem::path(output_dir) / "synapse_events.csv").string(), result.best_evaluation.synapse_events);
        write_neat_pareto_front_recordings(output_dir, population);
    }

    return result;
}

void EvolutionRunner::write_neat_pareto_front_recordings(
    const std::string& output_dir,
    const std::vector<Genome>& population)
{
    const std::filesystem::path front_dir = std::filesystem::path(output_dir) / "pareto_front";
    const std::filesystem::path manifest_path = std::filesystem::path(output_dir) / "pareto_front.csv";
    std::filesystem::remove_all(front_dir);
    std::filesystem::remove(manifest_path);

    const std::vector<std::size_t> selected = select_pareto_front_representatives(
        population,
        config_.neat.recorded_pareto_front_count);
    if (selected.empty()) {
        return;
    }

    std::filesystem::create_directories(front_dir);

    std::ofstream manifest(manifest_path);
    if (!manifest) {
        throw std::runtime_error("Could not open Pareto front manifest CSV for writing: " + manifest_path.string());
    }

    manifest << "solution_index,genome_id,species_id,pareto_rank,crowding_distance,"
                "task_score_norm,spike_energy_norm,synapse_count_norm,neuron_count_norm,time_cost_norm,"
                "raw_foods,raw_occluded_foods,raw_fitness,raw_spikes,recorded_foods,recorded_occluded_foods,recorded_fitness,recorded_spikes,"
                "neurons,hidden_nodes,enabled_synapses,disabled_synapses,clock_input_threshold,trajectory_dir\n";
    manifest << std::setprecision(10);

    for (std::size_t solution_index = 0; solution_index < selected.size(); ++solution_index) {
        const Genome& genome = population[selected[solution_index]];
        const Brain brain = genome.to_brain(config_.brain);

        EvaluationResult best_recorded_life;
        bool has_recorded_life = false;
        for (std::size_t trial = 0; trial < config_.neat.recorded_pareto_front_trials; ++trial) {
            EvaluationResult candidate = environment_.evaluate(brain, rng_, true);
            if (!has_recorded_life || better_evaluation(candidate, best_recorded_life)) {
                best_recorded_life = std::move(candidate);
                has_recorded_life = true;
            }
        }

        const std::string solution_name = "solution_" + (solution_index < 10 ? std::string("0") : std::string())
            + std::to_string(solution_index);
        const std::filesystem::path solution_dir = front_dir / solution_name;
        std::filesystem::create_directories(solution_dir);

        write_run_metadata_csv((solution_dir / "metadata.csv").string(), config_);
        write_solution_metrics_csv(solution_dir / "solution_metrics.csv", genome, config_.brain, best_recorded_life);
        write_trajectory_csv((solution_dir / "best_trajectory.csv").string(), best_recorded_life.trajectory);
        write_brain_activity_csv((solution_dir / "brain_activity.csv").string(), best_recorded_life.brain_activity);
        write_brain_synapses_csv((solution_dir / "brain_synapses.csv").string(), best_recorded_life.brain_synapses);
        write_synapse_events_csv((solution_dir / "synapse_events.csv").string(), best_recorded_life.synapse_events);

        manifest << solution_index << ','
                 << genome.id << ','
                 << genome.species_id << ','
                 << genome.pareto_rank << ','
                 << genome.crowding_distance << ','
                 << genome.metrics.task_score_norm << ','
                 << genome.metrics.spike_energy_norm << ','
                 << genome.metrics.synapse_count_norm << ','
                 << genome.metrics.neuron_count_norm << ','
                 << genome.metrics.time_cost_norm << ','
                 << genome.metrics.raw_foods_collected << ','
                 << genome.metrics.raw_occluded_foods_collected << ','
                 << genome.metrics.raw_fitness << ','
                 << genome.metrics.raw_spikes << ','
                 << best_recorded_life.foods_collected << ','
                 << best_recorded_life.occluded_foods_collected << ','
                 << best_recorded_life.fitness << ','
                 << best_recorded_life.spikes << ','
                 << genome.nodes.size() << ','
                 << genome.hidden_node_count() << ','
                 << genome.enabled_connection_count() << ','
                 << genome.disabled_connection_count() << ','
                 << clock_input_threshold(genome, config_.brain) << ','
                 << "pareto_front/" << solution_name << '\n';
    }
}

std::vector<EvolutionRunner::ScoredGenome> EvolutionRunner::evaluate_population(const std::vector<Brain>& population)
{
    std::vector<ScoredGenome> scored;
    scored.reserve(population.size());

    for (const Brain& brain : population) {
        EvaluationResult aggregate;
        for (std::size_t trial = 0; trial < config_.trials_per_genome; ++trial) {
            EvaluationResult trial_result = environment_.evaluate(brain, rng_, false);
            aggregate.fitness += trial_result.fitness;
            aggregate.reward += trial_result.reward;
            aggregate.penalty += trial_result.penalty;
            aggregate.spikes += trial_result.spikes;
            aggregate.foods_collected += trial_result.foods_collected;
            aggregate.occluded_foods_collected += trial_result.occluded_foods_collected;
        }

        const double trials = static_cast<double>(config_.trials_per_genome);
        aggregate.fitness /= trials;
        aggregate.reward /= trials;
        aggregate.penalty /= trials;
        aggregate.spikes /= trials;
        aggregate.foods_collected /= trials;
        aggregate.occluded_foods_collected /= trials;
        scored.push_back({brain, aggregate});
    }

    return scored;
}

void EvolutionRunner::evaluate_neat_population(std::vector<Genome>& population)
{
    for (auto& genome : population) {
        const Brain brain = genome.to_brain(config_.brain);
        EvaluationResult aggregate;
        for (std::size_t trial = 0; trial < config_.trials_per_genome; ++trial) {
            EvaluationResult trial_result = environment_.evaluate(brain, rng_, false);
            aggregate.fitness += trial_result.fitness;
            aggregate.reward += trial_result.reward;
            aggregate.penalty += trial_result.penalty;
            aggregate.spikes += trial_result.spikes;
            aggregate.foods_collected += trial_result.foods_collected;
            aggregate.occluded_foods_collected += trial_result.occluded_foods_collected;
        }

        const double trials = static_cast<double>(config_.trials_per_genome);
        aggregate.fitness /= trials;
        aggregate.reward /= trials;
        aggregate.penalty /= trials;
        aggregate.spikes /= trials;
        aggregate.foods_collected /= trials;
        aggregate.occluded_foods_collected /= trials;

        genome.metrics = make_evaluation_metrics(
            aggregate,
            genome.complexity(),
            config_.neat.objectives,
            config_.environment);
        genome.objectives = make_objective_vector(genome.metrics, config_.neat.objectives.objective_set);
        genome.scalar_display_score = genome.metrics.scalar_display_score;
    }
}

void EvolutionRunner::assign_nsga2_metadata(std::vector<Genome>& population) const
{
    std::vector<std::vector<double>> objective_values;
    objective_values.reserve(population.size());
    for (const auto& genome : population) {
        objective_values.push_back(genome.objectives);
    }

    const auto descriptors = make_objective_descriptors(config_.neat.objectives.objective_set);
    const auto fronts = non_dominated_sort(objective_values, descriptors);

    for (auto& genome : population) {
        genome.pareto_rank = std::numeric_limits<std::size_t>::max();
        genome.crowding_distance = 0.0;
    }

    for (std::size_t rank = 0; rank < fronts.size(); ++rank) {
        const auto distances = compute_crowding_distance(objective_values, fronts[rank], descriptors);
        for (const std::size_t index : fronts[rank]) {
            population[index].pareto_rank = rank;
            population[index].crowding_distance = distances[index];
        }
    }
}

GenerationStats EvolutionRunner::summarize(std::size_t generation, const std::vector<ScoredGenome>& scored) const
{
    GenerationStats stats;
    stats.generation = generation;
    stats.best_fitness = scored.front().evaluation.fitness;
    stats.best_reward = scored.front().evaluation.reward;
    stats.best_penalty = scored.front().evaluation.penalty;
    stats.best_spikes = scored.front().evaluation.spikes;
    stats.best_synapses = scored.front().brain.stats().synapse_count;
    stats.best_foods_collected = scored.front().evaluation.foods_collected;
    stats.best_occluded_foods_collected = scored.front().evaluation.occluded_foods_collected;
    stats.best_max_hidden_bias = max_hidden_bias(scored.front().brain);
    stats.best_task_score = scored.front().evaluation.foods_collected;
    stats.best_scalar_display_score = scored.front().evaluation.fitness;

    for (const auto& item : scored) {
        stats.mean_fitness += item.evaluation.fitness;
        stats.mean_reward += item.evaluation.reward;
        stats.mean_penalty += item.evaluation.penalty;
        stats.mean_spikes += static_cast<double>(item.evaluation.spikes);
        stats.mean_synapses += static_cast<double>(item.brain.stats().synapse_count);
        stats.mean_foods_collected += static_cast<double>(item.evaluation.foods_collected);
        stats.mean_occluded_foods_collected += item.evaluation.occluded_foods_collected;
        stats.mean_max_hidden_bias += max_hidden_bias(item.brain);
        if (has_clock_candidate(item.brain)) {
            ++stats.clock_candidate_genomes;
        }
    }

    const double count = static_cast<double>(scored.size());
    stats.mean_fitness /= count;
    stats.mean_reward /= count;
    stats.mean_penalty /= count;
    stats.mean_spikes /= count;
    stats.mean_synapses /= count;
    stats.mean_foods_collected /= count;
    stats.mean_occluded_foods_collected /= count;
    stats.mean_max_hidden_bias /= count;
    stats.mean_task_score = stats.mean_foods_collected;

    return stats;
}

GenerationStats EvolutionRunner::summarize_neat(
    std::size_t generation,
    const std::vector<Genome>& population,
    const std::vector<Species>& species) const
{
    GenerationStats stats;
    stats.generation = generation;
    if (population.empty()) {
        return stats;
    }

    const Genome* best = &population.front();
    for (const auto& genome : population) {
        if (better_genome_for_recording(genome, *best)) {
            best = &genome;
        }
    }

    stats.best_fitness = best->metrics.raw_fitness;
    stats.best_reward = best->metrics.raw_reward;
    stats.best_penalty = best->metrics.raw_penalty;
    stats.best_spikes = best->metrics.raw_spikes;
    stats.best_synapses = best->enabled_connection_count();
    stats.best_foods_collected = best->metrics.raw_foods_collected;
    stats.best_occluded_foods_collected = best->metrics.raw_occluded_foods_collected;
    stats.best_max_hidden_bias = max_hidden_bias(*best);
    stats.best_task_score = best->metrics.task_score_norm;
    stats.best_pareto_rank = best->pareto_rank;
    stats.best_scalar_display_score = best->scalar_display_score;

    double finite_crowding_sum = 0.0;
    std::size_t finite_crowding_count = 0;

    for (const auto& genome : population) {
        stats.mean_fitness += genome.metrics.raw_fitness;
        stats.mean_reward += genome.metrics.raw_reward;
        stats.mean_penalty += genome.metrics.raw_penalty;
        stats.mean_spikes += genome.metrics.raw_spikes;
        stats.mean_synapses += static_cast<double>(genome.enabled_connection_count());
        stats.mean_foods_collected += genome.metrics.raw_foods_collected;
        stats.mean_occluded_foods_collected += genome.metrics.raw_occluded_foods_collected;
        stats.mean_max_hidden_bias += max_hidden_bias(genome);
        stats.mean_task_score += genome.metrics.task_score_norm;
        stats.mean_spike_energy_norm += genome.metrics.spike_energy_norm;
        stats.mean_synapse_count_norm += genome.metrics.synapse_count_norm;
        stats.mean_time_cost_norm += genome.metrics.time_cost_norm;
        stats.mean_neurons += static_cast<double>(genome.nodes.size());
        stats.mean_enabled_synapses += static_cast<double>(genome.enabled_connection_count());
        stats.best_scalar_display_score = std::max(stats.best_scalar_display_score, genome.scalar_display_score);
        stats.best_pareto_rank = std::min(stats.best_pareto_rank, genome.pareto_rank);

        if (genome.pareto_rank == 0) {
            ++stats.number_non_dominated;
        }
        if (std::isfinite(genome.crowding_distance)) {
            finite_crowding_sum += genome.crowding_distance;
            ++finite_crowding_count;
        }
        if (has_clock_candidate(genome, config_.brain)) {
            ++stats.clock_candidate_genomes;
        }
    }

    const double count = static_cast<double>(population.size());
    stats.mean_fitness /= count;
    stats.mean_reward /= count;
    stats.mean_penalty /= count;
    stats.mean_spikes /= count;
    stats.mean_synapses /= count;
    stats.mean_foods_collected /= count;
    stats.mean_occluded_foods_collected /= count;
    stats.mean_max_hidden_bias /= count;
    stats.mean_task_score /= count;
    stats.mean_spike_energy_norm /= count;
    stats.mean_synapse_count_norm /= count;
    stats.mean_time_cost_norm /= count;
    stats.mean_neurons /= count;
    stats.mean_enabled_synapses /= count;
    stats.mean_crowding_distance = finite_crowding_count == 0
        ? 0.0
        : finite_crowding_sum / static_cast<double>(finite_crowding_count);

    stats.species_count = species.size();
    for (const auto& item : species) {
        stats.largest_species_size = std::max(stats.largest_species_size, item.members.size());
    }

    return stats;
}

std::size_t EvolutionRunner::select_parent(const std::vector<ScoredGenome>& scored)
{
    std::size_t best_index = rng_.uniform_index(scored.size());
    for (std::size_t i = 1; i < config_.tournament_size; ++i) {
        const std::size_t candidate = rng_.uniform_index(scored.size());
        if (better_evaluation(scored[candidate].evaluation, scored[best_index].evaluation)) {
            best_index = candidate;
        }
    }
    return best_index;
}

void write_run_metadata_csv(const std::string& path, const EvolutionConfig& config)
{
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Could not open run metadata CSV for writing: " + path);
    }

    output << "key,value\n";
    output << std::setprecision(10);
    output << "ea_mode," << to_string(config.ea_mode) << '\n';
    output << "population_size," << config.population_size << '\n';
    output << "generations," << config.generations << '\n';
    output << "elite_count," << config.elite_count << '\n';
    output << "tournament_size," << config.tournament_size << '\n';
    output << "trials_per_genome," << config.trials_per_genome << '\n';
    output << "recorded_trajectory_trials," << config.recorded_trajectory_trials << '\n';
    output << "seed," << config.seed << '\n';
    output << "sensorimotor_regime," << to_string(config.environment.sensorimotor_regime) << '\n';
    output << "task_regime," << to_string(config.environment.task.regime) << '\n';
    output << "fitness_regime," << to_string(config.environment.fitness.regime) << '\n';
    output << "brain_input_count," << config.brain.input_count << '\n';
    output << "brain_hidden_count," << config.brain.hidden_count << '\n';
    output << "brain_output_count," << config.brain.output_count << '\n';
    output << "brain_has_clock_input," << (config.brain.has_clock_input ? 1 : 0) << '\n';
    output << "brain_clock_input_index," << config.brain.clock_input_index << '\n';
    output << "brain_has_episode_start_input," << (config.brain.has_episode_start_input ? 1 : 0) << '\n';
    output << "brain_episode_start_input_index," << config.brain.episode_start_input_index << '\n';
    output << "brain_background_activity_enabled," << (config.brain.background_activity_enabled ? 1 : 0) << '\n';
    output << "brain_background_event_rate_hz," << config.brain.background_event_rate_hz << '\n';
    output << "brain_background_event_current," << config.brain.background_event_current << '\n';
    output << "brain_max_bias_fraction_of_threshold," << config.brain.max_bias_fraction_of_threshold << '\n';
    output << "brain_synaptic_gain," << config.brain.synaptic_gain << '\n';
    output << "brain_seed_input_output_synapses," << (config.brain.seed_input_output_synapses ? 1 : 0) << '\n';
    output << "brain_seed_input_output_weight," << config.brain.seed_input_output_weight << '\n';
    output << "mutation_bias_sigma," << config.mutation.bias_sigma << '\n';
    output << "mutation_hidden_bias_min," << config.mutation.hidden_bias_min << '\n';
    output << "mutation_hidden_bias_max," << config.mutation.hidden_bias_max << '\n';
    output << "mutation_hidden_bias_jump_min_magnitude," << config.mutation.hidden_bias_jump_min_magnitude << '\n';
    output << "mutation_hidden_bias_jump_probability," << config.mutation.hidden_bias_jump_probability << '\n';
    output << "mutation_add_reciprocal_motif_probability," << config.mutation.add_reciprocal_motif_probability << '\n';
    output << "mutation_mutate_clock_threshold_probability," << config.mutation.mutate_clock_threshold_probability << '\n';
    output << "mutation_clock_threshold_sigma," << config.mutation.clock_threshold_sigma << '\n';
    output << "mutation_clock_threshold_min," << config.mutation.clock_threshold_min << '\n';
    output << "mutation_clock_threshold_max," << config.mutation.clock_threshold_max << '\n';
    output << "neat_initial_hidden_count," << config.neat.initial_hidden_count << '\n';
    output << "neat_recorded_pareto_front_count," << config.neat.recorded_pareto_front_count << '\n';
    output << "neat_recorded_pareto_front_trials," << config.neat.recorded_pareto_front_trials << '\n';
    output << "neat_interspecies_mate_probability," << config.neat.interspecies_mate_probability << '\n';
    output << "neat_mutation_mutate_weight_probability," << config.neat.mutation.mutate_weight_probability << '\n';
    output << "neat_mutation_weight_sigma," << config.neat.mutation.weight_sigma << '\n';
    output << "neat_mutation_weight_reset_probability," << config.neat.mutation.weight_reset_probability << '\n';
    output << "neat_mutation_mutate_node_probability," << config.neat.mutation.mutate_node_probability << '\n';
    output << "neat_mutation_clock_threshold_sigma," << config.neat.mutation.clock_threshold_sigma << '\n';
    output << "neat_mutation_clock_threshold_min," << config.neat.mutation.clock_threshold_min << '\n';
    output << "neat_mutation_clock_threshold_max," << config.neat.mutation.clock_threshold_max << '\n';
    output << "neat_mutation_add_node_probability," << config.neat.mutation.add_node_probability << '\n';
    output << "neat_mutation_add_connection_probability," << config.neat.mutation.add_connection_probability << '\n';
    output << "neat_mutation_add_reciprocal_motif_probability," << config.neat.mutation.add_reciprocal_motif_probability << '\n';
    output << "neat_mutation_enable_disable_probability," << config.neat.mutation.enable_disable_probability << '\n';
    output << "neat_mutation_crossover_probability," << config.neat.mutation.crossover_probability << '\n';
    output << "neat_speciation_compatibility_threshold," << config.neat.speciation.compatibility_threshold << '\n';
    output << "neat_speciation_c1," << config.neat.speciation.c1 << '\n';
    output << "neat_speciation_c2," << config.neat.speciation.c2 << '\n';
    output << "neat_speciation_c3," << config.neat.speciation.c3 << '\n';
    output << "objective_set," << to_string(config.neat.objectives.objective_set) << '\n';
    output << "objective_target_foods_per_trial," << config.neat.objectives.target_foods_per_trial << '\n';
    output << "objective_target_spike_rate," << config.neat.objectives.target_spike_rate << '\n';
    output << "objective_synapse_budget," << config.neat.objectives.synapse_budget << '\n';
    output << "objective_neuron_budget," << config.neat.objectives.neuron_budget << '\n';
    output << "environment_width," << config.environment.width << '\n';
    output << "environment_height," << config.environment.height << '\n';
    output << "environment_target_radius," << config.environment.target_radius << '\n';
    output << "environment_min_target_distance," << config.environment.min_target_distance << '\n';
    output << "environment_max_speed," << config.environment.max_speed << '\n';
    output << "environment_max_turn_rate," << config.environment.max_turn_rate << '\n';
    output << "environment_motor_gain," << config.environment.motor_gain << '\n';
    output << "environment_fov_degrees," << config.environment.fov_degrees << '\n';
    output << "environment_initial_heading_fov_fraction," << config.environment.initial_heading_fov_fraction << '\n';
    output << "environment_clock_input_enabled," << (config.environment.clock_input_enabled ? 1 : 0) << '\n';
    output << "environment_clock_input_value," << config.environment.clock_input_value << '\n';
    output << "environment_episode_start_input_enabled," << (config.environment.episode_start_input_enabled ? 1 : 0) << '\n';
    output << "environment_episode_start_pulse_brain_steps," << config.environment.episode_start_pulse_brain_steps << '\n';
    output << "task_cue_visible_steps," << config.environment.task.cue_visible_steps << '\n';
    output << "task_occlusion_min_steps," << config.environment.task.occlusion_min_steps << '\n';
    output << "task_occlusion_max_steps," << config.environment.task.occlusion_max_steps << '\n';
    output << "task_reveal_distance," << config.environment.task.reveal_distance << '\n';
    output << "environment_dt," << config.environment.env_dt << '\n';
    output << "environment_episode_steps," << config.environment.episode_steps << '\n';
    output << "environment_brain_steps_per_env_step," << config.environment.brain_steps_per_env_step << '\n';
    output << "environment_food_reward," << config.environment.food_reward << '\n';
    output << "fitness_progress_reward_scale," << config.environment.fitness.progress_reward_scale << '\n';
    output << "fitness_distance_improvement_reward_scale," << config.environment.fitness.distance_improvement_reward_scale << '\n';
    output << "fitness_visibility_reward_scale," << config.environment.fitness.visibility_reward_scale << '\n';
    output << "fitness_final_distance_penalty," << config.environment.fitness.final_distance_penalty << '\n';
    output << "environment_spike_penalty," << config.environment.spike_penalty << '\n';
    output << "environment_synapse_penalty," << config.environment.synapse_penalty << '\n';
    output << "environment_neuron_penalty," << config.environment.neuron_penalty << '\n';
    output << "environment_turn_penalty," << config.environment.turn_penalty << '\n';
    output << "environment_inactivity_penalty," << config.environment.inactivity_penalty << '\n';
    output << "environment_turn_budget_per_step," << config.environment.turn_budget_per_step << '\n';
    output << "environment_inactivity_budget_per_step," << config.environment.inactivity_budget_per_step << '\n';
    output << "environment_spike_budget_per_neuron_per_brain_step," << config.environment.spike_budget_per_neuron_per_brain_step << '\n';
    output << "environment_synapse_budget," << config.environment.synapse_budget << '\n';
    output << "environment_neuron_budget," << config.environment.neuron_budget << '\n';
}

void write_generation_stats_csv(const std::string& path, const std::vector<GenerationStats>& stats)
{
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Could not open generation stats CSV for writing: " + path);
    }

    output << "generation,best_fitness,mean_fitness,best_reward,mean_reward,best_penalty,mean_penalty,"
              "best_spikes,mean_spikes,best_synapses,mean_synapses,best_foods_collected,mean_foods_collected,"
              "best_occluded_foods_collected,mean_occluded_foods_collected,"
              "best_max_hidden_bias,mean_max_hidden_bias,clock_candidate_genomes,best_task_score,mean_task_score,"
              "best_pareto_rank,number_non_dominated,mean_spike_energy_norm,mean_synapse_count_norm,"
              "mean_time_cost_norm,mean_neurons,mean_enabled_synapses,species_count,largest_species_size,"
              "mean_crowding_distance,best_scalar_display_score\n";
    output << std::setprecision(10);
    for (const auto& row : stats) {
        output << row.generation << ','
               << row.best_fitness << ','
               << row.mean_fitness << ','
               << row.best_reward << ','
               << row.mean_reward << ','
               << row.best_penalty << ','
               << row.mean_penalty << ','
               << row.best_spikes << ','
               << row.mean_spikes << ','
               << row.best_synapses << ','
               << row.mean_synapses << ','
               << row.best_foods_collected << ','
               << row.mean_foods_collected << ','
               << row.best_occluded_foods_collected << ','
               << row.mean_occluded_foods_collected << ','
               << row.best_max_hidden_bias << ','
               << row.mean_max_hidden_bias << ','
               << row.clock_candidate_genomes << ','
               << row.best_task_score << ','
               << row.mean_task_score << ','
               << row.best_pareto_rank << ','
               << row.number_non_dominated << ','
               << row.mean_spike_energy_norm << ','
               << row.mean_synapse_count_norm << ','
               << row.mean_time_cost_norm << ','
               << row.mean_neurons << ','
               << row.mean_enabled_synapses << ','
               << row.species_count << ','
               << row.largest_species_size << ','
               << row.mean_crowding_distance << ','
               << row.best_scalar_display_score << '\n';
    }
}

} // namespace neuroevo
