#include "neuroevo/ecosystem.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string state(const neuroevo::EcosystemWorld& w) { std::ostringstream s; w.save_checkpoint(s); return s.str(); }
}
int main()
{
    try {
        neuroevo::EcosystemConfig config;
        config.initial_creatures=5;
        config.reproduction=false;
        neuroevo::EcosystemWorld original(config);
        for (int i=0;i<35;++i) original.step();
        std::istringstream input(state(original));
        auto resumed=neuroevo::EcosystemWorld::load_checkpoint(input);
        require(state(original)==state(resumed),"Checkpoint roundtrip lost world, brain, or RNG state");
        for (int i=0;i<45;++i) { original.step(); resumed.step(); }
        require(state(original)==state(resumed),"Resumed spiking simulation diverged from uninterrupted execution");
        require(original.totals.spikes>0,"Checkpoint continuation did not exercise spiking activity");

        // Pending digestion, opening progress, weather offset, and transient events
        // must survive checkpoints in addition to visible positions and genomes.
        original.config.phase_offset=179.95;
        original.config.reproduction=true;
        original.creatures.front().digestion.push_back({original.time()+0.25,3.75,neuroevo::FoodKind::FruitB});
        original.creatures.front().age=200;
        original.creatures.front().energy=180;
        original.resources.back().progress=2.75;
        original.events.push_back({original.time(),"test_event",1,2,3,4.5});
        std::istringstream input2(state(original));
        resumed=neuroevo::EcosystemWorld::load_checkpoint(input2);
        for (int i=0;i<8;++i) { original.step(); resumed.step(); }
        require(state(original)==state(resumed),"Checkpoint lost digestion, birth, mutation, or weather state");
        require(original.totals.births>0,"Continuation test failed to exercise offspring mutation");

        auto bad=state(original);
        bad.resize(bad.size()/2);
        bool rejected=false;
        try { std::istringstream broken(bad); neuroevo::EcosystemWorld::load_checkpoint(broken); }
        catch (const std::exception&) { rejected=true; }
        require(rejected,"Truncated checkpoint must be rejected");
        std::istringstream invalid("NEUROEVO_ECOSYSTEM_99");
        rejected=false;
        try { neuroevo::EcosystemWorld::load_checkpoint(invalid); }
        catch (const std::exception&) { rejected=true; }
        require(rejected,"Unknown checkpoint version must be rejected");
        std::cout << "Ecosystem checkpoints preserve exact continuation\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
