#pragma once

#include "neuroevo/brain.hpp"
#include <array>
#include <cmath>
#include <vector>

// Controlled inputs use the real calibrated encoder: threshold=rate*dt makes
// each binary input event emit exactly one sensory spike, with one-step travel.
// Hidden dynamics, gains, dt and refractory time remain ecosystem defaults.
namespace autapse {
using neuroevo::Brain;
constexpr std::size_t a = 6, b = 7;
struct Parameters {
    int delay = 5, cross_delay = 1;
    double self = 2.0, cross = 0.0, drive_weight = 0.5, inhibition = 0.1;
    double bias = 0.0;
};
inline Brain make(const Parameters& p)
{
    neuroevo::BrainConfig c;
    c.input_count = 6; c.hidden_count = 2; c.output_count = 1;
    c.background_activity_enabled = false;
    std::vector<Brain::Neuron> n(9);
    n[b].position.x = (p.cross_delay-0.5)*c.conduction_speed*c.dt;
    for (int i : {1,3,5}) n[i].position = n[b].position;
    for (int i = 0; i < 6; ++i) n[i].threshold = c.sensory_rate_hz*c.dt;
    n[a].bias = n[b].bias = p.bias;
    std::vector<Brain::Synapse> edges{
        {0,a,2.0,1}, {1,b,2.0,1},
        {2,a,p.drive_weight,1}, {3,b,p.drive_weight,1},
        {4,a,-p.inhibition,1}, {5,b,-p.inhibition,1},
        {a,a,p.self,static_cast<std::size_t>(p.delay)},
        {b,b,p.self,static_cast<std::size_t>(p.delay)},
        {a,b,-p.cross,1}, {b,a,-p.cross,1}
    };
    return Brain::from_components(c,n,edges);
}
struct Result {
    double rate_a = 0, rate_b = 0;
    std::vector<int> spikes_a, spikes_b;
};
template<class Input> Result run(Brain brain, int steps, Input input)
{
    Result r;
    for (int t = 0; t < steps; ++t) {
        brain.step(input(t));
        if (brain.neurons()[a].spiked) {
            r.spikes_a.push_back(t);
            if (t >= steps/2) ++r.rate_a;
        }
        if (brain.neurons()[b].spiked) {
            r.spikes_b.push_back(t);
            if (t >= steps/2) ++r.rate_b;
        }
    }
    const double duration = (steps-steps/2)*brain.config().dt;
    r.rate_a /= duration; r.rate_b /= duration;
    return r;
}
inline std::vector<double> blank() { return std::vector<double>(6,0.0); }
inline bool event(double& phase, double rate) {
    phase += rate*0.02;
    if (phase >= 1.0-1e-12) { phase -= 1.0; return true; }
    return false;
}
}
