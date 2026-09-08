#include "neuroevo/ecosystem.hpp"
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
    EcosystemConfig c;
    c.set_predation(true);
    // Mechanical expectations below use a controlled fixture, not tunable defaults.
    c.dt=0.1; c.health_per_mass=20;
    c.attack_damage=5; c.attack_cost=2;
    c.attack_base_fraction=1; // Isolate other combat rules from dietary strength.
    c.width=c.height=12; c.shelters=c.grazing_patches=c.fruit_patches=c.pods=c.initial_creatures=0;
    c.reproduction=c.storms_enabled=c.outdoor_food_relocates=false;
    c.basal_cost=c.movement_cost=c.turn_cost=c.forage_cost=c.call_cost=c.neuron_cost=c.synapse_cost=c.spike_cost=0;
    c.healing_rate=0; c.archive_eval_trials=0;
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
void combat()
{
    const auto nursery_attack = [](double attacker_x, double target_x, bool protected_target, double damage_rate) {
        auto cfg=empty().config;
        cfg.width=cfg.height=80; cfg.nursery_frontier=true;
        cfg.attack_damage=damage_rate;
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
    for (double damage_rate : {5.0,15.0,50.0,1000.0,EcosystemConfig{}.attack_damage}) {
        nursery_attack(40,40.6,true,damage_rate);
        nursery_attack(31.7,32.3,true,damage_rate); // Attacker outside, victim inside.
        nursery_attack(32.3,31.7,false,damage_rate); // Attacker inside, victim outside.
        nursery_attack(30,30.6,false,damage_rate);
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
    auto cfg=nursery_frontier_config(); cfg.initial_creatures=0; cfg.meat_decay=1;
    EcosystemWorld nursery(cfg,false); plant.stock=plant.capacity=0.01; plant.position={40,40};
    nursery.resources.push_back(plant); nursery.step();
    require(nursery.resources.empty() && nursery.totals.regrown_biomass==0,"Corpse regrew or relocated in nursery");
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
        cfg.mass_mutation_probability=cfg.carnivory_mutation_probability=1;
        cfg.mass_mutation_sigma=cfg.carnivory_mutation_sigma=0.7;
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
    w.config.mass_mutation_probability=w.config.carnivory_mutation_probability=0;
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
}
int main()
{
    try { combat(); dietary_attack_strength(); food_and_senses(); dietary_metabolism(); bodies_and_births(); std::cout<<"Predation tests passed\n"; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
