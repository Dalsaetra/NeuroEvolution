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
    s << "\"neuron_model\":\"" << neuron_model_name(b.config().neuron_model) << "\","
      << "\"inputs\":" << b.config().input_count << ",\"outputs\":" << b.config().output_count << ",\"neurons\":";
    std::size_t index=0;
    array(s,b.neurons(),[&](const Brain::Neuron& n) {
        const bool izh=b.config().neuron_model==NeuronModel::Izhikevich && index>=b.config().input_count;
        ++index;
        s << "{\"x\":" << n.position.x << ",\"y\":" << n.position.y << ",\"threshold\":" << (izh?30:n.threshold);
        if(izh)s<<",\"izhikevich\":{\"a\":"<<n.izhikevich.a<<",\"b\":"<<n.izhikevich.b
            <<",\"c\":"<<n.izhikevich.c<<",\"d\":"<<n.izhikevich.d<<'}';
        s << '}';
    });
    s << ",\"synapses\":";
    array(s,b.synapses(),[&](const Brain::Synapse& e) {
        s << "{\"pre\":" << e.pre << ",\"post\":" << e.post << ",\"weight\":" << e.weight << ",\"delay\":" << e.delay_steps << '}';
    });
}
void totals_json(std::ostream& s, const EcoTotals& t)
{
    s << "{\"births\":" << t.births << ",\"deaths\":" << t.deaths << ",\"maturations\":" << t.maturations
      << ",\"attacking\":" << t.attacking << ",\"healing\":" << t.healing
      << ",\"body_construction\":" << t.body_construction << ",\"external_body_energy\":" << t.external_body_energy
      << ",\"carcass_energy\":" << t.carcass_energy << ",\"meat_spoiled_energy\":" << t.meat_spoiled_energy
      << ",\"damage\":" << t.damage << ",\"predation_deaths\":" << t.predation_deaths
      << ",\"spikes\":" << t.spikes << ",\"pods_opened\":" << t.pods_opened
      << ",\"consumed_biomass\":" << t.consumed_biomass << ",\"regrown_biomass\":" << t.regrown_biomass
      << ",\"spoiled_biomass\":" << t.spoiled_biomass << ",\"energy_gained\":" << t.energy_gained
      << ",\"metabolism\":" << t.metabolism << ",\"movement\":" << t.movement << ",\"turning\":" << t.turning
      << ",\"foraging\":" << t.foraging << ",\"calling\":" << t.calling << ",\"neural\":" << t.neural
      << ",\"exposure\":" << t.exposure << ",\"reproduction_overhead\":" << t.reproduction_overhead
      << ",\"discarded_energy\":" << t.discarded_energy
      << ",\"natural_spiking_breeders\":" << t.natural_spiking_breeders << ",\"mature_offspring\":" << t.mature_offspring
      << ",\"founder_births\":" << t.founder_births
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
    checkpoint::write(s,"NEUROEVO_ECOSYSTEM_38");
    checkpoint::write_tuple(s,checkpoint::world_config_fields(config));
    checkpoint::write_tuple(s,checkpoint::brain_fields(config.brain));
    checkpoint::write_tuple(s,checkpoint::calibrated_brain_fields(config.brain));
    checkpoint::write_tuple(s,checkpoint::model_fields(config.brain));
    checkpoint::write_tuple(s,checkpoint::filtered_fields(config.brain));
    checkpoint::write_tuple(s,checkpoint::mutation_fields(config.mutation));
    checkpoint::write_tuple(s,checkpoint::intrinsic_mutation_fields(config.mutation));
    checkpoint::write(s,config.mutation.add_autapse_probability);
    checkpoint::write_tuple(s,checkpoint::birth_profile_fields(config.mutation.slight));
    checkpoint::write_tuple(s,checkpoint::birth_profile_fields(config.mutation.strong));
    checkpoint::write(s,config.nursery_meat_decay);
    checkpoint::write(s,config.nursery_food_respawn_delay,config.outdoor_food_respawn_delay);
    checkpoint::write(s,config.storm_health_damage,config.storm_damage);
    checkpoint::write(s,config.nursery_food_population_threshold,config.nursery_food_energy_factor,
        nursery_food_current_energy,nursery_above_food_threshold,nursery_food_reductions);
    checkpoint::write(s,config.nursery_food_reduction_delay,nursery_food_reduction_ready_at);
    checkpoint::write(s,config.shelter_predation_damage);
    checkpoint::write(s,config.mutate_initial_ancestors);
    checkpoint::write(s,next_resource_id);
    checkpoint::write_tuple(s,checkpoint::predation_total_fields(totals));
    checkpoint::write(s,step_index,next_creature_id,fruit_a_rich,capacity_limited);
    checkpoint::write_tuple(s,checkpoint::total_fields(totals));
    checkpoint::write_tuple(s,checkpoint::lineage_total_fields(totals));
    map_rng.save_state(s); mutation_rng.save_state(s); conflict_rng.save_state(s);
    checkpoint::write(s,terrain.size());
    for (const auto value : terrain) checkpoint::write_value(s,value);
    s << '\n';
    checkpoint::write(s,resources.size());
    for (const auto& r : resources)
        checkpoint::write(s,r.id,r.kind,r.position.x,r.position.y,r.stock,r.capacity,r.regrowth,
            r.energy_per_unit,r.pod_state,r.progress,r.opened_at,r.shelter_food,r.shelter_origin.x,r.shelter_origin.y,r.respawn_at);
    checkpoint::write(s,creatures.size());
    for (const auto& c : creatures) {
        checkpoint::write(s,c.id,c.parent_id,c.generation,c.position.x,c.position.y,c.heading,c.energy,
            c.age,c.last_birth,c.speed,c.turn,c.ingestion_pulse,c.digestion_pulse,c.controller,
            c.spikes,c.step_spikes,c.offspring,c.energy_gained,c.energy_spent,c.pod_work,c.exposed_time,c.matured);
        checkpoint::write(s,c.origin,c.genome_id,c.feeding_bouts,c.last_fed_time);
        checkpoint::write(s,c.action.forward,c.action.left,c.action.right,c.action.forage,c.action.call,c.action.attack);
        checkpoint::write(s,c.body.mass,c.body.carnivory,c.health,c.damage_pulse);
        for (const auto v : c.eaten) checkpoint::write_value(s,v);
        s << '\n';
        checkpoint::write(s,c.digestion.size());
        for (const auto& p : c.digestion) checkpoint::write(s,p.due,p.energy,p.kind);
        c.neural_rng.save_state(s);
        c.brain.save_state(s);
    }
    checkpoint::write(s,events.size());
    for (const auto& e : events) write_event(s,e);
    checkpoint::write(s,"FOOD_SOURCES_1",config.food_distribution);
    checkpoint::write_tuple(s,checkpoint::food_source_fields(config.food_sources));
    checkpoint::write(s,food_sources.size());
    for(const auto& source:food_sources)
        checkpoint::write(s,source.id,source.kind,source.position.x,source.position.y,source.radius,source.phase);
    checkpoint::write(s,resources.size());
    for(const auto& r:resources)checkpoint::write(s,r.source_id,r.ripening_remaining);
    checkpoint::write(s,"META_MUTATION_1");
    checkpoint::write(s,config.mutation.meta_mutation_enabled,config.mutation.meta_mutation_probability,config.mutation.meta_mutation_sigma,config.mutation.min_mutation_scale);
    checkpoint::write(s,creatures.size());
    for (const auto& c:creatures) checkpoint::write(s,c.id,c.mutation_scale);
    checkpoint::write(s,"BACKGROUND_WEATHER_1",config.background_food_patches,config.background_food_energy,config.storm_ramp);
    checkpoint::write(s,"MASS_ENERGY_1",config.mass_scaled_energy_capacity);
    checkpoint::write(s,"END_ECOSYSTEM");
}

