#include "fixtures.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace neuroevo;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void sensory_rates_and_motor_impulse()
{
    for (double dt : {0.01, 0.02}) {
        BrainConfig c;
        c.input_count = 1; c.hidden_count = 0; c.output_count = 1;
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
    EcosystemConfig c = neuroevo::controlled_config();
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
    return c;
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
    try { sensory_rates_and_motor_impulse(); shelter_and_nutrition();
        std::cout << "Calibrated sensorimotor interface passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
