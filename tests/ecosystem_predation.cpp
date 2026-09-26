#include "fixtures.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace neuroevo;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a, double b, const char* message) { require(std::abs(a-b) < 1e-8, message); }
std::string saved(const EcosystemWorld& w) { std::ostringstream s; w.save_checkpoint(s); return s.str(); }
EcosystemWorld empty()
{
    EcosystemConfig c = neuroevo::controlled_config();
    c.set_predation(true);
    // Mechanical expectations below use a controlled fixture, not tunable defaults.
    c.dt=0.1; c.health_per_mass=20;
    c.body_energy_per_mass=30;
    c.attack_damage=5; c.attack_cost=2; c.attack_range=0.8; c.attack_degrees=60;
    c.attack_base_fraction=1; // Isolate other combat rules from dietary strength.
    c.width=c.height=12; c.shelters=c.grazing_patches=c.fruit_patches=c.pods=c.initial_creatures=0;
    c.reproduction=c.storms_enabled=c.outdoor_food_relocates=false;
    c.basal_cost=c.movement_cost=c.turn_cost=c.forage_cost=c.call_cost=c.neuron_cost=c.synapse_cost=c.spike_cost=0;
    c.healing_rate=0;
    return EcosystemWorld(c,false);
}
void add(EcosystemWorld& w, Vec2 p, double heading=0, BodyGenes body={})
{
    EcoCreature c;
    c.id=c.genome_id=w.next_creature_id++;
    c.position=p; c.heading=heading; c.energy=100; c.body=body;
    c.health=w.max_health(c); c.brain=make_sparse_ancestral_brain(w.config);
    w.creatures.push_back(c);
}
EcoAction attack() { EcoAction a; a.attack=1; return a; }
EcoAction eat() { EcoAction a; a.forage=1; return a; }
double ledger(const EcosystemWorld& w)
{
    double stored=0;
    for (const auto& c:w.creatures) {
        stored+=c.energy+w.config.body_energy_per_mass*c.body.mass;
        for (const auto& p:c.digestion) stored+=p.energy;
    }
    for (const auto& r:w.resources) stored+=r.stock*r.energy_per_unit;
    const auto& t=w.totals;
    return stored+t.attacking+t.healing+t.metabolism+t.movement+t.turning+t.foraging+t.calling
        +t.neural+t.exposure+t.reproduction_overhead+t.discarded_energy+t.meat_spoiled_energy;
}
void storm_health()
{
    auto cfg=empty().config;cfg.width=cfg.height=80;cfg.nursery_frontier=true;
    cfg.storms_enabled=true;cfg.phase_offset=cfg.calm_duration+cfg.warning_duration;
    cfg.storm_health_damage=true;cfg.storm_damage=10;cfg.storm_cost=99;cfg.healing_rate=10;
    EcosystemWorld w(cfg,false);
    add(w,{10.5,10.5},0,{1,0}); add(w,{15.5,10.5},0,{2,0});
    add(w,{40,40});add(w,{20.5,10.5});
    w.terrain[40*cfg.width+40]=w.terrain[10*cfg.width+20]=Terrain::Shelter;
    for(auto& c:w.creatures)c.health=10;
    const auto initial=ledger(w);w.step({{},{},{},{}});
    near(w.creatures[0].health,9,"Storm health damage missing or healed immediately");
    near(w.creatures[1].health,9,"Storm health damage must be independent of mass");
    near(w.creatures[2].health,11,"Nursery failed to protect from storm damage");
    near(w.creatures[3].health,11,"Shelter failed to protect from storm damage");
    near(w.creatures[0].damage_pulse,1,"Storm did not set damage feedback");
    near(w.creatures[0].energy,100,"Health storm drained energy");
    near(w.totals.exposure,0,"Health storm recorded energy exposure");near(ledger(w),initial,"Storm health mode broke energy ledger");
    std::istringstream stream(saved(w));auto copy=EcosystemWorld::load_checkpoint(stream);
    w.step({{},{},{},{}});copy.step({{},{},{},{}});
    require(saved(w)==saved(copy),"Storm mode checkpoint continuation diverged");
    w.creatures[0].health=0.1;w.creatures[0].digestion.push_back({0,4,FoodKind::Graze});
    const auto before=ledger(w);w.step({{},{},{},{}});
    require(w.creatures.size()==3 && w.totals.predation_deaths==0,"Storm death counted as predation or victim survived");
    require(!w.resources.empty() && w.resources.back().kind==FoodKind::Meat,"Storm victim did not leave corpse");
    near(ledger(w),before,"Lethal storm duplicated energy");
    auto energy=empty();energy.config.storms_enabled=true;energy.config.phase_offset=cfg.phase_offset;
    energy.config.storm_cost=10;add(energy,{3,3});energy.step({{}});
    near(energy.creatures[0].energy,99,"Default energy storm changed");near(energy.creatures[0].health,20,"Energy storm damaged health");
    energy.config.storm_health_damage=true;energy.config.storm_damage=0;energy.step({{}});
    near(energy.creatures[0].energy,99,"Zero-damage storm drained energy");
    energy.config.storm_damage=10;energy.config.storms_enabled=false;energy.step({{}});
    near(energy.creatures[0].health,20,"Disabled storm damaged health");
    for(bool invalid_mode:{false,true}) {
        auto invalid=cfg;if(invalid_mode)invalid.set_predation(false);else invalid.storm_damage=-1;
        bool rejected=false;try{invalid.validate();}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Invalid storm configuration accepted");
    }
}

