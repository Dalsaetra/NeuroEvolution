#include "../tools/autapse_fixture.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace autapse;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() try {
    for (int d=1; d<=8; ++d) {
        Parameters p; p.delay=d;
        auto r=run(make(p),2000,[](int t) { auto x=blank(); x[0]=t==0; return x; });
        require(d<=2 ? r.rate_a==0 : std::abs(r.rate_a-50.0/d)<0.051,
            "Feedback must arrive after refractory; surviving single seed repeats at its delay");
        auto quiet=run(make(p),500,[](int) { return blank(); });
        require(quiet.spikes_a.empty(),"Feedback must not start without a seed");
    }
    // Exhaust all binary inhibitory arrival patterns within one feedback cycle.
    // h=0.1 is below the analytic continuous-input bound even at delay 8.
    for (int d=3; d<=8; ++d) for (int mask=0; mask<(1<<d); ++mask) {
        Parameters p; p.delay=d; p.inhibition=0.1;
        auto r=run(make(p),2000,[&](int t) {
            auto x=blank(); x[0]=t==0;
            x[4]=t>=10*d && ((mask >> (t%d)) & 1); return x;
        });
        require(std::abs(r.rate_a-50.0/d)<0.051,"Bounded inhibition destroyed the memory rhythm");
    }
    Parameters p; p.inhibition=0.6;
    auto erased=run(make(p),1000,[](int t) { auto x=blank(); x[0]=t==0; x[4]=t==500; return x; });
    require(erased.rate_a==0,"Supra-margin inhibition at feedback arrival should erase activity");
    auto refractory=run(make(p),1000,[](int t) { auto x=blank(); x[0]=t==0; x[4]=t==501; return x; });
    require(refractory.rate_a==10,"Inhibition during refractory should be discarded");
    auto original=make(p);
    for (int t=0;t<18;++t) { auto x=blank(); x[0]=t==0; original.step(x); }
    std::stringstream state; original.save_state(state); auto loaded=Brain::load_state(state);
    for (int t=0;t<300;++t) {
        original.step(blank()); loaded.step(blank());
        require(original.neurons()[a].spiked==loaded.neurons()[a].spiked
            && original.neurons()[a].potential==loaded.neurons()[a].potential,
            "Checkpoint must preserve self delay and in-flight feedback");
    }
    for (int invalid : {0,9}) {
        p.delay=invalid; bool threw=false;
        try { make(p); } catch (const std::invalid_argument&) { threw=true; }
        require(threw,"Invalid explicit self delay must be rejected");
    }
    // A longer delay can carry two circulating spikes, preserving two rates.
    for (double hz : {0.0,20.0}) {
        Parameters multi; multi.delay=8; multi.drive_weight=1.0;
        double clock=0;
        auto r=run(make(multi),4000,[&](int t) {
            auto x=blank(); x[0]=t==0; x[2]=t<1000 && event(clock,hz); return x;
        });
        require(std::abs(r.rate_a-(hz==0 ? 6.25 : 12.5))<0.051,
            "Long feedback loop should retain one or two circulating spikes after input withdrawal");
    }
    // Equal inhibition need not select the stronger drive: travel timing matters.
    for (int delay : {1,3}) {
        Parameters pair; pair.cross=0.75; pair.cross_delay=delay;
        double ca=0,cb=0;
        auto r=run(make(pair),2000,[&](int t) {
            auto x=blank(); x[0]=x[1]=t==0;
            x[2]=event(ca,15); x[3]=event(cb,5); return x;
        });
        require(r.rate_a==10 && r.rate_b==(delay==1 ? 10 : 0),
            "Synchronous refractory blind spot and delayed competitive selection changed");
    }
    std::cout << "Autapse memory, bounded inhibition, erasure, rates, competition and checkpoint checks passed\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
