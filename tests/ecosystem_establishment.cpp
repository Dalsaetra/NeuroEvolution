#include "neuroevo/ecosystem.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace neuroevo;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string state(const EcosystemWorld& w) { std::ostringstream s; w.save_checkpoint(s); return s.str(); }
std::string brain_state(const Brain& b) { std::ostringstream s; b.save_state(s); return s.str(); }
EcosystemConfig config()
{
    EcosystemConfig c;
    c.width=c.height=12;
    c.initial_creatures=1;
    c.shelters=c.grazing_patches=c.fruit_patches=c.pods=0;
    c.establishment=true;
    c.archive_eval_trials=0; // These tests isolate observed-score and immigration policies.
    c.reproduction=false;
    c.basal_cost=c.movement_cost=c.turn_cost=c.forage_cost=c.call_cost=0;
    c.neuron_cost=c.synapse_cost=c.spike_cost=c.storm_cost=0;
    c.archive_min_age=10;
    c.archive_min_energy=5;
    c.archive_min_feeding_bouts=2;
    c.archive_min_efficiency=0.5;
    return c;
}
EcoCreature success(EcosystemWorld& w, FoodKind niche=FoodKind::Graze, double gain=20)
{
    EcoCreature c;
    c.id=w.next_creature_id++;
    c.genome_id=c.id;
    c.brain=w.creatures.front().brain;
    c.age=20; c.energy_gained=gain; c.energy_spent=10; c.feeding_bouts=2;
    c.eaten[static_cast<std::size_t>(niche)]=gain;
    return c;
}
void archive_evidence_and_diversity()
{
    auto cfg=config(); cfg.archive_capacity=4;
    EcosystemWorld w(cfg);
    auto c=success(w);
    c.energy_gained=0; c.age=10000;
    w.consider_archive(c,false);
    require(w.archive.empty(),"Living a long time without food must not qualify");
    c.energy_gained=20; c.age=1;
    w.consider_archive(c,false);
    require(w.archive.empty(),"Food before minimum trial age qualified");
    c.age=20; c.feeding_bouts=1;
    w.consider_archive(c,false);
    require(w.archive.empty(),"A single incidental feeding bout qualified");
    c.feeding_bouts=2; c.energy_spent=100;
    w.consider_archive(c,false);
    require(w.archive.empty(),"Grossly inefficient feeding qualified");
    c.energy_spent=10;
    c.age=20; c.controller=ControllerKind::Reactive;
    w.consider_archive(c,true);
    require(w.archive.empty(),"Scripted controller contaminated genome archive");
    c.controller=ControllerKind::Spiking;
    w.consider_archive(c,false);
    const double initial_score=w.archive.front().score;
    c.age=1000;
    w.consider_archive(c,false);
    require(w.archive.front().trials.size()==1 && w.archive.front().score==initial_score,
        "Repeated sampling or lifespan alone inflated archive score");
    c.energy_spent=40;
    w.consider_archive(c,false);
    require(w.archive.front().score<initial_score,"Operating costs did not penalize inefficient feeding");
    const double before_failure=w.archive.front().score;
    c.id=w.next_creature_id++; c.energy_gained=0; c.feeding_bouts=0;
    w.consider_archive(c,true);
    require(w.archive.front().trials.size()==2 && w.archive.front().score<before_failure,
        "A failed clone lifetime did not reduce its genome's score");
    for (int i=0;i<20;++i) { c.id=w.next_creature_id++; w.consider_archive(c,true); }
    require(w.archive.front().trials.size()==8,"Per-genome evidence grew without bound");
    for (int i=0;i<8;++i) w.consider_archive(success(w,FoodKind::Graze,30+i),true);
    for (auto niche:{FoodKind::FruitA,FoodKind::FruitB,FoodKind::Pod}) w.consider_archive(success(w,niche,5),true);
    std::array<int,4> niches{};
    for (const auto& e:w.archive) ++niches[static_cast<std::size_t>(e.niche)];
    require(w.archive.size()==4 && std::all_of(niches.begin(),niches.end(),[](int n){return n==1;}),
        "One diet displaced reserved slots for other successful diets");
}
void split_placement_and_accounting()
{
    auto cfg=config(); cfg.immigration_floor=5; cfg.immigration_batch=5;
    cfg.mutation.mutate_weight_probability=1;
    EcosystemWorld w(cfg);
    w.consider_archive(success(w),true);
    const auto genome=brain_state(w.archive.front().genome);
    const auto genome_id=w.archive.front().genome_id;
    const auto tiles=w.terrain;
    std::ostringstream map_before,mutation_before;
    w.map_rng.save_state(map_before); w.mutation_rng.save_state(mutation_before);
    w.creatures.clear();
    w.update_establishment();
    require(w.creatures.empty(),"Immigration ignored its interval");
    w.step_index=50;
    w.update_establishment();
    require(w.creatures.size()==5 && w.totals.immigrants==5,"Population floor was not filled");
    require(w.totals.immigrant_mutations==3 && w.totals.immigrant_slight_mutations==2
        && w.totals.immigrant_strong_mutations==1 && w.totals.immigrant_clones==2 && w.totals.immigrant_random==0,
        "Complete immigration deck did not deliver 40/40/20 split");
    require(w.totals.births==0 && w.totals.immigrant_energy==5*cfg.founder_energy && w.totals.energy_gained==0,
        "External arrivals were counted as births or food energy");
    for (const auto& c:w.creatures) {
        require(c.parent_id==0 && c.age==0 && c.offspring==0 && c.controller==ControllerKind::Spiking,
            "Immigrant did not start an independent spiking lifetime");
        auto reset=c.brain; reset.reset_state();
        require(brain_state(c.brain)==brain_state(reset),"Immigrant inherited neural activity");
        if (c.origin==CreatureOrigin::ArchiveClone)
            require(c.genome_id==genome_id && brain_state(c.brain)==genome,"Archive clone changed genotype");
        if (c.origin==CreatureOrigin::ArchiveMutation)
            require(c.genome_id==c.id && brain_state(c.brain)!=genome,"Archive mutant retained source genotype identity");
        if (c.origin==CreatureOrigin::ArchiveStrongMutation)
            require(c.genome_id==c.id && brain_state(c.brain)!=genome,"Strong archive mutant retained source genotype identity");
        require(w.traversable(c.position),"Immigrant placed in blocked terrain");
        for (const auto& other:w.creatures) if(c.id!=other.id)
            require(length(c.position-other.position)>=2*cfg.radius,"Immigrant bodies overlap");
    }
    w.step_index=100; w.update_establishment();
    require(w.totals.immigrants==5,"Immigration continued above its floor");
    std::ostringstream map_after,mutation_after;
    w.map_rng.save_state(map_after); w.mutation_rng.save_state(mutation_after);
    require(w.terrain==tiles && map_before.str()==map_after.str() && mutation_before.str()==mutation_after.str(),
        "Immigration altered terrain or unrelated random streams");
    w.creatures.clear(); w.archive.clear(); w.step_index=150; w.update_establishment();
    require(w.creatures.empty() && w.totals.immigrants==5 && w.totals.immigrant_random==0
        && w.totals.archive_empty_checks==1,
        "Empty archive introduced an unrelated random genome");
}
void archive_tournament_selection()
{
    auto cfg=config();
    cfg.immigration_floor=1;
    cfg.immigration_batch=1;
    cfg.archive_tournament_size=3;
    EcosystemWorld w(cfg);
    for (double gain : {10.0,20.0,30.0}) w.consider_archive(success(w,FoodKind::Graze,gain),true);
    require(w.archive.size()==3,"Tournament fixture did not create three archive candidates");
    const auto best=std::max_element(w.archive.begin(),w.archive.end(),[](const auto& a,const auto& b){return a.score<b.score;});
    const auto expected_source=best->source_id;
    w.creatures.clear();
    w.step_index=50;
    w.update_establishment();
    require(w.creatures.size()==1 && w.creatures.front().source_id==expected_source,
        "Full archive tournament did not select the highest-scoring candidate");
}
void death_capture_and_resume()
{
    auto cfg=config(); cfg.immigration_floor=5; cfg.immigration_interval=0.3;
    cfg.basal_cost=100;
    EcosystemWorld w(cfg);
    w.creatures.front().energy_gained=20; w.creatures.front().age=20;
    w.creatures.front().feeding_bouts=2;
    w.creatures.front().eaten[0]=10;
    for(int i=0;i<3;++i) w.step();
    require(w.archive.size()==1 && w.immigration_deck_cursor==2,"Fixture did not exercise partially drawn archive deck");
    std::istringstream input(state(w)); auto resumed=EcosystemWorld::load_checkpoint(input);
    require(state(w)==state(resumed),"Checkpoint lost archive, evidence, or policy state");
    for(int i=0;i<100;++i) { w.step(); resumed.step(); }
    require(state(w)==state(resumed),"Immigration continuation diverged from saved RNG/deck state");
    require(w.totals.immigrants>5 && w.totals.deaths>0,"Continuation did not exercise repeated immigration");

    EcosystemWorld dead(cfg);
    dead.creatures.front().energy=0.01; dead.creatures.front().age=20;
    dead.creatures.front().energy_gained=20;
    dead.creatures.front().feeding_bouts=2;
    dead.step({EcoAction{}});
    require(dead.creatures.empty() && dead.archive.size()==1 && dead.archive.front().trials.front().finished,
        "Death lost a partial success before archiving it");
    require(dead.archive.front().trials.front().observed_at==dead.time(),"Death trial has the wrong observation time");
    for(int i=0;i<2;++i) dead.step();
    require(dead.creatures.size()==2,"Empty world did not recover on the next immigration check");
}
void taper_and_withdrawal()
{
    auto cfg=config(); cfg.immigration_interval=0.2;
    cfg.calm_duration=0.2; cfg.warning_duration=cfg.storm_duration=0.1;
    EcosystemWorld w(cfg);
    w.totals.births=1;
    for(int i=0;i<30;++i) w.step({EcoAction{}});
    require(w.immigration_enabled(),"The first birth prematurely withdrew support");
    w.totals.natural_spiking_breeders=1;
    w.next_immigration_check=w.time(); w.update_establishment();
    require(std::abs(w.next_immigration_check-w.time()-0.4)<1e-9 && w.immigration_enabled(),
        "First natural spiking breeder should taper support without withdrawing it");
    w.totals.natural_spiking_breeders=2;
    w.last_immigration_time=w.time();
    for(int i=0;i<11;++i) w.step({EcoAction{}});
    require(w.immigration_enabled(),"Support ended before three full environmental cycles");
    for(int i=0;i<2;++i) w.step({EcoAction{}});
    require(w.immigration_withdrawn && !w.immigration_enabled(),"Sustained breeding population did not graduate");
    w.creatures.clear(); w.step_index+=100; w.update_establishment();
    require(w.creatures.empty(),"Support silently restarted after withdrawal");

    cfg.immigration_auto_stop=false;
    EcosystemWorld persistent(cfg);
    persistent.totals.natural_spiking_breeders=10;
    for(int i=0;i<40;++i) persistent.step({EcoAction{}});
    require(persistent.immigration_enabled(),"Keep-immigration setting was ignored");
}
void natural_breeding_only()
{
    auto cfg=config(); cfg.reproduction=true; cfg.maturity_age=0;
    EcosystemWorld w(cfg);
    w.creatures.front().energy=190;
    w.step({EcoAction{}});
    require(w.totals.births==1 && w.totals.founder_births==1 && w.totals.births_first_100s==1
        && w.totals.natural_spiking_breeders==0,"Founder reproduction was classified incorrectly");
    auto child=std::find_if(w.creatures.begin(),w.creatures.end(),[](const auto& c){return c.parent_id!=0;});
    require(child!=w.creatures.end() && child->origin==CreatureOrigin::Birth,"Natural child origin was not recorded");
    child->energy=190;
    w.step(std::vector<EcoAction>(w.creatures.size()));
    require(w.totals.natural_spiking_breeders==1 && w.totals.mature_offspring==1
        && w.totals.descendant_births==1,"Natural spiking breeder milestone missing");
    w.step(std::vector<EcoAction>(w.creatures.size()));
    require(w.totals.natural_spiking_breeders==1,"Natural breeder counted repeatedly");
}
}
int main()
{
    try {
        archive_evidence_and_diversity(); split_placement_and_accounting(); archive_tournament_selection(); death_capture_and_resume();
        taper_and_withdrawal(); natural_breeding_only();
        std::cout << "Archive establishment, immigration, and continuation verified\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