EcosystemWorld EcosystemWorld::load_checkpoint(std::istream& s)
{
    std::string version;
    checkpoint::read(s,version);
    const bool mass_energy_version = version == "NEUROEVO_ECOSYSTEM_38";
    const bool background_weather_version = mass_energy_version || version == "NEUROEVO_ECOSYSTEM_37";
    const bool meta_floor_version = background_weather_version || version == "NEUROEVO_ECOSYSTEM_36";
    const bool meta_version = meta_floor_version || version == "NEUROEVO_ECOSYSTEM_35";
    const bool source_version = meta_version || version == "NEUROEVO_ECOSYSTEM_34";
    const bool ancestor_mutation_version = source_version || version == "NEUROEVO_ECOSYSTEM_33";
    const bool shelter_damage_version = ancestor_mutation_version || version == "NEUROEVO_ECOSYSTEM_32";
    const bool nursery_cooldown_version = shelter_damage_version || version == "NEUROEVO_ECOSYSTEM_31";
    const bool nursery_nutrition_version = nursery_cooldown_version || version == "NEUROEVO_ECOSYSTEM_30";
    const bool storm_damage_version = nursery_nutrition_version || version == "NEUROEVO_ECOSYSTEM_29";
    const bool respawn_version = storm_damage_version || version == "NEUROEVO_ECOSYSTEM_28";
    const bool general_food_version = respawn_version || version == "NEUROEVO_ECOSYSTEM_27";
    const bool autapse_version = general_food_version || version == "NEUROEVO_ECOSYSTEM_26";
    const bool regional_meat_version = autapse_version || version == "NEUROEVO_ECOSYSTEM_25";
    const bool filtered_version = regional_meat_version || version == "NEUROEVO_ECOSYSTEM_24";
    const bool modern = filtered_version || version == "NEUROEVO_ECOSYSTEM_23";
    if (!modern && version != "NEUROEVO_ECOSYSTEM_22")
        throw std::runtime_error("Unsupported ecosystem checkpoint version. Start a new nursery run; use the previous build to resume older checkpoints.");
    EcosystemConfig cfg;
    cfg.mass_scaled_energy_capacity=false;
    cfg.background_food_patches=0;
    cfg.storm_ramp=false; // Historical checkpoints retain flat damage and the harvest cutoff.
    cfg.mutation.meta_mutation_enabled=false; // Preserve historical reproduction policy.
    cfg.mutation.min_mutation_scale=0.5;
    cfg.food_distribution=FoodDistribution::Scattered; // Historical worlds keep their original renewal rules.
    checkpoint::read_tuple(s,checkpoint::world_config_fields(cfg));
    checkpoint::read_tuple(s,checkpoint::brain_fields(cfg.brain));
    checkpoint::read_tuple(s,checkpoint::calibrated_brain_fields(cfg.brain));
    if (modern) checkpoint::read_tuple(s,checkpoint::model_fields(cfg.brain));
    if (filtered_version) checkpoint::read_tuple(s,checkpoint::filtered_fields(cfg.brain));
    else if (cfg.brain.neuron_model == NeuronModel::FilteredLif) throw std::runtime_error("Filtered LIF requires ecosystem checkpoint version 24");
    checkpoint::read_tuple(s,checkpoint::mutation_fields(cfg.mutation));
    if (modern) checkpoint::read_tuple(s,checkpoint::intrinsic_mutation_fields(cfg.mutation));
    cfg.mutation.add_autapse_probability=0; // Preserve old runs' creation policy.
    if (autapse_version) checkpoint::read(s,cfg.mutation.add_autapse_probability);
    checkpoint::read_tuple(s,checkpoint::birth_profile_fields(cfg.mutation.slight));
    checkpoint::read_tuple(s,checkpoint::birth_profile_fields(cfg.mutation.strong));
    cfg.nursery_meat_decay=cfg.meat_decay; // Older checkpoints used one rate everywhere.
    if (regional_meat_version) checkpoint::read(s,cfg.nursery_meat_decay);
    cfg.nursery_food_respawn_delay=cfg.outdoor_food_respawn_delay=0;
    if (respawn_version) checkpoint::read(s,cfg.nursery_food_respawn_delay,cfg.outdoor_food_respawn_delay);
    cfg.storm_health_damage=false;
    if (storm_damage_version) checkpoint::read(s,cfg.storm_health_damage,cfg.storm_damage);
    // Existing checkpoints retain the constant nutrition policy they were run with.
    cfg.nursery_food_population_threshold=0;
    double nursery_energy=cfg.nursery_food_energy;
    bool nursery_above=false;
    std::uint64_t nursery_reductions=0;
    if (nursery_nutrition_version)
        checkpoint::read(s,cfg.nursery_food_population_threshold,cfg.nursery_food_energy_factor,
            nursery_energy,nursery_above,nursery_reductions);
    cfg.nursery_food_reduction_delay=0; // Older runs had no cooldown.
    double nursery_ready_at=0;
    if (nursery_cooldown_version)
        checkpoint::read(s,cfg.nursery_food_reduction_delay,nursery_ready_at);
    if (nursery_ready_at < 0) throw std::runtime_error("Invalid nursery food reduction deadline");
    cfg.shelter_predation_damage=true; // Older runs allowed attacks in ordinary shelters.
    if (shelter_damage_version) checkpoint::read(s,cfg.shelter_predation_damage);
    cfg.mutate_initial_ancestors=false;
    if (ancestor_mutation_version) checkpoint::read(s,cfg.mutate_initial_ancestors);
    if (nursery_energy < 0 || nursery_energy > cfg.nursery_food_energy)
        throw std::runtime_error("Invalid current nursery food energy");
    if (!general_food_version && cfg.predation)
        throw std::runtime_error("Predation checkpoint uses the old food sensor layout. Start a new run; use the previous build to resume this checkpoint.");
    EcosystemWorld w(cfg,false);
    w.nursery_food_current_energy=nursery_energy;
    w.nursery_above_food_threshold=nursery_above;
    w.nursery_food_reductions=nursery_reductions;
    w.nursery_food_reduction_ready_at=nursery_ready_at;
    checkpoint::read(s,w.next_resource_id);
    if (!w.next_resource_id) throw std::runtime_error("Invalid next resource ID");
    checkpoint::read_tuple(s,checkpoint::predation_total_fields(w.totals));
    checkpoint::read(s,w.step_index,w.next_creature_id,w.fruit_a_rich,w.capacity_limited);
    checkpoint::read_tuple(s,checkpoint::total_fields(w.totals));
    checkpoint::read_tuple(s,checkpoint::lineage_total_fields(w.totals));
    std::apply([](const auto&... v) {
        if (((v < 0) || ...)) throw std::runtime_error("Negative ecosystem total");
    }, std::tuple_cat(checkpoint::total_fields(w.totals), checkpoint::predation_total_fields(w.totals)));
    if (w.next_creature_id == 0 || w.totals.founder_births+w.totals.descendant_births != w.totals.births
        || w.totals.births_first_100s > w.totals.births)
        throw std::runtime_error("Invalid lineage totals");
    w.map_rng.load_state(s); w.mutation_rng.load_state(s); w.conflict_rng.load_state(s);
    const auto tiles = checkpoint::count(s,cfg.width*cfg.height);
    if (tiles != cfg.width*cfg.height) throw std::runtime_error("Checkpoint terrain size mismatch");
    w.terrain.resize(tiles);
    for (auto& tile : w.terrain) {
        checkpoint::read(s,tile);
        if (tile < Terrain::Ground || tile > Terrain::Shelter) throw std::runtime_error("Invalid terrain in checkpoint");
    }
    w.resources.resize(checkpoint::count(s,1000000));
    std::set<std::uint64_t> ids;
    for (auto& r : w.resources) {
        checkpoint::read(s,r.id,r.kind,r.position.x,r.position.y,r.stock,r.capacity,r.regrowth,
            r.energy_per_unit,r.pod_state,r.progress,r.opened_at);
        checkpoint::read(s,r.shelter_food);
        checkpoint::read(s,r.shelter_origin.x,r.shelter_origin.y);
        if (respawn_version) checkpoint::read(s,r.respawn_at);
        if (r.respawn_at < 0 && r.respawn_at != -1) throw std::runtime_error("Invalid food respawn deadline");
        if (!ids.insert(r.id).second || r.kind < FoodKind::Graze || r.kind > (cfg.predation ? FoodKind::Meat : FoodKind::Pod)
            || r.pod_state < PodState::Closed || r.pod_state > PodState::Refilling
            || r.stock < 0 || r.stock > r.capacity+1e-8 || r.capacity <= 0 || r.regrowth < 0
            || (r.kind == FoodKind::Meat && (r.regrowth != 0 || r.shelter_food))
            || r.energy_per_unit < 0 || r.progress < 0 || !w.traversable(r.position))
            throw std::runtime_error("Invalid resource checkpoint");
    }
    w.creatures.resize(checkpoint::count(s,cfg.max_population));
    ids.clear();
    for (auto& c : w.creatures) {
        checkpoint::read(s,c.id,c.parent_id,c.generation,c.position.x,c.position.y,c.heading,c.energy,
            c.age,c.last_birth,c.speed,c.turn,c.ingestion_pulse,c.digestion_pulse,c.controller,
            c.spikes,c.step_spikes,c.offspring,c.energy_gained,c.energy_spent,c.pod_work,c.exposed_time,c.matured);
        checkpoint::read(s,c.origin,c.genome_id,c.feeding_bouts,c.last_fed_time);
        checkpoint::read(s,c.action.forward,c.action.left,c.action.right,c.action.forage,c.action.call);
        checkpoint::read(s,c.action.attack,c.body.mass,c.body.carnivory,c.health,c.damage_pulse);
        if (c.body.mass < eco_min_mass || c.body.mass > eco_max_mass || c.body.carnivory < 0 || c.body.carnivory > 1
            || c.health <= 0 || c.health > w.max_health(c)+1e-8 || c.damage_pulse < 0
            || c.action.attack < 0 || c.action.attack > 1) throw std::runtime_error("Invalid predation creature state");
        for (std::size_t k = 0; k < 5u; ++k) checkpoint::read(s,c.eaten[k]);
        c.digestion.resize(checkpoint::count(s,1000000));
        for (auto& p : c.digestion) {
            checkpoint::read(s,p.due,p.energy,p.kind);
            if (p.energy < 0 || p.kind < FoodKind::Graze || p.kind > (cfg.predation ? FoodKind::Meat : FoodKind::Pod))
                throw std::runtime_error("Invalid digestive packet checkpoint");
        }
        c.neural_rng.load_state(s);
        c.brain = Brain::load_state(s);
        if (c.id == 0 || c.id >= w.next_creature_id || !ids.insert(c.id).second
            || c.genome_id == 0 || c.genome_id >= w.next_creature_id
            || c.origin < CreatureOrigin::Founder || c.origin > CreatureOrigin::Birth
            || c.energy <= 0 || c.age < 0
            || c.controller < ControllerKind::Spiking || c.controller > ControllerKind::Random
            || !w.traversable(c.position) || c.brain.config().input_count != cfg.brain.input_count
            || c.brain.config().output_count != cfg.brain.output_count
            || c.brain.config().neuron_model != cfg.brain.neuron_model
            || std::abs(c.brain.config().dt-cfg.brain.dt) > 1e-12)
            throw std::runtime_error("Invalid creature checkpoint");
    }
    w.events.resize(checkpoint::count(s,1000000));
    for (auto& e : w.events) read_event(s,e);
    if(source_version) {
        checkpoint::marker(s,"FOOD_SOURCES_1");
        checkpoint::read(s,w.config.food_distribution);
        checkpoint::read_tuple(s,checkpoint::food_source_fields(w.config.food_sources));
        w.config.validate();
        w.food_sources.resize(checkpoint::count(s,2100));
        for(std::size_t i=0;i<w.food_sources.size();++i) {
            auto& source=w.food_sources[i];
            checkpoint::read(s,source.id,source.kind,source.position.x,source.position.y,source.radius,source.phase);
            if(source.id!=i+1 || source.kind<FoodSourceKind::Field || source.kind>FoodSourceKind::PodTree
                || source.radius<=0 || !w.traversable(source.position) || w.sheltered(source.position)
                || w.config.food_distribution!=FoodDistribution::FieldsAndTrees)
                throw std::runtime_error("Invalid food source checkpoint");
        }
        if(checkpoint::count(s,1000000)!=w.resources.size())throw std::runtime_error("Food source resource count mismatch");
        for(auto& r:w.resources) {
            checkpoint::read(s,r.source_id,r.ripening_remaining);
            if(r.source_id>w.food_sources.size() || (r.ripening_remaining < 0 && r.ripening_remaining!=-1)
                || (!r.source_id && r.ripening_remaining!=-1))throw std::runtime_error("Invalid source resource state");
            if(!r.source_id)continue;
            const auto& source=w.food_sources[static_cast<std::size_t>(r.source_id-1)];
            const bool fruit=r.kind==FoodKind::FruitA || r.kind==FoodKind::FruitB;
            if(w.sheltered(r.position) || length(r.position-source.position)>source.radius+1e-8
                || r.shelter_food || (source.kind==FoodSourceKind::Field && r.kind!=FoodKind::Graze)
                || (source.kind==FoodSourceKind::FruitTree && !fruit)
                || (source.kind==FoodSourceKind::PodTree && r.kind!=FoodKind::Pod)
                || (fruit && (r.regrowth<=0 || (r.ripening_remaining>=0 && r.stock>1e-8)))
                || (!fruit && r.ripening_remaining!=-1))throw std::runtime_error("Invalid food source membership");
        }
    }
    if (meta_version) {
        checkpoint::marker(s,"META_MUTATION_1");
        checkpoint::read(s,w.config.mutation.meta_mutation_enabled,w.config.mutation.meta_mutation_probability,w.config.mutation.meta_mutation_sigma);
        if (meta_floor_version) checkpoint::read(s,w.config.mutation.min_mutation_scale);
        if (checkpoint::count(s,w.creatures.size())!=w.creatures.size()) throw std::runtime_error("Mutation scale count mismatch");
        for (auto& c:w.creatures) {
            std::uint64_t id;checkpoint::read(s,id,c.mutation_scale);
            if (id!=c.id || c.mutation_scale<w.config.mutation.min_mutation_scale || c.mutation_scale>MutationConfig::max_mutation_scale)
                throw std::runtime_error("Invalid inherited mutation scale");
        }
        w.config.validate();
    }
    if (background_weather_version) {
        checkpoint::marker(s,"BACKGROUND_WEATHER_1");
        checkpoint::read(s,w.config.background_food_patches,w.config.background_food_energy,w.config.storm_ramp);
        w.config.validate();
    }
    if (mass_energy_version) {
        checkpoint::marker(s,"MASS_ENERGY_1");
        checkpoint::read(s,w.config.mass_scaled_energy_capacity);
        w.config.validate();
    }
    for(const auto& c:w.creatures) if(c.energy>w.config.max_energy(c.body.mass)+1e-8)
        throw std::runtime_error("Creature energy exceeds its body capacity");
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
      << ",\"current_food_energy\":" << w.nursery_food_current_energy
      << ",\"food_population_threshold\":" << w.config.nursery_food_population_threshold
      << ",\"food_energy_factor\":" << w.config.nursery_food_energy_factor
      << ",\"food_reduction_delay\":" << w.config.nursery_food_reduction_delay
      << ",\"food_reductions\":" << w.nursery_food_reductions
      << ",\"food_patches\":" << w.config.nursery_food_patches
      << ",\"food_relocates\":" << (w.config.nursery_food_relocates?"true":"false") << "}"
      << ",\"fov_degrees\":" << w.config.fov_degrees << ",\"seed\":" << w.config.seed
      << ",\"predation\":" << (w.config.predation ? "true" : "false")
      << ",\"shelter_predation_damage\":" << (w.config.shelter_predation_damage ? "true" : "false")
      << ",\"mutate_initial_ancestors\":" << (w.config.mutate_initial_ancestors ? "true" : "false")
      << ",\"founder_mass\":" << w.config.founder_mass
      << ",\"founder_carnivory\":" << w.config.founder_carnivory
      << ",\"meta_mutation_enabled\":" << (w.config.mutation.meta_mutation_enabled ? "true" : "false")
      << ",\"meta_mutation_probability\":" << w.config.mutation.meta_mutation_probability
      << ",\"meta_mutation_sigma\":" << w.config.mutation.meta_mutation_sigma
      << ",\"mutation_scale_floor\":" << w.config.mutation.min_mutation_scale
      << ",\"health_per_mass\":" << w.config.health_per_mass
      << ",\"body_energy_per_mass\":" << w.config.body_energy_per_mass
      << ",\"attack_range\":" << w.config.attack_range
      << ",\"attack_degrees\":" << w.config.attack_degrees
      << ",\"attack_damage\":" << w.config.attack_damage
      << ",\"attack_base_fraction\":" << w.config.attack_base_fraction
      << ",\"carnivore_basal_fraction\":" << w.config.carnivore_basal_fraction
      << ",\"attack_cost\":" << w.config.attack_cost
      << ",\"storm_health_damage\":" << (w.config.storm_health_damage ? "true" : "false")
      << ",\"storm_damage\":" << w.config.storm_damage
      << ",\"healing_rate\":" << w.config.healing_rate
      << ",\"healing_cost\":" << w.config.healing_cost
      << ",\"meat_energy\":" << w.config.meat_energy
      << ",\"meat_decay\":" << w.config.meat_decay
      << ",\"nursery_food_respawn_delay\":" << w.config.nursery_food_respawn_delay
      << ",\"outdoor_food_respawn_delay\":" << w.config.outdoor_food_respawn_delay
      << ",\"nursery_meat_decay\":" << w.config.nursery_meat_decay
      << ",\"carcass_recovery\":" << w.config.carcass_recovery
      << ",\"mass_mutation_probability\":" << w.config.mutation.mass_mutation_probability
      << ",\"mass_mutation_sigma\":" << w.config.mutation.mass_mutation_sigma
      << ",\"carnivory_mutation_probability\":" << w.config.mutation.carnivory_mutation_probability
      << ",\"carnivory_mutation_sigma\":" << w.config.mutation.carnivory_mutation_sigma
      << ",\"max_speed\":" << w.config.max_speed << ",\"max_turn_rate\":" << w.config.max_turn_rate
      << ",\"shelter_size\":" << w.config.shelter_size
      << ",\"outdoor_food_relocates\":" << (w.config.outdoor_food_relocates?"true":"false")
      << ",\"graze_decay\":" << w.config.graze_decay << ",\"fruit_decay\":" << w.config.fruit_decay
      << ",\"shelter_food_decay\":" << w.config.shelter_food_decay
      << ",\"typed_food_proximity\":" << (w.config.typed_food_proximity ? "true" : "false")
      << ",\"shelter_food_energy\":" << w.config.shelter_food_energy
      << ",\"shelter_food_capacity\":" << w.config.shelter_food_capacity
      << ",\"shelter_food_regrowth\":" << w.config.shelter_food_regrowth
      << ",\"mass_scaled_energy_capacity\":" << (w.config.mass_scaled_energy_capacity ? "true" : "false")
      << ",\"energy_capacity\":" << w.config.energy_capacity << ",\"pod_work\":" << w.config.pod_work
      << ",\"food_energy\":{\"graze\":" << w.config.graze_energy
      << ",\"poor_fruit\":" << w.config.poor_fruit_energy << ",\"rich_fruit\":" << w.config.rich_fruit_energy
      << ",\"pod\":" << w.config.pod_energy << "}"
      << ",\"fruit_a_rich\":" << (w.fruit_a_rich ? "true" : "false")
      << ",\"controller\":"; quoted(s,to_string(w.config.controller));
    s << ",\"initial_creatures\":" << w.creatures.size() << ",\"max_population\":" << w.config.max_population
      << ",\"reproduction\":" << (w.config.reproduction ? "true" : "false")
      << ",\"storms_enabled\":" << (w.config.storms_enabled ? "true" : "false")
      << ",\"sensory_interface\":" << (w.config.extended_senses ? 2 : 1)
      << ",\"calibrated_io\":" << (w.config.brain.calibrated_io ? "true" : "false")
      << ",\"neuron_model\":\"" << neuron_model_name(w.config.brain.neuron_model) << "\""
      << ",\"brain_dt\":" << w.config.brain.dt
      << ",\"synaptic_tau\":" << w.config.brain.synaptic_tau
      << ",\"sensory_rate_hz\":" << w.config.brain.sensory_rate_hz
      << ",\"motor_rate_tau\":" << w.config.brain.motor_rate_tau
      << ",\"motor_reference_hz\":" << w.config.brain.motor_reference_hz
      << ",\"motor_gain\":" << w.config.motor_gain << ",\"actuator_tau\":" << w.config.actuator_tau
      << ",\"input_labels\":";
    array(s,ecosystem_input_labels(w.config.extended_senses, w.config.predation, w.config.typed_food_proximity),[&](const std::string& v){ quoted(s,v); });
    s << ",\"terrain\":";
    array(s,w.terrain,[&](Terrain v){ s << static_cast<int>(v); });
    s << ",\"food_distribution\":\"" << food_distribution_name(w.config.food_distribution) << "\""
      << ",\"background_food_patches\":" << w.config.background_food_patches
      << ",\"background_food_energy\":" << w.config.background_food_energy
      << ",\"storm_ramp\":" << (w.config.storm_ramp ? "true" : "false")
      << ",\"field_spacing\":" << w.config.food_sources.field_spacing
      << ",\"food_sources\":";
    array(s,w.food_sources,[&](const FoodSource& source) {
        s << "{\"id\":" << source.id << ",\"kind\":\"" << to_string(source.kind)
          << "\",\"x\":" << source.position.x << ",\"y\":" << source.position.y
          << ",\"radius\":" << source.radius << ",\"phase\":" << source.phase << '}';
    });
    s << ",\"resources\":";
    array(s,w.resources,[&](const EcoResource& r){
        s << "{\"id\":" << r.id << ",\"kind\":"; quoted(s,to_string(r.kind));
        s << ",\"shelter_food\":" << (r.shelter_food ? "true" : "false");
        s << ",\"source_id\":" << r.source_id << ",\"regrowth\":" << r.regrowth;
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
    s << ",\"storm_intensity\":" << w.storm_intensity()
      << ",\"cue\":" << w.storm_cue() << ",\"capacity_limited\":" << (w.capacity_limited ? "true" : "false")
      << ",\"nursery_food_energy\":" << w.nursery_food_current_energy
      << ",\"nursery_food_reductions\":" << w.nursery_food_reductions
      << ",\"totals\":"; totals_json(s,w.totals);
    s << ",\"creatures\":";
    std::size_t index = 0;
    array(s,w.creatures,[&](const EcoCreature& c){
        s << "{\"id\":" << c.id << ",\"parent\":" << c.parent_id << ",\"generation\":" << c.generation
          << ",\"x\":" << c.position.x << ",\"y\":" << c.position.y << ",\"heading\":" << c.heading
          << ",\"max_energy\":" << w.config.max_energy(c.body.mass)
          << ",\"mutation_scale\":" << c.mutation_scale
          << ",\"mass\":" << c.body.mass << ",\"carnivory\":" << c.body.carnivory
          << ",\"health\":" << c.health << ",\"max_health\":" << w.max_health(c)
          << ",\"damage\":" << c.damage_pulse << ",\"attack\":" << c.action.attack
          << ",\"maximum_speed\":" << w.maximum_speed(c)
          << ",\"energy\":" << c.energy << ",\"age\":" << c.age << ",\"speed\":" << c.speed
          << ",\"forward\":" << c.action.forward << ",\"turn\":" << c.turn
          << ",\"forage\":" << c.action.forage << ",\"call\":" << c.action.call
          << ",\"spikes\":" << c.spikes << ",\"offspring\":" << c.offspring
          << ",\"energy_gained\":" << c.energy_gained << ",\"energy_spent\":" << c.energy_spent
          << ",\"pod_work\":" << c.pod_work << ",\"genome_id\":" << c.genome_id
          << ",\"feeding_bouts\":" << c.feeding_bouts << ",\"origin\":";
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
        s << "{\"id\":" << r.id << ",\"x\":" << r.position.x << ",\"y\":" << r.position.y;
        // Plants already have descriptors in metadata. Corpses can appear after
        // recording starts and must remain self-contained when frames are sampled.
        if (r.kind == FoodKind::Meat)
            s << ",\"kind\":\"meat\",\"capacity\":" << r.capacity << ",\"value\":" << r.energy_per_unit;
        else if (w.in_nursery(r.position))
            s << ",\"value\":" << r.energy_per_unit;
        s << ",\"stock\":" << r.stock << ",\"ripening_remaining\":" << r.ripening_remaining
          << ",\"progress\":" << r.progress << ",\"state\":";
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
         "founder_births,descendant_births,births_first_100s,natural_spiking_breeders,mature_offspring,"
         "nursery_population,frontier_population,attacking,healing,body_construction,external_body_energy,"
         "carcass_energy,meat_spoiled_energy,damage,predation_deaths,mean_mass,mean_carnivory,mean_health_fraction,meat_biomass,"
         "nursery_food_energy,nursery_food_reductions,mean_mutation_scale,min_mutation_scale,max_mutation_scale\n";
}
void write_ecosystem_stats(std::ostream& s, const EcosystemWorld& w)
{
    double energy=0,pending=0,biomass=0;
    for (const auto& c : w.creatures) { energy+=c.energy; for (const auto& p : c.digestion) pending+=p.energy; }
    for (const auto& r : w.resources) biomass+=r.stock;
    const auto& t=w.totals;
    s << std::setprecision(12) << w.step_index << ',' << w.time() << ',' << w.creatures.size() << ','
      << t.births << ',' << t.deaths << ',' << t.maturations << ','
      << (w.creatures.empty()?0:energy/static_cast<double>(w.creatures.size())) << ',' << energy << ',' << pending << ','
      << biomass << ',' << to_string(w.weather()) << ',' << t.spikes << ',' << t.pods_opened << ','
      << t.consumed_biomass << ',' << t.regrown_biomass << ',' << t.spoiled_biomass << ',' << t.energy_gained << ','
      << t.metabolism << ',' << t.movement << ',' << t.turning << ',' << t.foraging << ',' << t.calling << ','
      << t.neural << ',' << t.exposure << ',' << t.reproduction_overhead << ',' << t.discarded_energy << ','
      << (w.capacity_limited?1:0) << ',' << t.founder_births << ',' << t.descendant_births << ','
      << t.births_first_100s << ',' << t.natural_spiking_breeders << ',' << t.mature_offspring;
    const auto nursery = std::count_if(w.creatures.begin(),w.creatures.end(),[&](const auto& c){return w.in_nursery(c.position);});
    double mass=0,carnivory=0,health=0,meat=0;
    double scale_sum=0,scale_min=w.creatures.empty()?0:MutationConfig::max_mutation_scale,scale_max=0;
    for(const auto& c:w.creatures){scale_sum+=c.mutation_scale;scale_min=std::min(scale_min,c.mutation_scale);scale_max=std::max(scale_max,c.mutation_scale);}
    for (const auto& c:w.creatures) { mass+=c.body.mass; carnivory+=c.body.carnivory; health+=c.health/w.max_health(c); }
    for (const auto& r:w.resources) if (r.kind==FoodKind::Meat) meat+=r.stock;
    const double population=static_cast<double>(std::max<std::size_t>(1,w.creatures.size()));
    s << ',' << nursery << ',' << w.creatures.size()-nursery
      << ',' << t.attacking
      << ',' << t.healing
      << ',' << t.body_construction
      << ',' << t.external_body_energy
      << ',' << t.carcass_energy
      << ',' << t.meat_spoiled_energy
      << ',' << t.damage
      << ',' << t.predation_deaths
      << ',' << mass/population << ',' << carnivory/population << ',' << health/population << ',' << meat
      << ',' << w.nursery_food_current_energy << ',' << w.nursery_food_reductions
      << ',' << scale_sum/population << ',' << scale_min << ',' << scale_max << '\n';
}
} // namespace neuroevo
