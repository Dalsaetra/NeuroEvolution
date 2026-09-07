#include "neuroevo/ecosystem.hpp"
#include "ecosystem_terrain.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace neuroevo {
EcosystemConfig nursery_frontier_config()
{
    EcosystemConfig c;
    c.nursery_frontier = true;
    c.set_predation(true);
    c.width = c.height = 80;
    c.max_population = 200;
    c.shelters = 20; c.shelter_size = 8;
    c.shelter_food_energy = 35; c.shelter_food_capacity = 2;
    c.nursery_food_energy = 30; c.nursery_food_capacity = 2; c.nursery_food_regrowth = 0.02;
    c.nursery_food_patches = 16;
    c.grazing_patches = 300; c.fruit_patches = 80; c.pods = 120;
    c.graze_energy = 60; c.poor_fruit_energy = 45; c.rich_fruit_energy = 90; c.pod_energy = 120;
    c.graze_capacity = 4; c.fruit_capacity = 5; c.pod_capacity = 10;
    c.interaction_degrees = 80;
    c.calm_duration = 180; c.warning_duration = 40; c.storm_duration = 60; c.storm_cost = 6.0;
    c.energy_capacity = 200;
    c.maturity_age = 30; c.reproduction_threshold = 150;
    c.reproduction_cost = 60; c.offspring_energy = 60; c.reproduction_cooldown = 30;
    c.establishment = false; c.archive_eval_trials = 0;
    return c;
}

