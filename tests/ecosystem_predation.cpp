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
    require(seen[2*eco_sector_channels+1]==1 && seen[eco_meat_offset+2]==1,"Plant hid meat in the same sector");
    const double amount=seen[eco_meat_offset+2*eco_sectors+2];
    w.resources.back().stock*=0.5; w.resources.back().capacity*=0.5;
    near(w.observe(0)[eco_meat_offset+2*eco_sectors+2],amount*0.5,"Meat amount uses per-corpse capacity instead of a shared scale");
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
    // Shared meat uses the existing proportional ingestion allocation.
    w=empty(); add(w,{3,3},0,{1,1}); add(w,{3.6,3},3.141592653589793,{1,1});
    plant.kind=FoodKind::Meat; plant.position={3.3,3}; plant.stock=plant.capacity=0.1;
    w.resources.push_back(plant); w.step({eat(),eat()});
    near(w.creatures[0].eaten[4],0.05,"First eater received unfair corpse share");
    near(w.creatures[1].eaten[4],0.05,"Second eater received unfair corpse share");
    require(w.resources.empty(),"Depleted meat was not removed");
    // A tiny corpse inside relocating nursery terrain must decay away permanently.
    auto cfg=nursery_frontier_config(); cfg.initial_creatures=0; cfg.meat_decay=1;
    EcosystemWorld nursery(cfg,false); plant.stock=plant.capacity=0.01; plant.position={40,40};
    nursery.resources.push_back(plant); nursery.step();
    require(nursery.resources.empty() && nursery.totals.regrown_biomass==0,"Corpse regrew or relocated in nursery");
}
void bodies_and_births()
{
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
    try { combat(); food_and_senses(); bodies_and_births(); std::cout<<"Predation tests passed\n"; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
