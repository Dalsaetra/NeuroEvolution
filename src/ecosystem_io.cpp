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
      << ",\"discarded_energy\":" << t.discarded_energy << '}';
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
    checkpoint::write(s,"NEUROEVO_ECOSYSTEM_1");
    checkpoint::write_tuple(s,checkpoint::world_config_fields(config));
    checkpoint::write_tuple(s,checkpoint::brain_fields(config.brain));
    checkpoint::write_tuple(s,checkpoint::mutation_fields(config.mutation));
    checkpoint::write(s,step_index,next_creature_id,fruit_a_rich,capacity_limited);
    checkpoint::write_tuple(s,checkpoint::total_fields(totals));
    map_rng.save_state(s); mutation_rng.save_state(s); conflict_rng.save_state(s);
    checkpoint::write(s,terrain.size());
    for (const auto value : terrain) checkpoint::write_value(s,value);
    s << '\n';
    checkpoint::write(s,resources.size());
    for (const auto& r : resources)
        checkpoint::write(s,r.id,r.kind,r.position.x,r.position.y,r.stock,r.capacity,r.regrowth,
            r.energy_per_unit,r.pod_state,r.progress,r.opened_at);
    checkpoint::write(s,creatures.size());
    for (const auto& c : creatures) {
        checkpoint::write(s,c.id,c.parent_id,c.generation,c.position.x,c.position.y,c.heading,c.energy,
            c.age,c.last_birth,c.speed,c.turn,c.ingestion_pulse,c.digestion_pulse,c.controller,
            c.spikes,c.step_spikes,c.offspring,c.energy_gained,c.energy_spent,c.pod_work,c.exposed_time,c.matured);
        checkpoint::write(s,c.action.forward,c.action.left,c.action.right,c.action.forage,c.action.call);
        for (const auto v : c.eaten) checkpoint::write_value(s,v);
        s << '\n';
        checkpoint::write(s,c.digestion.size());
        for (const auto& p : c.digestion) checkpoint::write(s,p.due,p.energy,p.kind);
        c.neural_rng.save_state(s);
        c.brain.save_state(s);
    }
    checkpoint::write(s,events.size());
    for (const auto& e : events) write_event(s,e);
    checkpoint::write(s,"END_ECOSYSTEM");
}

EcosystemWorld EcosystemWorld::load_checkpoint(std::istream& s)
{
    checkpoint::marker(s,"NEUROEVO_ECOSYSTEM_1");
    EcosystemConfig cfg;
    checkpoint::read_tuple(s,checkpoint::world_config_fields(cfg));
    checkpoint::read_tuple(s,checkpoint::brain_fields(cfg.brain));
    checkpoint::read_tuple(s,checkpoint::mutation_fields(cfg.mutation));
    cfg.validate();
    EcosystemWorld w(cfg,false);
    checkpoint::read(s,w.step_index,w.next_creature_id,w.fruit_a_rich,w.capacity_limited);
    checkpoint::read_tuple(s,checkpoint::total_fields(w.totals));
    w.map_rng.load_state(s); w.mutation_rng.load_state(s); w.conflict_rng.load_state(s);
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
            || c.energy <= 0 || c.energy > cfg.energy_capacity+1e-8 || c.age < 0
            || c.controller < ControllerKind::Spiking || c.controller > ControllerKind::Random
            || !w.traversable(c.position) || c.brain.config().input_count != eco_input_count
            || c.brain.config().output_count != eco_output_count
            || std::abs(c.brain.config().dt-cfg.brain.dt) > 1e-12)
            throw std::runtime_error("Invalid creature checkpoint");
    }
    w.events.resize(checkpoint::count(s,1000000));
    for (auto& e : w.events) read_event(s,e);
    checkpoint::marker(s,"END_ECOSYSTEM");
    return w;
}

