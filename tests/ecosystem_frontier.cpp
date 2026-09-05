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
        // Walls may cover part of a shelter footprint, but cannot change its extent.
        std::size_t covered=0;
        for(const auto& r:shelters.resources) if(r.shelter_food) {
            const auto cx=std::size_t(r.position.x),cy=std::size_t(r.position.y);
            const auto low=size/2,high=size-1-low;
            for(auto y=cy-low;y<=cy+high;++y)for(auto x=cx-low;x<=cx+high;++x) {
                const auto tile=shelters.terrain[y*custom.width+x];
                require(tile==Terrain::Shelter||tile==Terrain::Wall,"Shelter footprint changed");
                covered+=tile==Terrain::Wall;
            }
        }
        require(count+covered==custom.shelters*size*size,"Configured shelter size was not used by map generation");
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
            const double expected = r.shelter_food ? cfg.shelter_food_energy : r.kind==FoodKind::Graze ? cfg.graze_energy
                : r.kind==FoodKind::Pod ? cfg.pod_energy
                : ((r.kind==FoodKind::FruitA)==w.fruit_a_rich ? cfg.rich_fruit_energy : cfg.poor_fruit_energy);
            require(r.energy_per_unit==expected,"Frontier patches must use their configured nutrition");
        }
    }
    for (std::size_t y=0;y<cfg.height;++y) for (std::size_t x=0;x<cfg.width;++x)
        if (!w.in_nursery({double(x)+.5,double(y)+.5}) && w.terrain[y*cfg.width+x]==Terrain::Shelter) ++frontier_shelter;
    const auto patches_per_axis=(cfg.nursery_size-3)/2;
    require(frontier_shelter<=cfg.shelters*cfg.shelter_size*cfg.shelter_size
        && frontier_shelter>=cfg.shelters*cfg.shelter_size*cfg.shelter_size*8/10 && nursery_food==(cfg.nursery_food_relocates?cfg.nursery_food_patches:patches_per_axis*patches_per_axis),
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
void rough_patches()
{
    for(bool frontier:{false,true})for(std::uint64_t seed=1;seed<=8;++seed) {
        auto cfg=frontier?nursery_frontier_config():EcosystemConfig{};
        cfg.seed=seed;cfg.initial_creatures=0;
        EcosystemWorld w(cfg),repeat(cfg);
        require(w.terrain==repeat.terrain,"Rough patches are not seeded deterministically");
        std::vector<bool> seen(w.terrain.size());std::size_t rough=0,floor=0;
        for(std::size_t i=0;i<w.terrain.size();++i) {
            floor+=w.terrain[i]==Terrain::Ground||w.terrain[i]==Terrain::Rough;
            if(w.terrain[i]!=Terrain::Rough||seen[i])continue;
            std::vector<std::size_t> component{i};seen[i]=true;
            for(std::size_t h=0;h<component.size();++h) {
                const auto cell=component[h];
                for(auto n:{cell-1,cell+1,cell-cfg.width,cell+cfg.width})
                    if(n<w.terrain.size()&&!seen[n]&&w.terrain[n]==Terrain::Rough) {
                        seen[n]=true;component.push_back(n);
                    }
            }
            require(component.size()>=8,"Rough ground contains tiny isolated fragments");
            rough+=component.size();
        }
        const double fraction=double(rough)/double(floor),target=frontier?.18:.12;
        require(std::abs(fraction-target)<.05,"Rough coverage drifted too far from its previous density");
        for(const auto& r:w.resources)require(w.traversable(r.position),"Rough repaint obstructed food");
    }
}
void wall_lines()
{
    std::size_t shelter_walls=0,bends=0;
    for(std::uint64_t seed=1;seed<=8;++seed) for(std::size_t gate:{0u,4u}) {
        auto cfg=nursery_frontier_config();cfg.seed=seed;cfg.initial_creatures=0;cfg.nursery_exit_width=gate;
        EcosystemWorld w(cfg);
        std::vector<bool> seen(w.terrain.size());std::size_t wall_count=0;
        const auto outdoor_wall=[&](std::size_t i) {
            const auto x=i%cfg.width,y=i/cfg.width;
            return x>0&&y>0&&x+1<cfg.width&&y+1<cfg.height&&w.terrain[i]==Terrain::Wall
                &&!w.in_nursery({double(x)+.5,double(y)+.5});
        };
        for(std::size_t i=0;i<w.terrain.size();++i)if(outdoor_wall(i)&&!seen[i]) {
            std::vector<std::size_t> cells{i};seen[i]=true;std::size_t ends=0;
            for(std::size_t head=0;head<cells.size();++head) {
                const auto c=cells[head];std::size_t degree=0;
                for(auto n:{c-1,c+1,c-cfg.width,c+cfg.width})if(outdoor_wall(n)) {
                    ++degree;if(!seen[n]){seen[n]=true;cells.push_back(n);}
                }
                require(degree==1||degree==2,"Wall component is a branch or isolated pillar");
                ends+=degree==1;
                if(degree==2 && (outdoor_wall(c-1)||outdoor_wall(c+1))
                    &&(outdoor_wall(c-cfg.width)||outdoor_wall(c+cfg.width)))++bends;
            }
            require(cells.size()>=3&&cells.size()<=7&&ends==2,"Wall is not a short open line");
            wall_count+=cells.size();
        }
        require(wall_count>0&&wall_count<=cfg.width*cfg.height/50,"Wall coverage is not sparse");
        for(const auto& r:w.resources) {
            require(w.traversable(r.position),"Resource inside wall");
            if(!r.shelter_food)continue;
            const auto cx=std::size_t(r.position.x),cy=std::size_t(r.position.y);
            const auto low=cfg.shelter_size/2,high=cfg.shelter_size-1-low;
            for(auto y=cy-low;y<=cy+high;++y)for(auto x=cx-low;x<=cx+high;++x)
                shelter_walls+=w.terrain[y*cfg.width+x]==Terrain::Wall;
        }
        // Closed nursery gates are intentional; outdoors must still be one region.
        std::vector<std::size_t> open;seen.assign(w.terrain.size(),false);
        std::size_t expected=0;
        for(std::size_t i=0;i<w.terrain.size();++i)if(w.terrain[i]!=Terrain::Wall
            &&!w.in_nursery({double(i%cfg.width)+.5,double(i/cfg.width)+.5})) {
            ++expected;if(open.empty()){open.push_back(i);seen[i]=true;}
        }
        for(std::size_t h=0;h<open.size();++h)for(auto n:{open[h]-1,open[h]+1,open[h]-cfg.width,open[h]+cfg.width})
            if(n<w.terrain.size()&&!seen[n]&&w.terrain[n]!=Terrain::Wall
                &&!w.in_nursery({double(n%cfg.width)+.5,double(n/cfg.width)+.5})) {seen[n]=true;open.push_back(n);}
        require(open.size()==expected,"Outdoor crevice or shelter became inaccessible");
    }
    require(shelter_walls>0&&bends>0,"Layouts must allow shelter walls and bent lines");
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
    require(w.resources[inner_index].stock>0 && w.resources[outer_index].stock==cfg.graze_capacity,"Depleted outdoor food must relocate even through storms");
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
        require(std::abs(patch.stock-w->resources[0].stock+w->totals.regrown_biomass-w->totals.consumed_biomass-w->totals.spoiled_biomass)<1e-8,
            "Feeding efficiency broke stock conservation");
    }
}
void relocation()
{
    auto cfg=nursery_frontier_config();cfg.initial_creatures=0;cfg.reproduction=false;
    cfg.phase_offset=cfg.calm_duration+cfg.warning_duration+1;
    EcosystemWorld w(cfg);
    auto& patch=w.resources.front();const auto old=patch.position;const auto id=patch.id;
    patch.stock=cfg.nursery_food_decay*cfg.dt/2; // Decay alone must trigger relocation.
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
    require(std::abs(w.resources.front().stock-(cfg.nursery_food_capacity-cfg.nursery_food_decay*cfg.dt))<1e-9,
        "Nursery food must decay at the configured rate during storms");
    require(w.resources.size()==cfg.nursery_food_patches+cfg.grazing_patches+cfg.fruit_patches+cfg.pods+cfg.shelters,
        "Relocation changed fixed resource count");
}
}
int main(){try{geography_and_inheritance();rough_patches();wall_lines();weather_and_costs();feeding_efficiency();relocation();std::cout<<"Nursery frontier passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
