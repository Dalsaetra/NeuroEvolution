#include "neuroevo/brain.hpp"
#include "neuroevo/fitness.hpp"
#include "neuroevo/neat.hpp"
#include "neuroevo/sensorimotor.hpp"
#include "neuroevo/task.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}

bool close(double lhs, double rhs, double epsilon = 1e-9)
{
    return std::abs(lhs - rhs) <= epsilon;
}

} // namespace

int main()
{
    {
        neuroevo::TaskConfig config;
        config.regime = neuroevo::TaskRegimeKind::CueOcclusion;
        config.cue_visible_steps = 2;
        config.occlusion_min_steps = 3;
        config.occlusion_max_steps = 3;
        config.reveal_distance = 0.05;

        neuroevo::Random rng(4);
        auto task = neuroevo::make_task_scenario(config, {});
        task->reset({0.0, 0.0}, rng);
        const auto cue = task->observe({0.0, 0.0}, 0);
        const neuroevo::Vec2 far_from_target{cue.target.x + 1.0, cue.target.y + 1.0};
        const auto hidden = task->observe(far_from_target, 2);
        const auto revealed = task->observe(far_from_target, 5);
        if (cue.phase != neuroevo::TaskPhase::Cue || !cue.sensory_available) {
            return fail("Cue-occlusion task should begin with a visible cue");
        }
        if (hidden.phase != neuroevo::TaskPhase::Occluded || hidden.sensory_available) {
            return fail("Cue-occlusion task did not enter its hidden phase");
        }
        const auto collection = task->advance(cue.target, 2, rng);
        if (collection.occluded_foods_collected != 1.0) {
            return fail("Collection during the scheduled hidden interval should be recorded");
        }
        if (revealed.phase != neuroevo::TaskPhase::Revealed || !revealed.sensory_available) {
            return fail("Cue-occlusion task did not reveal the target after the delay");
        }
    }

    {
        const auto sensory = neuroevo::sense_target(
            neuroevo::SensorimotorRegimeKind::TargetVector,
            {{0.0, 0.0}, 0.0},
            {1.0, 1.0},
            2.0,
            3.14159265358979323846,
            false);
        if (sensory.target_visible
            || std::any_of(sensory.inputs.begin(), sensory.inputs.end(), [](double value) { return value != 0.0; })) {
            return fail("An unavailable task target should produce zero target-vector inputs");
        }
    }

    {
        neuroevo::BrainConfig config;
        config.input_count = 0;
        config.hidden_count = 1;
        config.output_count = 0;
        config.dt = 0.02;
        config.background_activity_enabled = true;
        config.background_event_rate_hz = 50.0;
        config.background_event_current = 100.0;

        neuroevo::Brain::Neuron neuron;
        neuron.threshold = 1.0;
        neuron.background_sensitivity = 1.0;
        auto active = neuroevo::Brain::from_components(config, {neuron}, {});
        neuroevo::Random rng(8);
        if (active.step({}, &rng).spikes != 1) {
            return fail("Poisson background event should be scaled by neuron sensitivity");
        }

        neuron.background_sensitivity = 0.0;
        auto insensitive = neuroevo::Brain::from_components(config, {neuron}, {});
        neuroevo::Random rng2(8);
        if (insensitive.step({}, &rng2).spikes != 0) {
            return fail("Zero background sensitivity should reject background current");
        }
    }

    {
        const double clamped = neuroevo::clamp_subthreshold_bias(100.0, 1.0, 0.1, 0.95, -15.0);
        if (!close(clamped, 9.5) || clamped * 0.1 >= 1.0) {
            return fail("Bias clamp should keep isolated steady-state potential below threshold");
        }
    }

    {
        neuroevo::BrainConfig config;
        config.input_count = 0;
        config.hidden_count = 2;
        config.output_count = 0;
        config.initial_connection_probability = 0.0;
        neuroevo::Random rng(12);
        auto brain = neuroevo::Brain::random(config, rng);

        neuroevo::MutationConfig mutation;
        mutation.mutate_weight_probability = 0.0;
        mutation.mutate_neuron_probability = 1.0;
        mutation.threshold_sigma = 0.0;
        mutation.hidden_bias_jump_probability = 1.0;
        mutation.add_synapse_probability = 0.0;
        mutation.remove_synapse_probability = 0.0;
        mutation.add_reciprocal_motif_probability = 1.0;
        brain.mutate(mutation, rng);

        if (brain.synapses().size() != 2) {
            return fail("Scalar reciprocal motif mutation should add both directions");
        }
        for (const auto& neuron : brain.neurons()) {
            if (neuron.bias * config.membrane_tau >= neuron.threshold) {
                return fail("Scalar bias mutation produced an isolated self-spiking neuron");
            }
        }
    }

    {
        neuroevo::BrainConfig config;
        config.input_count = 1;
        config.sensory_input_count = 1;
        config.hidden_count = 1;
        config.output_count = 1;
        config.initial_connection_probability = 0;
        std::vector<neuroevo::Brain::Neuron> neurons(3);
        neurons[0].position = {0.1, 0.5};
        neurons[1].position = {0.5, 0.5};
        neurons[2].position = {0.9, 0.5};
        auto brain = neuroevo::Brain::from_components(config, neurons, {{0, 2, 2.0, 1}});
        neuroevo::MutationConfig mutation;
        mutation.mutate_weight_probability = 0;
        mutation.mutate_neuron_probability = 0;
        mutation.add_synapse_probability = 0;
        mutation.add_neuron_probability = 1;
        mutation.add_reciprocal_motif_probability = 0;
        mutation.remove_synapse_probability = 0;
        mutation.max_hidden_neurons = 2;
        neuroevo::Random rng(44);
        brain.mutate(mutation, rng);
        if (brain.config().hidden_count != 2 || brain.neurons().size() != 4 || brain.synapses().size() != 3) {
            return fail("Scalar add-neuron mutation did not grow one hidden node and a two-edge branch");
        }
        const auto has = [&](std::size_t pre, std::size_t post) {
            return std::any_of(brain.synapses().begin(), brain.synapses().end(), [&](const auto& edge) {
                return edge.pre == pre && edge.post == post;
            });
        };
        if (!has(0, 3) || !has(0, 2) || !has(2, 3)) {
            return fail("Add-neuron mutation did not preserve the parent edge and its new branch");
        }
    }

    {
        neuroevo::Genome genome;
        genome.nodes = {
            {1, neuroevo::NodeKind::Hidden, {0.4, 0.4}, 0.0, 1.0, 0.1},
            {2, neuroevo::NodeKind::Hidden, {0.6, 0.6}, 0.0, 1.0, 0.1},
        };
        neuroevo::BrainConfig brain;
        brain.input_count = 0;
        brain.output_count = 0;
        neuroevo::InnovationTracker tracker(3);
        neuroevo::Random rng(9);
        if (!neuroevo::mutate_add_reciprocal_motif(genome, brain, tracker, rng)) {
            return fail("Reciprocal motif mutation should connect two hidden neurons");
        }
        if (!genome.has_connection(1, 2) || !genome.has_connection(2, 1)) {
            return fail("Reciprocal motif mutation did not add both directions");
        }
    }

    {
        neuroevo::BrainConfig brain;
        brain.input_count = 5;
        brain.sensory_input_count = 4;
        brain.output_count = 3;
        brain.has_episode_start_input = true;
        brain.episode_start_input_index = 4;
        brain.initial_connection_probability = 1.0;
        neuroevo::InnovationTracker tracker(8);
        neuroevo::Random rng(15);
        const auto genome = neuroevo::make_minimal_genome(brain, tracker, rng, 2);

        bool start_reaches_hidden = false;
        for (const auto& connection : genome.connections) {
            const auto* target = genome.find_node(connection.target_node_id);
            if (connection.source_node_id == brain.episode_start_input_index
                && target != nullptr
                && target->kind == neuroevo::NodeKind::Hidden) {
                start_reaches_hidden = true;
            }
            if (connection.source_node_id == brain.episode_start_input_index
                && target != nullptr
                && target->kind == neuroevo::NodeKind::Output) {
                return fail("Episode-start input should not be seeded directly to a motor output");
            }
        }
        if (!start_reaches_hidden) {
            return fail("Episode-start input should be eligible for initial hidden wiring");
        }
    }

    {
        neuroevo::FitnessConfig config;
        config.regime = neuroevo::FitnessRegimeKind::Sparse;
        auto fitness = neuroevo::make_fitness_regime(config);
        neuroevo::FitnessStepContext context;
        context.task_reward = 10.0;
        context.progress = 1.0;
        const auto score = fitness->score_step(context);
        if (!close(score.reward, 10.0) || !close(fitness->terminal_penalty(1.0), 0.0)) {
            return fail("Sparse fitness should use task reward without distance shaping");
        }
    }

    return 0;
}
