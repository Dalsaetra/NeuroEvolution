#include "fixtures.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace neuroevo;
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void near(double a,double b){require(std::abs(a-b)<1e-12,"Mutation probability mismatch");}
std::string saved(const EcosystemWorld& w){std::ostringstream s;w.save_checkpoint(s);return s.str();}
EcosystemWorld fixture(std::uint64_t seed,double scale,double meta_probability) {
    auto c=controlled_config();c.seed=seed;c.initial_creatures=0;c.width=c.height=12;
    c.storms_enabled=false;c.reproduction=true;c.maturity_age=0;c.reproduction_cooldown=0;
    c.reproduction_threshold=100;c.reproduction_cost=60;c.offspring_energy=60;c.max_population=2;
    c.basal_cost=c.movement_cost=c.turn_cost=c.forage_cost=c.call_cost=c.neuron_cost=c.synapse_cost=c.spike_cost=0;
    c.mutation.meta_mutation_enabled=true;c.mutation.meta_mutation_probability=meta_probability;
    c.mutation.meta_mutation_sigma=0.3;c.mutation.disconnected_neuron_prune_probability=0;
    EcosystemWorld w(c,false);EcoCreature parent;parent.id=parent.genome_id=1;parent.position={6,6};
    parent.energy=200;parent.mutation_scale=scale;parent.brain=make_sparse_ancestral_brain(c);
    w.creatures={parent};w.next_creature_id=2;w.totals.births=w.totals.founder_births=1;
    return w;
}
int main() try {
    MutationConfig m;
    near(m.min_mutation_scale,1);
    for(const auto& row: {std::array<double,4>{0.5,.5,.45,.05}, {0.75,.5,.45,.05},
            {1,.5,.45,.05}, {1.5,.25,.475,.275}, {2,0,.5,.5}}) {
        auto p=m.inheritance_probabilities(row[0]);
        for(int i=0;i<3;++i)near(p[i],row[i+1]);
    }
    for(int i=0;i<=1500;++i) {
        auto p=m.inheritance_probabilities(.5+i*.001);
        near(p[0]+p[1]+p[2],1);for(auto x:p)require(x>=0 && x<=1,"Invalid mixture");
    }
    bool copy_gene_changed=false,lower=false,upper=false;
    for(std::uint64_t seed=1;seed<=120;++seed) {
        auto unchanged=fixture(seed,1,0),changed=fixture(seed,1,1);
        unchanged.step({{}});changed.step({{}});
        require(changed.creatures.size()==2 && unchanged.creatures.size()==2,"Birth missing");
        const auto& a=unchanged.creatures[1];const auto& b=changed.creatures[1];
        near(a.mutation_scale,1);near(changed.creatures[0].mutation_scale,1);
        require(b.mutation_scale>=1 && b.mutation_scale<=2,"Meta gene out of bounds");
        // Child's meta mutation must not change the already selected birth branch.
        require(a.brain.synapses().size()==b.brain.synapses().size(),"Child scale affected its own mutation");
        for(std::size_t i=0;i<a.brain.synapses().size();++i)near(a.brain.synapses()[i].weight,b.brain.synapses()[i].weight);
        if(a.genome_id==1 && b.mutation_scale!=1) {
            require(b.genome_id==b.id,"Changed meta gene retained clone identity");copy_gene_changed=true;
        }
        auto extreme=fixture(seed,1,1);extreme.config.mutation.meta_mutation_sigma=100;extreme.step({{}});
        lower|=extreme.creatures[1].mutation_scale==1;upper|=extreme.creatures[1].mutation_scale==2;
        auto maximum=fixture(seed,2,0);maximum.step({{}});
        require(maximum.creatures[1].genome_id!=1,"Maximum scale selected copy");
    }
    require(copy_gene_changed && lower && upper,"Meta mutation paths not exercised");
    auto w=fixture(5,1.4,1);std::istringstream in(saved(w));auto resumed=EcosystemWorld::load_checkpoint(in);
    w.step({{}});resumed.step({{}});require(saved(w)==saved(resumed),"Meta mutation resume diverged");
    auto old=without_allometry_header(saved(fixture(5,1,0)));old.replace(0,21,"NEUROEVO_ECOSYSTEM_34");
    old.erase(old.find("META_MUTATION_1"));old+="END_ECOSYSTEM\n";
    std::istringstream historical(old);auto legacy=EcosystemWorld::load_checkpoint(historical);
    require(!legacy.config.mutation.meta_mutation_enabled,"Old checkpoint enabled meta mutation");near(legacy.creatures[0].mutation_scale,1);
    auto zero=fixture(9,1.7,1);zero.config.mutation.meta_mutation_sigma=0;zero.step({{}});
    near(zero.creatures[1].mutation_scale,1.7);
    for(double bad : {0.,0.5,0.999,2.1}) {
        auto invalid=fixture(1,bad,0);bool rejected=false;
        try{invalid.step({{}});}catch(const std::runtime_error&){rejected=true;}
        require(rejected,"Out-of-range reproductive gene accepted without predation");
    }
    m.meta_mutation_enabled=false;m.copy_probability=1;m.slight_probability=0;
    near(m.inheritance_probabilities(2)[0],1);
    for(double invalid:{-1.,1.1}) {
        auto c=controlled_config();c.mutation.meta_mutation_probability=invalid;
        bool rejected=false;try{c.validate();}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Invalid meta probability accepted");
    }
    // Version 35 did not store its 0.5 floor. Loading it must preserve that
    // historical mixture and exact continuation, even after saving as v36.
    auto historical_world=fixture(8,0.75,1);
    historical_world.config.mutation.eye_mutation_probability=0;
    historical_world.config.mutation.min_mutation_scale=0.5;
    auto version35=without_allometry_header(saved(historical_world));
    version35.erase(version35.find("BACKGROUND_WEATHER_1"));version35+="END_ECOSYSTEM\n";
    version35.replace(0,21,"NEUROEVO_ECOSYSTEM_35");
    const auto line_start=version35.find('\n',version35.find("META_MUTATION_1"))+1;
    const auto line_end=version35.find('\n',line_start);
    std::istringstream fields(version35.substr(line_start,line_end-line_start));
    std::string enabled,probability,sigma;
    fields>>enabled>>probability>>sigma;
    version35.replace(line_start,line_end-line_start,enabled+" "+probability+" "+sigma);
    std::istringstream input35(version35);
    auto restored35=EcosystemWorld::load_checkpoint(input35);
    near(restored35.config.mutation.min_mutation_scale,0.5);
    auto mix=restored35.config.mutation.inheritance_probabilities(0.75);
    near(mix[0],.625);near(mix[1],.35);near(mix[2],.025);
    historical_world.step({{}});restored35.step({{}});
    require(saved(historical_world)==saved(restored35),"Historical meta floor continuation changed");
    std::istringstream roundtrip(saved(restored35));
    auto restored36=EcosystemWorld::load_checkpoint(roundtrip);
    near(restored36.config.mutation.min_mutation_scale,0.5);
    std::cout<<"Meta mutation tests passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
