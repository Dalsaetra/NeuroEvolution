#include "fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
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
    mutation.add_autapse_probability = 0;
    mutation.remove_synapse_probability = 0;
    mutation.rewire_synapse_probability = 0;
    mutation.remove_neuron_probability = 0;
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
    EcosystemConfig config = neuroevo::controlled_config();
    config.width = config.height = 24;
    config.initial_creatures = 0;
    config.max_population = 32;
    config.shelters = config.grazing_patches = config.fruit_patches = config.pods = 0;
    // Isolate feeding and lineage continuity before asking the ancestor to
    // evolve a shelter strategy in the full shared environment.
    config.calm_duration = 100000;
    config.mutation = exact_inheritance();
    return config;
}

void initial_ancestor_variation()
{
    require(EcosystemConfig{}.mutate_initial_ancestors,"Initial ancestor mutation should default to enabled");
    const auto saved=[](const EcosystemWorld& w){std::ostringstream s;w.save_checkpoint(s);return s.str();};
    for(bool frontier:{false,true}) {
        EcosystemConfig cfg;cfg.initial_creatures=12;cfg.nursery_frontier=frontier;cfg.food_distribution=FoodDistribution::Scattered;cfg.reproduction=false;
        cfg.mutation.mass_mutation_probability=cfg.mutation.carnivory_mutation_probability=1;
        EcosystemWorld varied(cfg), repeated(cfg);
        require(saved(varied)==saved(repeated),"Founder mutations are not seed-reproducible");
        auto plain_cfg=cfg;plain_cfg.mutate_initial_ancestors=false;
        EcosystemWorld plain(plain_cfg);
        require(varied.terrain==plain.terrain,"Founder mutation changed map generation");
        const auto template_brain=make_sparse_ancestral_brain(cfg);
        double body_energy=0;bool different_brains=false,different_bodies=false;
        for(std::size_t i=0;i<varied.creatures.size();++i) {
            const auto& c=varied.creatures[i];const auto& p=plain.creatures[i];
            require(same_genome(p.brain,template_brain)&&p.genome_id==1,"Disabled toggle changed ancestral template");
            require(p.body.mass==cfg.founder_mass&&p.body.carnivory==cfg.founder_carnivory,"Disabled toggle mutated body genes");
            require(c.genome_id==c.id&&c.parent_id==0&&c.generation==0,"Varied founders lost genome or ancestry identity");
            require(c.health==varied.max_health(c)&&c.energy==cfg.founder_energy,"Mutated founder health or energy is incorrect");
            require(c.position.x==p.position.x&&c.position.y==p.position.y&&c.heading==p.heading,"Mutation changed founder placement");
            body_energy+=cfg.body_energy_per_mass*c.body.mass;
            different_brains|=!same_genome(c.brain,varied.creatures.front().brain);
            different_bodies|=c.body.mass!=varied.creatures.front().body.mass;
        }
        require(different_brains&&different_bodies,"Founders shared a single brain or body mutation");
        require(std::abs(varied.totals.external_body_energy-body_energy)<1e-8,"Mutated founder mass broke body energy accounting");
        auto other_cfg=cfg;++other_cfg.seed;EcosystemWorld other(other_cfg);
        require(!same_genome(varied.creatures.front().brain,other.creatures.front().brain),"Changing the seed did not change founder mutation");
        std::istringstream input(saved(varied));auto resumed=EcosystemWorld::load_checkpoint(input);
        require(saved(varied)==saved(resumed),"Resume reapplied founder mutation or lost toggle");
        varied.step();resumed.step();
        require(saved(varied)==saved(resumed),"Varied founder checkpoint continuation diverged");
    }
    // Force the strong preset to add exactly one neuron; copy/slight settings
    // and the independent birth-pruning probability must not alter initialization.
    EcosystemConfig cfg;cfg.initial_creatures=4;cfg.mutation=exact_inheritance();
    cfg.mutation.copy_probability=1;cfg.mutation.slight_probability=0;
    cfg.mutation.strong.structural_probability=1;cfg.mutation.strong.local_edits=0;
    cfg.mutation.add_neuron_probability=1;cfg.mutation.disconnected_neuron_prune_probability=1;
    EcosystemWorld once(cfg);
    for(const auto& c:once.creatures)
        require(c.brain.config().hidden_count==make_sparse_ancestral_brain(cfg).config().hidden_count+1,
            "Initialization did not apply the strong preset exactly once");
    auto solo=make_ancestral_nursery(cfg);
    require(solo.creatures.front().brain.config().hidden_count==make_sparse_ancestral_brain(cfg).config().hidden_count+1,
        "Solo nursery did not apply ancestor mutation");
    cfg.sparse_ancestor=false;EcosystemWorld random_enabled(cfg);
    cfg.mutate_initial_ancestors=false;EcosystemWorld random_disabled(cfg);
    for(std::size_t i=0;i<random_enabled.creatures.size();++i)
        require(same_genome(random_enabled.creatures[i].brain,random_disabled.creatures[i].brain),
            "Ancestor toggle mutated random founders");
}

