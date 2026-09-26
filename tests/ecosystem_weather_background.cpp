#include "fixtures.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace neuroevo;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double a,double b,const char* message){check(std::abs(a-b)<1e-8,message);}
std::string saved(const EcosystemWorld& w){std::ostringstream s;w.save_checkpoint(s);return s.str();}
EcosystemWorld fixture(double elapsed,bool shelter=false)
{
    auto cfg=controlled_config();cfg.set_predation(true);cfg.storm_health_damage=true;cfg.storm_ramp=true;
    cfg.initial_creatures=0;cfg.reproduction=false;cfg.storms_enabled=true;
    cfg.calm_duration=2;cfg.warning_duration=2;cfg.storm_duration=8;cfg.phase_offset=4+elapsed;
    cfg.storm_damage=1;cfg.healing_rate=0;cfg.outdoor_food_relocates=false;
    cfg.basal_cost=cfg.movement_cost=cfg.turn_cost=cfg.forage_cost=cfg.call_cost=cfg.neuron_cost=cfg.synapse_cost=cfg.spike_cost=0;
    cfg.digestion_delay=0;EcosystemWorld w(cfg,false);
    EcoCreature c;c.id=c.genome_id=1;c.position={4.5,4.5};c.energy=50;c.body={1,0};c.health=20;
    c.brain=make_sparse_ancestral_brain(cfg);w.creatures={c};w.next_creature_id=2;
    if(shelter)w.terrain[4*cfg.width+4]=Terrain::Shelter;
    EcoResource r;r.id=1;r.position={4.6,4.5};r.stock=r.capacity=10;r.energy_per_unit=1;
    w.resources={r};w.next_resource_id=2;return w;
}
void storm_curve()
{
    for(double elapsed:{0.,2.,4.,6.,8.})for(bool shelter:{false,true}) {
        auto w=fixture(elapsed,shelter);
        const double intensity=elapsed<=4?elapsed/4:(8-elapsed)/4;
        near(w.storm_intensity(),intensity,"Storm triangle has wrong endpoints or slope");
        EcoAction a;a.forage=1;w.step({a});
        near(w.creatures[0].health,20-(shelter?0:intensity*.1),"Wrong ramped health damage or shelter protection");
        near(w.totals.consumed_biomass,.1*(shelter?1:1-.5*intensity),"Wrong harvesting ramp");
    }
    auto w=fixture(0);
    for(int i=0;i<80;++i)w.step({{}});
    near(w.totals.damage,4,"Integrated triangular damage must be half of flat peak damage");
    auto energy=fixture(4);energy.config.storm_health_damage=false;energy.config.storm_cost=2;
    energy.step({{}});near(energy.creatures[0].energy,49.8,"Energy-mode peak did not use intensity");
    energy=fixture(2);energy.config.storm_health_damage=false;energy.config.storm_cost=2;
    energy.step({{}});near(energy.creatures[0].energy,49.9,"Energy-mode ramp did not use intensity");
    auto disabled=fixture(4);disabled.config.storms_enabled=false;
    near(disabled.storm_intensity(),0,"Disabled storms retain intensity");
    disabled.step({{}});near(disabled.creatures[0].health,20,"Disabled storm damages health");
    auto offset=fixture(2);offset.config.phase_offset-=12;
    near(offset.storm_intensity(),.5,"Negative phase offset changed intensity");
    auto pod=fixture(4);pod.resources[0].kind=FoodKind::Pod;pod.resources[0].pod_state=PodState::Closed;
    EcoAction a;a.forage=1;pod.step({a});near(pod.creatures[0].pod_work,.05,"Pod effort ignores storm penalty");
    auto meat=fixture(4);meat.creatures[0].body.carnivory=1;meat.resources[0].kind=FoodKind::Meat;
    meat.step({a});near(meat.totals.consumed_biomass,.05,"Meat harvesting ignores storm penalty");
    // Protection is determined after movement, not at the beginning of a step.
    auto moving=fixture(4);moving.creatures[0].position={4.49,4.99};moving.creatures[0].heading=1.5707963267948966;
    moving.terrain[5*moving.config.width+4]=Terrain::Shelter;
    moving.resources[0].position={4.5,5.2};a.forward=1;moving.step({a});
    check(moving.sheltered(moving.creatures[0].position),"Movement fixture failed to enter shelter");
    near(moving.creatures[0].health,20,"Entering shelter failed to protect immediately");
    near(moving.totals.consumed_biomass,.1,"Entering shelter failed to restore harvesting immediately");
}
void background_food()
{
    for(std::uint64_t seed=1;seed<=4;++seed) {
        EcosystemConfig cfg;cfg.seed=seed;cfg.initial_creatures=0;cfg.reproduction=false;
        EcosystemWorld w(cfg),same(cfg);check(saved(w)==saved(same),"Background generation is not deterministic");
        std::size_t count=0,inside=0,outside=0;
        for(const auto& r:w.resources)if(!r.source_id && !w.in_nursery(r.position)) {
            ++count;if(w.sheltered(r.position))++inside;else ++outside;
            check(r.kind==FoodKind::Graze && !r.shelter_food,"Background food uses wrong lifecycle");
            near(r.energy_per_unit,cfg.background_food_energy,"Wrong background nutrition");
            check(r.stock>0 && r.stock<r.capacity,"Background food ages were not staggered");
            for(const auto& other:w.resources)if(r.id!=other.id)
                check(length(r.position-other.position)>=.8,"Background food overlaps another resource");
        }
        check(count==cfg.background_food_patches && inside>0 && outside>0,"Background quota missing inside or outside shelters");
        // Force decay depletion, checkpoint during cooldown, and compare renewal.
        for(auto& r:w.resources)if(!r.source_id && !w.in_nursery(r.position))r.stock=cfg.graze_decay*cfg.dt/2;
        w.step();std::istringstream input(saved(w));auto resumed=EcosystemWorld::load_checkpoint(input);
        for(int i=0;i<15;++i){w.step();resumed.step();check(saved(w)==saved(resumed),"Background respawn checkpoint diverged");}
        for(const auto& r:w.resources)if(!r.source_id && !w.in_nursery(r.position)) {
            check(r.stock>cfg.graze_capacity-.02,"Depleted background food did not respawn");
            near(r.energy_per_unit,cfg.background_food_energy,"Respawn lost low nutrition");
        }
    }
    EcosystemConfig cfg;cfg.initial_creatures=0;cfg.background_food_patches=0;EcosystemWorld w(cfg);
    for(const auto& r:w.resources)check(r.source_id || w.in_nursery(r.position),"Disabled background food still generated");
}
void checkpoint_compatibility()
{
    auto w=fixture(2);std::istringstream input(saved(w));auto resumed=EcosystemWorld::load_checkpoint(input);
    for(int i=0;i<110;++i){w.step({{}});resumed.step({{}});check(saved(w)==saved(resumed),"Ramped weather continuation diverged");}
    w=fixture(2);w.config.storm_ramp=false;w.config.mutation.eye_mutation_probability=0;
    auto old=without_allometry_header(saved(w));old.replace(0,21,"NEUROEVO_ECOSYSTEM_36");old.erase(old.find("BACKGROUND_WEATHER_1"));old+="END_ECOSYSTEM\n";
    std::istringstream legacy(old);auto restored=EcosystemWorld::load_checkpoint(legacy);
    check(!restored.config.storm_ramp && restored.config.background_food_patches==0,"Old checkpoint adopted new ecology");
    EcoAction a;a.forage=1;w.step({a});restored.step({a});
    check(saved(w)==saved(restored),"Legacy weather continuation changed");
    near(restored.totals.consumed_biomass,0,"Legacy storm lost harvesting cutoff");
    near(restored.totals.damage,.1,"Legacy storm lost flat damage");
}
}
int main(){try{storm_curve();background_food();checkpoint_compatibility();std::cout<<"Weather and background food tests passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
