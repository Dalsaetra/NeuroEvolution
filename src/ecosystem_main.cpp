#include "neuroevo/ecosystem.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void request_stop(int) { stop_requested = 1; }
std::size_t integer(const std::string& value, const std::string& name)
{
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument(name + " requires a non-negative integer");
    std::size_t end = 0;
    const auto n = std::stoull(value,&end);
    if (end != value.size() || n > std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument("Invalid value for " + name);
    return static_cast<std::size_t>(n);
}
double number(const std::string& value, const std::string& name)
{
    std::size_t end=0;
    const double n=std::stod(value,&end);
    if (end != value.size() || !std::isfinite(n)) throw std::invalid_argument(name + " requires a finite number");
    return n;
}
bool boolean(const std::string& value, const std::string& name)
{
    if (value != "0" && value != "1") throw std::invalid_argument(name + " requires 0 or 1");
    return value == "1";
}
neuroevo::EcosystemWorld load(const std::string& path)
{
    std::ifstream s(path);
    if (!s) throw std::runtime_error("Cannot read checkpoint: " + path);
    return neuroevo::EcosystemWorld::load_checkpoint(s);
}
void save(const std::filesystem::path& path, const neuroevo::EcosystemWorld& w)
{
    std::ofstream s(path);
    if (!s) throw std::runtime_error("Cannot write checkpoint: " + path.string());
    w.save_checkpoint(s);
    s.close();
    if (!s) throw std::runtime_error("Failed to finish checkpoint: " + path.string());
}
}

