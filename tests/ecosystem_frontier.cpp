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
    for (bool frontier : {false,true}) for (std::size_t size : {1u,4u,5u}) {
        auto custom=cfg;custom.nursery_frontier=frontier;custom.shelters=2;custom.shelter_size=size;
        EcosystemWorld shelters(custom);
        std::size_t count=0;
        for (std::size_t i=0;i<shelters.terrain.size();++i)
            if (shelters.terrain[i]==Terrain::Shelter
                && !shelters.in_nursery({double(i%custom.width)+.5,double(i/custom.width)+.5})) ++count;
        require(count==custom.shelters*size*size,"Configured shelter size was not used by map generation");
        std::istringstream checkpoint(saved(shelters));
        require(saved(EcosystemWorld::load_checkpoint(checkpoint))==saved(shelters),"Checkpoint lost shelter size");
    }
    for (std::size_t width : {0u, 1u, 3u, 6u}) {
        auto custom=cfg;custom.nursery_exit_width=width;
        EcosystemWorld gates(custom);
        const auto x0=(custom.width-custom.nursery_size)/2,y0=(custom.height-custom.nursery_size)/2;
        std::size_t openings[4]={};
        for(std::size_t i=0;i<custom.nursery_size;++i) {
            openings[0]+=gates.terrain[y0*custom.width+x0+i]==Terrain::Shelter;
            openings[1]+=gates.terrain[(y0+custom.nursery_size-1)*custom.width+x0+i]==Terrain::Shelter;
            openings[2]+=gates.terrain[(y0+i)*custom.width+x0]==Terrain::Shelter;
            openings[3]+=gates.terrain[(y0+i)*custom.width+x0+custom.nursery_size-1]==Terrain::Shelter;
        }
        for(auto count:openings)require(count==width,"Nursery gate width differs from configuration");
        std::istringstream checkpoint(saved(gates));
        require(saved(EcosystemWorld::load_checkpoint(checkpoint))==saved(gates),"Checkpoint lost custom gate width");
    }
    EcosystemWorld w(cfg);
    require(!w.config.establishment && !w.config.archive_eval_trials, "Frontier must disable all archive support");
    require(w.config.width>48 && w.config.height>48,"Frontier must be larger than the original habitat");
    for (const auto& c:w.creatures) require(w.in_nursery(c.position) && w.sheltered(c.position),"Founders must start in the protected nursery");
    std::size_t nursery_food=0,frontier_shelter=0;
    for (const auto& r:w.resources) {
        if (w.in_nursery(r.position)) {
            ++nursery_food;
            require(r.energy_per_unit==cfg.nursery_food_energy
                && r.capacity==cfg.nursery_food_capacity && r.regrowth==(cfg.nursery_food_relocates?0:cfg.nursery_food_regrowth),
                "Nursery patches must use the configured nutrition, capacity, and regrowth");
        } else {
            // Relative nutrition and carrying capacity are experimental tuning
            // choices, not invariants of the habitat implementation.
            const double expected = r.kind==FoodKind::Graze ? cfg.graze_energy
                : r.kind==FoodKind::Pod ? cfg.pod_energy
                : ((r.kind==FoodKind::FruitA)==w.fruit_a_rich ? cfg.rich_fruit_energy : cfg.poor_fruit_energy);
            require(r.energy_per_unit==expected,"Frontier patches must use their configured nutrition");
        }
    }
    for (std::size_t y=0;y<cfg.height;++y) for (std::size_t x=0;x<cfg.width;++x)
        if (!w.in_nursery({double(x)+.5,double(y)+.5}) && w.terrain[y*cfg.width+x]==Terrain::Shelter) ++frontier_shelter;
    const auto patches_per_axis=(cfg.nursery_size-3)/2;
    require(frontier_shelter==cfg.shelters*cfg.shelter_size*cfg.shelter_size && nursery_food==(cfg.nursery_food_relocates?cfg.nursery_food_patches:patches_per_axis*patches_per_axis),
        "Habitat food or shelter count does not match its configured dimensions");
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
    require(w.observe(0)[storm_input]==1 && w.observe(1)[storm_input]==1,
        "Nursery protection must not hide the global storm cue");
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
void relocation()
{
    auto cfg=nursery_frontier_config();cfg.initial_creatures=0;cfg.reproduction=false;
    EcosystemWorld w(cfg);
    auto& patch=w.resources.front();const auto old=patch.position;const auto id=patch.id;
    patch.stock=0;
    std::istringstream checkpoint(saved(w));auto resumed=EcosystemWorld::load_checkpoint(checkpoint);
    w.step();resumed.step();
    require(saved(w)==saved(resumed),"Checkpoint resume changed food relocation RNG or positions");
    require(w.resources.front().id==id && length(w.resources.front().position-old)>1.7,
        "Depleted nursery patch did not relocate with its stable ID");
    require(w.in_nursery(w.resources.front().position) && w.resources.front().stock==cfg.nursery_food_capacity,
        "Replacement patch must be full and inside nursery");
    require(std::any_of(w.events.begin(),w.events.end(),[](const auto& e){return e.type=="nursery_food_relocated";}),
        "Relocation event missing");
    const auto new_position=w.resources.front().position;w.step();
    require(length(w.resources.front().position-new_position)==0,"Nondepleted food moved");
    require(w.resources.size()==cfg.nursery_food_patches+cfg.grazing_patches+cfg.fruit_patches+cfg.pods,
        "Relocation changed fixed resource count");
}
}
int main(){try{geography_and_inheritance();weather_and_costs();feeding_efficiency();relocation();std::cout<<"Nursery frontier passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