void EcosystemWorld::generate_nursery_frontier()
{
    const auto x0 = (config.width - config.nursery_size) / 2;
    const auto y0 = (config.height - config.nursery_size) / 2;
    const auto x1 = x0 + config.nursery_size - 1, y1 = y0 + config.nursery_size - 1;
    const auto cx = x0 + config.nursery_size / 2, cy = y0 + config.nursery_size / 2;
    const auto gate_x = cx - config.nursery_exit_width / 2;
    const auto gate_y = cy - config.nursery_exit_width / 2;
    const auto pos = [&](std::size_t cell) { return Vec2{double(cell % config.width)+0.5,double(cell/config.width)+0.5}; };
    std::vector<std::size_t> outside, spawning;
    for (std::size_t y=0; y<config.height; ++y) for (std::size_t x=0; x<config.width; ++x) {
        const auto cell=y*config.width+x;
        if (x==0 || y==0 || x+1==config.width || y+1==config.height) { terrain[cell]=Terrain::Wall; continue; }
        if (in_nursery(pos(cell))) {
            // Four centered gates; width zero deliberately closes the nursery.
            const bool boundary=x==x0 || x==x1 || y==y0 || y==y1;
            const bool gate=((x==x0 || x==x1) && y>=gate_y && y-gate_y<config.nursery_exit_width)
                || ((y==y0 || y==y1) && x>=gate_x && x-gate_x<config.nursery_exit_width);
            terrain[cell]=boundary && !gate ? Terrain::Wall : Terrain::Shelter;
            if (x>x0+1 && x+2<x1 && y>y0+1 && y+2<y1) spawning.push_back(cell);
        } else {
            terrain[cell]=map_rng.chance(0.18) ? Terrain::Rough : Terrain::Ground;
            // Leave the nursery approaches open; obstacles are added after shelters.
            const bool near = x+3>=x0 && x<=x1+3 && y+3>=y0 && y<=y1+3;
            if (!near && terrain[cell]!=Terrain::Wall) outside.push_back(cell);
        }
    }
    const auto shuffle = [](auto& v, Random& rng) {
        for (std::size_t i=v.size(); i>1; --i) std::swap(v[i-1],v[rng.uniform_index(i)]);
    };
    shuffle(outside,map_rng);
    std::vector<Vec2> shelter_centers;
    std::size_t placed=0;
    const auto low=config.shelter_size/2, high=config.shelter_size-1-low;
    for (const auto cell:outside) {
        if (placed==config.shelters) break;
        const auto x=cell%config.width,y=cell/config.width;
        if (x<low+1 || y<low+1 || x+high+1>=config.width || y+high+1>=config.height) continue;
        bool clear=true;
        for (std::size_t yy=y-low; yy<=y+high; ++yy) for (std::size_t xx=x-low; xx<=x+high; ++xx)
            if (in_nursery(pos(yy*config.width+xx)) || terrain[yy*config.width+xx]==Terrain::Wall || terrain[yy*config.width+xx]==Terrain::Shelter) clear=false;
        if (!clear) continue;
        for (std::size_t yy=y-low; yy<=y+high; ++yy) for (std::size_t xx=x-low; xx<=x+high; ++xx)
            terrain[yy*config.width+xx]=Terrain::Shelter;
        shelter_centers.push_back(pos(cell));
        ++placed;
    }
    if (placed!=config.shelters) throw std::invalid_argument("Not enough frontier space for shelters");
    // Sparse one-cell-wide polylines: 3-7 cells with at most one right-angle
    // bend. Separate components cannot touch, even diagonally, so they cannot
    // accumulate into blobs or closed caves. Shelters are eligible floor too.
    Random wall_rng(config.seed ^ 0x77616c6c6c696e65ULL);
    const std::size_t wall_budget=outside.size()/50;
    std::size_t wall_count=0;
    const auto outdoor_connected = [&]() {
        std::vector<unsigned char> seen(terrain.size());
        std::vector<std::size_t> queue;
        std::size_t open=0;
        for(std::size_t i=0;i<terrain.size();++i)
            if(terrain[i]!=Terrain::Wall && !in_nursery(pos(i))) {
                ++open;
                if(queue.empty()){queue.push_back(i);seen[i]=1;}
            }
        for(std::size_t head=0;head<queue.size();++head) {
            const auto i=queue[head];
            for(const auto n:{i-1,i+1,i-config.width,i+config.width})
                if(n<terrain.size() && !seen[n] && terrain[n]!=Terrain::Wall && !in_nursery(pos(n))) {
                    seen[n]=1;queue.push_back(n);
                }
        }
        return queue.size()==open;
    };
    for(std::size_t attempt=0;attempt<wall_budget*40 && wall_count+3<=wall_budget;++attempt) {
        const auto anchor=outside[wall_rng.uniform_index(outside.size())];
        int x=int(anchor%config.width),y=int(anchor/config.width);
        const int count=3+int(wall_rng.uniform_index(std::min<std::size_t>(5,wall_budget-wall_count-2)));
        const int bend=wall_rng.chance(.6)?1+int(wall_rng.uniform_index(count-2)):count;
        int direction=int(wall_rng.uniform_index(4));
        const int turn=wall_rng.chance(.5)?1:3;
        constexpr int dx[]={1,0,-1,0},dy[]={0,1,0,-1};
        std::vector<std::pair<std::size_t,Terrain>> previous;
        bool clear=true;
        for(int step=0;step<count;++step) {
            const Vec2 p{double(x)+.5,double(y)+.5};
            if(x<2 || y<2 || x+2>=int(config.width) || y+2>=int(config.height)
                || (x+3>=int(x0) && x<=int(x1)+3 && y+3>=int(y0) && y<=int(y1)+3)) {clear=false;break;}
            // Keep food centers and their immediate approaches free.
            if(std::any_of(shelter_centers.begin(),shelter_centers.end(),[&](Vec2 c){return length(c-p)<1.5;})) {clear=false;break;}
            for(int yy=y-1;yy<=y+1;++yy) for(int xx=x-1;xx<=x+1;++xx)
                if(terrain[std::size_t(yy)*config.width+std::size_t(xx)]==Terrain::Wall) clear=false;
            if(!clear)break;
            const auto cell=std::size_t(y)*config.width+std::size_t(x);
            previous.emplace_back(cell,terrain[cell]);
            if(step==bend)direction=(direction+turn)%4;
            x+=dx[direction];y+=dy[direction];
        }
        if(!clear)continue;
        // Apply atomically, then reject any placement that disconnects outdoors.
        for(const auto& cell:previous)terrain[cell.first]=Terrain::Wall;
        if(!outdoor_connected()) {
            for(const auto& cell:previous)terrain[cell.first]=cell.second;
        } else wall_count+=previous.size();
    }
    cluster_rough_ground(*this,0.18);
    Random nursery_age_rng(config.seed ^ 0x6e757273616765ULL);
    Random food_age_rng(config.seed ^ 0x666f6f64616765ULL);
    const auto food = [&](Vec2 p, FoodKind kind, double energy, double capacity, double regrowth) {
        EcoResource r;
        r.id=resources.size()+1; r.position=p; r.kind=kind; r.energy_per_unit=energy;
        r.stock=r.capacity=capacity;
        if (config.outdoor_food_relocates && kind!=FoodKind::Pod && !in_nursery(p))
            r.stock *= food_age_rng.uniform(0.0,1.0);
        if (in_nursery(p) && config.nursery_food_decay>0) r.stock *= nursery_age_rng.uniform(0.0,1.0);
        r.regrowth=regrowth; resources.push_back(r);
    };
    // Finite separated patches. One patch's steady supply is below basal cost
    // at the preset, so a creature must move between patches and forage.
    if (config.nursery_food_relocates) {
        Random food_rng(config.seed ^ 0x6e757273666f6f64ULL);
        for (std::size_t i=0;i<config.nursery_food_patches;++i) {
            EcoResource r;r.id=resources.size()+1;r.kind=FoodKind::Graze;r.position={-100,-100};
            r.stock=r.capacity=config.nursery_food_capacity;r.energy_per_unit=config.nursery_food_energy;r.regrowth=0;
            if (config.nursery_food_decay>0) r.stock *= nursery_age_rng.uniform(0.0,1.0);
            if (!relocate_nursery_food(r,food_rng,false)) throw std::invalid_argument("Nursery food patches cannot fit with spacing; reduce patch count");
            resources.push_back(r);
        }
    } else for (auto y=y0+2; y+2<=y1; y+=2) for (auto x=x0+2; x+2<=x1; x+=2)
        food({double(x)+0.5,double(y)+0.5},FoodKind::Graze,config.nursery_food_energy,
            config.nursery_food_capacity,config.nursery_food_regrowth);
    fruit_a_rich=config.food_assignment<0 ? map_rng.chance(0.5) : config.food_assignment==0;
    std::vector<unsigned char> occupied(terrain.size());
    const auto next = [&](bool allow_shelter) {
        for(const auto cell:outside) {
            if(occupied[cell] || terrain[cell]==Terrain::Wall
                || (!allow_shelter && terrain[cell]==Terrain::Shelter))continue;
            occupied[cell]=1;return pos(cell);
        }
        throw std::invalid_argument("Too many frontier resources");
    };
    // Reserve open ground for restricted foods before placing flexible grazing.
    for (std::size_t i=0; i<config.fruit_patches; ++i) {
        const bool a=i%2==0;
        food(next(false),a?FoodKind::FruitA:FoodKind::FruitB,a==fruit_a_rich?config.rich_fruit_energy:config.poor_fruit_energy,
            config.fruit_capacity,config.fruit_regrowth);
    }
    for (std::size_t i=0; i<config.pods; ++i)
        food(next(false),FoodKind::Pod,config.pod_energy,config.pod_capacity,config.pod_regrowth);
    for (std::size_t i=0; i<config.grazing_patches; ++i)
        food(next(true),FoodKind::Graze,config.graze_energy,config.graze_capacity,config.graze_regrowth);
    // Ordinary grazing now supplies frontier shelters; no low-quality patches.
    Random spawn_rng(config.seed ^ 0x66726f6e74696572ULL);
    shuffle(spawning,spawn_rng);
    if (config.initial_creatures>spawning.size()) throw std::invalid_argument("Too many founders for nursery");
    for (std::size_t i=0; i<config.initial_creatures; ++i) {
        EcoCreature c;
        c.id=c.genome_id=next_creature_id++; c.position=pos(spawning[i]);
        c.heading=spawn_rng.uniform(-3.141592653589793,3.141592653589793);
        c.energy=config.founder_energy; c.controller=config.controller;
        Random genome_rng(config.seed ^ (c.id*104729ULL));
        c.brain=Brain::random(config.brain,genome_rng);
        c.neural_rng=Random(config.seed ^ (c.id*13007ULL));
        initialize_body(c);
        creatures.push_back(std::move(c));
    }
    if (config.reproduction && creatures.size()>=config.max_population) {
        capacity_limited=true;
        events.push_back({time(),"capacity_limited",0,0,0,double(creatures.size())});
    }
}
bool EcosystemWorld::relocate_nursery_food(EcoResource& resource, Random& rng, bool avoid_creatures)
{
    const auto x0=(config.width-config.nursery_size)/2,y0=(config.height-config.nursery_size)/2;
    std::vector<Vec2> candidates;
    for (auto y=y0+2;y<y0+config.nursery_size-2;++y) for (auto x=x0+2;x<x0+config.nursery_size-2;++x) {
        const Vec2 p{double(x)+.5,double(y)+.5};
        if (!traversable(p) || length(p-resource.position)<2) continue;
        if (std::any_of(resources.begin(),resources.end(),[&](const auto& r){return r.id!=resource.id && length(r.position-p)<1.8;})) continue;
        if (avoid_creatures && std::any_of(creatures.begin(),creatures.end(),[&](const auto& c){return length(c.position-p)<config.interaction_range+config.radius+.5;})) continue;
        candidates.push_back(p);
    }
    if (candidates.empty()) return false; // Remain depleted; retry next step, never overlap bodies.
    auto p=candidates[rng.uniform_index(candidates.size())];
    // Continuous jitter removes alignment to a predictable cell-center grid.
    p.x+=rng.uniform(-.15,.15);p.y+=rng.uniform(-.15,.15);
    resource.position=p;
    return true;
}
void EcosystemWorld::add_shelter_food(Vec2 position)
{
    EcoResource r;
    r.id=resources.size()+1; r.position=position; r.shelter_origin=position; r.shelter_food=true;
    r.stock=r.capacity=config.shelter_food_capacity;
    r.energy_per_unit=config.shelter_food_energy; r.regrowth=config.shelter_food_regrowth;
    resources.push_back(r);
}

