#include "neuroevo/ecosystem.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

namespace {

using namespace neuroevo;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

MutationConfig exact_inheritance()
{
    MutationConfig mutation;
    mutation.mutate_weight_probability = 0;
    mutation.mutate_neuron_probability = 0;
    mutation.add_synapse_probability = 0;
    mutation.add_neuron_probability = 0;
    mutation.add_reciprocal_motif_probability = 0;
    mutation.remove_synapse_probability = 0;
    mutation.remove_neuron_probability = 0;
    mutation.mutate_clock_threshold_probability = 0;
    mutation.hidden_bias_jump_probability = 0;
    return mutation;
}

bool same_genome(const Brain& a, const Brain& b)
{
    if (a.config().input_count != b.config().input_count
        || a.config().hidden_count != b.config().hidden_count
        || a.config().output_count != b.config().output_count
        || a.synapses().size() != b.synapses().size()
        || a.neurons().size() != b.neurons().size()) return false;
    for (std::size_t i = 0; i < a.neurons().size(); ++i) {
        const auto& x = a.neurons()[i];
        const auto& y = b.neurons()[i];
        if (x.position.x != y.position.x || x.position.y != y.position.y
            || x.bias != y.bias || x.threshold != y.threshold
            || x.background_sensitivity != y.background_sensitivity) return false;
    }
    for (std::size_t i = 0; i < a.synapses().size(); ++i) {
        const auto& x = a.synapses()[i];
        const auto& y = b.synapses()[i];
        if (x.pre != y.pre || x.post != y.post || x.weight != y.weight
            || x.delay_steps != y.delay_steps) return false;
    }
    return true;
}

EcosystemConfig nursery_config()
{
    EcosystemConfig config;
    config.width = config.height = 24;
    config.initial_creatures = 0;
    config.max_population = 32;
    config.shelters = config.grazing_patches = config.fruit_patches = config.pods = 0;
    config.establishment = false;
    // Isolate feeding and lineage continuity before asking the ancestor to
    // evolve a shelter strategy in the full shared environment.
    config.calm_duration = 100000;
    config.mutation = exact_inheritance();
    return config;
}

void sparse_genome_contract()
{
    EcosystemConfig config;
    const Brain ancestor = make_sparse_ancestral_brain(config);
    require(ancestor.config().input_count == eco_input_count
        && ancestor.config().hidden_count == 7
        && ancestor.config().output_count == eco_output_count,
        "The ancestor must use the ecosystem interface with only seven hidden neurons");
    require(ancestor.config().background_activity_enabled,
        "The ancestral locomotion circuit must permit low-rate spontaneous restarts");
    require(ancestor.synapses().size() == 41,
        "The ancestral circuit must stay sparse and reviewable");

    std::set<std::size_t> sensory_sources;
    const std::size_t output_begin = eco_input_count + ancestor.config().hidden_count;
    for (const auto& synapse : ancestor.synapses()) {
        if (synapse.pre < eco_input_count) sensory_sources.insert(synapse.pre);
        require(synapse.post < output_begin + eco_output_count, "Ancestral synapse endpoint is invalid");
    }
    require(sensory_sources.size() == 25,
        "The ancestor should include storm, sheltered state, and five shelter directions");
    const std::size_t body = eco_sectors * eco_sector_channels + 8;
    std::set<std::size_t> environmental{body + 4, body + 5};
    for (std::size_t sector = 0; sector < eco_sectors; ++sector)
        environmental.insert(eco_shelter_offset + sector);
    for (const auto source : environmental) {
        const auto edge = std::find_if(ancestor.synapses().begin(), ancestor.synapses().end(),
            [source](const auto& synapse) { return synapse.pre == source; });
        require(edge != ancestor.synapses().end() && edge->post >= eco_input_count
            && edge->post < output_begin && edge->weight != 0
            && std::abs(edge->weight) * config.brain.synaptic_gain < 5,
            "Environmental cues need weak connections into hidden neurons");
    }
    auto legacy = config;
    legacy.extended_senses = false;
    legacy.brain.input_count = legacy.brain.sensory_input_count = eco_legacy_input_count;
    const auto legacy_ancestor = make_sparse_ancestral_brain(legacy);
    require(legacy_ancestor.synapses().size() == 36,
        "Legacy ancestor must seed only the two available environmental cues");
    for (std::size_t sector = 0; sector < eco_sectors; ++sector) {
        for (std::size_t channel : {std::size_t{3}, std::size_t{4}, std::size_t{5}, std::size_t{6},
                 std::size_t{8}, std::size_t{9}, std::size_t{10}, std::size_t{11},
                 std::size_t{12}, std::size_t{13}}) {
            require(!sensory_sources.count(sector * eco_sector_channels + channel),
                "The ancestor must ignore food identity, pods, and social vision");
        }
    }

    Brain child = ancestor;
    Random rng(91);
    child.mutate(exact_inheritance(), rng);
    require(same_genome(ancestor, child),
        "Zero mutation must preserve a sparse genome instead of wiring every unused sensor");

    Brain restarted = ancestor;
    restarted.reset_state();
    Random activity_rng(312);
    std::vector<double> silence(eco_input_count, 0.0);
    bool moved = false;
    for (int i = 0; i < 500 && !moved; ++i)
        moved = restarted.step(silence, &activity_rng).motor_outputs[0] > 0;
    require(moved, "Low-rate background activity did not restart a silent locomotion circuit");
}

void solo_lineage_trial()
{
    const auto config = nursery_config();
    EcosystemWorld world = make_ancestral_nursery(config);
    const EcoCreature founder = world.creatures.front();

    bool grandchild_born = false;
    double lifetime_peak_energy = founder.energy;
    std::size_t feeding_streak = 0, longest_feeding_streak = 0;
    for (std::size_t step = 0; step < 20000 && !world.creatures.empty()
        && !world.capacity_limited && !grandchild_born; ++step) {
        world.step();
        const bool founder_ingested = std::any_of(world.events.begin(), world.events.end(),
            [](const EcoEvent& event) { return event.type == "ingestion" && event.creature == 1; });
        feeding_streak = founder_ingested ? feeding_streak + 1 : 0;
        longest_feeding_streak = std::max(longest_feeding_streak, feeding_streak);
        for (const auto& creature : world.creatures) lifetime_peak_energy = std::max(lifetime_peak_energy, creature.energy);
        grandchild_born = std::any_of(world.creatures.begin(), world.creatures.end(),
            [](const EcoCreature& creature) { return creature.generation >= 2; });
    }

    if (!grandchild_born) {
        double best_energy = 0, oldest = 0;
        std::uint64_t best_generation = 0;
        for (const auto& creature : world.creatures) {
            best_energy = std::max(best_energy, creature.energy);
            oldest = std::max(oldest, creature.age);
            best_generation = std::max(best_generation, creature.generation);
        }
        std::cerr << "lineage diagnostic: t=" << world.time() << " population=" << world.creatures.size()
            << " births=" << world.totals.births << " deaths=" << world.totals.deaths
            << " gained=" << world.totals.energy_gained << " best_energy=" << best_energy
            << " peak_energy=" << lifetime_peak_energy << " oldest=" << oldest
            << " max_generation=" << best_generation << " movement_cost=" << world.totals.movement
            << " forage_cost=" << world.totals.foraging << " spikes=" << world.totals.spikes << '\n';
    }
    require(world.totals.energy_gained > 0, "The solo ancestor must find and digest food");
    require(longest_feeding_streak >= 8,
        "The ancestor must remain on a feeding patch for at least 0.8 seconds");
    require(world.totals.births >= 2, "The solo founder must produce a continuing lineage");
    require(world.totals.mature_offspring >= 1,
        "At least one child must survive to reproductive maturity");
    require(world.totals.natural_spiking_breeders >= 1 && grandchild_born,
        "An ancestral child must feed and produce a grandchild through normal reproduction");
    for (const auto& creature : world.creatures) {
        require(same_genome(founder.brain, creature.brain),
            "The exact-inheritance nursery must preserve the ancestral genome across generations");
    }
}

void stable_mutation_contract()
{
    EcosystemConfig config;
    const auto parent = make_sparse_ancestral_brain(config);
    auto parameters = exact_inheritance();
    parameters.stable = true;
    parameters.mutate_weight_probability = 1;
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        auto child = parent;
        Random rng(seed);
        child.mutate(parameters, rng);
        require(child.synapses().size() == parent.synapses().size(), "Parameter mutation changed topology");
        std::size_t changed = 0;
        for (std::size_t i = 0; i < child.synapses().size(); ++i) {
            const auto& a = parent.synapses()[i]; const auto& b = child.synapses()[i];
            require(a.pre == b.pre && a.post == b.post && a.delay_steps == b.delay_steps,
                "Weight mutation changed routing or timing");
            changed += a.weight != b.weight;
        }
        require(changed <= 2 && changed > 0, "Stable mutation exceeded its local edit budget");
    }
    auto structural = parameters;
    structural.add_neuron_probability = 1;
    auto child = parent;
    Random rng(72);
    child.mutate(structural, rng);
    require(child.config().hidden_count == parent.config().hidden_count + 1,
        "Stable growth did not add a neuron");
    const auto output = parent.config().input_count + parent.config().hidden_count;
    for (std::size_t i = 0; i < parent.synapses().size(); ++i) {
        const auto& a = parent.synapses()[i]; const auto& b = child.synapses()[i];
        require(b.pre == a.pre + (a.pre >= output) && b.post == a.post + (a.post >= output)
            && a.weight == b.weight && a.delay_steps == b.delay_steps,
            "Structural growth disturbed the inherited pathway or also mutated parameters");
    }
    require(std::abs(child.synapses().back().weight) <= 0.15,
        "New branch must initially have weak influence");
    auto exact = parent;
    auto disabled = exact_inheritance(); disabled.stable = true;
    exact.mutate(disabled, rng);
    require(same_genome(parent, exact), "Disabled stable mutations changed the genome");
}