void write_ecosystem_metadata(std::ostream& s, const EcosystemWorld& w)
{
    s << std::setprecision(10);
    s << "{\"type\":\"metadata\",\"version\":1,\"width\":" << w.config.width << ",\"height\":" << w.config.height
      << ",\"dt\":" << w.config.dt << ",\"radius\":" << w.config.radius << ",\"vision_range\":" << w.config.vision_range
      << ",\"fov_degrees\":" << w.config.fov_degrees << ",\"seed\":" << w.config.seed
      << ",\"max_speed\":" << w.config.max_speed << ",\"max_turn_rate\":" << w.config.max_turn_rate
      << ",\"energy_capacity\":" << w.config.energy_capacity << ",\"pod_work\":" << w.config.pod_work
      << ",\"fruit_a_rich\":" << (w.fruit_a_rich ? "true" : "false")
      << ",\"controller\":"; quoted(s,to_string(w.config.controller));
    s << ",\"initial_creatures\":" << w.creatures.size() << ",\"reproduction\":" << (w.config.reproduction ? "true" : "false")
      << ",\"input_labels\":";
    array(s,ecosystem_input_labels(),[&](const std::string& v){ quoted(s,v); });
    s << ",\"terrain\":";
    array(s,w.terrain,[&](Terrain v){ s << static_cast<int>(v); });
    s << ",\"resources\":";
    array(s,w.resources,[&](const EcoResource& r){
        s << "{\"id\":" << r.id << ",\"kind\":"; quoted(s,to_string(r.kind));
        s << ",\"x\":" << r.position.x << ",\"y\":" << r.position.y << ",\"capacity\":" << r.capacity
          << ",\"value\":" << r.energy_per_unit << '}';
    });
    s << ",\"brains\":";
    array(s,w.creatures,[&](const EcoCreature& c){ s << "{\"id\":" << c.id << ','; graph(s,c); s << '}'; });
    s << "}\n";
}

void write_ecosystem_frame(std::ostream& s, const EcosystemWorld& w, bool record_brains,
    std::unordered_set<std::uint64_t>* known_brains, const std::vector<EcoEvent>* recorded_events)
{
    s << std::setprecision(10);
    s << "{\"type\":\"frame\",\"step\":" << w.step_index << ",\"time\":" << w.time()
      << ",\"weather\":"; quoted(s,to_string(w.weather()));
    s << ",\"cue\":" << w.storm_cue() << ",\"capacity_limited\":" << (w.capacity_limited ? "true" : "false")
      << ",\"totals\":"; totals_json(s,w.totals);
    s << ",\"creatures\":";
    std::size_t index = 0;
    array(s,w.creatures,[&](const EcoCreature& c){
        s << "{\"id\":" << c.id << ",\"parent\":" << c.parent_id << ",\"generation\":" << c.generation
          << ",\"x\":" << c.position.x << ",\"y\":" << c.position.y << ",\"heading\":" << c.heading
          << ",\"energy\":" << c.energy << ",\"age\":" << c.age << ",\"speed\":" << c.speed
          << ",\"turn\":" << c.turn << ",\"forage\":" << c.action.forage << ",\"call\":" << c.action.call
          << ",\"spikes\":" << c.spikes << ",\"offspring\":" << c.offspring
          << ",\"energy_gained\":" << c.energy_gained << ",\"energy_spent\":" << c.energy_spent
          << ",\"pod_work\":" << c.pod_work << ",\"eaten\":";
        array(s,c.eaten,[&](double v){s << v;});
        s << ",\"controller\":"; quoted(s,to_string(c.controller));
        s << ",\"observation\":";
        array(s,w.observe(index++),[&](double v){s << v;});
        const bool new_graph = !known_brains || known_brains->insert(c.id).second;
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
        s << "{\"id\":" << r.id << ",\"stock\":" << r.stock << ",\"progress\":" << r.progress << ",\"state\":";
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
         "metabolism,movement,turning,foraging,calling,neural,exposure,reproduction_overhead,discarded_energy,capacity_limited\n";
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
      << (w.capacity_limited?1:0) << '\n';
}
} // namespace neuroevo
