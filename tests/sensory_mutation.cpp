#include "fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace neuroevo;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

MutationConfig growth_only()
{
    MutationConfig mutation;
    mutation.mutate_weight_probability = mutation.mutate_neuron_probability = 0;
    mutation.add_neuron_probability = mutation.add_reciprocal_motif_probability = 0;
    mutation.add_autapse_probability = 0;
    mutation.remove_neuron_probability = mutation.remove_synapse_probability = 0;
    mutation.rewire_synapse_probability = 0;
    mutation.add_synapse_probability = 1;
    return mutation;
}

void category_distribution(bool extended)
{
    const auto& groups = ecosystem_input_groups(extended);
    BrainConfig config;
    config.input_count = extended ? eco_input_count : eco_legacy_input_count;
    config.hidden_count = 0; config.output_count = 1;
    const Brain parent(config);
    std::vector<std::size_t> membership(config.input_count, groups.size());
    for (std::size_t group = 0; group < groups.size(); ++group)
        for (auto input : groups[group]) {
            require(input < membership.size() && membership[input] == groups.size(), "Invalid sensory partition");
            membership[input] = group;
        }
    require(std::find(membership.begin(), membership.end(), groups.size()) == membership.end(), "Unclassified input");
    const auto labels = ecosystem_input_labels(extended);
    const auto storm = std::find(labels.begin(), labels.end(), "storm_cue") - labels.begin();
    require(groups[membership[storm]].size() == 1, "Storm must be a singleton category");
    if (extended) {
        const auto shelter = membership[eco_shelter_offset];
        require(groups[shelter].size() == eco_sectors, "Shelter sectors must share one category");
        for (std::size_t sector = 0; sector < eco_sectors; ++sector)
            require(membership[eco_shelter_offset + sector] == shelter, "Shelter category split across sectors");
    }
    std::vector<std::size_t> counts(groups.size()), inputs(config.input_count);
    Random rng(1234);
    const auto mutation = growth_only();
    for (std::size_t trial = 0; trial < groups.size() * 1000; ++trial) {
        auto child = parent;
        child.mutate(mutation, rng, groups);
        require(!child.synapses().empty(), "Growth failed on an empty sensory graph");
        const auto pre = child.synapses()[0].pre;
        require(child.synapses().size() == groups[membership[pre]].size(), "Growth did not add the entire sensory category");
        require(std::abs(std::abs(child.synapses()[0].weight) - 0.5 * 32 / config.synaptic_gain) < 1e-12,
            "New stable synapse did not use the increased magnitude");
        ++counts[membership[pre]];
        for (const auto& edge : child.synapses()) {
            require(membership[edge.pre] == membership[pre] && edge.post == child.synapses()[0].post
                && edge.weight == child.synapses()[0].weight, "Sensory group did not share one destination and weight");
            ++inputs[edge.pre];
        }
    }
    for (auto count : counts) require(count > 800 && count < 1200, "Sensory categories are not equally sampled");
    for (std::size_t group = 0; group < groups.size(); ++group)
        for (auto input : groups[group]) {
            require(inputs[input] == counts[group], "Sensory group growth omitted a direction");
        }
}

void saturated_categories()
{
    BrainConfig config;
    config.input_count = 6; config.hidden_count = 0; config.output_count = 1;
    const Brain::InputGroups groups{{0}, {1, 2, 3, 4, 5}};
    std::vector<Brain::Neuron> neurons(7);
    std::vector<Brain::Synapse> edges{{1, 6, 2, 1}, {2, 6, -3, 1}, {3, 6, 1, 1}, {4, 6, 1, 1}};
    const auto parent = Brain::from_components(config, neurons, edges);
    Random rng(5678);
    std::size_t singleton = 0;
    for (int trial = 0; trial < 4000; ++trial) {
        auto child = parent;
        child.mutate(growth_only(), rng, groups);
        require(child.synapses().size() == 5, "Partially saturated graph failed growth");
        singleton += child.synapses().back().pre == 0;
        child.mutate(growth_only(), rng, groups);
        require(child.synapses().size() == 6, "Fully occupied category blocked remaining category");
        child.mutate(growth_only(), rng, groups);
        require(child.synapses().size() == 6, "Saturated graph created duplicate edges");
        for (std::size_t i=0; i<edges.size(); ++i)
            require(child.synapses()[i].weight==edges[i].weight, "Group addition overwrote an existing specialized weight");
    }
    require(singleton > 1800 && singleton < 2200, "Partial saturation biased category choice");
}

