#include "neuroevo/ecosystem.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace neuroevo {
namespace {
constexpr double pi=3.141592653589793, epsilon=1e-9;
// A smooth irregular footprint fits entirely within the reserved source disc.
bool field_contains(const FoodSource& source, Vec2 p)
{
    const auto d=p-source.position;
    const double a=std::atan2(d.y,d.x);
    const double edge=source.radius*(.82+.10*std::sin(3*a+source.phase)+.07*std::sin(5*a-source.phase));
    return length(d)<=edge;
}
}
const char* food_distribution_name(FoodDistribution preset)
{
    switch(preset) {
        case FoodDistribution::Scattered:return "scattered";
        case FoodDistribution::FieldsAndTrees:return "fields-and-trees";
    }
    throw std::invalid_argument("Invalid food distribution");
}
const char* to_string(FoodSourceKind kind)
{
    switch(kind) {
        case FoodSourceKind::Field:return "field";
        case FoodSourceKind::FruitTree:return "fruit-tree";
        case FoodSourceKind::PodTree:return "pod-tree";
    }
    throw std::invalid_argument("Invalid food source");
}
bool EcosystemWorld::reserved_for_food(Vec2 p, double margin) const
{
    return std::any_of(food_sources.begin(),food_sources.end(),[&](const auto& source) {
        return length(p-source.position)<source.radius+margin;
    });
}
void EcosystemWorld::place_food_sources()
{
    const auto& cfg=config.food_sources;
    Random rng(config.seed ^ 0x666f6f64736f7572ULL);
    const double nx=double((config.width-config.nursery_size)/2);
    const double ny=double((config.height-config.nursery_size)/2);
    const auto add=[&](FoodSourceKind kind,double radius) {
        const double lower=radius+3, xmax=config.width-lower, ymax=config.height-lower;
        if(xmax<=lower || ymax<=lower) throw std::invalid_argument("Food source too large for world");
        double best=-1;Vec2 choice;
        // Best-candidate disc packing spreads sources and leaves empty corridors.
        // Large fields are reserved first; shelters and walls use these reservations.
        for(int attempt=0;attempt<2048;++attempt) {
            const Vec2 p{rng.uniform(lower,xmax),rng.uniform(lower,ymax)};
            const double dx=std::max({nx-p.x,0.0,p.x-(nx+config.nursery_size)});
            const double dy=std::max({ny-p.y,0.0,p.y-(ny+config.nursery_size)});
            double clearance=std::min({std::hypot(dx,dy)-radius-3,
                p.x-lower,xmax-p.x,p.y-lower,ymax-p.y});
            for(const auto& other:food_sources)
                clearance=std::min(clearance,length(p-other.position)-radius-other.radius-cfg.source_gap);
            if(clearance>best) {best=clearance;choice=p;}
        }
        if(best<0) throw std::invalid_argument("Food sources cannot fit with requested spacing; reduce source counts/radii/gap or enlarge the world");
        food_sources.push_back({food_sources.size()+1,kind,choice,radius,rng.uniform(0,2*pi)});
    };
    for(std::size_t i=0;i<cfg.fields;++i)add(FoodSourceKind::Field,cfg.field_radius);
    for(std::size_t i=0;i<cfg.fruit_trees;++i)add(FoodSourceKind::FruitTree,cfg.tree_radius);
    for(std::size_t i=0;i<cfg.pod_trees;++i)add(FoodSourceKind::PodTree,cfg.tree_radius);
}
void EcosystemWorld::generate_source_food()
{
    const auto& cfg=config.food_sources;
    Random rng(config.seed ^ 0x726970656e696e67ULL);
    std::size_t fruit_tree=0;
    const auto append=[&](EcoResource resource) {
        if(!traversable(resource.position) || sheltered(resource.position) || in_nursery(resource.position))
            throw std::runtime_error("Food source overlaps shelter or obstacle");
        resource.id=resources.size()+1;
        resources.push_back(resource);
    };
    for(const auto& source:food_sources) {
        if(source.kind==FoodSourceKind::Field) {
            const double spacing=cfg.field_spacing;
            for(double y=source.position.y-source.radius;y<=source.position.y+source.radius;y+=spacing)
                for(double x=source.position.x-source.radius;x<=source.position.x+source.radius;x+=spacing) {
                    const Vec2 p{x,y};if(!field_contains(source,p))continue;
                    const auto d=p-source.position;
                    const double radial=std::max(0.0,1-length(d)/source.radius);
                    const double variation=.5+.25*std::sin(d.x*.35+source.phase)+.25*std::cos(d.y*.29-source.phase);
                    const double density=.25+.75*(.55*radial+.45*variation);
                    EcoResource r;r.source_id=source.id;r.position=p;r.kind=FoodKind::Graze;
                    // Scale by cell area: changing grid resolution does not multiply production.
                    r.capacity=cfg.field_capacity*density*spacing*spacing;
                    r.regrowth=cfg.field_regrowth*density*spacing*spacing;
                    r.stock=r.capacity*rng.uniform(.35,1);r.energy_per_unit=cfg.field_energy;
                    append(r);
                }
        } else {
            const bool fruit=source.kind==FoodSourceKind::FruitTree;
            const std::size_t count=fruit?cfg.fruit_sites:cfg.pod_sites;
            const auto kind=fruit ? ((fruit_tree++%2)==0 ? FoodKind::FruitA:FoodKind::FruitB):FoodKind::Pod;
            for(std::size_t i=0;i<count;++i) {
                const double angle=source.phase+2*pi*i/count;
                const double radius=source.radius*.75;
                EcoResource r;r.source_id=source.id;r.kind=kind;
                r.position={source.position.x+radius*std::cos(angle),source.position.y+radius*std::sin(angle)};
                r.capacity=fruit?config.fruit_capacity:config.pod_capacity;
                r.regrowth=(fruit?cfg.fruit_production:cfg.pod_production)/count;
                r.energy_per_unit=fruit ? ((kind==FoodKind::FruitA)==fruit_a_rich ? config.rich_fruit_energy:config.poor_fruit_energy):config.pod_energy;
                if(fruit) {
                    if(rng.chance(.4))r.stock=r.capacity;
                    else r.ripening_remaining=rng.uniform(0,r.capacity/r.regrowth);
                } else {
                    r.stock=r.capacity*rng.uniform(.2,1);
                    r.pod_state=PodState::Refilling;
                }
                append(r);
            }
        }
    }
}
void EcosystemWorld::renew_source_food(double end, bool storm)
{
    for(auto& r:resources) {
        if(!r.source_id)continue;
        if(r.kind==FoodKind::FruitA || r.kind==FoodKind::FruitB) {
            const double spoiled=std::min(r.stock,config.fruit_decay*config.dt);
            r.stock-=spoiled;totals.spoiled_biomass+=spoiled;
            if(r.stock<=epsilon && r.ripening_remaining<0) r.ripening_remaining=r.capacity/r.regrowth;
            if(r.ripening_remaining>=0 && !storm) {
                r.ripening_remaining=std::max(0.0,r.ripening_remaining-config.dt);
                if(r.ripening_remaining<=epsilon) {
                    totals.regrown_biomass+=r.capacity-r.stock;r.stock=r.capacity;
                    r.ripening_remaining=-1;r.respawn_at=-1;
                    events.push_back({end,"fruit_ripened",0,0,r.id,r.stock});
                }
            }
            continue;
        }
        if(storm || (r.kind==FoodKind::Pod && r.pod_state!=PodState::Refilling))continue;
        const double growth=std::max(0.0,std::min(r.capacity-r.stock,r.regrowth*config.dt));
        r.stock+=growth;totals.regrown_biomass+=growth;
        if(r.stock>epsilon)r.respawn_at=-1;
        if(r.kind==FoodKind::Pod && r.stock+epsilon>=r.capacity) {
            r.pod_state=PodState::Closed;r.progress=0;
            events.push_back({end,"pod_ripened",0,0,r.id,r.stock});
        }
    }
}
} // namespace neuroevo
