#include "neuroevo/ecosystem.hpp"
#include "../src/checkpoint_fields.hpp"
#include <iomanip>
#include <limits>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string state(const neuroevo::EcosystemWorld& w) { std::ostringstream s; w.save_checkpoint(s); return s.str(); }

void test_rng_runtime_mismatch()
{
    neuroevo::Random original(123), restored;
    for (int i = 0; i < 417; ++i) original.next_u64();
    std::ostringstream saved;
    original.save_state(saved);
    std::istringstream same_runtime(saved.str());
    restored.load_state(same_runtime);
    for (int i = 0; i < 1000; ++i)
        require(original.next_u64() == restored.next_u64(), "RNG roundtrip changed its sequence");

    std::istringstream tokens(saved.str());
    std::vector<std::string> words;
    std::string word;
    while (tokens >> word) words.push_back(word);
    if (words.size() == 313) words.pop_back();
    else if (words.size() == 312) words.push_back("0");
    else return; // Only the two historical Windows runtime formats differ here.
    std::ostringstream foreign;
    for (const auto& value : words) foreign << value << ' ';
    foreign << "\n12345\n";
    std::istringstream input(foreign.str());
    bool rejected = false;
    try { restored.load_state(input); }
    catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("different C++ random-generator format") != std::string::npos;
    }
    require(rejected, "Foreign RNG serialization must report a runtime mismatch");
    int next = 0;
    input >> next;
    require(next == 12345, "RNG parser consumed the following checkpoint field");
}

}
int main()
{
    try {
        test_rng_runtime_mismatch();
        neuroevo::EcosystemConfig config;
        config.initial_creatures=5;
        config.graze_energy=3.25;
        config.reproduction=false;
        config.mutation.remove_neuron_probability=0.017;
        config.mutation.copy_probability=0.2;
        config.mutation.slight_probability=0.3;
        config.mutation.slight.sigma_scale=0.61;
        config.mutation.strong.structural_probability=0.72;
        config.mutation.strong.local_edits=5;
        config.mutation.mass_mutation_sigma=0.27;
        config.mutation.carnivory_mutation_probability=0.8;
        config.mutation.disconnected_neuron_prune_probability=0.13;
        neuroevo::EcosystemWorld original(config);
        for (int i=0;i<35;++i) original.step();
        std::istringstream input(state(original));
        auto resumed=neuroevo::EcosystemWorld::load_checkpoint(input);
        require(resumed.config.mutation.remove_neuron_probability==0.017,"Checkpoint lost pruning rate");
        require(resumed.config.graze_energy==3.25,"Checkpoint lost food energy settings");
        require(state(original)==state(resumed),"Checkpoint roundtrip lost world, brain, or RNG state");
        for (int i=0;i<45;++i) { original.step(); resumed.step(); }
        require(state(original)==state(resumed),"Resumed spiking simulation diverged from uninterrupted execution");
        require(original.totals.spikes>0,"Checkpoint continuation did not exercise spiking activity");

        // Pending digestion, opening progress, weather offset, and transient events
        // must survive checkpoints in addition to visible positions and genomes.
        original.config.phase_offset=179.95;
        original.config.storms_enabled=false;
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
