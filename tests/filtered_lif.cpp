#include "neuroevo/ecosystem.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace neuroevo;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string saved(const Brain& b){std::ostringstream s;b.save_state(s);return s.str();}
Brain restored(const Brain& b){std::istringstream s(saved(b));return Brain::load_state(s);}
Brain single(double dt,double drive=0,double current=0,double self=0) {
    auto c=BrainConfig::filtered_lif(dt);c.input_count=c.hidden_count=0;c.output_count=1;
    c.background_activity_enabled=false;
    std::vector<Brain::Neuron> n(1);n[0].bias=drive/c.membrane_tau;n[0].synaptic_current=current/c.membrane_tau;
    std::vector<Brain::Synapse> edges;if(self)edges.push_back({0,0,self,static_cast<std::size_t>(std::lround(.06/dt))});
    return Brain::from_components(c,n,edges);
}
int main() try {
    EcosystemConfig high;high.brain.select_model(NeuronModel::FilteredLif);
    auto low=high;low.brain.synaptic_gain=8;
    const auto strong=make_sparse_ancestral_brain(high),weak=make_sparse_ancestral_brain(low);
    require(strong.synapses().size()==weak.synapses().size(),"Gain altered ancestor topology");
    for(std::size_t i=0;i<strong.synapses().size();++i)
        require(strong.synapses()[i].weight==weak.synapses()[i].weight,"Ancestor weights cancelled filtered gain change");
    for(double dt:{.005,.002}) {
        auto cfg=BrainConfig::filtered_lif(dt);
        require(cfg.max_delay_steps==static_cast<std::size_t>(std::lround(.160/dt)),"Physical maximum delay changed");
        auto quiet=single(dt);for(int i=0;i<100;++i)quiet.step({});
        require(quiet.neurons()[0].potential==0 && !quiet.neurons()[0].spiked,"Zero-input cell fired");
        // Independent fine-step Euler solution before any threshold crossing.
        auto b=single(dt,.1,.7);double v=0,s=.7;
        for(int i=0;i<5000;++i){v+=.00001*(-v+.1+s)/.05;s-=.00001*s/.1;}
        for(int i=0;i<std::lround(.05/dt);++i)b.step({});
        require(std::abs(b.neurons()[0].potential-v)<.0001,"Filtered voltage disagrees with fine Euler reference");
        require(std::abs(.05*b.neurons()[0].synaptic_current-s)<.0001,"Current decay disagrees with reference");
        // Self feedback retains an initiated burst; increasing drive raises rate.
        int previous=0;
        for(double drive:{0.,3.6}) {
            auto memory=single(dt,drive,8,1);int count=0;
            for(int t=0;t<std::lround(8/dt);++t){memory.step({});if(t>=std::lround(6/dt))count+=memory.neurons()[0].spiked;}
            std::cout<<dt<<" s memory drive "<<drive<<": "<<count/2.<<" Hz\n";
            require(count>previous,"Persistent firing failed or graded rate did not increase");previous=count;
        }
        // Incoming inhibition during refractory must be stored, then integrated.
        auto c=cfg;c.input_count=1;c.hidden_count=0;c.output_count=1;c.sensory_rate_hz=1/dt;
        std::vector<Brain::Neuron> n(2);n[1].potential=1.2;
        auto inhibited=Brain::from_components(c,n,{{0,1,-.5,1}});
        inhibited.step({1});require(inhibited.neurons()[1].spiked,"Initialization spike missing");
        inhibited.step({0});require(inhibited.neurons()[1].synaptic_current<0 && inhibited.neurons()[1].potential==0,"Refractory arrival was discarded");
        auto copy=restored(inhibited);require(saved(copy)==saved(inhibited),"Filtered checkpoint lost current or refractory state");
        for(int i=0;i<100;++i){inhibited.step({.2});copy.step({.2});}
        require(inhibited.neurons()[1].potential<0,"Refractory inhibition never affected voltage");
        require(saved(copy)==saved(inhibited),"Filtered checkpoint continuation diverged");
        copy.reset_state();require(copy.neurons()[1].synaptic_current==0 && copy.neurons()[1].potential==0,"Reset retained synaptic memory");
        // Pair driven through actual phase-encoded sensory events: equal weights,
        // 60 vs 20 Hz input streams, reversed at four seconds.
        c=cfg;c.input_count=4;c.hidden_count=2;c.output_count=0;c.sensory_rate_hz=1/dt;c.background_activity_enabled=false;
        n.assign(6,{});n[5].position.x=(.06/dt-.5)*c.conduction_speed*dt;
        n[1].position=n[3].position=n[5].position;
        auto pair=Brain::from_components(c,n,{{0,4,.6,1},{1,5,.6,1},{2,4,8,1},{3,5,8,1},
            {4,4,1,static_cast<std::size_t>(std::lround(.06/dt))},{5,5,1,static_cast<std::size_t>(std::lround(.06/dt))},
            {4,5,-.45,1},{5,4,-.45,1}});
        int a1=0,b1=0,a2=0,b2=0;
        for(int t=0;t<std::lround(8/dt);++t) {
            bool reverse=t>=std::lround(4/dt);
            pair.step({(reverse?20:60)*dt,(reverse?60:20)*dt,t==0?1.:0.,t==0?1.:0.});
            if(t>=std::lround(2/dt) && !reverse){a1+=pair.neurons()[4].spiked;b1+=pair.neurons()[5].spiked;}
            if(t>=std::lround(6/dt)){a2+=pair.neurons()[4].spiked;b2+=pair.neurons()[5].spiked;}
        }
        std::cout<<dt<<" s WTA rates: "<<a1/2.<<'/'<<b1/2.<<" -> "<<a2/2.<<'/'<<b2/2.<<" Hz\n";
        require(a1>0 && b2>0 && a1>=3*b1 && b2>=3*a2,"Filtered sensory-driven WTA failed reversal");
        // Keep pending recurrent events and nonzero currents across a checkpoint.
        auto resumed=restored(pair);for(int i=0;i<103;++i){pair.step({.1,.2,0,0});resumed.step({.1,.2,0,0});}
        require(saved(pair)==saved(resumed),"Recurrent checkpoint lost delayed activity");
        MutationConfig m;Random rng(283);auto evolving=Brain::random(cfg,rng);
        for(int i=0;i<100;++i)evolving.mutate(m,rng);
        require(evolving.config().synaptic_tau==.1 && evolving.config().membrane_tau==.05,"Mutation changed fixed time constants");
    }
    // Stable equal-time-constant limit, checked against analytic t*exp(-t/tau).
    auto c=BrainConfig::filtered_lif();c.synaptic_tau=c.membrane_tau;c.input_count=c.hidden_count=0;c.output_count=1;
    std::vector<Brain::Neuron> n(1);n[0].synaptic_current=10;
    auto equal=Brain::from_components(c,n,{});equal.step({});
    require(std::abs(equal.neurons()[0].potential-10*c.dt*std::exp(-c.dt/c.membrane_tau))<1e-12,"Equal tau integration is singular");
    for(double bad:{0.,.02,.003}) {
        bool rejected=false;try{auto invalid=BrainConfig::filtered_lif(bad);(void)invalid;}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Invalid filtered timestep accepted");
    }
    std::cout<<"Filtered LIF checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