void storm_survival_scales_linearly()
{
    auto w=empty();
    w.config.storms_enabled=true;
    w.config.phase_offset=w.config.calm_duration+w.config.warning_duration;
    w.config.storm_health_damage=true;w.config.storm_damage=10;
    add(w,{3,3},0,{0.5,0});add(w,{5,3},0,{1,0});add(w,{7,3},0,{2,0});
    for(int step=1;step<=40;++step) {
        w.step(std::vector<EcoAction>(w.creatures.size()));
        const std::size_t expected=(step<10 ? 1 : 0)+(step<20 ? 1 : 0)+(step<40 ? 1 : 0);
        require(w.creatures.size()==expected,"Storm survival time must scale linearly with mass");
    }
    require(w.totals.deaths==3 && w.totals.predation_deaths==0,"Storm survival deaths misclassified");
}

void shelter_damage()
{
    // Exercise both shelter policies independently of the tunable default.
    for(bool enabled:{false,true}) for(bool attacker_inside:{false,true})
        for(bool target_inside:{false,true}) for(bool lethal:{false,true}) {
        auto w=empty();w.config.shelter_predation_damage=enabled;
        add(w,{3.7,3.5});add(w,{4.3,3.5});
        if(attacker_inside)w.terrain[3*w.config.width+3]=Terrain::Shelter;
        if(target_inside)w.terrain[3*w.config.width+4]=Terrain::Shelter;
        if(lethal)w.creatures[1].health=0.25;
        const double health=w.creatures[1].health;
        const double damage=(!enabled&&target_inside)?0:std::min(health,0.5);
        std::istringstream input(saved(w));auto resumed=EcosystemWorld::load_checkpoint(input);
        w.step({attack(),{}});resumed.step({attack(),{}});
        require(saved(w)==saved(resumed),"Shelter damage policy lost on checkpoint resume");
        require(w.creatures.size()==(damage>=health?1u:2u),"Shelter protection used the wrong creature or ignored its setting");
        if(w.creatures.size()==2) {
            near(w.creatures[1].health,health-damage,"Shelter damage used attacker's shelter state");
            near(w.creatures[1].damage_pulse,damage,"Shelter damage feedback incorrect");
        }
        near(w.totals.damage,damage,"Shelter damage totals incorrect");
        require(w.totals.predation_deaths==(damage>=health?1u:0u),"Shelter protection failed on lethal attack");
        near(w.totals.attacking,0.2,"Shelter protection removed attack cost");
        const auto hit=std::find_if(w.events.begin(),w.events.end(),[](const auto& e){return e.type=="attack_hit";});
        require(hit!=w.events.end(),"Shelter attack event missing");
        near(hit->amount,(!enabled&&target_inside)?0:0.5,"Shelter attack event recorded wrong damage");
    }
    for(bool entering:{false,true}) {
        auto w=empty();w.config.shelter_predation_damage=false;w.config.max_speed=2;
        w.terrain[3*w.config.width+4]=Terrain::Shelter;
        const double heading=entering?0:3.141592653589793;
        add(w,{entering?3.3:4.7,3.5},heading);
        add(w,{entering?3.9:4.1,3.5},heading);
        EcoAction move;move.forward=1;
        w.step({attack(),move});
        require(w.sheltered(w.creatures[1].position)==entering,"Target failed to cross shelter boundary");
        near(w.creatures[1].health,entering?20:19.5,"Shelter protection used target's position before movement");
    }
}

