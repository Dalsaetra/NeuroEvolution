#include "neuroevo/ecosystem.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace neuroevo;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string state(const EcosystemWorld& w) { std::ostringstream s; w.save_checkpoint(s); return s.str(); }
std::string evaluation_state(const NewbornEvaluation& e) {
    std::ostringstream s;
    s.precision(17);
    s << e.score;
    for (const auto& t : e.trials) s << ' ' << t.seed << ' ' << t.offspring << ' ' << t.descendant_births
        << ' ' << t.elapsed << ' ' << t.food_energy << ' ' << t.operating_energy
        << ' ' << t.mature_offspring << ' ' << t.focal_age << ' ' << t.first_birth << ' ' << t.second_birth
        << ' ' << t.matured << ' ' << t.focal_alive << ' ' << t.capacity_limited;
    return s.str();
}

void sensory_rates_and_motor_impulse()
{
    for (double dt : {0.01, 0.02}) {
        BrainConfig c;
        c.input_count = c.sensory_input_count = 1; c.hidden_count = 0; c.output_count = 1;
        c.dt = dt; c.calibrated_io = true; c.background_activity_enabled = false;
        for (double intensity : {0.0, 0.05, 0.1, 0.25, 0.5, 1.0}) {
            Brain b(c);
            int spikes = 0;
            for (int i=0;i<static_cast<int>(10/dt);++i) {
                b.step({intensity});
                spikes += b.neurons()[0].spiked;
            }
            require(std::abs(spikes-10*c.sensory_rate_hz*intensity) <= 1.01,
                "Weak sensory values must encode proportional rates independent of dt");
        }
        std::vector<Brain::Neuron> n(2);
        n[0].position={0,0}; n[1].position={0,0};
        // A single sensory spike produces one output spike, then a rate trace.
        Brain b=Brain::from_components(c,n,{{0,1,1000,1}});
        double peak=0, integral=0;
        for (int i=0;i<static_cast<int>(4/dt);++i) {
            const auto r=b.step({i < static_cast<int>(std::ceil(1.0/(c.sensory_rate_hz*dt))) ? 1.0 : 0.0});
            peak=std::max(peak,r.motor_outputs[0]);
            integral+=r.motor_outputs[0]*dt;
        }
        require(peak > 0 && peak < 0.6,"One output spike must not saturate the calibrated motor readout");
        require(std::abs(integral-1.0/c.motor_reference_hz)<1e-6,
            "Normalized motor impulse area must not depend on the brain timestep");
    }
}

EcosystemConfig small_config()
{
    EcosystemConfig c;
    c.width=c.height=12; c.initial_creatures=1;
    c.shelters=c.grazing_patches=c.fruit_patches=c.pods=0;
    c.storms_enabled=false;
    c.brain.hidden_count=0;
    c.brain.background_activity_enabled=false;
    c.brain.initial_connection_probability=0;
    c.basal_cost=1;
    c.movement_cost=c.turn_cost=c.forage_cost=c.call_cost=0;
    c.neuron_cost=c.synapse_cost=c.spike_cost=0;
    c.offspring_energy=1; c.maturity_age=2;
    c.archive_eval_trials=3; c.archive_eval_seconds=4;
    return c;
}

