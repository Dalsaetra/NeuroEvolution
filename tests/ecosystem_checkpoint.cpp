#include "neuroevo/ecosystem.hpp"
#include "../src/checkpoint_fields.hpp"
#include <iomanip>
#include <limits>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string state(const neuroevo::EcosystemWorld& w) { std::ostringstream s; w.save_checkpoint(s); return s.str(); }
// The historical layout deliberately excludes all establishment fields.
std::string legacy_state(const neuroevo::EcosystemWorld& w)
{
    using namespace neuroevo;
    std::ostringstream s;
    s << std::setprecision(std::numeric_limits<double>::max_digits10);
    checkpoint::write(s,"NEUROEVO_ECOSYSTEM_1");
    checkpoint::write_tuple(s,checkpoint::world_config_fields(w.config));
    checkpoint::write_tuple(s,checkpoint::brain_fields(w.config.brain));
    checkpoint::write_tuple(s,checkpoint::legacy_mutation_fields(w.config.mutation));
    checkpoint::write(s,w.step_index,w.next_creature_id,w.fruit_a_rich,w.capacity_limited);
    checkpoint::write_tuple(s,checkpoint::total_fields(w.totals));
    w.map_rng.save_state(s); w.mutation_rng.save_state(s); w.conflict_rng.save_state(s);
    checkpoint::write(s,w.terrain.size());
    for(auto t:w.terrain) checkpoint::write(s,t);
    checkpoint::write(s,w.resources.size());
    for(const auto& r:w.resources) checkpoint::write(s,r.id,r.kind,r.position.x,r.position.y,r.stock,r.capacity,
        r.regrowth,r.energy_per_unit,r.pod_state,r.progress,r.opened_at);
    checkpoint::write(s,w.creatures.size());
    for(const auto& c:w.creatures) {
        checkpoint::write(s,c.id,c.parent_id,c.generation,c.position.x,c.position.y,c.heading,c.energy,
            c.age,c.last_birth,c.speed,c.turn,c.ingestion_pulse,c.digestion_pulse,c.controller,
            c.spikes,c.step_spikes,c.offspring,c.energy_gained,c.energy_spent,c.pod_work,c.exposed_time,c.matured);
        checkpoint::write(s,c.action.forward,c.action.left,c.action.right,c.action.forage,c.action.call);
        for(auto v:c.eaten) checkpoint::write(s,v);
        checkpoint::write(s,c.digestion.size());
        for(const auto& p:c.digestion) checkpoint::write(s,p.due,p.energy,p.kind);
        c.neural_rng.save_state(s);
        std::ostringstream brain; c.brain.save_state(brain);
        std::istringstream lines(brain.str());
        std::string line;
        std::getline(lines,line); s << "NEUROEVO_BRAIN_1\n";
        std::getline(lines,line); s << line << '\n';
        std::getline(lines,line); // v2 interface fields did not exist in v1.
        s << lines.rdbuf();
    }
    checkpoint::write(s,w.events.size());
    for(const auto& e:w.events) checkpoint::write(s,e.time,e.type,e.creature,e.other,e.resource,e.amount);
    checkpoint::write(s,"END_ECOSYSTEM");
    return s.str();
}
}
int main()
{
    try {
        neuroevo::EcosystemConfig config;
        config.initial_creatures=5;
        config.archive_tournament_size=4;
        config.graze_energy=3.25;
        config.reproduction=false;
        config.mutation.remove_neuron_probability=0.017;
        neuroevo::EcosystemWorld original(config);
        for (int i=0;i<35;++i) original.step();
        std::istringstream input(state(original));
        auto resumed=neuroevo::EcosystemWorld::load_checkpoint(input);
        require(resumed.config.mutation.stable,"Checkpoint lost stable mutation policy");
        require(resumed.config.mutation.remove_neuron_probability==0.017,"Checkpoint lost pruning rate");
        require(resumed.config.archive_tournament_size==4,"Checkpoint lost archive tournament size");
        require(resumed.config.graze_energy==3.25,"Checkpoint lost food energy settings");
        require(state(original)==state(resumed),"Checkpoint roundtrip lost world, brain, or RNG state");
        for (int i=0;i<45;++i) { original.step(); resumed.step(); }
        require(state(original)==state(resumed),"Resumed spiking simulation diverged from uninterrupted execution");
        require(original.totals.spikes>0,"Checkpoint continuation did not exercise spiking activity");

        // Pending digestion, opening progress, weather offset, and transient events
        // must survive checkpoints in addition to visible positions and genomes.
        original.config.phase_offset=179.95;
        original.config.storms_enabled=false;
        original.config.reproduction=true;
        original.creatures.front().digestion.push_back({original.time()+0.25,3.75,neuroevo::FoodKind::FruitB});
        original.creatures.front().age=200;
        original.creatures.front().energy=180;
        original.resources.back().progress=2.75;
        original.events.push_back({original.time(),"test_event",1,2,3,4.5});
        std::istringstream input2(state(original));
        resumed=neuroevo::EcosystemWorld::load_checkpoint(input2);
        for (int i=0;i<8;++i) { original.step(); resumed.step(); }
        require(state(original)==state(resumed),"Checkpoint lost digestion, birth, mutation, or weather state");
        require(original.totals.births>0,"Continuation test failed to exercise offspring mutation");

        // Storm toggling did not exist in v1, whose implicit behavior was on.
        // Construct a genuinely historical 87-input world for the v1 fixture.
        auto legacy_config = config;
        legacy_config.outdoor_food_relocates = false;
        legacy_config.shelters = 0;
        legacy_config.mutation.remove_neuron_probability = 0;
        legacy_config.extended_senses = legacy_config.brain.calibrated_io = false;
        legacy_config.brain.input_count = legacy_config.brain.sensory_input_count = neuroevo::eco_legacy_input_count;
        legacy_config.actuator_tau = 0;
        legacy_config.archive_eval_trials = 0;
        legacy_config.motor_gain = 8;
        legacy_config.graze_energy = 2; legacy_config.poor_fruit_energy = 4;
        legacy_config.rich_fruit_energy = 10; legacy_config.pod_energy = 12;
        original = neuroevo::EcosystemWorld(legacy_config);
        for (int i=0;i<12;++i) original.step();
        std::istringstream legacy(legacy_state(original));
        auto upgraded=neuroevo::EcosystemWorld::load_checkpoint(legacy);
        require(!upgraded.config.mutation.stable,"Historical checkpoint must retain its mutation policy");
        require(upgraded.config.mutation.remove_neuron_probability==0,"Historical checkpoint enabled neuron pruning");
        require(!upgraded.config.establishment && upgraded.archive.empty(),"Legacy checkpoints unexpectedly enabled immigration");
        require(upgraded.config.graze_energy==2 && upgraded.config.poor_fruit_energy==4
            && upgraded.config.rich_fruit_energy==10 && upgraded.config.pod_energy==12,
            "Legacy checkpoint did not preserve historical food energy");
        require(legacy_state(upgraded)==legacy_state(original),"Legacy checkpoint migration lost historical state");
        for(int i=0;i<10;++i) { original.step(); upgraded.step(); }
        require(legacy_state(upgraded)==legacy_state(original),"Legacy checkpoint continuation changed ecological behavior");

        auto bad=state(original);
        bad.resize(bad.size()/2);
        bool rejected=false;
        try { std::istringstream broken(bad); neuroevo::EcosystemWorld::load_checkpoint(broken); }
        catch (const std::exception&) { rejected=true; }
        require(rejected,"Truncated checkpoint must be rejected");
        std::istringstream invalid("NEUROEVO_ECOSYSTEM_99");
        rejected=false;
        try { neuroevo::EcosystemWorld::load_checkpoint(invalid); }
        catch (const std::exception&) { rejected=true; }
        require(rejected,"Unknown checkpoint version must be rejected");
        std::cout << "Ecosystem checkpoints preserve exact continuation\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
