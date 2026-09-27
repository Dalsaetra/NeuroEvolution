#include "fixtures.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace neuroevo;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void near(double a,double b,const char* m){check(std::abs(a-b)<1e-8,m);}
std::string saved(const EcosystemWorld& w){std::ostringstream s;w.save_checkpoint(s);return s.str();}
EcosystemWorld fixture(double carnivory) {
    auto cfg=controlled_config();cfg.set_predation(true);cfg.initial_creatures=0;cfg.reproduction=cfg.storms_enabled=false;
    cfg.basal_cost=cfg.movement_cost=cfg.turn_cost=cfg.forage_cost=cfg.call_cost=cfg.neuron_cost=cfg.synapse_cost=cfg.spike_cost=cfg.healing_rate=0;
    cfg.graze_decay=cfg.fruit_decay=0;cfg.outdoor_food_relocates=false;cfg.digestion_delay=.5;
    EcosystemWorld w(cfg,false);EcoCreature c;c.id=c.genome_id=1;c.position={4,4};c.body.carnivory=carnivory;
    c.energy=100;c.health=20;c.brain=make_sparse_ancestral_brain(cfg);w.creatures={c};w.next_creature_id=2;
    EcoResource r;r.id=1;r.kind=FoodKind::BitterFruit;r.position={4.5,4};r.stock=r.capacity=3;r.energy_per_unit=cfg.bitter_fruit_energy;
    w.resources={r};w.next_resource_id=2;return w;
}
EcoAction eat(){EcoAction a;a.forage=1;return a;}
int main() try {
    for(double c:{0.,.25,.49,.5,.51,.75,1.}) {
        auto w=fixture(c);w.step({eat()});
        near(w.creatures[0].eaten[5],.1,"Bitter fruit was not consumed at the ordinary rate");
        near(w.creatures[0].energy,100,"Bitter fruit bypassed digestion delay");
        near(w.creatures[0].health,20-std::max(0.,2*c-1),"Wrong toxicity curve");
        const double gained=12*std::max(0.,1-2*c);
        near(w.creatures[0].digestion.front().energy,gained,"Wrong nutrition curve");
        near(w.totals.discarded_energy,12-gained,"Diet conversion lost energy accounting");
        std::istringstream input(saved(w));auto resumed=EcosystemWorld::load_checkpoint(input);
        for(int step=0;step<6;++step){w.step({{}});resumed.step({{}});}
        check(saved(w)==saved(resumed),"Bitter digestion checkpoint changed continuation");
        near(w.creatures[0].energy,100+gained,"Delayed energy delivery incorrect");
    }
    auto lethal=fixture(1);lethal.creatures[0].health=.2;lethal.step({eat()});
    check(lethal.creatures.empty() && lethal.totals.deaths==1 && lethal.totals.predation_deaths==0,"Poison death counted as predation");
    check(std::count_if(lethal.resources.begin(),lethal.resources.end(),[](const auto& r){return r.kind==FoodKind::Meat;})==1,"Poison death produced wrong corpse count");
    check(std::any_of(lethal.events.begin(),lethal.events.end(),[](const auto& e){return e.type=="death" && e.cause=="poisoning";}),"Poison death cause missing");
    auto w=fixture(.75);w.config.healing_rate=100;w.step({eat()});near(w.creatures[0].health,19.5,"Healing erased poisoning in the same interval");

    w=fixture(0);auto observation=w.observe(0);const auto sector=1u;
    near(observation[eco_bitter_offset+sector],1,"Bitter presence missing");
    near(observation[eco_bitter_offset+eco_sectors+sector],1-.5/w.config.vision_range,"Bitter proximity missing");
    near(observation[eco_bitter_offset+2*eco_sectors+sector],1,"Bitter amount missing");
    near(observation[sector*eco_sector_channels+2],1-.5/w.config.vision_range,"General food sensor omitted bitter fruit");
    near(observation[eco_plant_offset+sector],1-.5/w.config.vision_range,"Plant sensor omitted bitter fruit");
    for(bool typed:{false,true}) {
        w.config.typed_food_proximity=typed;observation=w.observe(0);
        near(observation[sector*eco_sector_channels+8],0,"Bitter fruit corrupted pod input");
    }
    w.resources[0].stock=0;observation=w.observe(0);near(observation[eco_bitter_offset+sector],0,"Depleted bitter fruit still present");
    check(observation[eco_depleted_offset+sector]>0,"Depleted bitter fruit invisible");
    w.resources[0].stock=3;w.resources[0].position={6.5,4};w.terrain[4*w.config.width+5]=Terrain::Wall;
    near(w.observe(0)[eco_bitter_offset+sector],0,"Bitter fruit visible through wall");
    std::vector<int> covered(eco_predation_input_count);
    for(const auto& group:ecosystem_input_groups(true,true))for(auto input:group)++covered.at(input);
    for(int count:covered)check(count==1,"Bitter sensor groups invalid");

    w=fixture(0);w.food_sources={{1,FoodSourceKind::BitterTree,{4.5,4},3,0}};
    auto& r=w.resources[0];r.source_id=1;r.stock=0;r.capacity=.2;r.regrowth=.1;r.ripening_remaining=2;
    for(int i=0;i<20;++i)w.step({{}});
    near(w.resources[0].stock,.2,"Bitter tree did not ripen");
    near(w.resources[0].ripening_remaining,-1,"Bitter fruit did not become edible");
    // Real source checkpoints require the source distribution preset.
    w.config.food_distribution=FoodDistribution::FieldsAndTrees;std::istringstream valid_tree(saved(w));
    auto resumed=EcosystemWorld::load_checkpoint(valid_tree);check(saved(w)==saved(resumed),"Bitter tree checkpoint changed");

    auto old=fixture(0);old.resources.clear();old.config.brain.input_count=eco_bitter_offset;
    old.creatures[0].brain=make_sparse_ancestral_brain(old.config);
    auto previous=saved(old);previous.erase(previous.find("BITTER_FRUIT_1"));previous+="END_ECOSYSTEM\n";
    previous.replace(0,21,"NEUROEVO_ECOSYSTEM_45");
    std::istringstream old_input(previous);auto restored=EcosystemWorld::load_checkpoint(old_input);
    check(restored.observe(0).size()==eco_bitter_offset && restored.config.food_sources.bitter_trees==0,"Historical run gained bitter fruit or inputs");
    std::cout<<"Bitter fruit tests passed"<<std::endl;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