void mass_energy_capacity()
{
    for(double mass:{0.5,0.75,1.0,1.5,2.0}) {
        auto w=empty();w.config.mass_scaled_energy_capacity=true;
        w.config.energy_capacity=250;w.config.body_energy_per_mass=60;w.config.reproduction_cost=60;
        w.config.reproduction_threshold=180;w.config.offspring_energy=60;
        add(w,{3,3},0,{mass,0});
        const double capacity=125*(1+mass);
        near(w.config.max_energy(mass),capacity,"Mass energy capacity used wrong cost scaling");
        require(capacity>w.config.reproduction_threshold,"Threshold exceeds capacity");
        w.creatures[0].energy=capacity-1;
        w.creatures[0].digestion.push_back({0,10,FoodKind::Graze});
        const auto before=ledger(w);w.step({{}});
        near(w.creatures[0].energy,capacity,"Digestion did not enforce individual capacity");
        near(w.totals.discarded_energy,9,"Overflow energy not discarded");
        near(ledger(w),before,"Capacity change created energy");
        const auto energy_input=eco_sectors*eco_sector_channels+8;
        near(w.observe(0)[energy_input],1,"Energy sensor did not normalize by mass capacity");
        std::istringstream checkpoint(saved(w));auto resumed=EcosystemWorld::load_checkpoint(checkpoint);
        require(saved(w)==saved(resumed),"Mass capacity checkpoint failed");
        w.step({{}});resumed.step({{}});
        require(saved(w)==saved(resumed),"Mass capacity resume diverged");
    }
    auto cfg=empty().config;cfg.mass_scaled_energy_capacity=true;cfg.set_predation(false);
    near(cfg.max_energy(2),cfg.energy_capacity,"Non-predation capacity changed");
}