void crowding_escape()
{
    auto config = nursery_config();
    config.reproduction = false;
    config.basal_cost = config.movement_cost = config.turn_cost = config.forage_cost = 0;
    config.neuron_cost = config.synapse_cost = config.spike_cost = config.storm_cost = 0;
    EcosystemWorld world(config, false);
    EcoCreature first, second;
    first.id = first.genome_id = 1;
    second.id = second.genome_id = 2;
    first.position = {11.75, 12.0};
    second.position = {12.25, 12.0};
    first.heading = 0;
    second.heading = 3.14159265358979323846;
    first.energy = second.energy = config.founder_energy;
    first.age = second.age = 1;
    first.brain = make_sparse_ancestral_brain(config);
    second.brain = first.brain;
    first.neural_rng = Random(101);
    second.neural_rng = Random(202);
    world.creatures = {first, second};
    world.next_creature_id = 3;
    double widest = length(first.position - second.position);
    for (int i = 0; i < 300; ++i) {
        world.step();
        widest = std::max(widest, length(world.creatures[0].position - world.creatures[1].position));
    }
    require(widest > 1.0, "Contact-aware ancestors remained mutually blocked until an external removal");
}

} // namespace

int main()
{
    try {
        sparse_genome_contract();
        stable_mutation_contract();
        crowding_escape();
        solo_lineage_trial();
        std::cout << "sparse ancestral brain and solo lineage trial passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ancestor test failed: " << error.what() << '\n';
        return 1;
    }
}
