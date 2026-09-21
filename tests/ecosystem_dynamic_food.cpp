#include "fixtures.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace neuroevo;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string save(const EcosystemWorld& w){std::ostringstream s;w.save_checkpoint(s);return s.str();}
void optional_regrowth() {
    for(bool nursery:{false,true}) for(bool relocate:{false,true}) {
        auto cfg=scattered_config();cfg.width=cfg.height=80;cfg.initial_creatures=0;cfg.storms_enabled=false;
        cfg.nursery_food_relocates=cfg.outdoor_food_relocates=relocate;
        cfg.nursery_food_decay=cfg.graze_decay=cfg.fruit_decay=0;
        EcosystemWorld w(cfg,false);
        EcoResource r;r.id=1;r.position=nursery?Vec2{40,40}:Vec2{10,10};r.stock=0.5;r.capacity=1;
        w.resources={r};w.step();check(w.resources[0].stock==0.5,"Zero regrowth replenished food");
        w.resources[0].regrowth=1;w.step();
        check(std::abs(w.resources[0].stock-0.6)<1e-9,"Relocation disabled explicit passive regrowth");
        w.resources[0].stock=0.99;w.step();check(w.resources[0].stock==1,"Regrowth exceeded capacity");
    }
    auto cfg=scattered_config();cfg.width=cfg.height=80;cfg.initial_creatures=0;cfg.nursery_food_regrowth=0.7;
    EcosystemWorld w(cfg);
    for(const auto& r:w.resources)if(w.in_nursery(r.position))
        check(r.regrowth==0.7,"Moving nursery food lost configured regrowth");
}