void combat()
{
    const auto nursery_attack = [](double attacker_x, double target_x, bool protected_target, double damage_rate, bool shelter_damage) {
        auto cfg=empty().config;
        cfg.width=cfg.height=80; cfg.nursery_frontier=true;
        cfg.attack_damage=damage_rate;
        cfg.shelter_predation_damage=shelter_damage;
        EcosystemWorld nursery(cfg,false); // Open ground isolates gate-crossing geometry.
        add(nursery,{attacker_x,40},attacker_x<target_x ? 0 : 3.141592653589793);
        add(nursery,{target_x,40});
        const double initial_health=nursery.creatures[1].health;
        const double expected_damage=protected_target ? 0 : std::min(initial_health,cfg.attack_damage*cfg.dt);
        nursery.step({attack(),{}});
        if (expected_damage>=initial_health) {
            require(nursery.creatures.size()==1 && nursery.totals.predation_deaths==1,
                "Lethal attack on an outside target failed");
        } else {
            require(nursery.creatures.size()==2,"Nursery protection failed to prevent a lethal hit");
            near(nursery.creatures[1].health,initial_health-expected_damage,"Nursery damage protection follows wrong position");
            near(nursery.creatures[1].damage_pulse,expected_damage,"Protected attack generated injury feedback");
        }
        near(nursery.totals.damage,expected_damage,"Protected damage entered totals");
        near(nursery.totals.attacking,0.2,"Nursery attacks must still consume energy");
        near(nursery.creatures[0].energy,99.8,"Nursery attacker did not pay for effort");
        near(nursery.creatures[0].action.attack,1,"Nursery protection disabled attack output");
    };
    for (bool shelter_damage : {false,true}) for (double damage_rate : {5.0,15.0,50.0,1000.0,EcosystemConfig{}.attack_damage}) {
        nursery_attack(40,40.6,true,damage_rate,shelter_damage);
        nursery_attack(31.7,32.3,true,damage_rate,shelter_damage); // Attacker outside, victim inside.
        nursery_attack(32.3,31.7,false,damage_rate,shelter_damage); // Attacker inside, victim outside.
        nursery_attack(30,30.6,false,damage_rate,shelter_damage);
    }

    auto w=empty(); add(w,{3,3}); add(w,{3.6,3},3.141592653589793);
    const double initial=ledger(w);
    w.creatures[0].health=w.creatures[1].health=0.4;
    auto reverse=w; std::reverse(reverse.creatures.begin(),reverse.creatures.end());
    w.step({attack(),attack()}); reverse.step({attack(),attack()});
    require(w.creatures.empty() && w.resources.size()==2,"Simultaneous attacks must allow mutual kills");
    require(saved(w)==saved(reverse),"Combat/corpse creation depends on creature vector order");
    near(w.totals.attacking,0.4,"Attack costs must be paid for both attacks");
    near(ledger(w),initial,"Combat or corpses created energy");
    require(w.totals.predation_deaths==2,"Missing predation death accounting");

    w=empty(); add(w,{3,3}); add(w,{3.6,3});
    w.creatures[0].energy=0.1;
    w.step({attack(),{}});
    near(w.creatures.back().health,19.75,"Damage must scale with affordable attack effort");

    for (const auto p : {Vec2{2.4,3},Vec2{3,3.6},Vec2{4,3}}) {
        w=empty(); add(w,{3,3}); add(w,p); w.step({attack(),{}});
        near(w.creatures[1].health,20,"Attack reached behind, outside cone or beyond range");
        near(w.creatures[0].energy,99.8,"Missed attack must cost energy");
    }
    w=empty(); w.config.attack_range=3; add(w,{2.5,3.5}); add(w,{4.5,3.5});
    w.terrain[3*w.config.width+3]=Terrain::Wall; w.step({attack(),{}});
    near(w.creatures[1].health,20,"Attacks must not pass through walls");
    w=empty(); add(w,{3,3}); add(w,{3.55,3}); add(w,{4.1,3}); w.config.attack_range=2;
    w.step({attack(),{}, {}});
    near(w.creatures[1].health,19.5,"Attack missed closest target");
    near(w.creatures[2].health,20,"One attack damaged multiple targets");
    auto half=empty(); add(half,{3,3}); add(half,{3.6,3}); w=half;
    w.config.dt=0.2;
    half.step({attack(),{}}); half.step({attack(),{}}); w.step({attack(),{}});
    near(half.creatures[1].health,w.creatures[1].health,"Attack damage depends on timestep");
    near(half.creatures[0].energy,w.creatures[0].energy,"Attack cost depends on timestep");
    w=empty(); add(w,{3,3}); add(w,{3.6,3}); w.config.healing_rate=100;
    w.config.basal_cost=1; w.creatures[1].health=0.4;
    w.creatures[1].digestion.push_back({0,10,FoodKind::Meat});
    const auto funded=ledger(w); w.step({attack(),{}});
    require(w.creatures.size()==1,"Healing revived a lethal attack victim");
    near(w.totals.energy_gained,0,"Killed creature digested a due packet");
    near(w.totals.metabolism,0.2,"Killed creature escaped costs already incurred this step");
    near(ledger(w),funded,"Lethal damage duplicated or lost pending energy");
}
void dietary_attack_strength()
{
    struct Case { double carnivory, damage; };
    for (const auto test : {Case{0,0.125}, Case{0.01,0.12875}, Case{0.5,0.3125}, Case{1,0.5}}) {
        auto w=empty(); w.config.attack_base_fraction=0.25;
        add(w,{3,3},0,{1,test.carnivory}); add(w,{3.6,3},0,{1,1});
        const auto initial=ledger(w);
        w.step({attack(),{}});
        near(w.creatures[1].health,20-test.damage,"Damage must scale with the attacker's carnivory");
        near(w.totals.damage,test.damage,"Diet-scaled damage missing from totals");
        near(w.totals.attacking,0.2,"Carnivory must not discount attack energy cost");
        near(ledger(w),initial,"Diet-scaled attacks created energy");
        w.creatures[0].energy=0.1; // Only half of a full attack is affordable.
        w.step({attack(),{}});
        require(w.creatures.size()==1,"Energy-exhausted attacker survived");
        near(w.creatures[0].health,20-1.5*test.damage,"Diet strength bypassed affordable attack effort");
    }
    auto w=empty(); w.config.attack_base_fraction=0.4;
    add(w,{3,3}); add(w,{3.6,3});
    w.step({attack(),{}});
    near(w.creatures[1].health,19.8,"Configured herbivore baseline was ignored");
    std::istringstream checkpoint(saved(w)); auto resumed=EcosystemWorld::load_checkpoint(checkpoint);
    near(resumed.config.attack_base_fraction,0.4,"Checkpoint lost attack baseline");
    for (int i=0;i<3;++i) { w.step({attack(),{}}); resumed.step({attack(),{}}); }
    require(saved(w)==saved(resumed),"Diet-scaled attacks diverged after resume");
}
void general_food_senses()
{
    auto w=empty(); add(w,{3,3});
    const auto sector=eco_sectors/2, food=sector*eco_sector_channels+2;
    EcoResource plant; plant.id=1; plant.position={5,3}; plant.stock=plant.capacity=1;
    EcoResource meat=plant; meat.id=2; meat.kind=FoodKind::Meat; meat.position={4,3};
    w.resources={plant,meat};
    const auto check=[&](double plant_distance,double meat_distance) {
        const auto inputs=w.observe(0);
        const double plants=plant_distance<0 ? 0 : 1-plant_distance/w.config.vision_range;
        const double meats=meat_distance<0 ? 0 : 1-meat_distance/w.config.vision_range;
        near(inputs[food],std::max(plants,meats),"General food proximity missed nearest food");
        near(inputs[eco_plant_offset+sector],plants,"Plant proximity included meat");
        near(inputs[eco_meat_offset+eco_sectors+sector],meats,"Meat proximity changed");
    };
    check(2,1);
    std::swap(w.resources[0],w.resources[1]); check(2,1);
    w.resources[0].position={6,3}; check(2,3);
    w.resources[1].stock=0; check(-1,3);
    w.resources[0].stock=0; check(-1,-1);
    w.resources[0].stock=1; w.resources[0].position={2,3}; check(-1,-1);
    const auto labels=ecosystem_input_labels(true,true,true);
    require(labels.size()==eco_predation_input_count && labels[eco_plant_offset+sector]=="vision_1_plant_proximity",
        "Plant sensor label/layout mismatch");
    std::vector<int> coverage(eco_predation_input_count);
    for (const auto& group : ecosystem_input_groups(true,true)) for (auto input : group) ++coverage.at(input);
    for (auto count : coverage) require(count==1,"Sensor mutation groups do not partition inputs");
    const auto brain=make_sparse_ancestral_brain(w.config);
    bool food_connected=false;
    for (const auto& edge : brain.synapses()) {
        if (edge.pre==food) food_connected=true;
        require(edge.pre<eco_plant_offset || edge.pre>=eco_predation_input_count,
            "Ancestor unexpectedly connects plant-only sensor");
    }
    require(food_connected,"Ancestor lacks general food wiring");
    std::string old=saved(w); old.replace(0,std::string("NEUROEVO_ECOSYSTEM_27").size(),"NEUROEVO_ECOSYSTEM_26");
    bool rejected=false;
    try { std::istringstream input(old); EcosystemWorld::load_checkpoint(input); }
    catch (const std::runtime_error& e) { rejected=std::string(e.what()).find("old food sensor layout")!=std::string::npos; }
    require(rejected,"Old predation sensor layout was not clearly rejected");
}

