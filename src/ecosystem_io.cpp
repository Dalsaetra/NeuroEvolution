#include "neuroevo/ecosystem.hpp"
#include "checkpoint_fields.hpp"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <numeric>
#include <set>

namespace neuroevo {
namespace {
void quoted(std::ostream& s, const std::string& text)
{
    s << '"';
    for (const unsigned char ch : text) {
        if (ch == '"' || ch == '\\') s << '\\' << ch;
        else if (ch < 32) s << "\\u00" << "0123456789abcdef"[ch >> 4] << "0123456789abcdef"[ch & 15];
        else s << ch;
    }
    s << '"';
}
template<class Range, class Fn> void array(std::ostream& s, const Range& range, Fn fn)
{
    s << '['; bool first = true;
    for (const auto& value : range) { if (!first) s << ','; first = false; fn(value); }
    s << ']';
}
void graph(std::ostream& s, const EcoCreature& c)
{
    const auto& b = c.brain;
    s << "\"inputs\":" << b.config().input_count << ",\"outputs\":" << b.config().output_count << ",\"neurons\":";
    array(s,b.neurons(),[&](const Brain::Neuron& n) {
        s << "{\"x\":" << n.position.x << ",\"y\":" << n.position.y << ",\"threshold\":" << n.threshold << '}';
    });
    s << ",\"synapses\":";
    array(s,b.synapses(),[&](const Brain::Synapse& e) {
        s << "{\"pre\":" << e.pre << ",\"post\":" << e.post << ",\"weight\":" << e.weight << ",\"delay\":" << e.delay_steps << '}';
    });
}
void totals_json(std::ostream& s, const EcoTotals& t)
{
    s << "{\"births\":" << t.births << ",\"deaths\":" << t.deaths << ",\"maturations\":" << t.maturations
      << ",\"spikes\":" << t.spikes << ",\"pods_opened\":" << t.pods_opened
      << ",\"consumed_biomass\":" << t.consumed_biomass << ",\"regrown_biomass\":" << t.regrown_biomass
      << ",\"spoiled_biomass\":" << t.spoiled_biomass << ",\"energy_gained\":" << t.energy_gained
      << ",\"metabolism\":" << t.metabolism << ",\"movement\":" << t.movement << ",\"turning\":" << t.turning
      << ",\"foraging\":" << t.foraging << ",\"calling\":" << t.calling << ",\"neural\":" << t.neural
      << ",\"exposure\":" << t.exposure << ",\"reproduction_overhead\":" << t.reproduction_overhead
      << ",\"discarded_energy\":" << t.discarded_energy
      << ",\"immigrants\":" << t.immigrants << ",\"immigrant_mutations\":" << t.immigrant_mutations
      << ",\"immigrant_slight_mutations\":" << t.immigrant_slight_mutations
      << ",\"immigrant_strong_mutations\":" << t.immigrant_strong_mutations
      << ",\"immigrant_clones\":" << t.immigrant_clones << ",\"immigrant_random\":" << t.immigrant_random
      << ",\"archive_fallbacks\":" << t.archive_fallbacks << ",\"archive_empty_checks\":" << t.archive_empty_checks
      << ",\"immigrant_energy\":" << t.immigrant_energy
      << ",\"natural_spiking_breeders\":" << t.natural_spiking_breeders << ",\"mature_offspring\":" << t.mature_offspring
      << ",\"founder_births\":" << t.founder_births << ",\"immigrant_births\":" << t.immigrant_births
      << ",\"descendant_births\":" << t.descendant_births << ",\"births_first_100s\":" << t.births_first_100s << '}';
}
void read_event(std::istream& s, EcoEvent& e)
{
    checkpoint::read(s,e.time,e.type,e.creature,e.other,e.resource,e.amount);
}
void write_event(std::ostream& s, const EcoEvent& e)
{
    checkpoint::write(s,e.time,e.type,e.creature,e.other,e.resource,e.amount);
}
}

void EcosystemWorld::save_checkpoint(std::ostream& s) const
{
    s << std::setprecision(std::numeric_limits<double>::max_digits10);
    checkpoint::write(s,"NEUROEVO_ECOSYSTEM_15");
    checkpoint::write_tuple(s,checkpoint::world_config_fields(config));
    checkpoint::write_tuple(s,checkpoint::brain_fields(config.brain));
    checkpoint::write_tuple(s,checkpoint::mutation_fields(config.mutation));
    checkpoint::write_tuple(s,checkpoint::establishment_config_fields(config));
    checkpoint::write_tuple(s,checkpoint::food_energy_config_fields(config));
    checkpoint::write_tuple(s,checkpoint::calibrated_brain_fields(config.brain));
    checkpoint::write_tuple(s,checkpoint::interface_config_fields(config));
    checkpoint::write(s,config.mutation.stable);
    checkpoint::write(s,config.nursery_frontier,config.nursery_size,config.nursery_food_energy,
        config.nursery_food_capacity,config.nursery_food_regrowth);
    checkpoint::write(s,config.nursery_exit_width);
    checkpoint::write(s,config.shelter_size);
    checkpoint::write(s,config.nursery_food_patches,config.nursery_food_relocates);
    checkpoint::write(s,config.mutation.remove_neuron_probability);
    checkpoint::write(s,config.outdoor_food_relocates,config.graze_decay,config.fruit_decay,
        config.shelter_food_energy,config.shelter_food_capacity,config.shelter_food_regrowth);
    checkpoint::write(s,config.nursery_food_decay);
    checkpoint::write(s,step_index,next_creature_id,fruit_a_rich,capacity_limited);
    checkpoint::write_tuple(s,checkpoint::total_fields(totals));
    checkpoint::write_tuple(s,checkpoint::establishment_total_fields(totals));
    map_rng.save_state(s); mutation_rng.save_state(s); conflict_rng.save_state(s);
    immigration_rng.save_state(s);
    checkpoint::write(s,immigration_deck_cursor,next_immigration_check,last_immigration_time,support_stable_since,immigration_withdrawn);
    for (const auto origin : immigration_deck) checkpoint::write_value(s,origin);
    s << '\n';
    checkpoint::write(s,terrain.size());
    for (const auto value : terrain) checkpoint::write_value(s,value);
    s << '\n';
    checkpoint::write(s,resources.size());
    for (const auto& r : resources)
        checkpoint::write(s,r.id,r.kind,r.position.x,r.position.y,r.stock,r.capacity,r.regrowth,
            r.energy_per_unit,r.pod_state,r.progress,r.opened_at,r.shelter_food);
    checkpoint::write(s,creatures.size());
    for (const auto& c : creatures) {
        checkpoint::write(s,c.id,c.parent_id,c.generation,c.position.x,c.position.y,c.heading,c.energy,
            c.age,c.last_birth,c.speed,c.turn,c.ingestion_pulse,c.digestion_pulse,c.controller,
            c.spikes,c.step_spikes,c.offspring,c.energy_gained,c.energy_spent,c.pod_work,c.exposed_time,c.matured);
        checkpoint::write(s,c.origin,c.genome_id,c.source_id,c.feeding_bouts,c.last_fed_time);
        checkpoint::write(s,c.action.forward,c.action.left,c.action.right,c.action.forage,c.action.call);
        for (const auto v : c.eaten) checkpoint::write_value(s,v);
        s << '\n';
        checkpoint::write(s,c.digestion.size());
        for (const auto& p : c.digestion) checkpoint::write(s,p.due,p.energy,p.kind);
        c.neural_rng.save_state(s);
        c.brain.save_state(s);
    }
    checkpoint::write(s,archive.size());
    for (const auto& entry : archive) {
        checkpoint::write(s,entry.genome_id,entry.source_id,entry.niche,entry.score,entry.trials.size());
        for (const auto& trial : entry.trials) checkpoint::write_tuple(s,checkpoint::archive_trial_fields(trial));
        entry.genome.save_state(s);
    }
    checkpoint::write(s,newborn_evaluations.size());
    for (const auto& [id, evaluation] : newborn_evaluations) {
        checkpoint::write(s,id,evaluation.score,evaluation.trials.size());
        for (const auto& trial : evaluation.trials) checkpoint::write_tuple(s,checkpoint::newborn_trial_fields(trial));
    }
    checkpoint::write(s,events.size());
    for (const auto& e : events) write_event(s,e);
    checkpoint::write(s,"END_ECOSYSTEM");
}

EcosystemWorld EcosystemWorld::load_checkpoint(std::istream& s)
{
    std::string version;
    checkpoint::read(s,version);
    const bool nursery_decay_state = version == "NEUROEVO_ECOSYSTEM_15";
    const bool dynamic_food_state = nursery_decay_state || version == "NEUROEVO_ECOSYSTEM_14";
    const bool pruning_state = dynamic_food_state || version == "NEUROEVO_ECOSYSTEM_13";
    const bool moving_food_state = pruning_state || version == "NEUROEVO_ECOSYSTEM_12";
    const bool shelter_state = moving_food_state || version == "NEUROEVO_ECOSYSTEM_11";
    const bool exit_state = shelter_state || version == "NEUROEVO_ECOSYSTEM_10";
    const bool frontier_state = exit_state || version == "NEUROEVO_ECOSYSTEM_9";
    const bool stable_state = frontier_state || version == "NEUROEVO_ECOSYSTEM_8";
    const bool calibrated_state = stable_state || version == "NEUROEVO_ECOSYSTEM_7";
    const bool current_state = calibrated_state || version == "NEUROEVO_ECOSYSTEM_6";
    const bool v5_state = version == "NEUROEVO_ECOSYSTEM_5";
    const bool v4_state = version == "NEUROEVO_ECOSYSTEM_4";
    const bool v3_state = version == "NEUROEVO_ECOSYSTEM_3";
    const bool legacy_establishment_state = version == "NEUROEVO_ECOSYSTEM_2";
    const bool modern_state = current_state || v5_state || v4_state || v3_state;
    const bool establishment_state = modern_state || legacy_establishment_state;
    if (!establishment_state && version != "NEUROEVO_ECOSYSTEM_1")
        throw std::runtime_error("Unknown ecosystem checkpoint version");
    EcosystemConfig cfg;
    cfg.nursery_food_decay = 0;
    cfg.outdoor_food_relocates = false; // Validate the policy only after its versioned fields are read.
    checkpoint::read_tuple(s,checkpoint::world_config_fields(cfg));
    checkpoint::read_tuple(s,checkpoint::brain_fields(cfg.brain));
    if (modern_state) checkpoint::read_tuple(s,checkpoint::mutation_fields(cfg.mutation));
    else checkpoint::read_tuple(s,checkpoint::legacy_mutation_fields(cfg.mutation));
    if (current_state || v5_state) checkpoint::read_tuple(s,checkpoint::establishment_config_fields(cfg));
    else if (v4_state) checkpoint::read_tuple(s,checkpoint::v4_establishment_config_fields(cfg));
    else if (v3_state) checkpoint::read_tuple(s,checkpoint::v3_establishment_config_fields(cfg));
    else if (legacy_establishment_state) checkpoint::read_tuple(s,checkpoint::legacy_establishment_config_fields(cfg));
    if (current_state) checkpoint::read_tuple(s,checkpoint::food_energy_config_fields(cfg));
    else {
        // Earlier checkpoints predate configurable nutrition and their stored
        // resources use these densities. Preserve exact continuation.
        cfg.graze_energy = 2;
        cfg.poor_fruit_energy = 4;
        cfg.rich_fruit_energy = 10;
        cfg.pod_energy = 12;
    }
    if (calibrated_state) {
        checkpoint::read_tuple(s,checkpoint::calibrated_brain_fields(cfg.brain));
        checkpoint::read_tuple(s,checkpoint::interface_config_fields(cfg));
    } else {
        cfg.extended_senses = false;
        cfg.brain.calibrated_io = false;
        cfg.actuator_tau = 0;
        cfg.archive_eval_trials = 0;
    }
    cfg.validate();
    cfg.mutation.stable = false;
    if (stable_state) checkpoint::read(s,cfg.mutation.stable);
    if (frontier_state) checkpoint::read(s,cfg.nursery_frontier,cfg.nursery_size,cfg.nursery_food_energy,
        cfg.nursery_food_capacity,cfg.nursery_food_regrowth);
    if (exit_state) checkpoint::read(s,cfg.nursery_exit_width);
    else cfg.nursery_exit_width=3; // Historical maps used fixed three-cell gates.
    if (shelter_state) checkpoint::read(s,cfg.shelter_size);
    else cfg.shelter_size=3;
    cfg.nursery_food_relocates=false;
    if (moving_food_state) checkpoint::read(s,cfg.nursery_food_patches,cfg.nursery_food_relocates);
    cfg.mutation.remove_neuron_probability = 0;
    if (pruning_state) checkpoint::read(s,cfg.mutation.remove_neuron_probability);
    cfg.outdoor_food_relocates = false;
    if (dynamic_food_state) checkpoint::read(s,cfg.outdoor_food_relocates,cfg.graze_decay,cfg.fruit_decay,
        cfg.shelter_food_energy,cfg.shelter_food_capacity,cfg.shelter_food_regrowth);
    if (nursery_decay_state) checkpoint::read(s,cfg.nursery_food_decay);
    EcosystemWorld w(cfg,false);
    checkpoint::read(s,w.step_index,w.next_creature_id,w.fruit_a_rich,w.capacity_limited);
    checkpoint::read_tuple(s,checkpoint::total_fields(w.totals));
    if (modern_state) checkpoint::read_tuple(s,checkpoint::establishment_total_fields(w.totals));
    else if (legacy_establishment_state) {
        checkpoint::read_tuple(s,checkpoint::legacy_establishment_total_fields(w.totals));
        w.totals.immigrant_slight_mutations = w.totals.immigrant_mutations;
    }
    w.map_rng.load_state(s); w.mutation_rng.load_state(s); w.conflict_rng.load_state(s);
    if (establishment_state) {
        w.immigration_rng.load_state(s);
        checkpoint::read(s,w.immigration_deck_cursor,w.next_immigration_check,w.last_immigration_time,w.support_stable_since,w.immigration_withdrawn);
        std::array<std::size_t,6> counts{};
        for (auto& origin : w.immigration_deck) {
            checkpoint::read(s,origin);
            const auto maximum = modern_state ? CreatureOrigin::ArchiveStrongMutation : CreatureOrigin::RandomImmigrant;
            if (origin < CreatureOrigin::Founder || origin > maximum)
                throw std::runtime_error("Invalid immigration deck origin");
            ++counts[static_cast<std::size_t>(origin)];
        }
        const auto& t = w.totals;
        const bool old_modern_deck = counts[2]==3 && counts[3]==1 && counts[5]==1;
        const bool current_deck = counts[2]==2 && counts[3]==2 && counts[5]==1;
        const bool legacy_deck = counts[2]==3 && counts[3]==1 && counts[4]==1;
        const bool invalid_deck = w.immigration_deck_cursor > 5
            || (w.immigration_deck_cursor < 5
                && !(modern_state ? (old_modern_deck || current_deck) : legacy_deck));
        if (invalid_deck
            || w.next_immigration_check < 0 || w.last_immigration_time < 0 || w.support_stable_since < -1
            || t.immigrant_energy < 0 || t.immigrants != t.immigrant_mutations+t.immigrant_clones+t.immigrant_random
            || (modern_state && t.immigrant_mutations != t.immigrant_slight_mutations+t.immigrant_strong_mutations)
            || t.founder_births+t.immigrant_births+t.descendant_births > t.births
            || t.births_first_100s > t.births
            || t.archive_fallbacks > t.immigrant_random)
            throw std::runtime_error("Invalid immigration checkpoint state");
        if (legacy_establishment_state) for (auto& origin : w.immigration_deck)
            if (origin == CreatureOrigin::RandomImmigrant) origin = CreatureOrigin::ArchiveStrongMutation;
    }
    const auto tiles = checkpoint::count(s,cfg.width*cfg.height);
    if (tiles != cfg.width*cfg.height) throw std::runtime_error("Checkpoint terrain size mismatch");
    w.terrain.resize(tiles);
    for (auto& tile : w.terrain) {
        checkpoint::read(s,tile);
        if (tile < Terrain::Ground || tile > Terrain::Shelter) throw std::runtime_error("Invalid terrain in checkpoint");
    }
    w.resources.resize(checkpoint::count(s,tiles));
    std::set<std::uint64_t> ids;
    for (auto& r : w.resources) {
        checkpoint::read(s,r.id,r.kind,r.position.x,r.position.y,r.stock,r.capacity,r.regrowth,
            r.energy_per_unit,r.pod_state,r.progress,r.opened_at);
        if (dynamic_food_state) checkpoint::read(s,r.shelter_food);
        if (!ids.insert(r.id).second || r.kind < FoodKind::Graze || r.kind > FoodKind::Pod
            || r.pod_state < PodState::Closed || r.pod_state > PodState::Refilling
            || r.stock < 0 || r.stock > r.capacity+1e-8 || r.capacity <= 0 || r.regrowth < 0
            || r.energy_per_unit < 0 || r.progress < 0 || !w.traversable(r.position))
            throw std::runtime_error("Invalid resource checkpoint");
    }
    w.creatures.resize(checkpoint::count(s,cfg.max_population));
    ids.clear();
    for (auto& c : w.creatures) {
        checkpoint::read(s,c.id,c.parent_id,c.generation,c.position.x,c.position.y,c.heading,c.energy,
            c.age,c.last_birth,c.speed,c.turn,c.ingestion_pulse,c.digestion_pulse,c.controller,
            c.spikes,c.step_spikes,c.offspring,c.energy_gained,c.energy_spent,c.pod_work,c.exposed_time,c.matured);
        if (establishment_state) checkpoint::read(s,c.origin,c.genome_id,c.source_id,c.feeding_bouts,c.last_fed_time);
        else { c.genome_id=c.id; c.origin=c.parent_id ? CreatureOrigin::Birth : CreatureOrigin::Founder; }
        checkpoint::read(s,c.action.forward,c.action.left,c.action.right,c.action.forage,c.action.call);
        for (auto& v : c.eaten) checkpoint::read(s,v);
        c.digestion.resize(checkpoint::count(s,1000000));
        for (auto& p : c.digestion) {
            checkpoint::read(s,p.due,p.energy,p.kind);
            if (p.energy < 0 || p.kind < FoodKind::Graze || p.kind > FoodKind::Pod)
                throw std::runtime_error("Invalid digestive packet checkpoint");
        }
        c.neural_rng.load_state(s);
        c.brain = Brain::load_state(s);
        if (c.id == 0 || c.id >= w.next_creature_id || !ids.insert(c.id).second
            || c.genome_id == 0 || c.genome_id >= w.next_creature_id || c.source_id >= w.next_creature_id
            || c.origin < CreatureOrigin::Founder || c.origin > CreatureOrigin::ArchiveStrongMutation
            || c.energy <= 0 || c.energy > cfg.energy_capacity+1e-8 || c.age < 0
            || c.controller < ControllerKind::Spiking || c.controller > ControllerKind::Random
            || !w.traversable(c.position) || c.brain.config().input_count != cfg.brain.input_count
            || c.brain.config().output_count != eco_output_count
            || std::abs(c.brain.config().dt-cfg.brain.dt) > 1e-12)
            throw std::runtime_error("Invalid creature checkpoint");
    }
    if (establishment_state) {
        w.archive.resize(checkpoint::count(s,cfg.archive_capacity));
        ids.clear();
        for (auto& entry : w.archive) {
            checkpoint::read(s,entry.genome_id,entry.source_id,entry.niche,entry.score);
            entry.trials.resize(checkpoint::count(s,8));
            if (!entry.genome_id || entry.genome_id >= w.next_creature_id || !ids.insert(entry.genome_id).second
                || !entry.source_id || entry.source_id >= w.next_creature_id || entry.score < 0 || entry.trials.empty()
                || entry.niche < FoodKind::Graze || entry.niche > FoodKind::Pod)
                throw std::runtime_error("Invalid genome archive checkpoint");
            std::set<std::uint64_t> trial_ids;
            for (auto& trial : entry.trials) {
                checkpoint::read_tuple(s,checkpoint::archive_trial_fields(trial));
                if (!trial.creature_id || trial.creature_id >= w.next_creature_id || !trial_ids.insert(trial.creature_id).second
                    || trial.energy_gained < 0 || trial.energy_spent < 0 || trial.age < 0 || trial.observed_at < 0)
                    throw std::runtime_error("Invalid archive trial checkpoint");
            }
            entry.genome = Brain::load_state(s);
            if (entry.genome.config().input_count != cfg.brain.input_count || entry.genome.config().output_count != eco_output_count
                || std::abs(entry.genome.config().dt-cfg.brain.dt)>1e-12)
                throw std::runtime_error("Incompatible archived brain checkpoint");
        }
    }
    if (calibrated_state) {
        const auto evaluations = checkpoint::count(s,1000000);
        for (std::size_t i = 0; i < evaluations; ++i) {
            std::uint64_t id = 0;
            NewbornEvaluation evaluation;
            checkpoint::read(s,id,evaluation.score);
            evaluation.trials.resize(checkpoint::count(s,32));
            if (!id || id >= w.next_creature_id || evaluation.score < 0
                || evaluation.trials.size() != cfg.archive_eval_trials || evaluation.trials.empty())
                throw std::runtime_error("Invalid newborn evaluation checkpoint");
            for (auto& trial : evaluation.trials) {
                checkpoint::read_tuple(s,checkpoint::newborn_trial_fields(trial));
                if (trial.elapsed < 0 || trial.focal_age < 0 || trial.focal_age > trial.elapsed+1e-8
                    || trial.food_energy < 0 || trial.operating_energy < 0
                    || trial.first_birth < -1 || trial.second_birth < -1)
                    throw std::runtime_error("Invalid newborn trial checkpoint");
            }
            if (!w.newborn_evaluations.emplace(id,std::move(evaluation)).second)
                throw std::runtime_error("Duplicate newborn evaluation checkpoint");
        }
        if (cfg.archive_eval_trials > 0) for (const auto& entry : w.archive) {
            const auto found = w.newborn_evaluations.find(entry.genome_id);
            if (found == w.newborn_evaluations.end() || found->second.score != entry.score)
                throw std::runtime_error("Archive lacks matching newborn evaluation evidence");
        }
    }
    w.events.resize(checkpoint::count(s,1000000));
    for (auto& e : w.events) read_event(s,e);
    checkpoint::marker(s,"END_ECOSYSTEM");
    return w;
}

void write_ecosystem_metadata(std::ostream& s, const EcosystemWorld& w, bool record_brain_graphs)
{
    s << std::setprecision(10);
    s << "{\"type\":\"metadata\",\"version\":1,\"width\":" << w.config.width << ",\"height\":" << w.config.height
      << ",\"dt\":" << w.config.dt << ",\"radius\":" << w.config.radius << ",\"vision_range\":" << w.config.vision_range
      << ",\"nursery\":{\"enabled\":" << (w.config.nursery_frontier?"true":"false")
      << ",\"x\":" << (w.config.width-std::min(w.config.width,w.config.nursery_size))/2
      << ",\"y\":" << (w.config.height-std::min(w.config.height,w.config.nursery_size))/2
      << ",\"size\":" << w.config.nursery_size << ",\"exit_width\":" << w.config.nursery_exit_width
      << ",\"food_decay\":" << w.config.nursery_food_decay
      << ",\"food_energy\":" << w.config.nursery_food_energy
      << ",\"food_patches\":" << w.config.nursery_food_patches
      << ",\"food_relocates\":" << (w.config.nursery_food_relocates?"true":"false") << "}"
      << ",\"fov_degrees\":" << w.config.fov_degrees << ",\"seed\":" << w.config.seed
      << ",\"max_speed\":" << w.config.max_speed << ",\"max_turn_rate\":" << w.config.max_turn_rate
      << ",\"shelter_size\":" << w.config.shelter_size
      << ",\"outdoor_food_relocates\":" << (w.config.outdoor_food_relocates?"true":"false")
      << ",\"graze_decay\":" << w.config.graze_decay << ",\"fruit_decay\":" << w.config.fruit_decay
      << ",\"shelter_food_energy\":" << w.config.shelter_food_energy
      << ",\"shelter_food_capacity\":" << w.config.shelter_food_capacity
      << ",\"shelter_food_regrowth\":" << w.config.shelter_food_regrowth
      << ",\"energy_capacity\":" << w.config.energy_capacity << ",\"pod_work\":" << w.config.pod_work
      << ",\"food_energy\":{\"graze\":" << w.config.graze_energy
      << ",\"poor_fruit\":" << w.config.poor_fruit_energy << ",\"rich_fruit\":" << w.config.rich_fruit_energy
      << ",\"pod\":" << w.config.pod_energy << "}"
      << ",\"fruit_a_rich\":" << (w.fruit_a_rich ? "true" : "false")
      << ",\"controller\":"; quoted(s,to_string(w.config.controller));
    s << ",\"initial_creatures\":" << w.creatures.size() << ",\"reproduction\":" << (w.config.reproduction ? "true" : "false")
      << ",\"establishment\":" << (w.config.establishment ? "true" : "false")
      << ",\"storms_enabled\":" << (w.config.storms_enabled ? "true" : "false")
      << ",\"immigration_floor\":" << w.population_floor() << ",\"archive_capacity\":" << w.config.archive_capacity
      << ",\"archive_tournament_size\":" << w.config.archive_tournament_size
      << ",\"immigration_interval\":" << w.config.immigration_interval
      << ",\"archive_min_age\":" << w.config.archive_min_age
      << ",\"archive_min_energy\":" << w.config.archive_min_energy
      << ",\"archive_min_feeding_bouts\":" << w.config.archive_min_feeding_bouts
      << ",\"archive_min_efficiency\":" << w.config.archive_min_efficiency
      << ",\"sensory_interface\":" << (w.config.extended_senses ? 2 : 1)
      << ",\"calibrated_io\":" << (w.config.brain.calibrated_io ? "true" : "false")
      << ",\"sensory_rate_hz\":" << w.config.brain.sensory_rate_hz
      << ",\"motor_rate_tau\":" << w.config.brain.motor_rate_tau
      << ",\"motor_reference_hz\":" << w.config.brain.motor_reference_hz
      << ",\"motor_gain\":" << w.config.motor_gain << ",\"actuator_tau\":" << w.config.actuator_tau
      << ",\"archive_eval_trials\":" << w.config.archive_eval_trials
      << ",\"archive_eval_seconds\":" << w.config.archive_eval_seconds
      << ",\"archive_eval_seed\":" << w.config.archive_eval_seed
      << ",\"input_labels\":";
    array(s,ecosystem_input_labels(w.config.extended_senses),[&](const std::string& v){ quoted(s,v); });
    s << ",\"terrain\":";
    array(s,w.terrain,[&](Terrain v){ s << static_cast<int>(v); });
    s << ",\"resources\":";
    array(s,w.resources,[&](const EcoResource& r){
        s << "{\"id\":" << r.id << ",\"kind\":"; quoted(s,to_string(r.kind));
        s << ",\"shelter_food\":" << (r.shelter_food ? "true" : "false");
        s << ",\"x\":" << r.position.x << ",\"y\":" << r.position.y << ",\"capacity\":" << r.capacity
          << ",\"value\":" << r.energy_per_unit << '}';
    });
    s << ",\"brains\":";
    if (record_brain_graphs)
        array(s,w.creatures,[&](const EcoCreature& c){ s << "{\"id\":" << c.id << ','; graph(s,c); s << '}'; });
    else s << "[]";
    s << "}\n";
}

void write_ecosystem_frame(std::ostream& s, const EcosystemWorld& w, bool record_brains,
    bool record_observations, bool record_brain_graphs,
    std::unordered_set<std::uint64_t>* known_brains, const std::vector<EcoEvent>* recorded_events)
{
    s << std::setprecision(10);
    s << "{\"type\":\"frame\",\"step\":" << w.step_index << ",\"time\":" << w.time()
      << ",\"weather\":"; quoted(s,to_string(w.weather()));
    s << ",\"cue\":" << w.storm_cue() << ",\"capacity_limited\":" << (w.capacity_limited ? "true" : "false")
      << ",\"totals\":"; totals_json(s,w.totals);
    s << ",\"establishment\":{\"enabled\":" << (w.config.establishment ? "true" : "false")
      << ",\"active\":" << (w.immigration_enabled() ? "true" : "false")
      << ",\"withdrawn\":" << (w.immigration_withdrawn ? "true" : "false")
      << ",\"floor\":" << w.population_floor() << ",\"next_check\":" << w.next_immigration_check
      << ",\"stable_since\":" << w.support_stable_since << ",\"archive\":";
    array(s,w.archive,[&](const GenomeArchiveEntry& e){
        double gained=0;
        for (const auto& trial : e.trials) gained+=trial.energy_gained;
        s << "{\"genome_id\":" << e.genome_id << ",\"source_id\":" << e.source_id << ",\"niche\":";
        quoted(s,to_string(e.niche));
        s << ",\"score\":" << e.score << ",\"trials\":" << e.trials.size()
          << ",\"mean_food_energy\":" << gained/static_cast<double>(e.trials.size());
        const auto evaluated = w.newborn_evaluations.find(e.genome_id);
        if (evaluated != w.newborn_evaluations.end()) {
            double breeders = 0, lineages = 0;
            for (const auto& t : evaluated->second.trials) {
                breeders += t.offspring > 0;
                lineages += t.descendant_births > 0;
            }
            const auto count = evaluated->second.trials.size();
            s << ",\"newborn_trials\":" << count << ",\"newborn_breeder_fraction\":" << breeders/count
              << ",\"breeding_lineage_fraction\":" << lineages/count;
        }
        s << '}';
    });
    s << '}';
    s << ",\"creatures\":";
    std::size_t index = 0;
    array(s,w.creatures,[&](const EcoCreature& c){
        s << "{\"id\":" << c.id << ",\"parent\":" << c.parent_id << ",\"generation\":" << c.generation
          << ",\"x\":" << c.position.x << ",\"y\":" << c.position.y << ",\"heading\":" << c.heading
          << ",\"energy\":" << c.energy << ",\"age\":" << c.age << ",\"speed\":" << c.speed
          << ",\"forward\":" << c.action.forward << ",\"turn\":" << c.turn
          << ",\"forage\":" << c.action.forage << ",\"call\":" << c.action.call
          << ",\"spikes\":" << c.spikes << ",\"offspring\":" << c.offspring
          << ",\"energy_gained\":" << c.energy_gained << ",\"energy_spent\":" << c.energy_spent
          << ",\"pod_work\":" << c.pod_work << ",\"genome_id\":" << c.genome_id
          << ",\"source_id\":" << c.source_id << ",\"feeding_bouts\":" << c.feeding_bouts << ",\"origin\":";
        quoted(s,to_string(c.origin));
        s << ",\"eaten\":";
        array(s,c.eaten,[&](double v){s << v;});
        s << ",\"controller\":"; quoted(s,to_string(c.controller));
        if (record_observations) {
            s << ",\"observation\":";
            array(s,w.observe(index),[&](double v){s << v;});
        }
        ++index;
        const bool new_graph = record_brain_graphs && (!known_brains || known_brains->insert(c.id).second);
        if (record_brains || new_graph) {
            s << ",\"brain\":{";
            if (record_brains) {
                s << "\"potentials\":";
                array(s,c.brain.neurons(),[&](const Brain::Neuron& n){s << n.potential;});
                s << ",\"spiked\":[";
                bool first = true;
                for (std::size_t j=0; j<c.brain.neurons().size(); ++j) if (c.brain.neurons()[j].spiked) {
                    if (!first) s << ',';
                    first = false;
                    s << j;
                }
                s << ']';
            }
            if (new_graph) { if (record_brains) s << ','; graph(s,c); }
            s << '}';
        }
        s << '}';
    });
    s << ",\"resources\":";
    array(s,w.resources,[&](const EcoResource& r){
        s << "{\"id\":" << r.id << ",\"x\":" << r.position.x << ",\"y\":" << r.position.y
          << ",\"stock\":" << r.stock << ",\"progress\":" << r.progress << ",\"state\":";
        quoted(s,to_string(r.pod_state)); s << '}';
    });
    s << ",\"events\":";
    array(s,recorded_events ? *recorded_events : w.events,[&](const EcoEvent& e){
        s << "{\"time\":" << e.time << ",\"type\":"; quoted(s,e.type);
        s << ",\"creature\":" << e.creature << ",\"other\":" << e.other << ",\"resource\":" << e.resource << ",\"amount\":" << e.amount << '}';
    });
    s << "}\n";
    if (!s) throw std::runtime_error("Failed to write ecosystem replay");
}

void write_ecosystem_stats_header(std::ostream& s)
{
    s << "step,time,population,births,deaths,maturations,mean_energy,total_energy,pending_energy,food_biomass,"
         "weather,spikes,pods_opened,consumed_biomass,regrown_biomass,spoiled_biomass,energy_gained,"
         "metabolism,movement,turning,foraging,calling,neural,exposure,reproduction_overhead,discarded_energy,capacity_limited,"
         "immigrants,immigrant_mutations,immigrant_slight_mutations,immigrant_strong_mutations,immigrant_clones,immigrant_random,"
         "archive_fallbacks,archive_empty_checks,immigrant_energy,founder_births,immigrant_births,descendant_births,births_first_100s,"
         "natural_spiking_breeders,mature_offspring,archive_entries,immigration_active,immigration_withdrawn,"
         "archive_best_score,archive_median_score,newborn_evaluated_genomes,nursery_population,frontier_population\n";
}
void write_ecosystem_stats(std::ostream& s, const EcosystemWorld& w)
{
    double energy=0,pending=0,biomass=0;
    for (const auto& c : w.creatures) { energy+=c.energy; for (const auto& p : c.digestion) pending+=p.energy; }
    for (const auto& r : w.resources) biomass+=r.stock;
    const auto& t=w.totals;
    std::vector<double> archive_scores;
    for (const auto& entry:w.archive) archive_scores.push_back(entry.score);
    std::sort(archive_scores.begin(),archive_scores.end());
    const double archive_best=archive_scores.empty()?0:archive_scores.back();
    const double archive_median=archive_scores.empty()?0:
        (archive_scores[(archive_scores.size()-1)/2]+archive_scores[archive_scores.size()/2])/2;
    s << std::setprecision(12) << w.step_index << ',' << w.time() << ',' << w.creatures.size() << ','
      << t.births << ',' << t.deaths << ',' << t.maturations << ','
      << (w.creatures.empty()?0:energy/static_cast<double>(w.creatures.size())) << ',' << energy << ',' << pending << ','
      << biomass << ',' << to_string(w.weather()) << ',' << t.spikes << ',' << t.pods_opened << ','
      << t.consumed_biomass << ',' << t.regrown_biomass << ',' << t.spoiled_biomass << ',' << t.energy_gained << ','
      << t.metabolism << ',' << t.movement << ',' << t.turning << ',' << t.foraging << ',' << t.calling << ','
      << t.neural << ',' << t.exposure << ',' << t.reproduction_overhead << ',' << t.discarded_energy << ','
      << (w.capacity_limited?1:0) << ',' << t.immigrants << ',' << t.immigrant_mutations << ','
      << t.immigrant_slight_mutations << ',' << t.immigrant_strong_mutations << ','
      << t.immigrant_clones << ',' << t.immigrant_random << ',' << t.archive_fallbacks << ',' << t.archive_empty_checks << ','
      << t.immigrant_energy << ',' << t.founder_births << ',' << t.immigrant_births << ',' << t.descendant_births << ','
      << t.births_first_100s << ','
      << t.natural_spiking_breeders << ',' << t.mature_offspring << ',' << w.archive.size() << ','
      << (w.immigration_enabled()?1:0) << ',' << (w.immigration_withdrawn?1:0) << ','
      << archive_best << ',' << archive_median << ',' << w.newborn_evaluations.size();
    const auto nursery = std::count_if(w.creatures.begin(),w.creatures.end(),[&](const auto& c){return w.in_nursery(c.position);});
    s << ',' << nursery << ',' << w.creatures.size()-nursery << '\n';
}
} // namespace neuroevo
