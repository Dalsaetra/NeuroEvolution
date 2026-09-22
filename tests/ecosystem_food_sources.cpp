#include "fixtures.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace neuroevo;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,const char* message){check(std::abs(a-b)<1e-8,message);}
std::string saved(const EcosystemWorld& w){std::ostringstream s;w.save_checkpoint(s);return s.str();}
void generation()
{
    for(std::uint64_t seed=1;seed<=12;++seed) {
        EcosystemConfig cfg;cfg.seed=seed;cfg.initial_creatures=0;cfg.reproduction=false;
        EcosystemWorld w(cfg),same(cfg);
        check(saved(w)==saved(same),"Source generation is not deterministic");
        check(w.food_sources.size()==cfg.food_sources.fields+cfg.food_sources.fruit_trees+cfg.food_sources.pod_trees,"Source count mismatch");
        std::size_t field_cells=0,fruits=0,pods=0;double min_capacity=1e9,max_capacity=0;
        for(const auto& source:w.food_sources) {
            for(const auto& other:w.food_sources)if(source.id!=other.id)
                check(length(source.position-other.position)>=source.radius+other.radius+cfg.food_sources.source_gap,"Food sources overlap");
            std::vector<Vec2> cells;
            for(const auto& r:w.resources)if(r.source_id==source.id)cells.push_back(r.position);
            check(!cells.empty(),"Empty food source");
            if(source.kind==FoodSourceKind::Field) {
                std::set<std::size_t> seen{0};std::vector<std::size_t> pending{0};
                while(!pending.empty()) {
                    const auto i=pending.back();pending.pop_back();
                    for(std::size_t j=0;j<cells.size();++j)
                        if(length(cells[i]-cells[j])<=cfg.food_sources.field_spacing+1e-8 && seen.insert(j).second)pending.push_back(j);
                }
                check(seen.size()==cells.size(),"Field is not continuous");
            } else {
                double production=0;for(const auto& r:w.resources)if(r.source_id==source.id)production+=r.regrowth;
                near(production,source.kind==FoodSourceKind::FruitTree?cfg.food_sources.fruit_production:cfg.food_sources.pod_production,"Tree production multiplied by its sites");
            }
        }
        for(const auto& r:w.resources)if(r.source_id) {
            check(!w.in_nursery(r.position) && !w.sheltered(r.position) && w.traversable(r.position),"Source food intersects protection/obstacles");
            if(r.kind==FoodKind::Graze){++field_cells;min_capacity=std::min(min_capacity,r.capacity);max_capacity=std::max(max_capacity,r.capacity);}
            else if(r.kind==FoodKind::Pod)++pods;else ++fruits;
        }
        check(field_cells>200 && field_cells*cfg.food_sources.field_spacing*cfg.food_sources.field_spacing<cfg.width*cfg.height*.2,"Fields do not leave large empty areas");
        check(max_capacity>min_capacity*1.5,"Field density is uniform");
        check(fruits==cfg.food_sources.fruit_trees*cfg.food_sources.fruit_sites && pods==cfg.food_sources.pod_trees*cfg.food_sources.pod_sites,"Tree site counts mismatch");
        for(std::size_t i=0;i<w.terrain.size();++i)if(w.terrain[i]==Terrain::Shelter)
            check(!w.reserved_for_food({double(i%cfg.width)+.5,double(i/cfg.width)+.5},2),"Shelter overlaps reserved food area");
    }
    auto impossible=EcosystemConfig{};impossible.food_sources.fields=100;
    bool rejected=false;try{EcosystemWorld w(impossible);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Impossible source layout silently degraded");
}
void renewal_and_resume()
{
    EcosystemConfig cfg;cfg.initial_creatures=0;cfg.reproduction=false;cfg.storms_enabled=false;cfg.fruit_decay=0;
    EcosystemWorld w(cfg,false);
    w.food_sources={{1,FoodSourceKind::Field,{10,10},3,0},{2,FoodSourceKind::FruitTree,{20,10},3,0},{3,FoodSourceKind::PodTree,{30,10},3,0}};
    EcoResource field;field.id=1;field.source_id=1;field.position={10,10};field.capacity=2;field.regrowth=1;field.energy_per_unit=25;
    EcoResource fruit=field;fruit.id=2;fruit.source_id=2;fruit.position={20,10};fruit.kind=FoodKind::FruitA;fruit.ripening_remaining=.3;
    EcoResource pod=field;pod.id=3;pod.source_id=3;pod.position={30,10};pod.kind=FoodKind::Pod;pod.pod_state=PodState::Refilling;
    w.resources={field,fruit,pod};w.next_resource_id=4;
    w.step();near(w.resources[0].stock,.1,"Depleted field did not regrow locally");
    near(w.resources[1].stock,0,"Unripe fruit became edible early");near(w.resources[1].ripening_remaining,.2,"Ripening timer did not advance");
    w.config.storms_enabled=true;w.config.phase_offset=cfg.calm_duration+cfg.warning_duration;
    w.step();near(w.resources[0].stock,.1,"Field grew during storm");near(w.resources[1].ripening_remaining,.2,"Fruit ripened during storm");
    w.config.storms_enabled=false;
    std::istringstream input(saved(w));auto resumed=EcosystemWorld::load_checkpoint(input);
    check(saved(w)==saved(resumed),"Source checkpoint lost growing state");
    for(int i=0;i<30;++i){w.step();resumed.step();check(saved(w)==saved(resumed),"Source resume diverged");}
    near(w.resources[0].stock,2,"Field exceeded or failed to reach capacity");
    near(w.resources[1].stock,2,"Fruit never ripened");
    check(w.resources[1].ripening_remaining==-1 && w.resources[2].pod_state==PodState::Closed,"Renewal state transition failed");
    near(w.totals.regrown_biomass,6,"Renewal biomass ledger wrong");
    w.resources[1].stock=0;w.step();near(w.resources[1].stock,0,"Consumed fruit instantly refilled");
    check(w.resources[1].ripening_remaining>1.8,"Consumed fruit did not restart ripening");
    w.resources[1].source_id=99;bool rejected=false;
    try{std::istringstream invalid(saved(w));auto bad=EcosystemWorld::load_checkpoint(invalid);(void)bad;}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"Invalid source membership was accepted");
}
void harvesting_and_legacy()
{
    auto cfg=controlled_config();cfg.initial_creatures=0;cfg.reproduction=false;cfg.storms_enabled=false;
    cfg.mutation.min_mutation_scale=0.5; // Match the policy of the forged historical checkpoint.
    cfg.basal_cost=cfg.movement_cost=cfg.turn_cost=cfg.forage_cost=0;cfg.digestion_delay=0;
    EcosystemWorld w(cfg,false);
    EcoCreature c;c.id=c.genome_id=1;c.position={4,4};c.energy=50;c.brain=make_sparse_ancestral_brain(cfg);w.creatures.push_back(c);w.next_creature_id=2;
    for(int i=0;i<4;++i){EcoResource r;r.id=i+1;r.position={4.1,.1*i+4};r.stock=r.capacity=10;r.energy_per_unit=1;w.resources.push_back(r);}
    w.next_resource_id=5;EcoAction a;a.forage=1;w.step({a});
    near(w.totals.consumed_biomass,cfg.ingestion_rate*cfg.dt,"Dense food multiplied ingestion rate");
    auto old=saved(w);old.replace(0,std::string("NEUROEVO_ECOSYSTEM_34").size(),"NEUROEVO_ECOSYSTEM_33");
    old.erase(old.find("FOOD_SOURCES_1"));old+="END_ECOSYSTEM\n";
    std::istringstream input(old);auto restored=EcosystemWorld::load_checkpoint(input);
    check(restored.config.food_distribution==FoodDistribution::Scattered && restored.food_sources.empty(),"Historical checkpoint adopted the new preset");
    for(int i=0;i<10;++i){w.step({a});restored.step({a});check(saved(w)==saved(restored),"Historical scattered continuation changed");}
}
}
int main(){try{generation();renewal_and_resume();harvesting_and_legacy();std::cout<<"Food source tests passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