void sensory_thresholds()
{
    BrainConfig config;
    config.input_count=4; config.hidden_count=0; config.output_count=0;
    config.calibrated_io=true; config.background_activity_enabled=false;
    const Brain parent(config);
    const Brain::InputGroups groups{{0},{1,2,3}};
    auto mutation=growth_only();
    mutation.structural_edit_probability=0;
    mutation.local_edit_limit=1;
    mutation.mutate_neuron_probability=1;
    mutation.threshold_sigma=0.2;
    Random rng(328);
    std::size_t singleton=0,other=0;
    for (int trial=0;trial<4000;++trial) {
        auto child=parent; child.mutate(mutation,rng,groups);
        std::size_t changed=0;
        for (std::size_t i=0;i<4;++i) {
            const auto& n=child.neurons()[i];
            require(n.threshold>=0.2 && n.threshold<=5,"Sensory threshold escaped bounds");
            if (n.threshold!=1) { ++changed; if (i==0)++singleton; else ++other; }
        }
        require(changed==1,"Sensory threshold mutation exceeded one local edit");
        require(child.synapses().empty(),"Threshold mutation changed topology");
    }
    require(singleton>1800 && singleton<2200 && other==4000-singleton,
        "Sensory threshold mutations were biased toward larger categories");
    mutation.mutate_neuron_probability=0;
    auto disabled=parent; disabled.mutate(mutation,rng,groups);
    for(const auto& n:disabled.neurons())require(n.threshold==1,"Disabled sensory mutations changed thresholds");
    auto slow_neurons=parent.neurons();
    for(auto& n:slow_neurons)n.threshold=2;
    auto slow=Brain::from_components(config,slow_neurons,{}), fast=parent;
    std::size_t fast_spikes=0,slow_spikes=0;
    for(int i=0;i<100;++i){fast_spikes+=fast.step({0.5,0.5,0.5,0.5}).spikes;slow_spikes+=slow.step({0.5,0.5,0.5,0.5}).spikes;}
    require(fast_spikes>slow_spikes && slow_spikes>0,"Sensory threshold failed to modulate firing rate");
}

void group_edit_contract()
{
    BrainConfig config;
    config.input_count=3; config.hidden_count=0; config.output_count=2;
    const Brain::InputGroups groups{{0,1,2}};
    std::vector<Brain::Neuron> neurons(5);
    for (std::size_t i=0;i<neurons.size();++i) neurons[i].position={0.1*double(i),0.5};
    const auto partial=Brain::from_components(config,neurons,{{0,3,2,1},{1,3,-3,1}});
    Random rng(442);
    for (int trial=0;trial<200;++trial) {
        auto child=partial;child.mutate(growth_only(),rng,groups);
        const auto& edges=child.synapses();
        require(edges.size()==3 || edges.size()==5,"Group addition spanned destinations or added only part of a group");
        require(edges[0].weight==2 && edges[1].weight==-3,"Existing group weights changed");
        const auto post=edges.back().post;
        for (std::size_t input=0;input<3;++input)
            require(std::count_if(edges.begin(),edges.end(),[&](const auto& e){return e.pre==input && e.post==post;})==1,
                "Destination lacks exactly one edge per sensor");
        for (std::size_t i=2;i<edges.size();++i) {
            require(edges[i].post==post && edges[i].weight==edges.back().weight,"New members differ in weight or destination");
            const auto expected=std::clamp<std::size_t>(std::max<std::size_t>(1,
                static_cast<std::size_t>(std::ceil(length(neurons[edges[i].pre].position-neurons[post].position)
                    /config.conduction_speed/config.dt))),1,config.max_delay_steps);
            require(edges[i].delay_steps==expected,"Group addition lost source-specific delay");
        }
    }

    std::vector<Brain::Synapse> edges;
    for (std::size_t post=3;post<5;++post)
        for (std::size_t pre=0;pre<3;++pre) edges.push_back({pre,post,double(pre+1),1});
    const auto parent=Brain::from_components(config,neurons,edges);
    auto removal=growth_only();removal.add_synapse_probability=0;removal.remove_synapse_probability=1;
    std::size_t grouped=0;
    for (int trial=0;trial<4000;++trial) {
        auto child=parent;child.mutate(removal,rng,groups);
        const auto size=child.synapses().size();
        require(size==3 || size==5,"Removal was neither one edge nor one complete category at a destination");
        if(size==3) {
            ++grouped;
            for(const auto& e:child.synapses())require(e.post==child.synapses()[0].post,"Group removal affected multiple destinations");
        }
        for(const auto& e:child.synapses())require(e.weight==double(e.pre+1),"Removal changed surviving weights");
    }
    require(grouped>1800 && grouped<2200,"Group removal is not a 50/50 choice");
    auto ungrouped=parent;ungrouped.mutate(removal,rng);
    require(ungrouped.synapses().size()==5,"Ungrouped removal deleted multiple edges");

    auto weights=growth_only();weights.structural_edit_probability=0;
    weights.mutate_weight_probability=1;weights.local_edit_limit=1;
    auto specialized=parent;specialized.mutate(weights,rng,groups);
    std::size_t changed=0;
    for(std::size_t i=0;i<edges.size();++i)changed+=specialized.synapses()[i].weight!=edges[i].weight;
    require(changed==1,"Weight mutation coupled members of a sensor group");

    // A disconnected hidden neuron should receive the whole category when repaired.
    config.hidden_count=1;
    neurons.resize(6);
    const auto repair=Brain::from_components(config,neurons,{{3,4,1,1}});
    auto repaired=repair;repaired.mutate(growth_only(),rng,groups);
    require(repaired.synapses().size()==4,"Repair added only one direction");
    for(std::size_t i=1;i<4;++i)require(repaired.synapses()[i].post==3,"Group repair missed disconnected hidden neuron");
    const auto hidden_only=Brain::from_components(config,neurons,{{3,4,1,1},{3,5,1,1}});
    auto pruned=hidden_only;pruned.mutate(removal,rng,groups);
    require(pruned.synapses().size()==1,"Hidden-origin removal changed behavior");
}