void food_and_senses()
{
    auto w=empty(); add(w,{3,3},0,{1,1}); add(w,{3.6,3});
    w.creatures[1].health=0.4;
    EcoResource plant; plant.id=1; plant.position={3.1,3}; plant.stock=plant.capacity=2; plant.energy_per_unit=10;
    w.resources.push_back(plant);
    auto both=attack(); both.forage=1;
    const auto initial=ledger(w);
    w.step({both,{}});
    require(w.creatures.front().digestion.empty(),"A new corpse was eaten in its creation tick");
    const auto seen=w.observe(0);
    require(seen[(eco_sectors/2)*eco_sector_channels+1]==1 && seen[eco_meat_offset+eco_sectors/2]==1,"Plant hid meat in the same sector");
    const double amount=seen[eco_meat_offset+2*eco_sectors+eco_sectors/2];
    w.resources.back().stock*=0.5; w.resources.back().capacity*=0.5;
    near(w.observe(0)[eco_meat_offset+2*eco_sectors+eco_sectors/2],amount*0.5,"Meat amount uses per-corpse capacity instead of a shared scale");
    w.resources.back().stock*=2; w.resources.back().capacity*=2;
    w.step({eat()});
    near(w.resources.front().stock,2,"Pure carnivore consumed blocking plant");
    require(w.creatures.front().digestion.size()==1 && w.creatures.front().digestion[0].kind==FoodKind::Meat,"Meat did not use digestion queue");
    near(ledger(w),initial,"Eating/decay failed energy accounting");
    std::istringstream checkpoint(saved(w)); auto resumed=EcosystemWorld::load_checkpoint(checkpoint);
    require(saved(w)==saved(resumed),"Checkpoint lost predation state");
    for (int i=0;i<60;++i) { w.step({eat()}); resumed.step({eat()}); }
    require(saved(w)==saved(resumed),"Predation continuation diverged");
    near(ledger(w),initial,"Digestion created energy");
    require(w.creatures[0].energy_gained>0,"Meat energy never arrived");

    w=empty(); add(w,{3,3},0,{1,0.25});
    near(w.dietary_efficiency(w.creatures[0],FoodKind::Meat),0.25,"Wrong meat efficiency");
    near(w.dietary_efficiency(w.creatures[0],FoodKind::FruitA),0.75,"Wrong plant efficiency");
    w.resources.push_back(plant); w.step({eat()});
    near(w.creatures[0].digestion[0].energy,0.75,"Diet not applied to queued energy");
    near(w.totals.discarded_energy,0.25,"Digestive inefficiency disappeared from ledger");
    for (double carnivory : {0.0,0.01,0.5,1.0}) {
        auto passing=empty(); add(passing,{3,3},0,{1,carnivory});
        EcoResource meat; meat.id=1; meat.kind=FoodKind::Meat; meat.position={3.3,3};
        meat.stock=meat.capacity=1; meat.energy_per_unit=passing.config.meat_energy;
        passing.resources.push_back(meat);
        const auto initial_energy=ledger(passing);
        auto moving_eat=eat(); moving_eat.forward=1;
        passing.step({moving_eat});
        const double bite=passing.config.ingestion_rate*passing.config.dt*carnivory;
        near(passing.creatures[0].eaten[4],bite,"Passing creature consumed meat faster than its carnivory permits");
        near(passing.resources[0].stock,1-bite-passing.config.meat_decay*passing.config.dt,
            "Low-carnivory eater removed excess meat from the environment");
        near(passing.creatures[0].energy_gained,0,"Meat bypassed digestion delay");
        if (carnivory>0) {
            near(passing.creatures[0].digestion[0].due,passing.time()+passing.config.digestion_delay,
                "Carnivory changed digestion delay instead of intake rate");
            near(passing.creatures[0].digestion[0].energy,bite*meat.energy_per_unit*carnivory,
                "Slower meat consumption changed digestive efficiency");
        } else require(passing.creatures[0].digestion.empty(),"Herbivore consumed meat");
        near(ledger(passing),initial_energy,"Carnivory-scaled intake broke energy conservation");
    }
    // Shared meat uses the existing proportional ingestion allocation.
    w=empty(); add(w,{3,3},0,{1,1}); add(w,{3.6,3},3.141592653589793,{1,1});
    plant.kind=FoodKind::Meat; plant.position={3.3,3}; plant.stock=plant.capacity=0.1;
    w.resources.push_back(plant); w.step({eat(),eat()});
    near(w.creatures[0].eaten[4],0.05,"First eater received unfair corpse share");
    near(w.creatures[1].eaten[4],0.05,"Second eater received unfair corpse share");
    require(w.resources.empty(),"Depleted meat was not removed");
    w=empty(); add(w,{3,3},0,{1,0.01}); add(w,{3.6,3},3.141592653589793,{1,1});
    w.resources.push_back(plant); w.step({eat(),eat()});
    near(w.creatures[0].eaten[4],0.1*0.01/1.01,"Shared corpse allocation ignored low carnivory");
    near(w.creatures[1].eaten[4],0.1/1.01,"Carnivore lost meat to an oversized low-carnivory request");
    // A tiny corpse inside relocating nursery terrain must decay away permanently.
    auto cfg=EcosystemConfig{}; cfg.initial_creatures=0; cfg.width=cfg.height=80; cfg.nursery_meat_decay=1;
    EcosystemWorld nursery(cfg,false); plant.stock=plant.capacity=0.01; plant.position={40,40};
    nursery.resources.push_back(plant); nursery.step();
    require(nursery.resources.empty() && nursery.totals.regrown_biomass==0,"Corpse regrew or relocated in nursery");
}
void regional_meat_decay()
{
    for (bool storm : {false,true}) {
        auto cfg=empty().config; cfg.width=cfg.height=80; cfg.nursery_frontier=true;
        cfg.meat_decay=0.2; cfg.nursery_meat_decay=0.6;
        cfg.storms_enabled=storm; cfg.phase_offset=cfg.calm_duration+cfg.warning_duration;
        EcosystemWorld w(cfg,false);
        for (double x : {40.0,31.5,30.5,32.0,48.0,41.0}) {
            EcoResource meat; meat.id=w.next_resource_id++; meat.kind=FoodKind::Meat;
            meat.position={x,40.5}; meat.stock=meat.capacity=x==41 ? 0.001 : 1;
            meat.energy_per_unit=20; w.resources.push_back(meat);
        }
        w.terrain[40*cfg.width+30]=Terrain::Shelter;
        const auto initial=ledger(w);
        w.step();
        require(w.resources.size()==5,"Regional decay did not remove depleted corpse");
        for (std::size_t i=0;i<5;++i)
            near(w.resources[i].stock,(i==0 || i==3) ? 0.94 : 0.98,
                "Meat decay used wrong region, shelter rule, or nursery boundary");
        near(ledger(w),initial,"Regional meat decay broke energy accounting");
        near(w.totals.regrown_biomass,0,"Regional meat decay regenerated biomass");
        std::istringstream checkpoint(saved(w)); auto resumed=EcosystemWorld::load_checkpoint(checkpoint);
        near(resumed.config.nursery_meat_decay,0.6,"Checkpoint lost nursery meat decay");
        near(resumed.config.meat_decay,0.2,"Checkpoint lost outside meat decay");
        w.step(); resumed.step();
        require(saved(w)==saved(resumed),"Regional meat decay diverged on resume");
        w.config.nursery_meat_decay=0; w.config.meat_decay=0;
        w.config.validate();
        const double remaining=ledger(w), nursery_stock=w.resources[0].stock, outside_stock=w.resources[1].stock;
        w.step();
        near(w.resources[0].stock,nursery_stock,"Zero nursery decay did not disable spoilage");
        near(w.resources[1].stock,outside_stock,"Zero outside decay did not disable spoilage");
        near(ledger(w),remaining,"Disabled meat decay changed energy");
    }
}