void respawn_delays() {
    for (bool relocate : {false,true}) for (auto kind : {FoodKind::Graze,FoodKind::FruitA,FoodKind::FruitB}) {
        auto cfg=scattered_config(); cfg.width=cfg.height=80; cfg.initial_creatures=0; cfg.dt=0.1; cfg.storms_enabled=false;
        cfg.nursery_food_relocates=cfg.outdoor_food_relocates=relocate;
        cfg.nursery_food_respawn_delay=0.2; cfg.outdoor_food_respawn_delay=0.4;
        cfg.nursery_food_decay=cfg.graze_decay=cfg.fruit_decay=0;
        EcosystemWorld w(cfg,false);
        EcoResource r; r.kind=kind; r.id=1; r.position={40,40}; r.capacity=1; r.regrowth=1;
        w.resources.push_back(r); r.id=2; r.position={10,10}; w.resources.push_back(r);
        w.next_resource_id=3;
        w.step(); // Depletion observed at t=0.1, deadlines t=0.3 and t=0.5.
        check(w.resources[0].stock==0 && w.resources[1].stock==0,"Food respawned before cooldown");
        std::istringstream input(save(w)); auto resumed=EcosystemWorld::load_checkpoint(input);
        for(int i=0;i<4;++i) {
            w.step(); resumed.step();
            check(save(w)==save(resumed),"Food cooldown continuation diverged");
            if(i==0)check(w.resources[0].stock==0,"Nursery cooldown ended early");
            if(i<3)check(w.resources[1].stock==0,"Outside cooldown ended early");
            if(i==1)check(w.resources[0].stock>0,"Nursery food did not respawn on time");
        }
        check(w.resources[1].stock>0,"Outside food did not respawn on time");
        w.resources[0].stock=0; w.step();
        check(w.resources[0].stock==0 && w.resources[0].respawn_at>0.6,"Second depletion reused old deadline");
    }
    for (bool nursery : {false,true}) {
        auto cfg=scattered_config(); cfg.width=cfg.height=80; cfg.initial_creatures=0; cfg.dt=0.1;
        cfg.nursery_food_respawn_delay=cfg.outdoor_food_respawn_delay=0.2;
        cfg.nursery_food_decay=cfg.graze_decay=0.01;
        EcosystemWorld w(cfg,false);
        EcoResource r; r.id=1; r.capacity=1; r.stock=0.0005;
        r.position=nursery ? Vec2{40,40} : Vec2{10,10}; w.resources={r};
        w.step(); check(w.resources[0].stock==0,"Decay bypassed cooldown");
        w.step(); check(w.resources[0].stock==0,"Decay cooldown ended early");
        w.step(); check(w.resources[0].stock==1,"Decay cooldown did not expire");
    }
    for (bool nursery : {false,true}) {
        auto cfg=scattered_config();
        if(nursery)cfg.nursery_food_respawn_delay=-1;else cfg.outdoor_food_respawn_delay=-1;
        bool rejected=false;try{cfg.validate();}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Negative respawn delay accepted");
    }
}
void population_nutrition() {
    auto cfg=scattered_config(); cfg.initial_creatures=51; cfg.reproduction=false;
    cfg.storms_enabled=false; cfg.nursery_food_decay=0;
    cfg.nursery_food_energy=80; cfg.nursery_food_population_threshold=50;
    cfg.nursery_food_energy_factor=0.8;
    cfg.nursery_food_reduction_delay=0;
    EcosystemWorld w(cfg);
    // Remove collision obstacles so occupancy changes are explicit in this test.
    std::fill(w.terrain.begin(),w.terrain.end(),Terrain::Ground);
    const auto inside=w.creatures.back().position;
    w.creatures.back().position={2,2};
    w.creatures.front().digestion.push_back({1000,23,FoodKind::Graze});
    EcoResource meat; meat.id=w.resources.back().id+1;meat.kind=FoodKind::Meat;
    w.next_resource_id=meat.id+1;
    meat.position=inside;meat.stock=meat.capacity=1;meat.energy_per_unit=60;
    w.resources.push_back(meat);
    const auto before=w.resources;
    const auto step=[](EcosystemWorld& x){x.step(std::vector<EcoAction>(x.creatures.size()));};
    step(w);
    check(w.nursery_food_reductions==0,"Exactly 50 nursery residents triggered a reduction");
    w.creatures.back().position=inside;step(w);
    check(w.nursery_food_reductions==1 && w.nursery_food_current_energy==64,"First upward crossing did not reduce 80 to 64");
    check(w.config.nursery_food_energy==80,"Reduction overwrote starting configuration");
    for(std::size_t i=0;i<w.resources.size();++i) {
        const auto& r=w.resources[i];
        const double expected=w.in_nursery(r.position)&&r.kind==FoodKind::Graze&&!r.shelter_food?64:before[i].energy_per_unit;
        check(r.energy_per_unit==expected,"Nutrition change affected the wrong food resource");
    }
    check(w.creatures.front().digestion.front().energy==23,"Reduction changed already ingested energy");
    check(std::count_if(w.events.begin(),w.events.end(),[](const auto& e){return e.type=="nursery_food_energy_reduced"&&e.amount==64;})==1,"Reduction event missing");
    std::istringstream input(save(w));auto resumed=EcosystemWorld::load_checkpoint(input);
    step(w);step(resumed);
    check(save(w)==save(resumed),"Nutrition checkpoint continuation diverged");
    check(w.nursery_food_reductions==1,"Remaining above the threshold reduced energy again");
    for(auto* x:{&w,&resumed}){x->creatures.back().position={2,2};step(*x);}
    std::istringstream rearmed(save(w));resumed=EcosystemWorld::load_checkpoint(rearmed);
    for(auto* x:{&w,&resumed}){x->creatures.back().position=inside;step(*x);}
    check(save(w)==save(resumed),"Rearmed checkpoint lost the next crossing");
    check(w.nursery_food_reductions==2&&std::abs(w.nursery_food_current_energy-51.2)<1e-10,"Second crossing did not compound");
    for(auto& r:w.resources)if(r.kind==FoodKind::Graze&&w.in_nursery(r.position)){
        check(w.relocate_nursery_food(r,w.map_rng,false),"Test relocation failed");
        check(std::abs(r.energy_per_unit-51.2)<1e-10,"Relocation reset reduced nutrition");break;
    }
    w.config.nursery_food_population_threshold=0;w.nursery_above_food_threshold=false;step(w);
    check(w.nursery_food_reductions==2,"Disabled mechanic still reduced nutrition");
    for(double factor:{0.0,-0.1,1.1}) {
        cfg.nursery_food_energy_factor=factor;bool rejected=false;
        try{cfg.validate();}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Invalid nutrition factor accepted");
    }
}
void population_nutrition_cooldown() {
    check(scattered_config().nursery_food_reduction_delay==1000,"Default nutrition cooldown changed");
    for (double dt : {0.1,0.2}) {
        auto cfg=scattered_config();cfg.initial_creatures=2;cfg.nursery_food_population_threshold=1;
        cfg.reproduction=false;cfg.storms_enabled=false;cfg.nursery_food_reduction_delay=1;cfg.dt=dt;
        EcosystemWorld w(cfg);
        std::fill(w.terrain.begin(),w.terrain.end(),Terrain::Ground);
        const auto inside=w.creatures.back().position;
        const auto step=[](EcosystemWorld& x){x.step(std::vector<EcoAction>(x.creatures.size()));};
        step(w);
        check(w.nursery_food_reductions==1,"Cooldown delayed the first reduction");
        check(std::abs(w.nursery_food_reduction_ready_at-(dt+1))<1e-9,"Cooldown is not in simulation seconds");
        auto boundary=w;
        w.creatures.back().position={2,2};step(w);
        w.creatures.back().position=inside;step(w);
        check(w.nursery_food_reductions==1,"Crossing during cooldown reduced nutrition");
        check(std::abs(w.nursery_food_reduction_ready_at-(dt+1))<1e-9,"Ignored crossing extended cooldown");
        std::istringstream input(save(w));auto resumed=EcosystemWorld::load_checkpoint(input);
        check(save(w)==save(resumed),"Checkpoint lost cooldown or ignored crossing state");
        while(w.time()<dt+1+dt) {
            step(w);step(resumed);
            check(save(w)==save(resumed),"Cooldown continuation diverged");
            check(w.nursery_food_reductions==1,"Cooldown expiry replayed an ignored crossing");
        }
        for(auto* x:{&w,&resumed}) {
            x->creatures.back().position={2,2};step(*x);
            x->creatures.back().position=inside;step(*x);
        }
        check(w.nursery_food_reductions==2,"New crossing after cooldown did not reduce nutrition");
        check(save(w)==save(resumed),"Post-cooldown crossing diverged on resume");
        check(std::abs(w.nursery_food_reduction_ready_at-(w.time()+1))<1e-9,"Second reduction did not start a fresh cooldown");
        w.creatures.back().position={2,2};step(w);
        w.creatures.back().position=inside;step(w);
        check(w.nursery_food_reductions==2,"Second episode bypassed cooldown");
        // A crossing exactly at the deadline is eligible, independent of timestep.
        boundary.creatures.back().position={2,2};
        while(boundary.time()+dt < boundary.nursery_food_reduction_ready_at-1e-9)step(boundary);
        boundary.creatures.back().position=inside;step(boundary);
        check(boundary.nursery_food_reductions==2,"Crossing at cooldown deadline was ignored");
        w.generate_world();
        check(w.nursery_food_reduction_ready_at==0,"New world retained old cooldown");
        step(w);check(w.nursery_food_reductions==1,"New world did not allow its first reduction");
    }
    for(double delay:{-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto cfg=scattered_config();cfg.nursery_food_reduction_delay=delay;bool rejected=false;
        try{cfg.validate();}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Invalid nutrition cooldown accepted");
    }
}
int main(){try{
    population_nutrition_cooldown();
    population_nutrition();
    optional_regrowth();
    respawn_delays();
    for(bool frontier:{false,true}) {
        auto cfg=frontier?scattered_config():controlled_config();
        cfg.initial_creatures=0;cfg.reproduction=false;
        cfg.nursery_food_respawn_delay=cfg.outdoor_food_respawn_delay=0;
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
        w.config.basal_cost=0.2; // This fixture tests a sustainable shelter supply.
        shelter.regrowth=0.6;
        w.resources={shelter};w.resources[0].stock=0;w.creatures[0].position=shelter.position;
        w.config.phase_offset=cfg.calm_duration+cfg.warning_duration+1;
        w.step({{}});check(w.resources[0].stock>0,"Shelter regrowth during storms");
        w.config.calm_duration=1;w.config.warning_duration=1;w.config.storm_duration=1000;w.config.phase_offset=3;
        const double energy=w.creatures[0].energy;
        for(int i=0;i<2000;++i){check(!w.creatures.empty(),"Shelter feeder starved");w.step({feed});}
        check(w.creatures[0].energy>=energy,"Shelter supply fails to cover basal plus feeding costs");
        check(length(w.resources[0].position-shelter.position)==0,"Shelter food moved");
    }
    auto bad=scattered_config();bad.graze_decay=bad.ingestion_rate;
    bool rejected=false;try{bad.validate();}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Decay must be slower than eating");
    std::cout<<"Dynamic food passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
