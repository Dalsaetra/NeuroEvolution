#include "neuroevo/ecosystem.hpp"
#include <cmath>
#include <iostream>
#include <vector>
using namespace neuroevo;
int main() {
    std::cout<<"model,dt,gain,threshold_scale,noise,input,hidden_hz,motor_hz\n";
    EcosystemConfig eco;const auto ancestor=make_sparse_ancestral_brain(eco);
    for(bool filtered:{false,true})for(double gain:filtered?std::vector<double>{20,12.8,10,8,6.4}:std::vector<double>{32})
        for(double threshold_scale:filtered && gain==20?std::vector<double>{1,2,2.5}:std::vector<double>{1})
        for(bool noise:{false,true})for(double level:{.1,.3,.6}) {
            auto c=filtered?BrainConfig::filtered_lif():BrainConfig{};
            c.hidden_count=ancestor.config().hidden_count;c.synaptic_gain=gain;c.background_activity_enabled=noise;
            auto neurons=ancestor.neurons();
            for(std::size_t i=c.input_count;i<neurons.size();++i)neurons[i].threshold*=threshold_scale;
            auto b=Brain::from_components(c,neurons,ancestor.synapses());
            Random rng(731);
            std::vector<double> input(c.input_count,level);double hidden=0,motor=0;
            for(int t=0;t<std::lround(12/c.dt);++t) {
                b.step(input,&rng);
                if(t>=std::lround(2/c.dt))for(std::size_t i=c.input_count;i<b.neurons().size();++i)
                    if(b.neurons()[i].spiked){if(i<c.input_count+c.hidden_count)++hidden;else ++motor;}
            }
            std::cout<<(filtered?"filtered-lif":"lif")<<','<<c.dt<<','<<gain<<','<<threshold_scale<<','<<noise<<','<<level<<','
                <<hidden/(10*c.hidden_count)<<','<<motor/(10*c.output_count)<<'\n';
        }
}