void rewiring_contract()
{
    BrainConfig config;
    config.input_count=3; config.hidden_count=4; config.output_count=2;
    Random initial(51);
    const auto parent=Brain::random(config,initial);
    {
        auto mutation=growth_only();
        mutation.add_synapse_probability=0;
        mutation.rewire_synapse_probability=1;
        std::size_t source=0,destination=0;
        for(std::uint64_t seed=0;seed<512;++seed) {
            auto child=parent; Random rng(seed); child.mutate(mutation,rng);
            require(child.synapses().size()==parent.synapses().size()
                && child.neurons().size()==parent.neurons().size(),"Rewiring changed graph size");
            std::size_t changes=0;
            for(std::size_t i=0;i<child.synapses().size();++i) {
                const auto& a=parent.synapses()[i];const auto& b=child.synapses()[i];
                require(a.weight==b.weight,"Rewiring changed a weight");
                if(a.pre!=b.pre || a.post!=b.post) {
                    ++changes; require((a.pre==b.pre)!=(a.post==b.post),"Rewiring moved both ends");
                    source+=a.pre!=b.pre; destination+=a.post!=b.post;
                    const auto expected=std::clamp<std::size_t>(std::max<std::size_t>(1,
                        static_cast<std::size_t>(std::ceil(length(child.neurons()[b.pre].position-child.neurons()[b.post].position)
                            /config.conduction_speed/config.dt))),1,config.max_delay_steps);
                    require(b.delay_steps==expected,"Rewiring failed to recalculate delay");
                }
                require(b.pre<config.input_count+config.hidden_count && b.post>=config.input_count
                    && b.pre!=b.post,"Rewiring created invalid routing");
                for(std::size_t j=0;j<i;++j)require(child.synapses()[j].pre!=b.pre
                    || child.synapses()[j].post!=b.post,"Rewiring created duplicate edges");
            }
            require(changes==1,"Rewiring failed to move exactly one endpoint on an open graph");
            child.step({0.5,0.5,0.5});
        }
        require(source>190 && source<322 && destination==512-source,"Endpoint selection is biased");
        Brain empty(config); Random rng(9); empty.mutate(mutation,rng);
        require(empty.synapses().empty(),"Rewiring added a synapse to an empty graph");
    }
}

int main()
{
    try {
        sensory_thresholds();
        group_edit_contract();
        rewiring_contract();
        {
            for (bool extended : {false, true}) category_distribution(extended);
            saturated_categories();
        }
        std::cout << "Sensory mutation sampling passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