void dietary_metabolism()
{
    for (double mass : {0.5,1.0,2.0}) for (double carnivory : {0.0,0.5,1.0}) {
        auto w=empty(); w.config.basal_cost=1; w.config.carnivore_basal_fraction=0.5;
        w.config.movement_cost=0.3;
        add(w,{3,3},0,{mass,carnivory});
        const double before=ledger(w);
        EcoAction move; move.forward=1;
        w.step({move});
        const double expected=0.1*mass*(carnivory==0 ? 1 : carnivory==0.5 ? 0.75 : 0.5);
        near(w.totals.metabolism,expected,"Basal metabolism must decrease linearly with carnivory and scale with mass");
        near(w.totals.movement,0.03,"Dietary basal discount changed movement cost");
        near(w.creatures[0].energy,100-expected-0.03,"Dietary metabolism debit is incorrect");
        near(ledger(w),before,"Dietary metabolism broke energy conservation");
    }
    auto w=empty(); w.config.basal_cost=1; w.config.carnivore_basal_fraction=0.2;
    add(w,{3,3},0,{1,1});
    w.step({{}});
    near(w.totals.metabolism,0.02,"Custom carnivore basal fraction was ignored");
    std::istringstream checkpoint(saved(w)); auto resumed=EcosystemWorld::load_checkpoint(checkpoint);
    near(resumed.config.carnivore_basal_fraction,0.2,"Checkpoint lost carnivore basal fraction");
    w.step({{}}); resumed.step({{}});
    require(saved(w)==saved(resumed),"Dietary metabolism diverged on resume");
    w.creatures[0].energy=0.005;
    const double before=ledger(w), spent=w.totals.metabolism;
    w.step({{}});
    require(w.creatures.empty(),"Dietary discount made exhausted creature immortal");
    near(w.totals.metabolism-spent,0.005,"Metabolic debit exceeded available reserve");
    near(ledger(w),before,"Discounted starvation created energy");
    w=empty(); w.config.set_predation(false); w.config.basal_cost=1;
    add(w,{3,3},0,{2,1}); w.step({{}});
    near(w.totals.metabolism,0.1,"Disabled body mechanics changed historical metabolism");
}
void bodies_and_births()
{
    for (double mass : {0.5,1.0,2.0}) for (bool storm : {false,true}) for (bool shelter : {false,true}) {
        auto weather_world=empty(); add(weather_world,{3.5,3.5},0,{mass,0});
        weather_world.config.storms_enabled=storm;
        weather_world.config.phase_offset=weather_world.config.calm_duration+weather_world.config.warning_duration;
        if (shelter) weather_world.terrain[3*weather_world.config.width+3]=Terrain::Shelter;
        const double initial=ledger(weather_world);
        const double drain=storm && !shelter ? weather_world.config.storm_cost*weather_world.config.dt/mass : 0;
        weather_world.step({{}});
        near(weather_world.creatures[0].energy,100-drain,"Storm drain must scale inversely with mass only when exposed");
        near(weather_world.totals.exposure,drain,"Mass-scaled storm cost missing from totals");
        near(weather_world.creatures[0].exposed_time,storm && !shelter ? weather_world.config.dt : 0,
            "Body mass changed exposure duration");
        near(ledger(weather_world),initial,"Storm protection broke energy conservation");
    }
    auto w=empty(); add(w,{3,3},0,{2,0}); w.config.basal_cost=1;
    near(w.max_health(w.creatures[0]),40,"Health must scale with mass");
    EcoAction move; move.forward=1; w.step({move});
    near(w.creatures[0].speed,w.config.max_speed/std::sqrt(2.0),"Speed must scale with inverse square root mass");
    near(w.totals.metabolism,0.2,"Metabolism must scale with mass");
    w.config.healing_rate=1; w.creatures[0].health=30;
    const auto initial=ledger(w); w.step({{}});
    near(w.creatures[0].health,30.1,"Health failed to regenerate");
    near(ledger(w),initial,"Healing created energy");
    // Mutated offspring bodies are charged at actual child mass, not parent's mass.
    int mass_changes=0, diet_changes=0, copies=0;
    for (int seed=0;seed<80;++seed) {
        w=empty(); w.mutation_rng=Random(seed); add(w,{5,5},0,{1.5,0.6});
        auto& cfg=w.config; cfg.reproduction=true; cfg.maturity_age=0; cfg.reproduction_cooldown=100;
        cfg.mutation.mass_mutation_probability=cfg.mutation.carnivory_mutation_probability=1;
        cfg.mutation.mass_mutation_sigma=cfg.mutation.carnivory_mutation_sigma=0.7;
        w.creatures[0].energy=200;
        const auto before=ledger(w); w.step({{}});
        require(w.creatures.size()==2,"Funded birth failed");
        const auto& child=w.creatures[1];
        near(w.totals.body_construction,cfg.body_energy_per_mass*child.body.mass,"Child body construction was not charged");
        require(child.body.mass>=eco_min_mass && child.body.mass<=eco_max_mass
            && child.body.carnivory>=0 && child.body.carnivory<=1,"Mutation escaped trait bounds");
        near(child.health,w.max_health(child),"Child inherited injury");
        mass_changes+=child.body.mass!=1.5;
        diet_changes+=child.body.carnivory!=0.6;
        if (child.genome_id==w.creatures[0].genome_id) {
            ++copies;
            near(child.body.mass,1.5,"Exact clone mutated mass"); near(child.body.carnivory,0.6,"Exact clone mutated diet");
        }
        near(ledger(w),before,"Birth created body energy");
        // Starve the offspring. The structural corpse was paid for at birth.
        w.creatures[1].energy=0; // Account for deliberately removed reserve.
        const auto after_removal=ledger(w); w.step({{}, {}});
        near(ledger(w),after_removal,"Starvation corpse created energy");
    }
    require(mass_changes>10 && diet_changes>10 && copies>0,"Births did not exercise body mutations and exact copies");
    w=empty(); add(w,{5,5}); w.config.reproduction=true; w.config.maturity_age=0;
    w.config.reproduction_cost=w.config.reproduction_threshold=100;
    w.config.mutation.mass_mutation_probability=w.config.mutation.carnivory_mutation_probability=0;
    w.creatures[0].energy=110; w.step({{}});
    require(w.creatures.size()==1 && w.totals.births==0,"Birth proceeded without construction energy");
    w.creatures[0].energy=130;
    const auto funded=ledger(w); w.step({{}});
    require(w.totals.births==1 && w.totals.deaths==1 && w.creatures.size()==1 && w.resources.size()==1,
        "Parent exhausted by birth failed to leave one corpse");
    near(ledger(w),funded,"Birth-boundary parent death created energy");
    Random rng(7); const BodyGenes parent{1.3,0.7};
    for (int i=0;i<20;++i) {
        const auto clone=w.inherit_body(parent,true,rng);
        near(clone.mass,parent.mass,"Zero mass mutation changed genes");
        near(clone.carnivory,parent.carnivory,"Zero diet mutation changed genes");
    }
    const auto groups=ecosystem_input_groups(true,true);
    std::vector<int> coverage(eco_predation_input_count);
    for (const auto& g:groups) for (auto i:g) ++coverage.at(i);
    require(std::all_of(coverage.begin(),coverage.end(),[](int n){return n==1;}),"Sensory mutation groups do not partition new inputs");
    const auto brain=make_sparse_ancestral_brain(w.config);
    const auto attack_index=brain.neurons().size()-1;
    require(brain.config().output_count==6 && brain.neurons()[attack_index].background_sensitivity==0
        && std::none_of(brain.synapses().begin(),brain.synapses().end(),[&](auto e){return e.post==attack_index;}),
        "Ancestral attack output should start silent and disconnected");
    auto mutated=brain;
    auto mutation=w.config.mutation;
    mutation.add_synapse_probability=1;
    mutation.add_neuron_probability=mutation.remove_neuron_probability=mutation.remove_synapse_probability=0;
    bool wired=false;
    for (int i=0;i<500 && !wired;++i) {
        mutated.mutate(mutation,rng,groups);
        wired=std::any_of(mutated.synapses().begin(),mutated.synapses().end(),[&](auto e){return e.post==attack_index;});
    }
    require(wired,"Structural mutation cannot connect the attack output");
}
void configurable_inheritance()
{
    // Force each branch so tuning the mixture cannot be masked by chance.
    for (int branch=0; branch<3; ++branch) {
        auto w=empty();
        w.config.reproduction=true; w.config.maturity_age=0;
        w.config.reproduction_threshold=100;
        auto& mutation=w.config.mutation;
        mutation.copy_probability=branch==0 ? 1 : 0;
        mutation.slight_probability=branch==1 ? 1 : 0;
        mutation.disconnected_neuron_prune_probability=0;
        mutation.mass_mutation_probability=1;
        mutation.carnivory_mutation_probability=0;
        mutation.slight.body_probability_scale=0;
        mutation.strong.body_probability_scale=1;
        mutation.strong.body_sigma_scale=2;
        mutation.strong.structural_probability=0;
        mutation.strong.local_edits=0;
        add(w,{5,5}); w.creatures[0].energy=200;
        w.config.validate();
        const auto parent=w.creatures.front().brain;
        w.step({{}});
        require(w.creatures.size()==2,"Configured inheritance failed to produce a child");
        const auto& child=w.creatures.back();
        if (branch==2) require(child.body.mass!=1,"Strong body profile was ignored");
        else near(child.body.mass,1,"Copy or disabled slight body profile mutated mass");
        if (branch==0) {
            require(child.genome_id==w.creatures.front().genome_id,"Frozen inheritance changed genome identity");
            require(child.brain.synapses().size()==parent.synapses().size(),"Frozen inheritance changed topology");
            for (std::size_t i=0;i<parent.synapses().size();++i)
                near(child.brain.synapses()[i].weight,parent.synapses()[i].weight,"Frozen inheritance changed a weight");
        }
    }
    for (int invalid=0; invalid<4; ++invalid) {
        auto c=empty().config;
        if (invalid==0) c.mutation.copy_probability=0.9; // Sum exceeds one.
        if (invalid==1) c.mutation.strong.structural_probability=1.1;
        if (invalid==2) c.mutation.slight.body_sigma_scale=-1;
        if (invalid==3) c.mutation.disconnected_neuron_prune_probability=-0.1;
        bool rejected=false;
        try { c.validate(); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"Invalid inheritance tuning was accepted");
    }
}
}
int main()
{
    try { mass_energy_capacity(); storm_health(); storm_survival_scales_linearly(); shelter_damage(); general_food_senses(); configurable_inheritance(); combat(); dietary_attack_strength(); food_and_senses(); regional_meat_decay(); dietary_metabolism(); bodies_and_births(); std::cout<<"Predation tests passed\n"; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
