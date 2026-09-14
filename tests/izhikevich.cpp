#include "neuroevo/ecosystem.hpp"
#include "../src/checkpoint_fields.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace neuroevo;
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
Brain cell(double bias, double dt=0.001, IzhikevichParameters p={}) {
    auto c=BrainConfig::izhikevich(); c.input_count=0; c.hidden_count=1; c.output_count=0; c.dt=dt;
    c.background_activity_enabled=false;
    std::vector<Brain::Neuron> n(1); n[0].bias=bias; n[0].izhikevich=p;
    return Brain::from_components(c,n,{});
}
std::vector<double> spikes(Brain b, double seconds=2) {
    std::vector<double> result;
    for (int t=0;t<static_cast<int>(std::round(seconds/b.config().dt));++t) {
        b.step({});
        require(std::isfinite(b.neurons()[0].potential) && std::isfinite(b.neurons()[0].recovery),"Non-finite cell");
        if (b.neurons()[0].spiked) result.push_back(t*b.config().dt);
    }
    return result;
}
int tail(const std::vector<double>& times) { int n=0; for(double t:times) if(t>=1) ++n; return n; }
// Independent small-step simultaneous Euler reference, with immediate reset.
int reference(double current) {
    double v=-65,u=-13; int count=0;
    constexpr double h=0.01;
    for (int i=0;i<200000;++i) {
        double dv=0.04*v*v+5*v+140-u+current, du=0.02*(0.2*v-u);
        v+=h*dv; u+=h*du;
        if(v>=30) {v=-65;u+=8;if(i>=100000)++count;}
    }
    return count;
}
std::string saved(const Brain& b) {std::ostringstream s;b.save_state(s);return s.str();}
std::string saved(const EcosystemWorld& w) {std::ostringstream s;w.save_checkpoint(s);return s.str();}
int main() try {
    require(spikes(cell(0)).empty(),"RS cell must stay quiet without drive");
    const auto low=spikes(cell(10)), high=spikes(cell(20));
    require(low.size()>4 && low[1]-low[0]<low.back()-low[low.size()-2],"RS cell must adapt");
    require(tail(high)>tail(low),"More DC drive must raise the RS firing rate");
    IzhikevichParameters fs;fs.a=0.1;fs.d=2;
    require(tail(spikes(cell(10,0.001,fs)))>2*tail(low),"FS parameters must produce faster firing");
    for(double current:{5.0,10.0,20.0}) for(double dt:{0.001,0.0005,0.00025})
        require(std::abs(tail(spikes(cell(current,dt)))-reference(current))<=3,
            "Tonic rate differs materially from independent 0.01 ms reference");
    auto c=BrainConfig::izhikevich();c.input_count=1;c.hidden_count=1;c.output_count=1;
    c.dt=0.0005; // Two-update physical pulses, including a wrapped buffer cursor.
    std::vector<Brain::Neuron> neurons(3);neurons[1].izhikevich=fs;
    Brain original=Brain::from_components(c,neurons,{{0,1,2,1},{1,1,1,60},{1,2,1,1}});
    for(int i=0;i<161;++i) original.step({1});
    std::istringstream input(saved(original));auto loaded=Brain::load_state(input);
    require(saved(original)==saved(loaded),"Izhikevich checkpoint lost state");
    for(int i=0;i<1000;++i){original.step({0.5});loaded.step({0.5});}
    require(saved(original)==saved(loaded),"Izhikevich checkpoint continuation diverged");
    loaded.reset_state();
    require(loaded.neurons()[1].potential==fs.c && loaded.neurons()[1].recovery==fs.b*fs.c,
        "Reset must use each neuron's inherited parameters");
    // Parameter edits must leave fixed intrinsic parameters alone by default.
    MutationConfig mutation;mutation.structural_edit_probability=0;mutation.mutate_weight_probability=0;
    mutation.mutate_neuron_probability=1;mutation.local_edit_limit=1;
    Random rng(871);
    for(int i=0;i<300;++i) loaded.mutate(mutation,rng);
    require(loaded.neurons()[1].izhikevich.a==fs.a && loaded.neurons()[1].izhikevich.d==fs.d,
        "Intrinsic mutation must be opt-in");
    mutation.izhikevich_intrinsic_probability=1;
    for(int i=0;i<300;++i) loaded.mutate(mutation,rng);
    const auto p=loaded.neurons()[1].izhikevich;
    require(p.a>=0.005 && p.a<=0.1 && p.d>=0.5 && p.d<=12 && p.b==fs.b && p.c==fs.c
        && (p.a!=fs.a || p.d!=fs.d),"Intrinsic mutation violated its a/d policy");
    EcosystemConfig config;config.brain.select_model(NeuronModel::Izhikevich);
    config.initial_creatures=2;config.reproduction=false;config.mutation.izhikevich_intrinsic_probability=0.2;
    EcosystemWorld world(config);
    for(int i=0;i<5;++i)world.step();
    std::istringstream world_input(saved(world));auto resumed=EcosystemWorld::load_checkpoint(world_input);
    for(int i=0;i<10;++i){world.step();resumed.step();}
    require(saved(world)==saved(resumed),"World did not preserve model, intrinsic policy or recovery state");
    bool rejected=false;
    try {auto bad=BrainConfig::izhikevich();bad.dt=0.02;Brain invalid(bad);}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Unsafe Izhikevich timestep must be rejected");
    // Genuine version-3 layout: no new config, parameter or recovery records.
    BrainConfig old;old.input_count=0;old.hidden_count=1;old.output_count=0;
    std::ostringstream legacy;
    checkpoint::write(legacy,"NEUROEVO_BRAIN_3");
    checkpoint::write_tuple(legacy,checkpoint::brain_fields(old));
    checkpoint::write_tuple(legacy,checkpoint::calibrated_brain_fields(old));
    checkpoint::write(legacy,1,0,0);checkpoint::write(legacy,0,0,0,0,1,0,0,0);
    for(std::size_t i=0;i<=old.max_delay_steps;++i)checkpoint::write_value(legacy,0);
    std::istringstream old_input(legacy.str());auto restored=Brain::load_state(old_input);
    require(restored.config().neuron_model==NeuronModel::Lif,"Old brain did not load as LIF");
    // A selected RS two-cell circuit must follow a reversal at multiple
    // resolutions with the same physical one-ms synaptic waveform.
    for(double dt:{0.001,0.0005,0.00025,0.0001}) {
        auto w=BrainConfig::izhikevich();w.dt=dt;w.input_count=6;w.hidden_count=2;w.output_count=0;
        w.max_delay_steps=static_cast<std::size_t>(std::lround(0.02/dt));w.sensory_rate_hz=1/dt;
        w.background_activity_enabled=false;
        std::vector<Brain::Neuron> n(8);n[7].position.x=(0.001/dt-0.5)*6*dt;
        for(int i:{1,3,5})n[i].position=n[7].position;
        const double weak=4.8*1000*dt/32,boost=2.4*1000*dt/32;
        auto pair=Brain::from_components(w,n,{{0,6,boost,1},{1,7,boost,1},
            {2,6,2,1},{3,7,2,1},{4,6,weak,1},{5,7,weak,1},
            {6,6,2,static_cast<std::size_t>(std::lround(0.01/dt))},
            {7,7,2,static_cast<std::size_t>(std::lround(0.01/dt))},{6,7,-1,1},{7,6,-1,1}});
        int first_a=0,first_b=0,second_a=0,second_b=0;
        for(int i=0;i<static_cast<int>(std::lround(4/dt));++i) {
            const double t=i*dt;pair.step({double(t<2),double(t>=2),double(i==0),double(i==0),1,1});
            if(t>=1 && t<2){first_a+=pair.neurons()[6].spiked;first_b+=pair.neurons()[7].spiked;}
            if(t>=3){second_a+=pair.neurons()[6].spiked;second_b+=pair.neurons()[7].spiked;}
        }
        require(first_a>=75 && first_a<=80 && first_b==0 && second_a==0 && second_b>=75 && second_b<=80,
            "Validated RS circuit lost selective reversal or timestep convergence");
    }
    std::cout<<"Izhikevich reference rates, adaptation, inheritance, opt-in mutation and exact continuation passed\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
