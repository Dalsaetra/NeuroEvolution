#include "fixtures.hpp"
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
        auto cfg=frontier?EcosystemConfig{}:controlled_config();
        cfg.initial_creatures=0;cfg.reproduction=false;
        EcosystemWorld w(cfg);
        std::array<std::size_t,4> counts{};std::size_t shelter_count=0;
        for(const auto& r:w.resources) {
            if(r.shelter_food){++shelter_count;check(w.sheltered(r.position)&&!w.in_nursery(r.position),"Shelter food geography");}
            else if(!w.in_nursery(r.position))++counts[static_cast<std::size_t>(r.kind)];
        }
        check(shelter_count==(frontier?0:cfg.shelters),"One patch per shelter");
        check(counts[0]==cfg.grazing_patches&&counts[1]+counts[2]==cfg.fruit_patches&&counts[3]==cfg.pods,"Outdoor quotas");
        EcosystemWorld repeat(cfg);
        double lowest=1,highest=0;
        for(std::size_t i=0;i<w.resources.size();++i) {
            const auto& r=w.resources[i];
            check(r.stock==repeat.resources[i].stock,"Initial food ages must be seed reproducible");
            if(r.shelter_food||r.kind==FoodKind::Pod) {
                check(r.stock==r.capacity,"Only dynamic outdoor forage and fruit start decayed");
            } else {
                check(r.stock>=0&&r.stock<r.capacity,"Outdoor food must start partially decayed");
                lowest=std::min(lowest,r.stock/r.capacity);highest=std::max(highest,r.stock/r.capacity);
            }
        }
        check(lowest<.25&&highest>.75,"Initial decay ages should span the lifetime");
        auto static_cfg=cfg;static_cfg.outdoor_food_relocates=false;static_cfg.nursery_food_decay=0;
        EcosystemWorld static_world(static_cfg);
        for(std::size_t i=0;i<w.resources.size();++i) {
            check(static_world.resources[i].stock==static_world.resources[i].capacity,"Static food starts full");
            check(length(static_world.resources[i].position-w.resources[i].position)==0,"Food aging must not change geography");
        }
        if(frontier) {
            std::size_t indoor_graze=0;
            for(const auto& r:w.resources)if(w.sheltered(r.position)&&!w.in_nursery(r.position)) {
                check(r.kind==FoodKind::Graze&&!r.shelter_food,"Restricted food spawned inside shelter");
                check(r.energy_per_unit==cfg.graze_energy&&r.capacity==cfg.graze_capacity,"Shelter graze uses different nutrition");
                ++indoor_graze;
            }
            check(indoor_graze>0,"Ordinary grazing did not spawn inside shelters");
        }
        const auto initial=w.resources;
        w.step();
        double expected_decay=0;
        for(std::size_t i=0;i<w.resources.size();++i) {
            const auto& r=w.resources[i];const auto& old=initial[i];
            if(r.kind==FoodKind::Pod) continue;
            const double decay=(r.shelter_food?cfg.shelter_food_decay:w.in_nursery(r.position)?cfg.nursery_food_decay:r.kind==FoodKind::Graze?cfg.graze_decay:cfg.fruit_decay)*cfg.dt;
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
            check(w.traversable(r.position)&&(!w.sheltered(r.position)||(frontier&&r.kind==FoodKind::Graze))&&!w.in_nursery(r.position),"Outdoor replacement geography");
            for(const auto& other:w.resources)if(other.id!=r.id)check(length(r.position-other.position)>=.8,"Overlapping replacement");
        }
        auto outdoor=*std::find_if(w.resources.begin(),w.resources.end(),[&](const auto& r){return !r.shelter_food&&!w.in_nursery(r.position)&&r.kind==FoodKind::Graze;});
        w.resources={outdoor};w.resources[0].stock=.01;
        EcoCreature c;c.id=1;c.position=outdoor.position;c.energy=90;c.brain=make_sparse_ancestral_brain(cfg);
        w.creatures={c};w.next_creature_id=2;EcoAction feed;feed.forage=1;
        w.step({feed});check(length(w.resources[0].position-outdoor.position)>=1,"Eating triggers replacement");
        if(frontier) {
            // Restrict valid destinations to prove grazing can move into shelter
            // while fruit cannot use the same destination.
            auto probe=w;probe.creatures.clear();probe.resources={outdoor};
            probe.terrain.assign(probe.terrain.size(),Terrain::Wall);
            probe.resources[0].position={2.5,2.5};
            probe.terrain[2*cfg.width+2]=Terrain::Ground;
            probe.terrain[2*cfg.width+4]=Terrain::Shelter;
            check(probe.relocate_outdoor_food(probe.resources[0]),"Graze cannot relocate onto shelter floor");
            check(probe.sheltered(probe.resources[0].position),"Graze missed only valid shelter destination");
            probe.resources[0].position={2.5,2.5};probe.resources[0].kind=FoodKind::FruitA;
            check(!probe.relocate_outdoor_food(probe.resources[0]),"Fruit relocated inside shelter");
            continue;
        }
        auto shelter=*std::find_if(initial.begin(),initial.end(),[](const auto& r){return r.shelter_food;});
        w.resources={shelter};w.creatures.clear();
        w.config.phase_offset=cfg.calm_duration+cfg.warning_duration+1;
        for(int i=0;i<30;++i) {
            const auto old=w.resources[0].position;
            w.resources[0].stock=cfg.shelter_food_decay*cfg.dt/2;
            std::istringstream saved_state(save(w));auto copy=EcosystemWorld::load_checkpoint(saved_state);
            w.step();copy.step();check(save(w)==save(copy),"Shelter relocation checkpoint diverged");
            const auto& patch=w.resources[0];
            check(length(patch.position-old)>=.8&&patch.stock==patch.capacity,"Shelter decay must replace full elsewhere");
            const int low=int(cfg.shelter_size/2),high=int(cfg.shelter_size)-1-low;
            check(patch.position.x>=shelter.position.x-low&&patch.position.x<=shelter.position.x+high
                &&patch.position.y>=shelter.position.y-low&&patch.position.y<=shelter.position.y+high,
                "Food drifted into a different shelter");
            check(w.sheltered(patch.position)&&!w.in_nursery(patch.position),"Shelter food landed in wall or nursery");
        }
        w.creatures={c};w.creatures[0].position=w.resources[0].position;
        const auto before_eating=w.resources[0].position;
        w.resources[0].stock=.01;w.step({feed});
        check(length(w.resources[0].position-before_eating)>=.8,"Eating must relocate shelter food");
        w.creatures.clear();w.config.shelter_size=1;w.resources={shelter};w.resources[0].stock=0;
        w.step();check(w.resources[0].stock==0&&length(w.resources[0].position-shelter.position)==0,
            "Single-tile shelter must retain its empty slot");
        w.config.shelter_size=cfg.shelter_size;
        w.creatures={c};w.config.shelter_food_decay=0; // Historical static policy still works.
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