void newborn_evidence()
{
    auto c=small_config();
    EcosystemWorld w(c);
    Brain silent(c.brain);
    const auto before=state(w);
    const auto e=w.evaluate_newborn(silent);
    require(state(w)==before,"Evaluation consumed live-world state or random numbers");
    require(e.trials.size()==3,"Evaluation must run every matched trial including failures");
    for (const auto& trial:e.trials) {
        require(!trial.matured && !trial.focal_alive && trial.offspring==0 && trial.elapsed<1.2,
            "Trials must begin at newborn energy rather than founder energy");
        require(std::abs(trial.operating_energy-1)<1e-8,"Evaluation spent more than the newborn reserve");
    }
    require(evaluation_state(e)==evaluation_state(w.evaluate_newborn(silent)),"Repeated evaluation is not deterministic");
    auto warm=silent;
    std::vector<double> inputs(eco_input_count,0.3);
    for (int i=0;i<7;++i) warm.step(inputs);
    require(evaluation_state(e)==evaluation_state(w.evaluate_newborn(warm)),"Evaluation inherited parental neural state");
    auto other=w;
    other.creatures.clear(); other.step_index=918;
    other.map_rng.next_u64(); other.mutation_rng.next_u64();
    require(evaluation_state(e)==evaluation_state(other.evaluate_newborn(silent)),"Candidate conditions depend on live-world state");

    auto costly=c;
    costly.forage_cost=2;
    EcosystemWorld effort_world(costly);
    std::vector<Brain::Neuron> active_neurons(eco_input_count+eco_output_count);
    active_neurons[eco_input_count+3].bias=100;
    Brain wasteful=Brain::from_components(costly.brain,active_neurons,{});
    const auto waste=effort_world.evaluate_newborn(wasteful);
    require(waste.score<effort_world.evaluate_newborn(silent).score,
        "Nonbreeding failures with identical terminal reserve loss must retain an efficiency/survival signal");
    auto active_warm=wasteful;
    for (int i=0;i<200;++i) active_warm.step(inputs);
    require(evaluation_state(waste)==evaluation_state(effort_world.evaluate_newborn(active_warm)),
        "Warm motor traces changed newborn effort or starvation time");

    // Purely mechanical fertile lineage: one reserve transfers at each birth.
    auto fertile=c;
    fertile.basal_cost=0; fertile.maturity_age=0.2;
    fertile.reproduction_threshold=fertile.reproduction_cost=fertile.offspring_energy=1;
    const auto reproduction=EcosystemWorld(fertile).evaluate_newborn(silent);
    EcosystemWorld parallel(fertile);
    for (const auto workers : {1u, 2u, 4u}) {
        parallel.evaluation_workers=workers;
        const auto unchanged=state(parallel);
        require(evaluation_state(reproduction)==evaluation_state(parallel.evaluate_newborn(silent)),
            "Parallel family trials changed evaluation evidence or score");
        require(state(parallel)==unchanged,"Parallel evaluation changed live state");
    }
    require(reproduction.score>e.score+100,"Breeding descendants must dominate unsuccessful survival");
    for (const auto& t:reproduction.trials)
        require(t.offspring==1 && t.descendant_births>0 && t.first_birth>=0.2 && t.second_birth==-1,
            "Evaluation did not retain focal and descendant reproduction separately");

    auto supported=c;
    supported.establishment=true; supported.archive_min_age=0; supported.archive_min_feeding_bouts=1;
    EcosystemWorld archive_world(supported);
    auto candidate=archive_world.creatures.front();
    candidate.brain=silent; candidate.age=60; candidate.offspring=1;
    archive_world.consider_archive(candidate,true);
    require(archive_world.newborn_evaluations.size()==1 && archive_world.archive.size()==1,
        "Archive candidate did not receive newborn evaluation");
    const double score=archive_world.archive.front().score;
    candidate.id=archive_world.next_creature_id++; candidate.offspring=100; candidate.energy_gained=99999;
    archive_world.consider_archive(candidate,true);
    require(archive_world.newborn_evaluations.size()==1 && archive_world.archive.front().score==score,
        "Live founder performance changed a cached newborn score");
    std::istringstream checkpoint(state(archive_world));
    auto resumed=EcosystemWorld::load_checkpoint(checkpoint);
    require(state(resumed)==state(archive_world),"Checkpoint lost newborn evaluation evidence");
    resumed.consider_archive(candidate,true);
    require(resumed.newborn_evaluations.size()==1,"Checkpoint resume reevaluated a cached genome");
}

void shelter_and_nutrition()
{
    auto c=small_config();
    EcosystemWorld w(c,false);
    EcoCreature self;
    self.position={3.5,5.5}; self.energy=60; self.brain=Brain(c.brain);
    w.creatures.push_back(self);
    w.terrain[5*c.width+5]=Terrain::Shelter;
    require(w.observe(0)[eco_shelter_offset+eco_sectors/2]>0,"Visible shelter lacks a directional cue");
    w.terrain[5*c.width+4]=Terrain::Wall;
    require(w.observe(0)[eco_shelter_offset+eco_sectors/2]==0,"Shelter cue leaks through walls");
    w.terrain[5*c.width+4]=Terrain::Ground;
    w.creatures[0].heading=3.141592653589793;
    for (std::size_t i=eco_shelter_offset;i<eco_input_count;++i)
        require(w.observe(0)[i]==0,"Shelter cue leaks outside field of view");
    w.config.poor_fruit_energy=13; w.config.rich_fruit_energy=40; w.config.pod_energy=60;
    w.creatures[0].digestion_pulse=13*c.ingestion_rate*c.dt;
    const double poor=w.observe(0)[eco_unsheltered_input-2];
    w.creatures[0].digestion_pulse=40*c.ingestion_rate*c.dt;
    const double rich=w.observe(0)[eco_unsheltered_input-2];
    require(poor>0 && rich>poor && rich<1,"Nutrition feedback saturates across configured food values");
}
}

int main()
{
    try { sensory_rates_and_motor_impulse(); newborn_evidence(); shelter_and_nutrition();
        std::cout << "Calibrated sensorimotor interface and newborn evaluation passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