bool EcosystemWorld::relocate_shelter_food(EcoResource& resource)
{
    const int cx=int(resource.shelter_origin.x),cy=int(resource.shelter_origin.y);
    const int low=int(config.shelter_size/2),high=int(config.shelter_size)-1-low;
    std::vector<Vec2> candidates;
    for(int y=cy-low;y<=cy+high;++y)for(int x=cx-low;x<=cx+high;++x) {
        Vec2 p{double(x)+.5,double(y)+.5};
        if(!sheltered(p)||in_nursery(p)||length(p-resource.position)<.8)continue;
        if(std::any_of(resources.begin(),resources.end(),[&](const auto& r){return r.id!=resource.id&&length(r.position-p)<.8;}))continue;
        candidates.push_back(p);
    }
    if(candidates.empty())return false;
    resource.position=candidates[map_rng.uniform_index(candidates.size())];
    return true;
}

bool EcosystemWorld::relocate_outdoor_food(EcoResource& resource)
{
    const auto valid = [&](Vec2 p) {
        if (!traversable(p) || (sheltered(p) && !(config.nursery_frontier && resource.kind==FoodKind::Graze)) || in_nursery(p) || length(p-resource.position)<1.0) return false;
        if (std::any_of(resources.begin(),resources.end(),[&](const auto& r) {
            return r.id!=resource.id && length(r.position-p)<0.8;
        })) return false;
        return true;
    };
    // Fast rejection sampling; exhaustive fallback handles crowded maps without
    // losing a patch quota. No creature exclusion: all valid outdoor cells qualify.
    for (int attempt=0;attempt<256;++attempt) {
        Vec2 p{double(map_rng.uniform_index(config.width))+.5,
               double(map_rng.uniform_index(config.height))+.5};
        if (valid(p)) { resource.position=p; return true; }
    }
    std::vector<Vec2> candidates;
    for (std::size_t y=0;y<config.height;++y) for (std::size_t x=0;x<config.width;++x) {
        Vec2 p{double(x)+.5,double(y)+.5};
        if (valid(p)) candidates.push_back(p);
    }
    if (candidates.empty()) return false; // Keep the empty slot and retry next step.
    resource.position=candidates[map_rng.uniform_index(candidates.size())];
    return true;
}
} // namespace neuroevo
