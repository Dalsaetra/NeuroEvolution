#include "fixtures.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace neuroevo;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void near(double a,double b,const char* m){if(std::abs(a-b)>1e-8)throw std::runtime_error(std::string(m)+": "+std::to_string(a)+" != "+std::to_string(b));}
std::string saved(const EcosystemWorld& w){std::ostringstream s;w.save_checkpoint(s);return s.str();}
EcosystemWorld fixture() {
    auto cfg=controlled_config();cfg.set_predation(true);cfg.funded_reproduction=true;
    cfg.width=cfg.height=12;cfg.initial_creatures=0;cfg.max_population=2;
    cfg.energy_capacity=250;cfg.body_energy_per_mass=60;cfg.reproduction_cost=cfg.offspring_energy=60;
    cfg.maturity_age=30;cfg.reproduction_cooldown=20;cfg.reproduction=true;cfg.storms_enabled=false;
    cfg.basal_cost=cfg.movement_cost=cfg.turn_cost=cfg.forage_cost=cfg.call_cost=cfg.neuron_cost=cfg.synapse_cost=cfg.spike_cost=cfg.healing_rate=0;
    cfg.mutation.copy_probability=1;cfg.mutation.slight_probability=0;
    cfg.mutation.meta_mutation_enabled=false;cfg.mutation.disconnected_neuron_prune_probability=0;
    EcosystemWorld w(cfg,false);EcoCreature c;c.id=c.genome_id=1;c.position={6,6};c.energy=100;c.age=31;
    c.brain=make_sparse_ancestral_brain(cfg);c.health=w.max_health(c);
    w.creatures={c};w.next_creature_id=2;w.totals.births=w.totals.founder_births=1;return w;
}
void feed(EcosystemWorld& w,double amount) {
    w.creatures[0].digestion.push_back({w.time(),amount,FoodKind::Graze});
    w.step(std::vector<EcoAction>(w.creatures.size()));
}
double ledger(const EcosystemWorld& w) {
    double energy=w.totals.discarded_energy+w.totals.metabolism+w.totals.reproduction_overhead+w.totals.meat_spoiled_energy;
    for(const auto& c:w.creatures){energy+=c.energy+c.reproductive_energy+w.config.body_energy_per_mass*c.body.mass;for(const auto& p:c.digestion)energy+=p.energy;}
    for(const auto& r:w.resources)energy+=r.stock*r.energy_per_unit;return energy;
}
int main() try {
    auto w=fixture();const double initial=ledger(w);
    feed(w,100);near(w.creatures[0].energy,150,"Survival split");near(w.creatures[0].reproductive_energy,50,"Reproduction split");
    check(w.creatures[0].gestation.has_value(),"No gestation");near(w.creatures[0].gestation->cost,120,"Mass target");
    near(w.observe(0)[eco_reproduction_offset],50.0/120,"Progress sensor");
    check(w.next_creature_id==2 && w.totals.births==1,"Unborn offspring registered as birth");
    std::istringstream in(saved(w));auto copy=EcosystemWorld::load_checkpoint(in);
    feed(w,140);feed(copy,140);check(saved(w)==saved(copy),"Gestation checkpoint diverged");
    check(w.creatures.size()==2,"Fully funded offspring not born");near(w.creatures[0].energy,220,"Birth drained survival reserve");
    near(w.creatures[0].reproductive_energy,0,"Birth did not debit reproductive reserve");
    near(w.creatures[1].energy,60,"Offspring energy");check(!w.creatures[1].gestation,"Newborn inherited gestation");
    near(ledger(w),initial+240,"Birth energy conservation");
    near(w.observe(0)[eco_reproduction_offset+1],1,"Cooldown sensor");
    feed(w,20);near(w.creatures[0].energy,240,"Cooldown stole survival income");check(!w.creatures[0].gestation,"Cooldown allowed investment");
    w.creatures[0].last_birth=w.time()-w.config.reproduction_cooldown;
    feed(w,20);near(w.creatures[0].reproductive_energy,10,"Investment did not resume after cooldown");

    w=fixture();w.creatures[0].reproduction_allocation=1;feed(w,20);
    near(w.creatures[0].energy,100,"Full allocation changed survival reserve");
    near(w.creatures[0].reproductive_energy,20,"Full allocation lost income");
    w.config.reproduction_threshold=1000;w.config.validate();
    std::istringstream independent_target(saved(w));auto independent=EcosystemWorld::load_checkpoint(independent_target);
    check(saved(w)==saved(independent),"Unused threshold prevented funded checkpoint continuation");

    w=fixture();w.creatures[0].age=0;feed(w,40);near(w.creatures[0].energy,140,"Immature income");
    check(!w.creatures[0].gestation,"Immature gestation");
    w=fixture();w.creatures[0].reproduction_allocation=0;feed(w,20);check(!w.creatures[0].gestation,"Zero allocation began gestation");
    w=fixture();w.config.reproduction=false;feed(w,20);check(!w.creatures[0].gestation,"Disabled reproduction invested");
    w=fixture();w.config.max_population=1;feed(w,240);
    near(w.creatures[0].reproductive_energy,120,"Blocked birth funding");const auto pending=w.creatures[0].gestation->brain.synapses().size();
    feed(w,100);near(w.creatures[0].energy,250,"Full funding overflow did not go to survival");
    near(w.totals.discarded_energy,70,"Survival overflow lost accounting");
    check(w.creatures[0].gestation->brain.synapses().size()==pending && w.next_creature_id==2,"Blocked birth rerolled genome");
    w.config.max_population=2;w.step({{}});check(w.creatures.size()==2,"Funded blocked birth failed after capacity released");
    w=fixture();std::fill(w.terrain.begin(),w.terrain.end(),Terrain::Wall);
    w.terrain[6*w.config.width+6]=Terrain::Ground;w.creatures[0].position={6.5,6.5};feed(w,240);
    check(w.creatures.size()==1 && w.creatures[0].gestation,"Blocked placement discarded offspring");
    near(w.creatures[0].reproductive_energy,120,"Blocked placement spent reproductive reserve");
    std::fill(w.terrain.begin(),w.terrain.end(),Terrain::Ground);w.step({{}});
    check(w.creatures.size()==2,"Birth did not resume after space cleared");

    w=fixture();w.config.max_population=1;w.config.mutation.copy_probability=0;
    w.config.mutation.mass_mutation_probability=1;w.config.mutation.strong.body_probability_scale=1;
    w.config.mutation.mass_mutation_sigma=.4;
    w.config.mutation.allocation_mutation_probability=1;w.config.mutation.allocation_mutation_sigma=.3;
    feed(w,2);const auto& g=*w.creatures[0].gestation;
    check(g.body.mass!=1,"Gestation did not mutate mass");near(g.cost,60+60*g.body.mass,"Target ignored mutated mass");
    check(g.reproduction_allocation!=.5,"Allocation did not evolve");
    const auto mutated_mass=g.body.mass,allocation=g.reproduction_allocation,cost=g.cost;
    feed(w,2*cost);w.config.max_population=2;w.step({{}});
    near(w.creatures[1].body.mass,mutated_mass,"Birth rerolled mass");near(w.creatures[1].reproduction_allocation,allocation,"Birth lost allocation");

    w=fixture();feed(w,100);w.creatures[0].health=0;const auto before=ledger(w);w.step({{}});
    check(w.creatures.empty() && w.resources.size()==1,"Gestating death produced extra bodies");
    near(w.resources[0].stock*w.resources[0].energy_per_unit,w.config.carcass_recovery*(60+150+50),"Corpse omitted reproductive reserve");
    near(ledger(w),before,"Death created energy");
    w=fixture();feed(w,100);w.creatures[0].energy=.01;w.config.basal_cost=1;w.step({{}});
    check(w.creatures.empty(),"Starving creature withdrew reproductive reserve");
    w=fixture();feed(w,100);std::ostringstream log;std::unordered_set<std::uint64_t> known;write_ecosystem_frame(log,w,true,true,true,&known);
    check(known.size()==1 && known.count(1)==1,"Unborn brain registered in replay");
    check(log.str().find("GESTATION_1")==std::string::npos && log.str().find("pending_brain")==std::string::npos,"Unborn genome leaked to replay");
    auto historical=fixture();historical.config.funded_reproduction=false;
    historical.config.brain.input_count=eco_reproduction_offset;
    historical.creatures[0].brain=make_sparse_ancestral_brain(historical.config);
    auto old=saved(historical);old.erase(old.find("GESTATION_1"));old+="END_ECOSYSTEM\n";
    old.replace(0,21,"NEUROEVO_ECOSYSTEM_38");
    std::istringstream legacy_stream(old);auto legacy=EcosystemWorld::load_checkpoint(legacy_stream);
    check(!legacy.config.funded_reproduction,"Historical checkpoint changed reproduction mode");
    check(legacy.observe(0).size()==eco_reproduction_offset,"Historical sensor layout changed");
    legacy.creatures[0].energy=200;legacy.step({{}});
    check(legacy.creatures.size()==2 && !legacy.creatures[0].gestation,"Historical reproduction changed");
    w=fixture();feed(w,2);auto corrupt=saved(w);
    const auto gestation=corrupt.find("GESTATION_1"), state_line=corrupt.find('\n',corrupt.find('\n',gestation)+1)+1;
    // Corrupt the parent's allocation field without changing any genome.
    const auto allocation_position=corrupt.find(' ',state_line)+1;
    corrupt.replace(allocation_position,corrupt.find(' ',allocation_position)-allocation_position,"1.5");
    bool rejected=false;try{std::istringstream bad(corrupt);EcosystemWorld::load_checkpoint(bad);}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"Invalid reproductive allocation accepted");
    const auto labels=ecosystem_input_labels(true,true,true);
    check(labels[eco_reproduction_offset]=="reproduction_progress" && labels.back()=="reproduction_cooldown","Sensor labels");
    std::cout<<"Gestation tests passed"<<std::endl;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
