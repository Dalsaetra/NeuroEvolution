#include "neuroevo/ecosystem.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace neuroevo {
EcosystemConfig nursery_frontier_config()
{
    EcosystemConfig c;
    c.nursery_frontier = true;
    c.width = c.height = 80;
    c.max_population = 256;
    c.shelters = 24;
    c.grazing_patches = 120; c.fruit_patches = 64; c.pods = 24;
    c.graze_energy = 14; c.poor_fruit_energy = 16; c.rich_fruit_energy = 36; c.pod_energy = 60;
    c.interaction_degrees = 80;
    c.calm_duration = 180; c.warning_duration = 45; c.storm_duration = 60; c.storm_cost = 1.2;
    c.maturity_age = 60; c.reproduction_threshold = 110;
    c.reproduction_cost = 65; c.offspring_energy = 60; c.reproduction_cooldown = 90;
    c.establishment = false; c.archive_eval_trials = 0;
    return c;
}

void EcosystemWorld::generate_nursery_frontier()
{
    const auto x0 = (config.width - config.nursery_size) / 2;
    const auto y0 = (config.height - config.nursery_size) / 2;
    const auto x1 = x0 + config.nursery_size - 1, y1 = y0 + config.nursery_size - 1;
    const auto cx = x0 + config.nursery_size / 2, cy = y0 + config.nursery_size / 2;
    const auto pos = [&](std::size_t cell) { return Vec2{double(cell % config.width)+0.5,double(cell/config.width)+0.5}; };
    std::vector<std::size_t> outside, spawning;
    for (std::size_t y=0; y<config.height; ++y) for (std::size_t x=0; x<config.width; ++x) {
        const auto cell=y*config.width+x;
        if (x==0 || y==0 || x+1==config.width || y+1==config.height) { terrain[cell]=Terrain::Wall; continue; }
        if (in_nursery(pos(cell))) {
            // Porous perimeter: four three-cell gates, no teleporting or confinement.
            const bool boundary=x==x0 || x==x1 || y==y0 || y==y1;
            const bool gate=((x==x0 || x==x1) && y+1>=cy && y<=cy+1)
                || ((y==y0 || y==y1) && x+1>=cx && x<=cx+1);
            terrain[cell]=boundary && !gate ? Terrain::Wall : Terrain::Shelter;
            if (x>x0+1 && x+2<x1 && y>y0+1 && y+2<y1) spawning.push_back(cell);
        } else {
            terrain[cell]=map_rng.chance(0.18) ? Terrain::Rough : Terrain::Ground;
            // Isolated rock pillars require navigation without partitioning the map.
            const bool near = x+3>=x0 && x<=x1+3 && y+3>=y0 && y<=y1+3;
            if (!near && x%7==2 && y%7==2) terrain[cell]=Terrain::Wall;
            if (!near && terrain[cell]!=Terrain::Wall) outside.push_back(cell);
        }
    }
    const auto shuffle = [](auto& v, Random& rng) {
        for (std::size_t i=v.size(); i>1; --i) std::swap(v[i-1],v[rng.uniform_index(i)]);
    };
    shuffle(outside,map_rng);
    std::size_t placed=0;
    for (const auto cell:outside) {
        if (placed==config.shelters) break;
        const auto x=cell%config.width,y=cell/config.width;
        if (x<2 || y<2 || x+2>=config.width || y+2>=config.height) continue;
        bool clear=true;
        for (std::size_t yy=y-1; yy<=y+1; ++yy) for (std::size_t xx=x-1; xx<=x+1; ++xx)
            if (terrain[yy*config.width+xx]==Terrain::Wall || terrain[yy*config.width+xx]==Terrain::Shelter) clear=false;
        if (!clear) continue;
        for (std::size_t yy=y-1; yy<=y+1; ++yy) for (std::size_t xx=x-1; xx<=x+1; ++xx)
            terrain[yy*config.width+xx]=Terrain::Shelter;
        ++placed;
    }
    if (placed!=config.shelters) throw std::invalid_argument("Not enough frontier space for shelters");
    const auto food = [&](Vec2 p, FoodKind kind, double energy, double capacity, double regrowth) {
        EcoResource r;
        r.id=resources.size()+1; r.position=p; r.kind=kind; r.energy_per_unit=energy;
        r.stock=r.capacity=capacity; r.regrowth=regrowth; resources.push_back(r);
    };
    // Finite separated patches. One patch's steady supply is below basal cost
    // at the preset, so a creature must move between patches and forage.
    for (auto y=y0+2; y+2<=y1; y+=2) for (auto x=x0+2; x+2<=x1; x+=2)
        food({double(x)+0.5,double(y)+0.5},FoodKind::Graze,config.nursery_food_energy,
            config.nursery_food_capacity,config.nursery_food_regrowth);
    fruit_a_rich=config.food_assignment<0 ? map_rng.chance(0.5) : config.food_assignment==0;
    std::size_t cursor=0;
    const auto next = [&]() {
        while (cursor<outside.size() && terrain[outside[cursor]]==Terrain::Shelter) ++cursor;
        if (cursor==outside.size()) throw std::invalid_argument("Too many frontier resources");
        return pos(outside[cursor++]);
    };
    for (std::size_t i=0; i<config.grazing_patches; ++i)
        food(next(),FoodKind::Graze,config.graze_energy,config.graze_capacity,config.graze_regrowth);
    for (std::size_t i=0; i<config.fruit_patches; ++i) {
        const bool a=i%2==0;
        food(next(),a?FoodKind::FruitA:FoodKind::FruitB,a==fruit_a_rich?config.rich_fruit_energy:config.poor_fruit_energy,
            config.fruit_capacity,config.fruit_regrowth);
    }
    for (std::size_t i=0; i<config.pods; ++i)
        food(next(),FoodKind::Pod,config.pod_energy,config.pod_capacity,config.pod_regrowth);
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
        creatures.push_back(std::move(c));
    }
    if (config.reproduction && creatures.size()>=config.max_population) {
        capacity_limited=true;
        events.push_back({time(),"capacity_limited",0,0,0,double(creatures.size())});
    }
}
} // namespace neuroevo
