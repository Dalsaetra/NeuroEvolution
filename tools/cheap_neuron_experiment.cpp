#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "neuroevo/brain.hpp"

// Research kernels only: these do not change Brain or checkpoint semantics.
// Time is in seconds; external drive is relative to each model's DC onset.
enum class Kind { Legacy, Izh, Filtered, Adaptive, Rate };
struct Spec { std::string name; Kind kind; double dt; };
const std::vector<Spec> specs{{"legacy_lif",Kind::Legacy,.02},
    {"izh_rs",Kind::Izh,.001},{"filtered_lif",Kind::Filtered,.005},
    {"adaptive_lif",Kind::Adaptive,.005},{"rate",Kind::Rate,.02}};
struct Cell {
    Spec spec; double v=0,u=-13,s=0,adapt=0,output=0;
    int refractory=0;
    double em,es,ea,cs,ca,er;
    explicit Cell(Spec p):spec(p),em(std::exp(-p.dt/.05)),es(std::exp(-p.dt/.1)),
        ea(std::exp(-p.dt/.2)),cs(.1/(.1-.05)*(es-em)),
        ca(.2/(.2-.05)*(ea-em)),er(std::exp(-p.dt/.1)) {
        if(p.kind==Kind::Izh)v=-65;
    }
    double step(double drive,double arrival) {
        output=0;
        switch(spec.kind) {
        case Kind::Legacy:
            if(refractory>0){--refractory;v=0;break;}
            v+=(spec.dt/.1)*(-v+drive)+.64*arrival;
            if(v>=1){v=0;output=1;refractory=std::lround(.04/spec.dt);}break;
        case Kind::Izh: {
            const double h=1000*spec.dt,current=4*drive+32*arrival;
            for(int k=0;k<2 && v<30;++k)v=std::min(30.,v+.5*h*(.04*v*v+5*v+140-u+current));
            u+=h*.02*(.2*v-u);
            if(v>=30){v=-65;u+=8;output=1;}break;
        }
        case Kind::Filtered: case Kind::Adaptive:
            s+=arrival;
            if(refractory>0){--refractory;v=0;}
            else {
                // Exact subthreshold solution for decaying current(s) and constant drive.
                v=em*v+(1-em)*drive+cs*s-ca*adapt;
                if(v>=1){v=0;output=1;refractory=std::lround(.01/spec.dt);}
            }
            s*=es;adapt*=ea;
            if(spec.kind==Kind::Adaptive)adapt+=.05*output;
            break;
        case Kind::Rate: {
            // A bounded population activity, not a single spiking neuron.
            const double x=std::max(0.,drive+arrival-1);
            v=er*v+(1-er)*x/(1+x);output=v;break;
        }
        }
        if(!std::isfinite(v+u+s+adapt))throw std::runtime_error("Non-finite state");
        return output;
    }
};
struct Params { double self=0,cross=0,delay=.01,weak=1.2,strong=2.4; };
struct Pair {
    std::array<Cell,2> cells;
    std::vector<std::array<double,2>> ring;
    Params p; int cursor=0,delay;
    Pair(Spec spec,Params q):cells{{Cell(spec),Cell(spec)}},p(q),
        delay(std::max(1L,std::lround(q.delay/spec.dt))) {ring.resize(delay+1);}
    std::array<double,2> step(double a,double b) {
        const auto arrival=ring[cursor];ring[cursor]={0,0};
        const double x=cells[0].step(a,arrival[0]),y=cells[1].step(b,arrival[1]);
        const int dest=(cursor+delay)%ring.size();
        ring[dest][0]+=p.self*x-p.cross*y;
        ring[dest][1]+=p.self*y-p.cross*x;
        cursor=(cursor+1)%ring.size();return {x,y};
    }
};
// All models see the same pre-generated external waveform at a given seed.
// 1 ms source grid; Poisson events filtered with 100 ms decay; coarser models
// receive the average over their interval. Mean weak/strong drive = 1.2/2.4.
using Wave=std::vector<std::array<double,2>>;
Wave waveform(Params p,int seed,bool noisy) {
    Wave w(8000);std::mt19937 rng(seed);double a=0,b=0;
    const double e=std::exp(-.001/.1);
    for(int t=0;t<8000;++t) {
        const bool reverse=t>=4000;
        if(noisy) {
            std::poisson_distribution<int> pa((reverse?20:40)*.001),pb((reverse?40:20)*.001);
            // Adjust amplitudes, keeping weak/strong event rates 20/40 Hz.
            a+=pa(rng)*(reverse?p.weak/20:p.strong/40)/.1;
            b+=pb(rng)*(reverse?p.strong/40:p.weak/20)/.1;
            w[t]={a*(1-e)*100,b*(1-e)*100};a*=e;b*=e;
        } else w[t]={reverse?p.weak:p.strong,reverse?p.strong:p.weak};
    }
    return w;
}
struct Result { double a1=0,b1=0,a2=0,b2=0; bool pass() const {
    return a1>0 && b2>0 && a1>=3*b1 && b2>=3*a2;
}};
Result compete(Spec spec,Params p,int seed,bool noisy,std::ostream* trace=nullptr,double offset=0) {
    Pair pair(spec,p);const auto wave=waveform(p,seed,noisy);Result r;
    const int stride=std::lround(spec.dt/.001);
    for(int t=0;t<8000;t+=stride) {
        std::array<double,2> drive{};
        for(int k=0;k<stride;++k)for(int j=0;j<2;++j)drive[j]+=wave[t+k][j]/stride;
        // Finite initial kick to both cells; phase offset is explicitly varied.
        drive[0]+=t<100?3:0;
        drive[1]+=(t>=std::lround(offset*1000) && t<std::lround(offset*1000)+100)?3:0;
        const auto output=pair.step(drive[0],drive[1]);
        const double scale=spec.kind==Kind::Rate?100*spec.dt:1;
        if(t>=2000 && t<4000){r.a1+=output[0]*scale/2;r.b1+=output[1]*scale/2;}
        if(t>=6000){r.a2+=output[0]*scale/2;r.b2+=output[1]*scale/2;}
        if(trace)*trace<<spec.name<<','<<spec.dt<<','<<t*.001<<','<<output[0]<<','<<output[1]<<','<<pair.cells[0].v<<','<<pair.cells[1].v<<'\n';
    }
    return r;
}
double retain(Spec spec,Params p,double drive=0,double inhibit=0,double onset=2,double duration=.02) {
    p.cross=0;Pair pair(spec,p);double count=0;
    const int first=std::lround(onset/spec.dt),last=first+std::lround(duration/spec.dt);
    for(int t=0;t<std::lround(8/spec.dt);++t) {
        auto o=pair.step(drive+(t<std::lround(.1/spec.dt)?6:0)-(t>=first && t<last?inhibit:0),0);
        if(t>=std::lround(6/spec.dt))count+=o[0]*(spec.kind==Kind::Rate?100*spec.dt:1)/2;
    }
    return count;
}
void row(std::ostream& f,Spec s,Params p,std::string mode,int seed,Result r) {
    f<<s.name<<','<<s.dt<<','<<p.self<<','<<p.cross<<','<<p.delay<<','<<p.weak<<','<<p.strong<<','<<mode<<','<<seed<<','<<r.a1<<','<<r.b1<<','<<r.a2<<','<<r.b2<<','<<r.pass()<<'\n';
}
const char* header="model,dt,self,cross,delay,weak,strong,mode,seed,a_before,b_before,a_after,b_after,pass\n";
void sweep(const std::filesystem::path& out) {
    std::ofstream f(out/"sweep.csv");f<<header;
    std::ofstream memory(out/"memory_sweep.csv");memory<<"model,self,delay,rate0,rate_low,rate_high\n";
    for(auto spec:specs) {
        const auto weights=spec.kind==Kind::Rate?std::vector<double>{0,2,3,4,4.5,5,6,8}:
            spec.kind==Kind::Legacy || spec.kind==Kind::Izh?std::vector<double>{0,.25,.5,1,2,4}:
            std::vector<double>{0,.2,.3,.4,.5,.6,.8,1,1.5};
        const auto crosses=spec.kind==Kind::Rate?std::vector<double>{1,2,3,4,6,8,12}:
            std::vector<double>{.1,.2,.4,.6,.8,1,1.5,2,3,4};
        for(double self:weights)for(double delay:{.02,.06,.1}) {
            Params p;p.self=self;p.delay=delay;
            memory<<spec.name<<','<<self<<','<<delay<<','<<retain(spec,p)<<','<<retain(spec,p,.4)<<','<<retain(spec,p,.8)<<'\n';
            for(double cross:crosses) {
                p.cross=cross;
                row(f,spec,p,"tonic",0,compete(spec,p,0,false));
                for(int seed=1;seed<=5;++seed)row(f,spec,p,"noise",seed,compete(spec,p,seed,true));
            }
        }
        std::cout<<"Sweep complete: "<<spec.name<<std::endl;
    }
}
void profile(const std::filesystem::path& out) {
    std::ofstream f(out/"profile.csv");f<<"model,dt,workload,repeat,steps,wall_ms,ns_per_cell_step,checksum\n";
    volatile double checksum=0;
    // Same minimal two-cell routing implementation; no Brain/ecosystem overhead.
    // Fixed 20 seconds x 1024 pairs, 1 warm-up + 9 rotating-order repeats.
    auto measured=specs;
    measured.push_back({"filtered_lif_2ms",Kind::Filtered,.002});
    measured.push_back({"adaptive_lif_2ms",Kind::Adaptive,.002});
    const int count=static_cast<int>(measured.size());
    for(bool active:{false,true})for(int repeat=-1;repeat<9;++repeat)for(int index=0;index<count;++index) {
        auto s=measured[(index+repeat+count)%count];Params p;p.self=.5;p.cross=.4;p.delay=.02;
        if(s.kind==Kind::Rate){p.self=5;p.cross=4;}
        std::vector<Pair> pairs;for(int k=0;k<1024;++k)pairs.emplace_back(s,p);
        const int ticks=std::lround(20/s.dt);
        const auto start=std::chrono::steady_clock::now();
        double sum=0;
        for(int t=0;t<ticks;++t)for(int k=0;k<1024;++k) {
            const auto o=pairs[k].step(active?1.2+(k%7)*.2:0,active?1.2+(k%3)*.2:0);sum+=o[0]+o[1];
        }
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        checksum+=sum;
        if(repeat>=0)f<<s.name<<','<<s.dt<<','<<(active?"active":"quiet")<<','<<repeat<<','<<ticks*2048<<','<<ms<<','<<ms*1e6/(ticks*2048)<<','<<sum<<'\n';
    }
    std::cout<<"Profile checksum "<<checksum<<'\n';
}
void focused(const std::filesystem::path& out) {
    std::ofstream f(out/"focused.csv");f<<header;
    for(auto spec:specs)if(spec.kind==Kind::Filtered || spec.kind==Kind::Adaptive || spec.kind==Kind::Rate) {
        const auto selfs=spec.kind==Kind::Rate?std::vector<double>{4.2,4.5,5}:
            std::vector<double>{.85,.9,.95,1};
        const auto crosses=spec.kind==Kind::Rate?std::vector<double>{1,1.5,2,2.5,3}:
            std::vector<double>{.25,.3,.35,.4,.45,.5,.6};
        for(double self:selfs)for(double cross:crosses)for(double delay:{.02,.06})
            for(auto drive:std::vector<std::array<double,2>>{{1.2,2.4},{1.2,3.6},{.4,1.8},{.6,3.6}}) {
                Params p{self,cross,delay,drive[0],drive[1]};
                row(f,spec,p,"tonic",0,compete(spec,p,0,false));
                for(int seed=1;seed<=5;++seed)row(f,spec,p,"noise",seed,compete(spec,p,seed,true));
            }
    }
}
void validate(const std::filesystem::path& out) {
    std::ofstream f(out/"validation.csv"),m(out/"retention.csv"),trace(out/"trace.csv");
    f<<header;m<<"model,dt,self,delay,drive,inhibition,onset,duration,late_rate\n";
    trace<<"model,dt,time,output_a,output_b,v_a,v_b\n";
    for(auto spec:specs)if(spec.kind==Kind::Filtered || spec.kind==Kind::Adaptive || spec.kind==Kind::Rate) {
        Params p{1,.45,spec.kind==Kind::Filtered?.06:.02,1.2,3.6};
        if(spec.kind==Kind::Rate){p.self=5;p.cross=2.5;}
        row(f,spec,p,"selected_tonic",0,compete(spec,p,0,false,&trace));
        for(int seed=1001;seed<=1100;++seed)row(f,spec,p,"heldout_noise",seed,compete(spec,p,seed,true));
        for(double self:{.9,1.,1.1})for(double cross:{.9,1.,1.1})for(double offset:{0.,.02,.04}) {
            auto q=p;q.self*=self;q.cross*=cross;
            row(f,spec,q,"weights_and_phase",std::lround(offset*1000),compete(spec,q,0,false,nullptr,offset));
        }
        for(double strong:{1.8,2.4,3.,3.6,4.2}) {
            auto q=p;q.strong=strong;
            row(f,spec,q,"contrast",0,compete(spec,q,0,false));
        }
        for(double dt:spec.kind==Kind::Rate?std::vector<double>{.02,.01,.005,.001}:std::vector<double>{.01,.005,.002,.001}) {
            auto fine=spec;fine.dt=dt;
            for(double offset:{0.,.02,.04})row(f,fine,p,"resolution",std::lround(offset*1000),compete(fine,p,0,false,nullptr,offset));
            const auto record=[&](double drive,double inhibition,double onset,double duration) {
                m<<fine.name<<','<<dt<<','<<p.self<<','<<p.delay<<','<<drive<<','<<inhibition<<','<<onset<<','<<duration<<','<<retain(fine,p,drive,inhibition,onset,duration)<<'\n';
            };
            for(double drive:{0.,.2,.4,.6,.8,1.,1.2,1.8,2.4,3.6})record(drive,0,2,0);
            // Every representable onset within a 200 ms window after settling.
            for(double inhibit:{.1,.25,.5})for(int phase=0;phase<std::lround(.2/dt);++phase)record(0,inhibit,2+phase*dt,.02);
            for(double inhibit:{.1,.25,.5})record(0,inhibit,2,6);
        }
    }
}
void verify() {
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    // Check copied baseline equations against production Brain, update by update.
    for(int index:{0,1}) {
        auto spec=specs[index];Cell c(spec);
        auto cfg=index?neuroevo::BrainConfig::izhikevich():neuroevo::BrainConfig{};
        cfg.input_count=cfg.hidden_count=0;cfg.output_count=1;cfg.background_activity_enabled=false;
        std::vector<neuroevo::Brain::Neuron> neurons(1);neurons[0].bias=index?7.2:18;
        auto brain=neuroevo::Brain::from_components(cfg,neurons,{});
        for(int t=0;t<1000;++t) {
            const auto spike=c.step(1.8,0);brain.step({});
            require((spike != 0)==brain.neurons()[0].spiked,"Baseline spike mismatch");
            require(std::abs(c.v-brain.neurons()[0].potential)<1e-9,"Baseline voltage mismatch");
        }
    }
    // Compare LIF firing under DC against its continuous-time analytic ISI.
    Cell lif({"fine",Kind::Filtered,.0001});int spikes=0;
    for(int t=0;t<100000;++t)spikes+=lif.step(2,0)>0;
    const double exact=1/(.01+.05*std::log(2.));
    require(std::abs(spikes/10.-exact)<.15,"LIF analytic rate mismatch");
    // Independent ODE Euler reference for filtered subthreshold dynamics.
    Cell coarse({"check",Kind::Adaptive,.005});coarse.s=.7;coarse.adapt=.2;
    double v=0,s=.7,a=.2;
    for(int t=0;t<5000;++t){v+=.00001*(-v+.1+s-a)/.05;s-=.00001*s/.1;a-=.00001*a/.2;}
    for(int t=0;t<10;++t)coarse.step(.1,0);
    require(std::abs(v-coarse.v)<.0001,"Filtered integration mismatch");
    // A stable isolated rate state has an analytic fixed point at self=5.
    Params p{5,0,.02};const auto rate=retain(specs[4],p);
    require(std::abs(rate-50*(1+std::sqrt(.2)))<.001,"Rate fixed-point mismatch");
    for(int index:{2,3,4}) {
        auto spec=specs[index];Params q{1,.45,index==2?.06:.02,1.2,3.6};
        if(index==4){q.self=5;q.cross=2.5;}
        Pair quiet(spec,q);
        for(int t=0;t<1000;++t){auto o=quiet.step(0,0);require(o[0]==0 && o[1]==0,"Spontaneous candidate activity");}
        auto disconnected=q;disconnected.self=0;
        require(retain(spec,disconnected)<1,"Memory persists without self feedback");
        require(retain(spec,q)>1,"Selected candidate lost memory");
        require(compete(spec,q,0,false).pass(),"Selected candidate failed reversal");
    }
    std::cout<<"Research kernel checks passed\n";
}
int main(int argc,char** argv) try {
    const std::string mode=argc>1?argv[1]:"sweep";
    const std::filesystem::path out=argc>2?argv[2]:"runs/cheap-neurons";
    std::filesystem::create_directories(out);
    if(mode=="sweep")sweep(out);
    else if(mode=="profile")profile(out);
    else if(mode=="focused")focused(out);
    else if(mode=="validate")validate(out);
    else if(mode=="verify")verify();
    else throw std::invalid_argument("Unknown experiment mode");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
