#include "autapse_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace autapse;
int main(int argc, char** argv) try {
    const std::filesystem::path out = argc > 1 ? argv[1] : "runs/autapse";
    std::filesystem::create_directories(out);
    std::ofstream sustain(out/"sustain.csv"), perturb(out/"inhibition.csv"),
        rates(out/"rates.csv"), pairs(out/"competition.csv"), traces(out/"traces.csv");
    if (!sustain || !perturb || !rates || !pairs || !traces)
        throw std::runtime_error("Cannot open experiment outputs");
    sustain << "delay_steps,self_weight,seeded,rate_hz\n";
    for (int d=1; d<=8; ++d) for (double w : {0.0,1.5,1.5625,1.6,1.8,2.0,2.5})
        for (bool seed : {false,true}) {
            Parameters p; p.delay=d; p.self=w;
            auto r=run(make(p),2000,[&](int t) { auto x=blank(); x[0]=seed && t==0; return x; });
            sustain << d << ',' << w << ',' << seed << ',' << r.rate_a << '\n';
        }
    perturb << "delay_steps,self_weight,inhibitory_weight,phase,mode,rate_hz\n";
    for (int d=3; d<=8; ++d) for (double w : {1.6,1.8,2.0,2.5})
        for (double h : {0.01,0.05,0.1,0.2,0.4,0.6,1.0}) for (int phase=0; phase<d; ++phase)
            for (int mode=0; mode<3; ++mode) {
                Parameters p; p.delay=d; p.self=w; p.inhibition=h;
                // First spike arrives at 1; perturbations arrive at 1+100*d+phase.
                const int start=100*d+phase;
                auto r=run(make(p),3000,[&](int t) {
                    auto x=blank(); x[0]=t==0;
                    x[4]=t>=start && (mode==2 || (mode==0 ? t==start : (t-start)%d==0));
                    return x;
                });
                perturb << d << ',' << w << ',' << h << ',' << phase << ',' << mode << ',' << r.rate_a << '\n';
            }
    rates << "delay_steps,self_weight,drive_weight,input_rate_hz,phase,bias,output_rate_hz,after_withdrawal_hz\n";
    for (int d : {3,5,8}) for (double w : {0.0,1.2,1.6,2.0,2.5})
        for (double e : {0.25,0.5,1.0}) for (int hz=0; hz<=50; hz+=2)
            for (double phase : {0.0,0.25,0.5,0.75}) {
                Parameters p; p.delay=d; p.self=w; p.drive_weight=e;
                double clock=phase;
                auto r=run(make(p),4000,[&](int t) {
                    auto x=blank(); x[0]=t==0; x[2]=t<2000 && event(clock,hz); return x;
                });
                int driven=0, withdrawn=0;
                for (int t:r.spikes_a) {
                    if (t>=1000 && t<2000) ++driven;
                    if (t>=3000) ++withdrawn;
                }
                rates << d << ',' << w << ',' << e << ',' << hz << ',' << phase << ",0," << driven/20.0 << ',' << withdrawn/20.0 << '\n';
            }
    pairs << "delay_steps,self_weight,cross_weight,cross_delay,weak_input_hz,strong_input_hz,start_offset,input_phase,rate_strong,rate_weak\n";
    int cases=0, wins=0, reversed=0, ties=0;
    for (int d : {3,5,8}) for (double w : {1.6,2.0,2.5})
        for (double h : {0.0,0.25,0.75,1.5,3.0}) for (int cross_delay : {1,3,5,8})
            for (double weak : {0.0,5.0,10.0}) for (double difference : {2.0,5.0,10.0})
                for (int offset=0; offset<d; ++offset) for (double phase : {0.0,0.25,0.5,0.75}) {
                    Parameters p; p.delay=d; p.self=w; p.cross=h; p.cross_delay=cross_delay;
                    double ca=0, cb=phase;
                    auto r=run(make(p),2000,[&](int t) {
                        auto x=blank(); x[0]=t==0; x[1]=t==offset;
                        x[2]=event(ca,weak+difference); x[3]=event(cb,weak); return x;
                    });
                    pairs << d << ',' << w << ',' << h << ',' << cross_delay << ',' << weak << ',' << weak+difference
                        << ',' << offset << ',' << phase << ',' << r.rate_a << ',' << r.rate_b << '\n';
                    ++cases;
                    if (r.rate_a > r.rate_b+0.1) ++wins;
                    else if (r.rate_b > r.rate_a+0.1) ++reversed;
                    else ++ties;
                }
    traces << "scenario,neuron,spike_step,time_seconds\n";
    for (int scenario=0; scenario<4; ++scenario) {
        Parameters p; p.cross=scenario>=2 ? 0.75 : 0; p.inhibition=0.6;
        double ca=0,cb=0;
        auto r=run(make(p),1000,[&](int t) {
            auto x=blank(); x[0]=t==0; x[1]=scenario>=2 && t==0;
            x[2]=event(ca,scenario==1 ? 40 : scenario>=2 ? (scenario==3 && t>=500 ? 5 : 15) : 0);
            x[3]=event(cb,scenario>=2 ? (scenario==3 && t>=500 ? 15 : 5) : 0);
            x[4]=scenario==0 && t==500;
            return x;
        });
        for (int t:r.spikes_a) traces << scenario << ",A," << t << ',' << t*0.02 << '\n';
        for (int t:r.spikes_b) traces << scenario << ",B," << t << ',' << t*0.02 << '\n';
    }
    std::cout << "Competition cases=" << cases << " stronger_faster=" << wins
        << " weaker_faster=" << reversed << " ties=" << ties << '\n';
    std::cout << "Wrote " << out.string() << '\n';
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