int main(int argc, char** argv)
{
    try {
        neuroevo::EcosystemConfig cfg;
        const neuroevo::RunConfig run;
        std::map<std::string,std::size_t*> sizes{
            {"--nursery-size",&cfg.nursery_size},
            {"--nursery-food-patches",&cfg.nursery_food_patches},
            {"--nursery-exit-width",&cfg.nursery_exit_width},
            {"--shelter-size",&cfg.shelter_size},
            {"--creatures",&cfg.initial_creatures},{"--max-population",&cfg.max_population},
            {"--width",&cfg.width},{"--height",&cfg.height},{"--shelters",&cfg.shelters},
            {"--grazing-patches",&cfg.grazing_patches},{"--fruit-patches",&cfg.fruit_patches},
            {"--pods",&cfg.pods},{"--hidden",&cfg.brain.hidden_count},
            {"--mutation-max-hidden",&cfg.mutation.max_hidden_neurons}};
        std::map<std::string,double*> numbers{
            {"--founder-mass",&cfg.founder_mass},
            {"--founder-carnivory",&cfg.founder_carnivory},
            {"--health-per-mass",&cfg.health_per_mass},
            {"--body-energy-per-mass",&cfg.body_energy_per_mass},
            {"--attack-range",&cfg.attack_range},
            {"--attack-degrees",&cfg.attack_degrees},
            {"--attack-damage",&cfg.attack_damage},
            {"--attack-base-fraction",&cfg.attack_base_fraction},
            {"--carnivore-basal-fraction",&cfg.carnivore_basal_fraction},
            {"--attack-cost",&cfg.attack_cost},
            {"--healing-rate",&cfg.healing_rate},
            {"--healing-cost",&cfg.healing_cost},
            {"--meat-energy",&cfg.meat_energy},
            {"--meat-decay",&cfg.meat_decay},
            {"--carcass-recovery",&cfg.carcass_recovery},
            {"--mass-mutation-probability",&cfg.mutation.mass_mutation_probability},
            {"--mass-mutation-sigma",&cfg.mutation.mass_mutation_sigma},
            {"--carnivory-mutation-probability",&cfg.mutation.carnivory_mutation_probability},
            {"--carnivory-mutation-sigma",&cfg.mutation.carnivory_mutation_sigma},

            {"--nursery-food-decay",&cfg.nursery_food_decay},
            {"--nursery-food-energy",&cfg.nursery_food_energy},
            {"--nursery-food-capacity",&cfg.nursery_food_capacity},
            {"--nursery-food-regrowth",&cfg.nursery_food_regrowth},
            {"--dt",&cfg.dt},{"--brain-dt",&cfg.brain.dt},{"--radius",&cfg.radius},
            {"--actuator-tau",&cfg.actuator_tau},{"--sensory-rate",&cfg.brain.sensory_rate_hz},
            {"--motor-rate-tau",&cfg.brain.motor_rate_tau},{"--motor-reference-hz",&cfg.brain.motor_reference_hz},
            {"--conduction-speed",&cfg.brain.conduction_speed},
            {"--max-speed",&cfg.max_speed},{"--turn-rate",&cfg.max_turn_rate},
            {"--vision-range",&cfg.vision_range},{"--fov-degrees",&cfg.fov_degrees},
            {"--hearing-range",&cfg.hearing_range},{"--interaction-range",&cfg.interaction_range},
            {"--interaction-degrees",&cfg.interaction_degrees},
            {"--energy-capacity",&cfg.energy_capacity},{"--founder-energy",&cfg.founder_energy},
            {"--basal-cost",&cfg.basal_cost},{"--movement-cost",&cfg.movement_cost},
            {"--turn-cost",&cfg.turn_cost},{"--forage-cost",&cfg.forage_cost},{"--call-cost",&cfg.call_cost},
            {"--neuron-cost",&cfg.neuron_cost},{"--synapse-cost",&cfg.synapse_cost},{"--spike-cost",&cfg.spike_cost},
            {"--rough-multiplier",&cfg.rough_multiplier},{"--ingestion-rate",&cfg.ingestion_rate},
            {"--digestion-delay",&cfg.digestion_delay},{"--graze-capacity",&cfg.graze_capacity},
            {"--fruit-capacity",&cfg.fruit_capacity},{"--pod-capacity",&cfg.pod_capacity},
            {"--graze-energy",&cfg.graze_energy},{"--poor-fruit-energy",&cfg.poor_fruit_energy},
            {"--rich-fruit-energy",&cfg.rich_fruit_energy},{"--pod-energy",&cfg.pod_energy},
            {"--graze-decay",&cfg.graze_decay},{"--fruit-decay",&cfg.fruit_decay},
            {"--shelter-food-decay",&cfg.shelter_food_decay},
            {"--shelter-food-energy",&cfg.shelter_food_energy},{"--shelter-food-capacity",&cfg.shelter_food_capacity},
            {"--shelter-food-regrowth",&cfg.shelter_food_regrowth},
            {"--graze-regrowth",&cfg.graze_regrowth},{"--fruit-regrowth",&cfg.fruit_regrowth},
            {"--pod-regrowth",&cfg.pod_regrowth},{"--pod-work",&cfg.pod_work},{"--pod-decay",&cfg.pod_decay},
            {"--pod-open-duration",&cfg.pod_open_duration},{"--calm-duration",&cfg.calm_duration},
            {"--warning-duration",&cfg.warning_duration},{"--storm-duration",&cfg.storm_duration},
            {"--storm-cost",&cfg.storm_cost},{"--phase-offset",&cfg.phase_offset},
            {"--maturity-age",&cfg.maturity_age},{"--reproduction-threshold",&cfg.reproduction_threshold},
            {"--reproduction-cost",&cfg.reproduction_cost},{"--offspring-energy",&cfg.offspring_energy},
            {"--reproduction-cooldown",&cfg.reproduction_cooldown},{"--motor-gain",&cfg.motor_gain},
            {"--connection-probability",&cfg.brain.initial_connection_probability},
            {"--synaptic-gain",&cfg.brain.synaptic_gain},{"--background-rate",&cfg.brain.background_event_rate_hz},
            {"--background-current",&cfg.brain.background_event_current},
            {"--mutation-weight-sigma",&cfg.mutation.weight_sigma},
            {"--mutation-bias-sigma",&cfg.mutation.bias_sigma},
            {"--mutation-threshold-sigma",&cfg.mutation.threshold_sigma},
            {"--mutation-position-sigma",&cfg.mutation.position_sigma},
            {"--mutation-background-sigma",&cfg.mutation.background_sensitivity_sigma},
            {"--mutate-weight-prob",&cfg.mutation.mutate_weight_probability},
            {"--mutate-neuron-prob",&cfg.mutation.mutate_neuron_probability},
            {"--mutate-add-synapse-prob",&cfg.mutation.add_synapse_probability},
            {"--mutate-add-neuron-prob",&cfg.mutation.add_neuron_probability},
            {"--mutate-reciprocal-motif-prob",&cfg.mutation.add_reciprocal_motif_probability},
            {"--mutate-remove-synapse-prob",&cfg.mutation.remove_synapse_probability}};
        std::size_t steps=run.steps,record_every=run.record_every,tail_record_every=run.tail_record_every,companions=0;
        double detailed_tail_seconds=run.detailed_tail_seconds;
        int typed_food_override=-1;
        double remove_neuron_override=0;
        bool remove_neuron_explicit=false;
        double rewire_override=-1;
        bool record_brains=run.record_brains,record_observations=run.record_observations;
        bool record_brain_graphs=run.record_brain_graphs,record_routine_events=run.record_routine_events,config_changed=false;
        auto companion_controller=neuroevo::ControllerKind::Reactive;
        std::string resume,founders,starting_genomes,founder_brain=cfg.sparse_ancestor ? "sparse-ancestor" : "random",habitat=cfg.nursery_frontier ? "nursery-frontier" : "generated";
        bool predation_explicit=false;
        bool founder_brain_explicit=false;
        const auto timestamp=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::filesystem::path out="runs/ecosystem_"+std::to_string(timestamp);
        for (int i=1; i<argc; ++i) {
            const std::string arg=argv[i];
            if (arg == "--help") {
                std::cout << "Nursery ecosystem with independent spiking brains\nTune include/neuroevo/config.hpp and rebuild to change defaults.\n\n"
                    "Usage: neuroevo_ecosystem --creatures 24 --steps 4800 --out runs/my_ecosystem\n\n"
                    "  --steps N                 World steps to run (additional steps with --resume)\n"
                    "  --record-every N          Save a replay frame every N steps\n"
                    "  --record-brains 0|1       Record neural activity\n"
                    "  --record-observations 0|1 Record sensory values per creature\n"
                    "  --sensorimotor X          calibrated or legacy senses; predation adds inputs (83 total by default)\n"
                    "  --typed-food-proximity 0|1  Per-food-type distance signals; also overrides resume\n"
                    "  --mutate-rewire-synapse-prob N  Rewiring operator weight (also overrides resume)\n"
                    "  --calibrated-io 0|1       Rate encoding/decoding; independent of sensory layout\n"
                    "  --mutate-remove-neuron-prob N  Hidden-neuron pruning (also overrides resume)\n"
                    "  --mutate-remove-synapse-prob N Synapse pruning\n"
                    "  --record-brain-graphs 0|1 Save each introduced genome graph\n"
                    "  --record-routine-events 0|1 Save ingestion/digestion/pod-work events\n"
                    "  --detailed-tail-seconds S Also save a bounded full-detail final replay\n"
                    "  --tail-record-every N     Detailed-tail interval in steps\n"
                    "  --seed N                  World seed\n"
                    "  --controller X            spiking, reactive, or random\n"
                    "  --founder-brain X         sparse-ancestor or random\n"
                    "  --habitat X               nursery-frontier, generated, or ancestor-nursery\n"
                    "  --companions N            Last N creatures use --companion-controller\n"
                    "  --companion-controller X  reactive, random, or spiking\n"
                    "  --food-assignment X       random, a-rich, or b-rich\n"
                    "  --reproduction 0|1        Enable automatic local reproduction\n"
                    "  --no-reproduction         Shortcut for --reproduction 0\n"
                    "  --communication 0|1       Enable calling and hearing\n"
                    "  --storms 0|1              Enable weather cycle and storm effects\n"
                    "  --no-storms               Keep storm sensor but hold it at zero and skip storms\n"
                    "  --resume FILE             Continue full state from a checkpoint (no world overrides)\n"
                    "  --predation 0|1           Combat/body/diet mechanics (on in nursery-frontier)\n"
                    "  --founder-mass X          Initial body mass, 0.5..2\n"
                    "  --founder-carnivory X     Initial meat efficiency, 0..1\n"
                    "  --mass-mutation-probability X / --mass-mutation-sigma X\n"
                    "  --carnivory-mutation-probability X / --carnivory-mutation-sigma X\n"
                    "  --attack-base-fraction X  Damage fraction at zero carnivory (0<X<=1)\n"
                    "  --carnivore-basal-fraction X  Basal rate at full carnivory (0<X<=1)\n"
                    "  --attack-range X / --attack-degrees X / --attack-damage X / --attack-cost X\n"
                    "  --health-per-mass X / --body-energy-per-mass X / --healing-rate X / --healing-cost X\n"
                    "  --meat-energy X / --meat-decay X / --carcass-recovery X\n"
                    "  --founders FILE           Copy/reset living brains from a checkpoint into a new world\n"
                    "  --starting-genomes DIR    Sample surviving genome IDs uniformly, with replacement, from DIR/checkpoint.eco\n"
                    "                            Fresh population size uses --creatures; sampling uses --seed\n"
                    "  --out DIR                 Fresh output directory (default timestamped runs/ecosystem_...)\n\n"
                    "Integer world/brain settings (defaults):\n";
                for (const auto& p:sizes) std::cout << "  " << std::left << std::setw(30) << p.first << *p.second << '\n';
                std::cout << "\nNumeric world/brain settings (defaults):\n";
                for (const auto& p:numbers) std::cout << "  " << std::left << std::setw(30) << p.first << *p.second << '\n';
                return 0;
            }
            if (arg == "--no-reproduction") { cfg.reproduction=false; config_changed=true; continue; }
            if (arg == "--no-storms") { cfg.storms_enabled=false; config_changed=true; continue; }
            if (i+1>=argc) throw std::invalid_argument("Missing value after "+arg);
            const std::string value=argv[++i];
            if (arg == "--out") out=value;
            else if (arg == "--steps") steps=integer(value,arg);
            else if (arg == "--typed-food-proximity") typed_food_override=boolean(value,arg);
            else if (arg == "--mutate-rewire-synapse-prob") {
                rewire_override=number(value,arg);
                if (rewire_override<0 || rewire_override>1)
                    throw std::invalid_argument("Synapse rewiring probability must be 0..1");
            }
            else if (arg == "--mutate-remove-neuron-prob") {
                remove_neuron_override=number(value,arg);
                if (remove_neuron_override<0 || remove_neuron_override>1)
                    throw std::invalid_argument("Neuron removal probability must be 0..1");
                remove_neuron_explicit=true;
            }
            else if (arg == "--record-every") record_every=integer(value,arg);
            else if (arg == "--record-brains") record_brains=boolean(value,arg);
            else if (arg == "--record-observations") record_observations=boolean(value,arg);
            else if (arg == "--record-brain-graphs") record_brain_graphs=boolean(value,arg);
            else if (arg == "--record-routine-events") record_routine_events=boolean(value,arg);
            else if (arg == "--detailed-tail-seconds") detailed_tail_seconds=number(value,arg);
            else if (arg == "--tail-record-every") tail_record_every=integer(value,arg);
            else if (arg == "--resume") resume=value;
            else if (arg == "--founders") founders=value;
            else if (arg == "--starting-genomes") starting_genomes=value;
            else {
                config_changed=true;
                if (arg == "--seed") cfg.seed=integer(value,arg);
                else if (arg == "--sensorimotor") {
                    if (value != "legacy" && value != "calibrated")
                        throw std::invalid_argument("--sensorimotor requires legacy or calibrated");
                    const bool calibrated = value == "calibrated";
                    cfg.extended_senses = cfg.brain.calibrated_io = calibrated;
                    cfg.brain.input_count = calibrated
                        ? neuroevo::eco_input_count : neuroevo::eco_legacy_input_count;
                    cfg.motor_gain = calibrated ? 1.0 : 8.0;
                    cfg.actuator_tau = calibrated ? 0.30 : 0.0;
                    cfg.brain.conduction_speed = calibrated ? 6.0 : 1.5;
                    cfg.brain.max_delay_steps = calibrated ? 8 : 24;
                }
                else if (arg == "--predation") { cfg.predation=boolean(value,arg); predation_explicit=true; }
                else if (arg == "--calibrated-io") cfg.brain.calibrated_io=boolean(value,arg);
                else if (arg == "--outdoor-food-relocates") cfg.outdoor_food_relocates=boolean(value,arg);
                else if (arg == "--nursery-food-relocates") cfg.nursery_food_relocates=boolean(value,arg);
                else if (arg == "--controller") cfg.controller=neuroevo::parse_controller(value);
                else if (arg == "--founder-brain") {
                    founder_brain_explicit=true;
                    if (value!="random" && value!="sparse-ancestor")
                        throw std::invalid_argument("--founder-brain requires random or sparse-ancestor");
                    founder_brain=value; cfg.sparse_ancestor=value=="sparse-ancestor";
                }
                else if (arg == "--habitat") {
                    if (value!="generated" && value!="ancestor-nursery" && value!="nursery-frontier")
                        throw std::invalid_argument("--habitat requires generated, ancestor-nursery, or nursery-frontier");
                    habitat=value;
                }
                else if (arg == "--companions") companions=integer(value,arg);
                else if (arg == "--companion-controller") companion_controller=neuroevo::parse_controller(value);
                else if (arg == "--reproduction") cfg.reproduction=boolean(value,arg);
                else if (arg == "--communication") cfg.communication=boolean(value,arg);
                else if (arg == "--storms") cfg.storms_enabled=boolean(value,arg);
                else if (arg == "--food-assignment") {
                    if (value=="random") cfg.food_assignment=-1;
                    else if (value=="a-rich") cfg.food_assignment=0;
                    else if (value=="b-rich") cfg.food_assignment=1;
                    else throw std::invalid_argument("--food-assignment requires random, a-rich, or b-rich");
                } else if (sizes.count(arg)) *sizes.at(arg)=integer(value,arg);
                else if (numbers.count(arg)) *numbers.at(arg)=number(value,arg);
                else throw std::invalid_argument("Unknown option: "+arg);
            }
        }
        if (steps==0 || record_every==0 || tail_record_every==0)
            throw std::invalid_argument("--steps and recording intervals must be positive");
        if (detailed_tail_seconds < 0) throw std::invalid_argument("--detailed-tail-seconds must be nonnegative");
        std::optional<neuroevo::EcosystemWorld> sampled_source;
        if (!starting_genomes.empty()) {
            if (!resume.empty() || !founders.empty() || founder_brain_explicit || habitat=="ancestor-nursery")
                throw std::invalid_argument("--starting-genomes supplies fresh founders; cannot combine with --resume, --founders, --founder-brain or ancestor-nursery");
            const auto checkpoint=std::filesystem::path(starting_genomes)/"checkpoint.eco";
            if (!std::filesystem::is_regular_file(checkpoint))
                throw std::invalid_argument("Starting genomes require a run folder containing checkpoint.eco: "+starting_genomes);
            sampled_source.emplace(load(checkpoint.string()));
            // Match the required motor/sensory interface without importing world rules.
            if (sampled_source->config.predation && !predation_explicit) cfg.predation=true;
            founders=checkpoint.string();
        }
        if (!founder_brain_explicit && cfg.controller!=neuroevo::ControllerKind::Spiking) founder_brain="random";
        cfg.set_predation(cfg.predation);
        if (habitat=="ancestor-nursery") {
            if (cfg.initial_creatures!=1) throw std::invalid_argument("The ancestor nursery requires --creatures 1");
            if (companions!=0) throw std::invalid_argument("The ancestor nursery does not accept companion founders");
            if (cfg.controller!=neuroevo::ControllerKind::Spiking)
                throw std::invalid_argument("The ancestor nursery requires --controller spiking");
            founder_brain="sparse-ancestor";
        }
        cfg.nursery_frontier=habitat=="nursery-frontier";
        if (!resume.empty() && (config_changed || !founders.empty()))
            throw std::invalid_argument("--resume restores the full configuration; use a new world with --founders to change it");
        if (!founders.empty() && founder_brain_explicit)
            throw std::invalid_argument("--founders and --founder-brain select two different genome sources");
        if (founder_brain_explicit && founder_brain=="sparse-ancestor" && cfg.controller!=neuroevo::ControllerKind::Spiking)
            throw std::invalid_argument("The sparse ancestor is a spiking brain and requires --controller spiking");
        if (resume.empty() && cfg.initial_creatures==0) throw std::invalid_argument("--creatures must be at least 1");
        if (companions>cfg.initial_creatures) throw std::invalid_argument("--companions cannot exceed --creatures");
        for (const char* name : {"ecosystem.jsonl","ecosystem_tail.jsonl","ecosystem_stats.csv","events.csv","summary.json","checkpoint.eco","initial.eco","starting_genomes.csv"})
            if (std::filesystem::exists(out/name)) throw std::runtime_error("Run output already exists; choose a fresh --out directory: "+out.string());
        auto world=resume.empty()?(habitat=="ancestor-nursery"
            ?neuroevo::make_ancestral_nursery(cfg):neuroevo::EcosystemWorld(cfg)):load(resume);
        if (typed_food_override>=0) world.config.typed_food_proximity=typed_food_override!=0;
        if (remove_neuron_explicit) world.config.mutation.remove_neuron_probability=remove_neuron_override;
        if (rewire_override>=0) world.config.mutation.rewire_synapse_probability=rewire_override;
        world.config.validate();
        std::ostringstream sampled_founders;
        if (!founders.empty()) {
            auto source=sampled_source ? std::move(*sampled_source) : load(founders);
            if (source.creatures.empty()) throw std::invalid_argument("Founder checkpoint contains no living creatures");
            std::map<std::uint64_t,std::uint64_t> genome_ids;
            std::map<std::uint64_t,std::size_t> unique_genomes;
            for (std::size_t i=0;i<source.creatures.size();++i)
                unique_genomes.emplace(source.creatures[i].genome_id,i);
            std::vector<std::size_t> pool;
            for (const auto& entry:unique_genomes) pool.push_back(entry.second);
            neuroevo::Random sampling_rng(world.config.seed ^ 0x737461727467656eULL);
            sampled_founders << "founder_id,genome_id,source_creature_id,source_genome_id\n";
            for (std::size_t i=0;i<world.creatures.size();++i) {
                const auto& source_creature=source.creatures[starting_genomes.empty()
                    ? i%source.creatures.size() : pool[sampling_rng.uniform_index(pool.size())]];
                const auto& brain=source_creature.brain;
                if (brain.config().dt != world.config.brain.dt) throw std::invalid_argument("Founder brains require matching --brain-dt");
                auto brain_config = world.config.brain;
                brain_config.hidden_count = brain.config().hidden_count;
                auto neurons = brain.neurons();
                auto synapses = brain.synapses();
                if (brain.config().input_count > brain_config.input_count || brain.config().output_count > brain_config.output_count)
                    throw std::invalid_argument("Founder interface cannot be reduced; enable calibrated senses and predation");
                const auto old_inputs = brain.config().input_count;
                const auto added = brain_config.input_count - old_inputs;
                neurons.insert(neurons.begin()+old_inputs,added,neuroevo::Brain::Neuron{});
                for (std::size_t input=old_inputs;input<brain_config.input_count;++input) {
                    neurons[input].position={0.05,(static_cast<double>(input)+0.5)/brain_config.input_count};
                    neurons[input].background_sensitivity=0;
                }
                for (auto& edge:synapses) {
                    if (edge.pre>=old_inputs) edge.pre+=added;
                    if (edge.post>=old_inputs) edge.post+=added;
                }
                for (auto output=brain.config().output_count; output<brain_config.output_count; ++output) {
                    neuroevo::Brain::Neuron neuron;
                    neuron.position={0.95,(static_cast<double>(output)+0.5)/brain_config.output_count};
                    neurons.push_back(neuron); // New attack output starts disconnected and silent.
                }
                if (world.config.predation && source.config.predation) {
                    world.totals.external_body_energy -= world.config.body_energy_per_mass * world.creatures[i].body.mass;
                    world.creatures[i].body = source_creature.body;
                    world.creatures[i].health = world.max_health(world.creatures[i]);
                    world.totals.external_body_energy += world.config.body_energy_per_mass * world.creatures[i].body.mass;
                }
                world.creatures[i].brain=neuroevo::Brain::from_components(brain_config,std::move(neurons),std::move(synapses));
                world.creatures[i].brain.reset_state();
                const auto entry=genome_ids.emplace(source_creature.genome_id,world.creatures[i].id);
                world.creatures[i].genome_id=entry.first->second;
                sampled_founders << world.creatures[i].id << ',' << entry.first->second << ','
                    << source_creature.id << ',' << source_creature.genome_id << '\n';
            }
            founder_brain="checkpoint";
        }
        for (std::size_t i=0;i<companions;++i)
            world.creatures[world.creatures.size()-1-i].controller=companion_controller;
        std::filesystem::create_directories(out);
        if (!starting_genomes.empty()) {
            std::ofstream provenance(out/"starting_genomes.csv");
            provenance << sampled_founders.str();
            provenance.close();
            if (!provenance) throw std::runtime_error("Cannot write starting_genomes.csv");
            std::cout << "Sampled " << world.creatures.size() << " founders from " << starting_genomes << '\n';
        }
        if (std::filesystem::exists(out/"performance.csv"))
            throw std::runtime_error("Performance output already exists; choose a fresh --out directory");
        std::ofstream performance(out/"performance.csv");
        if (!performance) throw std::runtime_error("Cannot open performance output");
        performance << "step,time,population,wall_seconds,step_wall_seconds,slowest_step_seconds\n"
            << std::setprecision(12);
        std::ofstream replay(out/"ecosystem.jsonl"),stats(out/"ecosystem_stats.csv"),events(out/"events.csv");
        if (!replay || !stats || !events) throw std::runtime_error("Cannot open run output files");
        replay << std::setprecision(10); events << std::setprecision(12);
        events << "time,type,creature,other,resource,amount\n";
        neuroevo::write_ecosystem_metadata(replay,world,record_brain_graphs);
        neuroevo::write_ecosystem_stats_header(stats);
        save(out/"initial.eco",world);
        std::unordered_set<std::uint64_t> known;
        for (const auto& c:world.creatures) known.insert(c.id);
        std::vector<neuroevo::EcoEvent> pending;
        std::vector<neuroevo::EcoEvent> tail_pending;
        std::deque<std::pair<std::string,std::vector<std::uint64_t>>> tail_frames;
        std::unordered_map<std::uint64_t,neuroevo::EcoCreature> tail_brains;
        std::unordered_map<std::uint64_t,std::size_t> tail_references;
        const double requested_tail_intervals=detailed_tail_seconds/(world.config.dt*tail_record_every);
        if (requested_tail_intervals>10000) throw std::invalid_argument("Detailed tail is limited to 10,000 sampled intervals");
        const auto tail_frame_limit=detailed_tail_seconds>0
            ? std::max<std::size_t>(1,static_cast<std::size_t>(std::ceil(requested_tail_intervals))+1) : 0;
        auto record=[&]() {
            neuroevo::write_ecosystem_frame(replay,world,record_brains,record_observations,record_brain_graphs,&known,&pending);
            neuroevo::write_ecosystem_stats(stats,world);
            pending.clear(); replay.flush(); stats.flush();
            if (!replay || !stats || !events) throw std::runtime_error("Failed to write simulation output");
        };
        auto record_tail=[&]() {
            if (!tail_frame_limit) return;
            std::vector<std::uint64_t> ids;
            ids.reserve(world.creatures.size());
            for (const auto& c:world.creatures) {
                ids.push_back(c.id);
                ++tail_references[c.id];
                if (!tail_brains.count(c.id)) { auto copy=c; copy.brain.reset_state(); tail_brains.emplace(c.id,std::move(copy)); }
            }
            std::ostringstream frame;
            neuroevo::write_ecosystem_frame(frame,world,true,true,false,nullptr,&tail_pending);
            tail_frames.emplace_back(frame.str(),std::move(ids));
            tail_pending.clear();
            while (tail_frames.size()>tail_frame_limit) {
                for (const auto id:tail_frames.front().second) if (--tail_references[id]==0) {
                    tail_references.erase(id); tail_brains.erase(id);
                }
                tail_frames.pop_front();
            }
        };
        record();
        record_tail();
        const auto starting_step=world.step_index;
        const auto started=std::chrono::steady_clock::now();
        double step_wall_seconds=0,slowest_step_seconds=0;
        auto record_performance=[&]() {
            performance << world.step_index << ',' << world.time() << ',' << world.creatures.size() << ','
                << std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count() << ','
                << step_wall_seconds << ',' << slowest_step_seconds << '\n';
            performance.flush();
            if (!performance) throw std::runtime_error("Failed to write performance output");
        };
        std::signal(SIGINT,request_stop); std::signal(SIGTERM,request_stop);
        std::uint64_t last_recorded=world.step_index;
        std::cout << "Ecosystem: " << world.creatures.size() << " creatures, " << world.config.brain.input_count
            << " sensory inputs, " << world.config.brain.output_count << " motor outputs\nOutput: " << out.string() << '\n';
        if (!resume.empty()) std::cout << "Founder genomes and controllers were restored from the checkpoint.\n";
        else if (founder_brain=="sparse-ancestor") std::cout
            << "Founders use the five-hidden-neuron sparse ancestral spiking brain.\n";
        else if (founder_brain=="checkpoint") std::cout
            << "Founder genomes were imported from a checkpoint and reset for new lifetimes.\n";
        else std::cout << "Founders use " << neuroevo::to_string(world.config.controller)
            << " controllers. Random spiking brains may not survive; survival must evolve.\n";
        for (std::size_t i=0;i<steps && !world.creatures.empty() && !stop_requested;++i) {
            const auto step_started=std::chrono::steady_clock::now();
            world.step();
            const double step_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-step_started).count();
            step_wall_seconds+=step_seconds;
            slowest_step_seconds=std::max(slowest_step_seconds,step_seconds);
            for (const auto& e:world.events) {
                const bool routine=e.type=="ingestion" || e.type=="digestion" || e.type=="pod_work" || e.type=="attack_hit";
                if (record_routine_events || !routine) {
                    events << e.time << ',' << e.type << ',' << e.creature << ',' << e.other << ',' << e.resource << ',' << e.amount << '\n';
                    pending.push_back(e);
                }
                if (tail_frame_limit) tail_pending.push_back(e);
            }
            if ((i+1)%record_every==0) { record(); last_recorded=world.step_index; }
            if ((i+1)%tail_record_every==0) record_tail();
            if ((i+1)%1000==0) record_performance();
            if ((i+1)%1000==0) {
                const auto frontier_population=std::count_if(world.creatures.begin(),world.creatures.end(),
                    [&](const auto& creature) { return !world.in_nursery(creature.position); });
                std::cout << "t=" << world.time() << "s population=" << world.creatures.size()
                    << " births=" << world.totals.births << " frontier_population=" << frontier_population << std::endl;
            }
        }
        if (last_recorded!=world.step_index || !pending.empty()) record();
        if (tail_frame_limit && (!tail_pending.empty() || world.step_index%tail_record_every!=0)) record_tail();
        if (tail_frame_limit) {
            std::ofstream tail(out/"ecosystem_tail.jsonl");
            if (!tail) throw std::runtime_error("Cannot write detailed-tail replay");
            auto metadata_world=world;
            metadata_world.creatures.clear();
            for (auto& item:tail_brains) metadata_world.creatures.push_back(std::move(item.second));
            std::sort(metadata_world.creatures.begin(),metadata_world.creatures.end(),[](const auto& a,const auto& b){return a.id<b.id;});
            neuroevo::write_ecosystem_metadata(tail,metadata_world,true);
            for (const auto& frame:tail_frames) tail << frame.first;
            tail.close();
            if (!tail) throw std::runtime_error("Failed to finish detailed-tail replay");
        }
        save(out/"checkpoint.eco",world);
        record_performance();
        performance.close();
        if (!performance) throw std::runtime_error("Failed to close performance output");
        const double wall_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        std::size_t max_hidden_neurons=0;
        double mean_hidden_neurons=0;
        for (const auto& creature:world.creatures) {
            const auto hidden=creature.brain.config().hidden_count;
            max_hidden_neurons=std::max(max_hidden_neurons,hidden);
            mean_hidden_neurons+=static_cast<double>(hidden);
        }
        if (!world.creatures.empty()) mean_hidden_neurons/=static_cast<double>(world.creatures.size());
        const char* status=stop_requested?"interrupted":
            world.creatures.empty()?"extinct":"completed";
        std::ofstream summary(out/"summary.json");
        summary << "{\n  \"status\":\"" << status << "\",\n  \"steps_run\":" << world.step_index-starting_step
            << ",\n  \"founder_brain\":\"" << (resume.empty()?founder_brain:"checkpoint") << "\""
            << ",\n  \"habitat\":\"" << (world.config.nursery_frontier?"nursery-frontier":resume.empty()?habitat:"checkpoint") << "\""
            << ",\n  \"predation\":{\"enabled\":" << (world.config.predation?"true":"false")
            << ",\"founder_mass\":" << world.config.founder_mass << ",\"founder_carnivory\":" << world.config.founder_carnivory
            << ",\"mass_mutation_probability\":" << world.config.mutation.mass_mutation_probability
            << ",\"mass_mutation_sigma\":" << world.config.mutation.mass_mutation_sigma
            << ",\"carnivory_mutation_probability\":" << world.config.mutation.carnivory_mutation_probability
            << ",\"carnivory_mutation_sigma\":" << world.config.mutation.carnivory_mutation_sigma
            << ",\"attack_base_fraction\":" << world.config.attack_base_fraction
            << ",\"carnivore_basal_fraction\":" << world.config.carnivore_basal_fraction
            << ",\"deaths\":" << world.totals.predation_deaths << ",\"attack_energy\":" << world.totals.attacking
            << ",\"healing_energy\":" << world.totals.healing << ",\"body_construction\":" << world.totals.body_construction
            << ",\"external_body_energy\":" << world.totals.external_body_energy
            << ",\"carcass_energy\":" << world.totals.carcass_energy << "}"
            << ",\n  \"nursery\":{\"enabled\":" << (world.config.nursery_frontier?"true":"false")
            << ",\"size\":" << world.config.nursery_size << ",\"food_energy\":" << world.config.nursery_food_energy
            << ",\"exit_width\":" << world.config.nursery_exit_width
            << ",\"food_capacity\":" << world.config.nursery_food_capacity
            << ",\"food_patches\":" << world.config.nursery_food_patches
            << ",\"food_relocates\":" << (world.config.nursery_food_relocates?"true":"false")
            << ",\"food_regrowth\":" << world.config.nursery_food_regrowth << "}"
            << ",\n  \"storms_enabled\":" << (world.config.storms_enabled?"true":"false")
            << ",\n  \"shelter_size\":" << world.config.shelter_size
            << ",\n  \"step\":" << world.step_index << ",\n  \"time\":" << world.time()
            << ",\n  \"population\":" << world.creatures.size() << ",\n  \"births\":" << world.totals.births
            << ",\n  \"founder_births\":" << world.totals.founder_births
            << ",\n  \"descendant_births\":" << world.totals.descendant_births
            << ",\n  \"births_first_100s\":" << world.totals.births_first_100s
            << ",\n  \"births_after_100s\":" << world.totals.births-world.totals.births_first_100s
            << ",\n  \"deaths\":" << world.totals.deaths
            << ",\n  \"food_energy\":" << world.totals.energy_gained
            << ",\n  \"operating_energy\":" << world.totals.metabolism+world.totals.movement+world.totals.turning
                +world.totals.foraging+world.totals.calling+world.totals.neural+world.totals.exposure
                +world.totals.attacking+world.totals.healing
            << ",\n  \"performance\":{\"step_wall_seconds\":" << step_wall_seconds
            << ",\"slowest_step_seconds\":" << slowest_step_seconds << "}"
            << ",\n  \"sensorimotor\":{\"inputs\":" << world.config.brain.input_count
            << ",\"calibrated_io\":" << (world.config.brain.calibrated_io?"true":"false")
            << ",\"sensory_rate_hz\":" << world.config.brain.sensory_rate_hz
            << ",\"motor_rate_tau\":" << world.config.brain.motor_rate_tau
            << ",\"motor_reference_hz\":" << world.config.brain.motor_reference_hz
            << ",\"actuator_tau\":" << world.config.actuator_tau << "}"
            << ",\n  \"natural_spiking_breeders\":" << world.totals.natural_spiking_breeders
            << ",\n  \"mature_offspring\":" << world.totals.mature_offspring
            << ",\n  \"mean_hidden_neurons\":" << mean_hidden_neurons
            << ",\n  \"max_hidden_neurons\":" << max_hidden_neurons
            << ",\n  \"record_every\":" << record_every
            << ",\n  \"record_brains\":" << (record_brains?"true":"false")
            << ",\n  \"record_observations\":" << (record_observations?"true":"false")
            << ",\n  \"record_brain_graphs\":" << (record_brain_graphs?"true":"false")
            << ",\n  \"detailed_tail_seconds\":" << detailed_tail_seconds
            << ",\n  \"ecology\":{\"maturity_age\":" << world.config.maturity_age
            << ",\"reproduction_threshold\":" << world.config.reproduction_threshold
            << ",\"reproduction_cost\":" << world.config.reproduction_cost
            << ",\"offspring_energy\":" << world.config.offspring_energy
            << ",\"reproduction_cooldown\":" << world.config.reproduction_cooldown
            << ",\"basal_cost\":" << world.config.basal_cost << ",\"storm_cost\":" << world.config.storm_cost
            << ",\"graze_energy\":" << world.config.graze_energy
            << ",\"poor_fruit_energy\":" << world.config.poor_fruit_energy
            << ",\"rich_fruit_energy\":" << world.config.rich_fruit_energy
            << ",\"pod_energy\":" << world.config.pod_energy << "}"
            << ",\n  \"mutation\":{\"copy_probability\":" << world.config.mutation.copy_probability
            << ",\"slight_probability\":" << world.config.mutation.slight_probability
            << ",\"weight_sigma\":" << world.config.mutation.weight_sigma
            << ",\"bias_sigma\":" << world.config.mutation.bias_sigma
            << ",\"threshold_sigma\":" << world.config.mutation.threshold_sigma
            << ",\"mutate_weight_probability\":" << world.config.mutation.mutate_weight_probability
            << ",\"mutate_neuron_probability\":" << world.config.mutation.mutate_neuron_probability
            << ",\"add_synapse_probability\":" << world.config.mutation.add_synapse_probability
            << ",\"add_neuron_probability\":" << world.config.mutation.add_neuron_probability
            << ",\"max_hidden_neurons\":" << world.config.mutation.max_hidden_neurons
            << ",\"rewire_synapse_probability\":" << world.config.mutation.rewire_synapse_probability
            << ",\"remove_synapse_probability\":" << world.config.mutation.remove_synapse_probability
            << ",\"remove_neuron_probability\":" << world.config.mutation.remove_neuron_probability << "}"
            << ",\n  \"wall_seconds\":" << wall_seconds << "\n}\n";
        summary.close(); events.close(); replay.close(); stats.close();
        if (!summary || !events || !replay || !stats) throw std::runtime_error("Failed to close output files");
        std::cout << status << ": t=" << world.time() << "s population=" << world.creatures.size()
            << " births=" << world.totals.births << " deaths=" << world.totals.deaths
            << " (" << wall_seconds << " wall seconds)\n"
            << "Replay: uv run python tools/view_ecosystem.py \"" << out.string() << "\"\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Ecosystem error: " << e.what() << '\n';
        return 1;
    }
}
