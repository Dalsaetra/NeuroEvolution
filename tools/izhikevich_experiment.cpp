#include "neuroevo/ecosystem.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

using namespace neuroevo;
using Clock=std::chrono::steady_clock;
struct Variant {std::string name; bool izh; double dt; IzhikevichParameters p;};
std::vector<Variant> variants() {
    return {{"lif20",false,0.02,{}},{"lif1",false,0.001,{}},
        {"izh_rs",true,0.001,{0.02,0.2,-65,8}},
        {"izh_low_adaptation",true,0.001,{0.02,0.2,-65,2}},
        {"izh_fast",true,0.001,{0.1,0.2,-65,2}}};
}
BrainConfig config(const Variant& v) {
    auto c=v.izh ? BrainConfig::izhikevich() : BrainConfig{};
    c.dt=v.dt;c.max_delay_steps=static_cast<std::size_t>(std::ceil(0.160/v.dt));
    // LIF fine-step control keeps the default 0.64*weight voltage impulse.
    if(!v.izh)c.synaptic_gain=32*0.02/v.dt;
    c.izhikevich_defaults=v.p;c.background_activity_enabled=false;
    return c;
}
void profile(const std::filesystem::path& out) {
    std::ofstream f(out/"profile.csv");
    f<<"workload,model,repeat,dt,neurons,synapses,updates,wall_ms,ns_per_brain_update,spikes\n";
    const auto vv=variants();
    volatile double checksum=0;
    // Isolate equation cost with identical silent activity, no edges or inputs,
    // and identical update counts. Compare ns/update, not simulated duration.
    for(int repeat=-1;repeat<7;++repeat)for(int order=0;order<3;++order) {
        const auto& v=vv[(order+repeat+3)%3];auto c=config(v);
        c.input_count=c.output_count=0;c.hidden_count=256;
        Brain brain(c);const auto start=Clock::now();
        for(int t=0;t<5000;++t){brain.step({});checksum+=brain.neurons()[0].potential;}
        const double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        if(repeat>=0)f<<"kernel_quiet,"<<v.name<<','<<repeat<<','<<v.dt<<",256,0,5000,"<<ms<<','<<ms*1e6/5000<<",0\n";
    }
    for(int size:{5,32,128}) for(int repeat=-1;repeat<7;++repeat) for(int order=0;order<3;++order) {
        const auto& v=vv[(order+repeat+3)%3];
        EcosystemConfig eco;eco.brain.hidden_count=size;
        Random topology(811);
        const auto base=size==5 ? make_sparse_ancestral_brain(eco) : Brain::random(eco.brain,topology);
        auto c=config(v);c.hidden_count=size;
        auto prototype=Brain::from_components(c,base.neurons(),base.synapses());
        constexpr int batch=32;
        std::vector<Brain> brains(batch,prototype);
        std::vector<std::vector<double>> inputs(batch,std::vector<double>(c.input_count));
        for(int k=0;k<batch;++k)for(std::size_t i=0;i<c.input_count;++i)inputs[k][i]=0.1+0.7*((k*13+i*7)%31)/30.0;
        const int ticks=static_cast<int>(std::round(1/v.dt));
        std::size_t spike_count=0;
        const auto start=Clock::now();
        for(int t=0;t<ticks;++t)for(int k=0;k<batch;++k) {
            const auto r=brains[k].step(inputs[k]);spike_count+=r.spikes;
            checksum+=r.motor_outputs[0];
        }
        const double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        if(repeat>=0)f<<"brain_"<<size<<','<<v.name<<','<<repeat<<','<<v.dt<<','
            <<prototype.stats().neuron_count<<','<<prototype.stats().synapse_count<<','<<ticks*batch<<','
            <<ms<<','<<ms*1e6/(ticks*batch)<<','<<spike_count<<'\n';
    }
    // Actual ecosystem stepping, without disk recording; construction excluded.
    for(bool noise:{false,true})for(int repeat=-1;repeat<7;++repeat)for(int order=0;order<3;++order) {
        const auto& v=vv[(order+repeat+3)%3];
        EcosystemConfig eco;eco.initial_creatures=24;eco.reproduction=false;
        EcosystemWorld world(eco);
        world.config.brain=config(v);
        world.config.brain.background_activity_enabled=noise;
        for(auto& creature:world.creatures) {
            auto c=world.config.brain;c.hidden_count=creature.brain.config().hidden_count;
            creature.brain=Brain::from_components(c,creature.brain.neurons(),creature.brain.synapses());
        }
        const auto start=Clock::now();
        for(int t=0;t<100;++t)world.step();
        const double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        checksum+=world.creatures.front().energy;
        if(repeat>=0)f<<(noise?"ecosystem_24_default_noise":"ecosystem_24")<<','<<v.name<<','<<repeat<<','<<v.dt<<",94,0,100,"<<ms<<",0,"<<world.totals.spikes<<'\n';
    }
    std::cout<<"Profile complete; checksum="<<checksum<<'\n';
}
struct Pair {
    double self=2,cross=0.75,delay=0.1,cross_delay=0.02;
    double weak=1.2,strong=1.8,offset=0;
    bool pulses=false;
    double a_scale_a=1,d_scale_a=1,a_scale_b=1,d_scale_b=1;
};
constexpr std::size_t A=6,B=7;
Brain make_pair(const Variant& v,const Pair& p) {
    auto c=config(v);c.input_count=6;c.hidden_count=2;c.output_count=0;
    c.sensory_rate_hz=1/c.dt; // exact scheduled event sources, not visual sensors
    std::vector<Brain::Neuron> n(8);
    for(auto& x:n)x.izhikevich=v.p;
    n[A].izhikevich.a*=p.a_scale_a;n[A].izhikevich.d*=p.d_scale_a;
    n[B].izhikevich.a*=p.a_scale_b;n[B].izhikevich.d*=p.d_scale_b;
    const auto cross_steps=std::max(1L,std::lround(p.cross_delay/c.dt));
    n[B].position.x=(cross_steps-0.5)*c.conduction_speed*c.dt;
    for(int i:{1,3,5})n[i].position=n[B].position;
    const double scale=v.izh ? 1000*c.dt : 1;
    const double rheobase=v.izh ? 4 : 10;
    const double weak_weight=p.pulses ? 0 : rheobase*p.weak*scale/c.synaptic_gain;
    const double boost_weight=p.pulses ? 0.5 : rheobase*(p.strong-p.weak)*scale/c.synaptic_gain;
    std::vector<Brain::Synapse> edges{{0,A,boost_weight,1},{1,B,boost_weight,1},
        {2,A,2,1},{3,B,2,1},{4,A,weak_weight,1},{5,B,weak_weight,1},
        {A,A,p.self,static_cast<std::size_t>(std::lround(p.delay/c.dt))},
        {B,B,p.self,static_cast<std::size_t>(std::lround(p.delay/c.dt))},
        {A,B,-p.cross,1},{B,A,-p.cross,1}};
    return Brain::from_components(c,n,edges);
}
struct Measurement {double a1=0,b1=0,a2=0,b2=0;};
Measurement measure(const Variant& v,const Pair& p,int seed,std::ostream* trace=nullptr) {
    auto brain=make_pair(v,p);Random rng(seed);Measurement m;
    std::vector<double> input(6,0);
    const int ticks=static_cast<int>(std::round(4/v.dt));
    for(int t=0;t<ticks;++t) {
        const double time=t*v.dt;const bool reversed=time>=2;
        input[0]=p.pulses ? rng.chance((reversed ? p.weak : p.strong)*v.dt) : !reversed;
        input[1]=p.pulses ? rng.chance((reversed ? p.strong : p.weak)*v.dt) : reversed;
        input[2]=t==0;input[3]=t==std::lround(p.offset/v.dt);
        input[4]=input[5]=!p.pulses;
        brain.step(input);
        const bool sa=brain.neurons()[A].spiked,sb=brain.neurons()[B].spiked;
        if(time>=1 && time<2){m.a1+=sa;m.b1+=sb;}
        if(time>=3){m.a2+=sa;m.b2+=sb;}
        if(trace) *trace<<v.name<<','<<time<<','<<brain.neurons()[A].potential<<','
            <<brain.neurons()[B].potential<<','<<sa<<','<<sb<<'\n';
    }
    return m; // Each measuring window is one second.
}
void wta(const std::filesystem::path& out) {
    std::ofstream f(out/"wta.csv"),rate(out/"fi.csv");
    f<<"model,self,cross,self_delay_ms,cross_delay_ms,weak,strong,offset_ms,mode,seed,a_before,b_before,a_after,b_after\n";
    rate<<"model,current,rate_hz\n";
    for(const auto& v:variants()) {
        for(int i=0;i<=30;++i) {
            Pair p;p.self=p.cross=0;p.weak=i/(v.izh ? 4.0 : 10.0);p.strong=p.weak;
            auto m=measure(v,p,1);
            rate<<v.name<<','<<i<<','<<m.a1<<'\n';
        }
        for(double self:{0.0,1.0,2.0,4.0})for(double cross:{0.25,0.75,1.5,3.0})
            for(double delay:{0.02,0.06,0.1})for(double cd:{0.02,0.06})
                for(int drive=0;drive<3;++drive)for(double offset:{0.0,0.02,0.04}) {
                    Pair p;p.self=self;p.cross=cross;p.delay=delay;p.cross_delay=cd;
                    p.weak=drive==0 ? 0.8 : drive==1 ? 1.2 : 1.8;
                    p.strong=drive==0 ? 1.2 : drive==1 ? 1.8 : 2.4;p.offset=offset;
                    const auto m=measure(v,p,1);
                    f<<v.name<<','<<self<<','<<cross<<','<<delay*1000<<','<<cd*1000<<','
                        <<p.weak<<','<<p.strong<<','<<offset*1000<<",tonic,1,"
                        <<m.a1<<','<<m.b1<<','<<m.a2<<','<<m.b2<<'\n';
                }
        std::cout<<"Tonic sweep complete: "<<v.name<<std::endl;
    }
}
void focused(const std::filesystem::path& out) {
    std::ofstream f(out/"wta_short_delays.csv");
    f<<"model,self,cross,self_delay_ms,cross_delay_ms,offset_ms,a_before,b_before,a_after,b_after\n";
    for(const auto& v:variants())if(v.izh)
        for(double self:{0.25,0.5,1.0,2.0,4.0})for(double cross:{0.5,1.0,2.0,3.0})
            for(double delay:{0.005,0.01,0.02,0.06})for(double cd:{0.001,0.005,0.02})
                for(double offset:{0.0,0.005,0.015}) {
                    Pair p;p.self=self;p.cross=cross;p.delay=delay;p.cross_delay=cd;p.offset=offset;
                    const auto m=measure(v,p,1);
                    f<<v.name<<','<<self<<','<<cross<<','<<delay*1000<<','<<cd*1000<<','<<offset*1000
                        <<','<<m.a1<<','<<m.b1<<','<<m.a2<<','<<m.b2<<'\n';
                }
    std::cout<<"Short-delay sweep complete\n";
}
void validate_candidates(const std::filesystem::path& out) {
    std::ofstream f(out/"validation.csv"),trace(out/"candidate_trace.csv");
    f<<"model,dt,mode,seed,self,cross,weak,strong,offset_ms,a_before,b_before,a_after,b_after\n";
    trace<<"model,time,v_a,v_b,spike_a,spike_b\n";
    const auto all=variants();
    for(int index=2;index<5;++index) {
        auto v=all[index];Pair base;
        base.self=index==2 ? 2 : index==3 ? 1 : 0.5;
        base.cross=index==4 ? 0.5 : 1;
        base.delay=index==4 ? 0.005 : 0.01;
        base.cross_delay=index==3 ? 0.02 : 0.001;
        measure(v,base,1,&trace);
        const auto record=[&](const Variant& variant,const Pair& p,const std::string& mode,int seed) {
            const auto m=measure(variant,p,seed);
            f<<variant.name<<','<<variant.dt<<','<<mode<<','<<seed<<','<<p.self<<','<<p.cross<<','
                <<p.weak<<','<<p.strong<<','<<p.offset*1000<<','<<m.a1<<','<<m.b1<<','<<m.a2<<','<<m.b2<<'\n';
        };
        for(double dt:{0.001,0.0005,0.00025,0.0001}) {
            auto fine=v;fine.dt=dt;
            for(double offset:{0.0,0.005,0.015}) {auto p=base;p.offset=offset;record(fine,p,"resolution",1);}
        }
        for(double self:{0.9,1.0,1.1})for(double cross:{0.9,1.0,1.1})
            for(double offset:{0.0,0.005,0.015})for(int drive=0;drive<3;++drive) {
                auto p=base;p.self*=self;p.cross*=cross;p.offset=offset;
                p.weak=drive==0 ? 1.1 : drive==1 ? 1.2 : 1.8;
                p.strong=drive==0 ? 1.3 : drive==1 ? 1.8 : 2.4;
                record(v,p,"weights_and_contrast",1);
            }
        Random perturb(981);
        for(int seed=1;seed<=100;++seed) {
            auto p=base;
            p.a_scale_a=std::exp(perturb.normal(0,0.05));p.a_scale_b=std::exp(perturb.normal(0,0.05));
            p.d_scale_a=std::exp(perturb.normal(0,0.05));p.d_scale_b=std::exp(perturb.normal(0,0.05));
            record(v,p,"intrinsic_5pct",seed);
            p=base;p.pulses=true;p.weak=20;p.strong=40;
            record(v,p,"poisson_20_40",seed);
        }
    }
    std::cout<<"Candidate validation complete\n";
}
int main(int argc,char** argv) try {
    const std::string mode=argc>1?argv[1]:"all";
    const std::filesystem::path out=argc>2?argv[2]:"runs/izhikevich";
    std::filesystem::create_directories(out);
    if(mode=="profile" || mode=="all")profile(out);
    if(mode=="wta" || mode=="all")wta(out);
    if(mode=="focused" || mode=="all")focused(out);
    if(mode=="validate" || mode=="all")validate_candidates(out);
    if(mode!="profile" && mode!="wta" && mode!="focused" && mode!="validate" && mode!="all")throw std::invalid_argument("Mode must be profile, wta, focused, validate or all");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
