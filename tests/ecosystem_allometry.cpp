#include "fixtures.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace neuroevo;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double a,double b,const char* message){check(std::abs(a-b)<1e-8,message);}
std::string saved(const EcosystemWorld& w){std::ostringstream s;w.save_checkpoint(s);return s.str();}
EcosystemWorld fixture()
{
    auto c=controlled_config();c.set_predation(true);c.mass_allometry=c.mass_scaled_energy_capacity=true;
    c.initial_creatures=0;c.reproduction=c.storms_enabled=c.outdoor_food_relocates=false;
    c.basal_cost=c.movement_cost=c.turn_cost=c.forage_cost=c.call_cost=c.neuron_cost=c.synapse_cost=c.spike_cost=c.healing_rate=0;
    c.graze_decay=c.fruit_decay=c.meat_decay=0;c.digestion_delay=0;
    return EcosystemWorld(c,false);
}
void add(EcosystemWorld& w,double mass,double diet=0,Vec2 position={4,4})
{
    EcoCreature c;c.id=c.genome_id=w.next_creature_id++;c.body={mass,diet};c.position=position;
    c.energy=100;c.health=w.max_health(c);c.brain=make_sparse_ancestral_brain(w.config);w.creatures.push_back(c);
}
void food(EcosystemWorld& w,FoodKind kind=FoodKind::Graze,double stock=10)
{
    EcoResource r;r.id=w.next_resource_id++;r.kind=kind;r.position={4.5,4.4};
    r.stock=r.capacity=stock;r.energy_per_unit=10;w.resources.push_back(r);
}
EcoAction eat(){EcoAction a;a.forage=1;return a;}
EcoAction move(){EcoAction a;a.forward=1;return a;}
double sensed(const EcosystemWorld& w,const char* name)
{
    const auto labels=ecosystem_input_labels(w.config.extended_senses,w.config.predation,w.config.typed_food_proximity,w.config.funded_reproduction);
    const auto it=std::find(labels.begin(),labels.end(),name);check(it!=labels.end(),"Missing sense");
    return w.observe(0)[static_cast<std::size_t>(it-labels.begin())];
}
void feeding()
{
    for(double m:{.5,1.,2.})for(auto kind:{FoodKind::Graze,FoodKind::Meat}) {
        auto w=fixture();add(w,m,.4);food(w,kind);auto a=eat();a.forage=.5;
        w.step({a});const double amount=.5*w.config.dt*std::pow(m,.8)*(kind==FoodKind::Meat?.4:1);
        near(w.totals.consumed_biomass,amount,"Wrong mass/diet bite rate");
        near(w.creatures[0].energy,100+amount*10*(kind==FoodKind::Meat?.4:.6),"Dietary digestion changed");
        near(w.creatures[0].energy+w.resources[0].stock*10+w.totals.discarded_energy,200,"Food energy not conserved");
        near(sensed(w,"ingestion"),.5*(kind==FoodKind::Meat?.4:1),"Ingestion feedback not normalized by body rate");
        const double nutrition=std::max({w.config.graze_energy,w.config.poor_fruit_energy,w.config.rich_fruit_energy,w.config.pod_energy,w.config.meat_energy});
        near(sensed(w,"digestion_gain"),(w.creatures[0].energy-100)/(nutrition*w.maximum_ingestion_rate(w.creatures[0])*w.config.dt),"Digestion feedback not normalized by body rate");
    }
    auto shared=fixture();add(shared,.5);add(shared,2,0,{4,4.8});food(shared,FoodKind::Graze,.05);
    shared.step({eat(),eat()});near(shared.totals.consumed_biomass,.05,"Shared resource overdrawn");
    near(shared.creatures[1].ingestion_pulse/shared.creatures[0].ingestion_pulse,std::pow(4.,.8),"Food share does not follow bite rate");
    auto custom=fixture();custom.config.ingestion_mass_exponent=0;add(custom,2);food(custom);custom.step({eat()});
    near(custom.totals.consumed_biomass,.1,"Configurable ingestion exponent ignored");
}
void strength()
{
    for(double m:{.5,1.,2.})for(double diet:{0.,.4,1.}) {
        auto w=fixture();add(w,m,diet);add(w,1,0,{4.6,4});EcoAction a;a.attack=1;
        w.step({a,{}});
        near(w.totals.damage,w.config.attack_damage*w.config.dt*std::pow(m,2./3.)*(w.config.attack_base_fraction+(1-w.config.attack_base_fraction)*diet),"Attack lost mass or diet scaling");
        near(w.totals.attacking,w.config.attack_cost*w.config.dt,"Attack cost changed");
        auto pod=fixture();add(pod,m,diet);food(pod,FoodKind::Pod);pod.step({eat()});
        near(pod.resources[0].progress,diet<1?.1*std::pow(m,2./3.):0,"Pod mass exponent or dietary eligibility changed");
    }
    auto pod=fixture();add(pod,.5);add(pod,2,0,{4,4.8});food(pod,FoodKind::Pod);pod.step({eat(),eat()});
    near(pod.resources[0].progress,.4*(std::pow(.5,2./3.)+std::pow(2.,2./3.))/2,"Cooperative pod work lost strength or cooperation");
}
void reserves()
{
    for(double diet:{0.,.5,1.})for(double m:{.5,1.,2.}) {
        auto w=fixture();w.config.basal_cost=1;add(w,m,diet);w.creatures[0].energy=w.config.max_energy(m);
        w.step({{}});const double rate=std::pow(m,.75)*(1-(1-w.config.carnivore_basal_fraction)*diet);
        near(w.totals.metabolism,rate*.1,"Wrong maintenance mass or diet scaling");
        near(w.creatures[0].energy,250*m-rate*.1,"Reserve capacity not linear in mass");
        near(w.config.max_energy(m)/rate,250*std::pow(m,.25)/(1-(1-w.config.carnivore_basal_fraction)*diet),"Basal endurance scaling wrong");
        w.creatures[0].digestion.push_back({w.time(),20,FoodKind::Graze});const auto before=w.creatures[0].energy;
        w.step({{}});near(w.creatures[0].energy+w.totals.discarded_energy+rate*.1,before+20,"Reserve overflow creates energy");
    }
}
void motion()
{
    auto w=fixture();add(w,.5);add(w,2,0,{4,8});w.step({move(),move()});
    check(w.creatures[0].speed>w.creatures[1].speed,"Smaller body fails to accelerate faster");
    near(w.creatures[0].speed,3*std::pow(.5,-.5)*.1,"Wrong acceleration");
    for(int i=0;i<20;++i)w.step({move(),move()});
    for(const auto& c:w.creatures)near(c.speed,w.config.max_speed*std::pow(c.body.mass,.25),"Wrong terminal speed");
    check(w.creatures[1].speed>w.creatures[0].speed,"Larger body lacks higher top speed");
    const auto previous=w.creatures[1].speed;w.step({{}, {}});
    near(w.creatures[1].speed,previous-3*std::pow(2.,-.5)*.1,"Braking not acceleration limited");
    for(int i=0;i<20;++i)w.step({{}, {}});
    near(w.creatures[1].speed,0,"Idle body never stops");
    w.config.max_speed=0;w.step({move(),move()});near(w.creatures[0].speed,0,"Zero max speed ignored");
    auto wall=fixture();add(wall,2,0,{4.7,4.5});wall.terrain[4*wall.config.width+5]=Terrain::Wall;
    for(int i=0;i<30;++i)wall.step({move()});
    check(wall.creatures[0].position.x<5-wall.config.radius,"Acceleration passed through wall");
}
void checkpoint()
{
    auto w=fixture();add(w,2);w.creatures[0].energy=400;
    w.config.ingestion_mass_exponent=.9;w.config.pod_mass_exponent=.6;w.config.attack_mass_exponent=.7;
    w.config.metabolism_mass_exponent=.8;w.config.energy_mass_exponent=1.1;
    w.config.speed_mass_exponent=.3;w.config.acceleration_mass_exponent=-.6;w.config.max_acceleration=2;
    w.step({move()});std::istringstream input(saved(w));auto copy=EcosystemWorld::load_checkpoint(input);
    check(saved(w)==saved(copy),"Checkpoint lost allometry configuration or large reserves");
    for(int i=0;i<20;++i){w.step({move()});copy.step({move()});check(saved(w)==saved(copy),"Acceleration continuation diverged");}
    w=fixture();w.config.mass_allometry=false;add(w,2);
    auto old=without_allometry_header(saved(w));old.replace(0,21,"NEUROEVO_ECOSYSTEM_39");
    std::istringstream legacy(old);copy=EcosystemWorld::load_checkpoint(legacy);
    check(!copy.config.mass_allometry,"Legacy checkpoint enabled allometry");
    for(int i=0;i<10;++i){w.step({move()});copy.step({move()});check(saved(w)==saved(copy),"Legacy continuation changed");}
    near(copy.creatures[0].speed,w.config.max_speed/std::sqrt(2.),"Legacy speed changed");
    near(copy.config.max_energy(2),w.config.energy_capacity*(w.config.reproduction_cost+2*w.config.body_energy_per_mass)/(w.config.reproduction_cost+w.config.body_energy_per_mass),"Legacy reserves changed");
    auto high=fixture().config;high.founder_mass=2;high.founder_energy=600;high.energy_mass_exponent=1.5;
    EcosystemWorld high_capacity(high,false);std::istringstream high_input(saved(high_capacity));
    check(saved(high_capacity)==saved(EcosystemWorld::load_checkpoint(high_input)),"Capacity settings validated before loading allometry");
}
void validation()
{
    for(double bad:{-3.,3.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto c=fixture().config;c.ingestion_mass_exponent=bad;bool rejected=false;
        try{c.validate();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid exponent accepted");
    }
    auto c=fixture().config;c.max_acceleration=0;bool rejected=false;
    try{c.validate();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Zero acceleration accepted");
    auto w=fixture();w.config.set_predation(false);add(w,2);food(w);w.step({eat()});
    near(w.totals.consumed_biomass,.1,"Non-body world gained mass-dependent ingestion");
    near(w.config.max_energy(2),w.config.energy_capacity,"Non-body world gained mass-dependent reserves");
}
}
int main()try{feeding();strength();reserves();motion();checkpoint();validation();std::cout<<"Allometry tests passed\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