void sparse_genome_contract()
{
    EcosystemConfig config = neuroevo::controlled_config();
    const Brain ancestor = make_sparse_ancestral_brain(config);
    require(ancestor.config().input_count == eco_input_count
        && ancestor.config().hidden_count == 0
        && ancestor.config().output_count == eco_output_count,
        "The ancestor must connect sensors directly to motors without hidden neurons");
    require(ancestor.synapses().size() == 10,
        "The ancestral circuit must contain only the ten survival connections");

    const std::size_t body = eco_sectors * eco_sector_channels + 8;
    const std::size_t contact = eco_sectors * eco_sector_channels + 4;
    const std::set<std::size_t> allowed_sources{
        body, body + 6, contact, contact + 1, contact + 3, 2,
        eco_sector_channels + 2, 2 * eco_sector_channels + 2};
    std::set<std::size_t> sensory_sources;
    for (const auto& synapse : ancestor.synapses()) {
        sensory_sources.insert(synapse.pre);
        require(synapse.pre < eco_input_count && synapse.post >= eco_input_count
            && synapse.post < eco_input_count + 4,
            "Ancestor must be feed-forward with call and attack disconnected");
    }
    require(sensory_sources == allowed_sources,
        "Ancestor must use only energy, ingestion, contact and general food proximity");
    for (const auto& neuron : ancestor.neurons())
        require(neuron.bias == 0 && neuron.background_sensitivity == 0,
            "Ancestor must not seed autonomous firing or spontaneous motor policies");

    auto legacy = config;
    legacy.extended_senses = false;
    legacy.brain.input_count = eco_legacy_input_count;
    const auto legacy_ancestor = make_sparse_ancestral_brain(legacy);
    require(legacy_ancestor.config().hidden_count == 0 && legacy_ancestor.synapses().size() == 10,
        "Legacy sensor layouts must use the same minimal circuit");

    Brain child = ancestor;
    Random rng(91);
    child.mutate(exact_inheritance(), rng);
    require(same_genome(ancestor, child),
        "Zero mutation must preserve a sparse genome instead of wiring every unused sensor");

    Brain restarted = ancestor;
    restarted.reset_state();
    std::vector<double> senses(eco_input_count, 0.0);
    for (int i = 0; i < 500; ++i)
        require(restarted.step(senses).motor_outputs[0] == 0,
            "A silent ancestor must not have an autonomous movement rhythm");
    senses[body] = 0.1;
    bool moved = false;
    for (int i = 0; i < 500 && !moved; ++i)
        moved = restarted.step(senses).motor_outputs[0] > 0;
    require(moved, "Low newborn energy must initiate movement without hidden neurons");
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
    EcosystemConfig config = neuroevo::controlled_config();
    const auto parent = make_sparse_ancestral_brain(config);
    auto parameters = exact_inheritance();
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
    require(std::abs(child.synapses().back().weight) <= 0.5,
        "New branch must initially have weak influence");
    auto exact = parent;
    auto disabled = exact_inheritance();
    exact.mutate(disabled, rng);
    require(same_genome(parent, exact), "Disabled stable mutations changed the genome");
}

void default_nursery_trials()
{
    // Exercise the actual nursery ecology, including moving food, body costs,
    // funded reproduction, weather and ordinary birth mutations.
    for (const bool varied : {false, true}) {
        for (const std::uint64_t seed : {1ULL, 8ULL, 42ULL}) {
            EcosystemConfig config;
            config.seed = seed;
            config.mutate_initial_ancestors = varied;
            EcosystemWorld world(config);
            bool grandchild_born = false, evolved_child = false;
            const auto ancestor = make_sparse_ancestral_brain(config);
            for (int step = 0; step < 4800 && !world.creatures.empty()
                && !world.capacity_limited; ++step) {
                world.step();
                for (const auto& creature : world.creatures) {
                    grandchild_born |= creature.generation >= 2;
                    evolved_child |= creature.generation > 0 && creature.age >= config.maturity_age
                        && !same_genome(ancestor, creature.brain);
                }
            }
            std::cout << "nursery seed=" << seed << " varied=" << varied
                << " population=" << world.creatures.size() << " births=" << world.totals.births
                << " mature=" << world.totals.mature_offspring << '\n';
            require(!world.creatures.empty() && world.totals.energy_gained > 0,
                "Minimal ancestors must sustain a feeding population in the default nursery");
            require(grandchild_born && world.totals.mature_offspring > 0,
                "Minimal ancestors must sustain multiple generations in the default nursery");
            require(evolved_child, "Minimal ancestors must produce viable mutated descendants");
        }
    }
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
        initial_ancestor_variation();
        stable_mutation_contract();
        crowding_escape();
        solo_lineage_trial();
        default_nursery_trials();
        std::cout << "sparse ancestral brain and solo lineage trial passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ancestor test failed: " << error.what() << '\n';
        return 1;
    }
}
