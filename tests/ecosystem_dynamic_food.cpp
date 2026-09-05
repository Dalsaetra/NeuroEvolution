#include "neuroevo/ecosystem.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace neuroevo;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string save(const EcosystemWorld& w){std::ostringstream s;w.save_checkpoint(s);return s.str();}
int main(){try{
    for(bool frontier:{false,true}) {
        auto cfg=frontier?nursery_frontier_config():EcosystemConfig{};
        cfg.initial_creatures=0;cfg.reproduction=false;
        EcosystemWorld w(cfg);
        std::array<std::size_t,4> counts{};std::size_t shelter_count=0;
        for(const auto& r:w.resources) {
            if(r.shelter_food){++shelter_count;check(w.sheltered(r.position)&&!w.in_nursery(r.position),"Shelter food geography");}
            else if(!w.in_nursery(r.position))++counts[static_cast<std::size_t>(r.kind)];
        }
        check(shelter_count==cfg.shelters,"One patch per shelter");
        check(counts[0]==cfg.grazing_patches&&counts[1]+counts[2]==cfg.fruit_patches&&counts[3]==cfg.pods,"Outdoor quotas");
        const auto initial=w.resources;
        w.step();
        double expected_decay=0;
        for(std::size_t i=0;i<w.resources.size();++i) {
            const auto& r=w.resources[i];const auto& old=initial[i];
            if(r.shelter_food||w.in_nursery(r.position)||r.kind==FoodKind::Pod) continue;
            const double decay=(r.kind==FoodKind::Graze?cfg.graze_decay:cfg.fruit_decay)*cfg.dt;
            check(std::abs(old.stock-r.stock-decay)<1e-9,"Slow decay without passive regrowth");
            expected_decay+=decay;
        }
        check(std::abs(expected_decay-w.totals.spoiled_biomass)<1e-8,"Decay accounting");
        for(auto& r:w.resources)if(!r.shelter_food&&!w.in_nursery(r.position)&&r.kind!=FoodKind::Pod)r.stock=0.00001;
        std::istringstream input(save(w));auto resumed=EcosystemWorld::load_checkpoint(input);
        w.step();resumed.step();check(save(w)==save(resumed),"Relocation resume determinism");
        check(w.resources.size()==initial.size(),"Replacement count");
        for(std::size_t i=0;i<w.resources.size();++i) {
            const auto& r=w.resources[i];const auto& old=initial[i];
            check(r.id==old.id&&r.kind==old.kind,"Stable patch type and ID");
            if(r.shelter_food||w.in_nursery(r.position)||r.kind==FoodKind::Pod)continue;
            check(length(r.position-old.position)>=1&&r.stock==r.capacity,"Decay triggers full replacement elsewhere");
            check(w.traversable(r.position)&&!w.sheltered(r.position)&&!w.in_nursery(r.position),"Outdoor replacement geography");
            for(const auto& other:w.resources)if(other.id!=r.id)check(length(r.position-other.position)>=.8,"Overlapping replacement");
        }
        auto outdoor=*std::find_if(w.resources.begin(),w.resources.end(),[&](const auto& r){return !r.shelter_food&&!w.in_nursery(r.position)&&r.kind==FoodKind::Graze;});
        w.resources={outdoor};w.resources[0].stock=.01;
        EcoCreature c;c.id=1;c.position=outdoor.position;c.energy=90;c.brain=make_sparse_ancestral_brain(cfg);
        w.creatures={c};w.next_creature_id=2;EcoAction feed;feed.forage=1;
        w.step({feed});check(length(w.resources[0].position-outdoor.position)>=1,"Eating triggers replacement");
        auto shelter=*std::find_if(initial.begin(),initial.end(),[](const auto& r){return r.shelter_food;});
        w.resources={shelter};w.resources[0].stock=0;w.creatures[0].position=shelter.position;
        w.config.phase_offset=cfg.calm_duration+cfg.warning_duration+1;
        w.step({{}});check(w.resources[0].stock>0,"Shelter regrowth during storms");
        w.config.calm_duration=1;w.config.warning_duration=1;w.config.storm_duration=1000;w.config.phase_offset=3;
        const double energy=w.creatures[0].energy;
        for(int i=0;i<2000;++i){check(!w.creatures.empty(),"Shelter feeder starved");w.step({feed});}
        check(w.creatures[0].energy>=energy,"Shelter supply fails to cover basal plus feeding costs");
        check(length(w.resources[0].position-shelter.position)==0,"Shelter food moved");
    }
    auto bad=EcosystemConfig{};bad.graze_decay=bad.ingestion_rate;
    bool rejected=false;try{bad.validate();}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Decay must be slower than eating");
    std::cout<<"Dynamic food passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
