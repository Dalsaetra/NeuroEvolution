#include "neuroevo/ecosystem.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <queue>

using namespace neuroevo;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string saved(const EcosystemWorld& w) { std::ostringstream s; w.save_checkpoint(s); return s.str(); }
void geography_and_inheritance()
{
    auto cfg=nursery_frontier_config();
    EcosystemWorld w(cfg);
    require(!w.config.establishment && !w.config.archive_eval_trials, "Frontier must disable all archive support");
    require(w.config.width>48 && w.config.height>48,"Frontier must be larger than the original habitat");
    for (const auto& c:w.creatures) require(w.in_nursery(c.position) && w.sheltered(c.position),"Founders must start in the protected nursery");
    std::size_t nursery_food=0,frontier_shelter=0;
    for (const auto& r:w.resources) {
        if (w.in_nursery(r.position)) {
            ++nursery_food;
            require(r.energy_per_unit==cfg.nursery_food_energy && r.regrowth*r.energy_per_unit<cfg.basal_cost,
                "A single nursery patch must not pay indefinite basal metabolism");
        } else require(r.energy_per_unit>cfg.nursery_food_energy,"Frontier food must have greater nutrition");
    }
    for (std::size_t y=0;y<cfg.height;++y) for (std::size_t x=0;x<cfg.width;++x)
        if (!w.in_nursery({double(x)+.5,double(y)+.5}) && w.terrain[y*cfg.width+x]==Terrain::Shelter) ++frontier_shelter;
    require(frontier_shelter==cfg.shelters*9 && nursery_food>50,"Habitat lost food or outer shelters");
    // Every open cell, including each gate and outer refuge, must be reachable.
    std::vector<bool> seen(w.terrain.size()); std::queue<std::size_t> queue;
    auto first=std::find_if(w.terrain.begin(),w.terrain.end(),[](auto t){return t!=Terrain::Wall;})-w.terrain.begin();
    queue.push(first); seen[first]=true; std::size_t reached=0;
    while (!queue.empty()) { auto i=queue.front();queue.pop();++reached;
        for (auto j:{i-1,i+1,i-cfg.width,i+cfg.width}) if(j<w.terrain.size()&&!seen[j]&&w.terrain[j]!=Terrain::Wall) {seen[j]=true;queue.push(j);}
    }
    require(reached==static_cast<std::size_t>(std::count_if(w.terrain.begin(),w.terrain.end(),[](auto t){return t!=Terrain::Wall;})),"Nursery or frontier is disconnected");
    auto fewer=cfg; fewer.initial_creatures=1; EcosystemWorld small(fewer);
    require(w.terrain==small.terrain && w.resources.size()==small.resources.size(),"Population changed geography");
    for(std::size_t i=0;i<w.resources.size();++i) require(w.resources[i].position.x==small.resources[i].position.x
        &&w.resources[i].position.y==small.resources[i].position.y,"Population changed resource locations");
    for(int i=0;i<10;++i)w.step();
    std::istringstream input(saved(w));auto resumed=EcosystemWorld::load_checkpoint(input);
    require(saved(w)==saved(resumed),"Frontier checkpoint lost habitat state");
    for(int i=0;i<10;++i){w.step();resumed.step();}
    require(saved(w)==saved(resumed),"Frontier resume diverged");
}
void weather_and_costs()
{
    auto cfg=nursery_frontier_config(); cfg.initial_creatures=0; cfg.reproduction=false;
    cfg.phase_offset=cfg.calm_duration+cfg.warning_duration+1;
    EcosystemWorld w(cfg);
    const auto inner=std::find_if(w.resources.begin(),w.resources.end(),[&](const auto& r){return w.in_nursery(r.position);});
    const auto outer=std::find_if(w.resources.begin(),w.resources.end(),[&](const auto& r){return !w.in_nursery(r.position)&&r.kind==FoodKind::Graze;});
    const auto inner_index=inner-w.resources.begin(),outer_index=outer-w.resources.begin();
    EcoCreature a; a.id=1; a.position=inner->position; a.energy=90; a.brain=make_sparse_ancestral_brain(cfg);
    EcoCreature b=a;b.id=2;b.position=outer->position;
    w.creatures={a,b};w.next_creature_id=3;
    const auto storm_input=eco_sectors*eco_sector_channels+8+5;
    require(w.observe(0)[storm_input]==0 && w.observe(1)[storm_input]==1,"Storm cue must be local to protected nursery");
    w.resources[inner_index].stock=0;w.resources[outer_index].stock=0;
    w.step({{}, {}});
    require(std::abs(w.creatures[0].energy-w.creatures[1].energy-cfg.storm_cost*cfg.dt)<1e-8,"Nursery protection is incorrect");
    require(w.creatures[0].energy<90,"Shelter must still charge ordinary metabolism");
    require(w.resources[inner_index].stock>0 && w.resources[outer_index].stock==0,"Only nursery food regrows through storms");
    // Even directly on food, passive creatures cannot obtain free energy.
    w.creatures.resize(1);
    for(int i=0;i<6000&&!w.creatures.empty();++i) w.step({{}});
    require(w.creatures.empty() && w.totals.energy_gained==0,"Passive nursery occupant survived without feeding");
    require(w.archive.empty()&&w.newborn_evaluations.empty()&&w.totals.immigrants==0,"Nursery secretly used archive support");
}
void feeding_efficiency()
{
    auto cfg=nursery_frontier_config();cfg.initial_creatures=0;cfg.reproduction=false;
    EcosystemWorld original(cfg);
    auto patch=*std::find_if(original.resources.begin(),original.resources.end(),[&](const auto& r){return original.in_nursery(r.position);});
    original.resources={patch};
    EcoCreature c;c.id=1;c.energy=90;c.position={patch.position.x-.4,patch.position.y};
    c.heading=0;c.brain=make_sparse_ancestral_brain(cfg);original.creatures={c};original.next_creature_id=2;
    auto moving=original,stopped=original;
    EcoAction fast;fast.forward=1;fast.forage=1;
    EcoAction still;still.forage=1;
    moving.step({fast});stopped.step({still});
    require(std::abs(moving.totals.consumed_biomass/stopped.totals.consumed_biomass-.35)<1e-8,
        "Nursery feeding must reward pausing, with a small taste still available while moving");
    for(const auto* w:{&moving,&stopped}) {
        const auto& packet=w->creatures[0].digestion.front();
        require(std::abs(packet.energy-w->totals.consumed_biomass*patch.energy_per_unit)<1e-8,
            "Feeding efficiency broke digestion energy conservation");
        require(std::abs(patch.stock-w->resources[0].stock+w->totals.regrown_biomass-w->totals.consumed_biomass)<1e-8,
            "Feeding efficiency broke stock conservation");
    }
}
}
int main(){try{geography_and_inheritance();weather_and_costs();feeding_efficiency();std::cout<<"Nursery frontier passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
