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
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
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
        // Apply habitat defaults before parsing so explicit numeric flags win,
        // regardless of their position relative to --habitat.
        for (int i=1; i+1<argc; ++i)
            if (std::string(argv[i])=="--habitat" && std::string(argv[i+1])=="nursery-frontier")
                cfg=neuroevo::nursery_frontier_config();
        std::map<std::string,std::size_t*> sizes{
            {"--nursery-size",&cfg.nursery_size},
            {"--nursery-food-patches",&cfg.nursery_food_patches},
            {"--nursery-exit-width",&cfg.nursery_exit_width},
            {"--shelter-size",&cfg.shelter_size},
            {"--creatures",&cfg.initial_creatures},{"--max-population",&cfg.max_population},
            {"--width",&cfg.width},{"--height",&cfg.height},{"--shelters",&cfg.shelters},
            {"--grazing-patches",&cfg.grazing_patches},{"--fruit-patches",&cfg.fruit_patches},
            {"--pods",&cfg.pods},{"--hidden",&cfg.brain.hidden_count},
            {"--immigration-floor",&cfg.immigration_floor},{"--immigration-batch",&cfg.immigration_batch},
            {"--archive-capacity",&cfg.archive_capacity},{"--withdrawal-cycles",&cfg.withdrawal_cycles},
            {"--archive-tournament-size",&cfg.archive_tournament_size},
            {"--archive-eval-trials",&cfg.archive_eval_trials},{"--archive-eval-seed",&cfg.archive_eval_seed},
            {"--archive-min-feeding-bouts",&cfg.archive_min_feeding_bouts},
            {"--mutation-max-hidden",&cfg.mutation.max_hidden_neurons}};
        std::map<std::string,double*> numbers{
            {"--nursery-food-energy",&cfg.nursery_food_energy},
            {"--nursery-food-capacity",&cfg.nursery_food_capacity},
            {"--nursery-food-regrowth",&cfg.nursery_food_regrowth},
            {"--dt",&cfg.dt},{"--brain-dt",&cfg.brain.dt},{"--radius",&cfg.radius},
            {"--immigration-interval",&cfg.immigration_interval},{"--archive-min-energy",&cfg.archive_min_energy},
            {"--archive-min-age",&cfg.archive_min_age},{"--archive-min-efficiency",&cfg.archive_min_efficiency},
            {"--archive-eval-seconds",&cfg.archive_eval_seconds},
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
        std::size_t steps=4800,record_every=10,tail_record_every=10,companions=0;
        double detailed_tail_seconds=0;
        int stable_mutations_override=-1;
        double remove_neuron_override=0;
        bool remove_neuron_explicit=false;
        std::size_t evaluation_workers=std::max(1u,std::min(4u,std::thread::hardware_concurrency()));
        bool record_brains=true,record_observations=true,record_brain_graphs=true,record_routine_events=true,config_changed=false;
        auto companion_controller=neuroevo::ControllerKind::Reactive;
        std::string resume,founders,founder_brain="random",habitat="generated";
        bool founder_brain_explicit=false;
        const auto timestamp=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::filesystem::path out="runs/ecosystem_"+std::to_string(timestamp);
        for (int i=1; i<argc; ++i) {
            const std::string arg=argv[i];
            if (arg == "--help") {
                std::cout << "Shared 2D ecosystem with independent spiking brains\n\n"
                    "Usage: neuroevo_ecosystem --creatures 24 --steps 4800 --out runs/my_ecosystem\n\n"
                    "  --steps N                 World steps to run (additional steps with --resume), default 4800\n"
                    "  --record-every N          Save a replay frame every N steps, default 10\n"
                    "  --record-brains 0|1       Record neural activity, default 1\n"
                    "  --record-observations 0|1 Record sensory values per creature, default 1\n"
                    "  --sensorimotor X          calibrated (97 inputs, default) or legacy (87 inputs)\n"
                    "  --calibrated-io 0|1       Rate encoding/decoding; independent of sensory layout\n"
                    "  --stable-mutations 0|1    Local edits and weak growth; new-world default 1, explicit resume override\n"
                    "  --mutate-remove-neuron-prob N  Hidden-neuron pruning (default 0.01; also overrides resume)\n"
                    "  --mutate-remove-synapse-prob N Synapse pruning (default 0.04)\n"
                    "  --archive-eval-trials N   Matched newborn family trials per genome; 0 = observed score\n"
                    "  --archive-eval-workers N  Parallel trial workers, 1..32; default up to 4 (also on resume)\n"
                    "  --record-brain-graphs 0|1 Save each introduced genome graph, default 1\n"
                    "  --record-routine-events 0|1 Save ingestion/digestion/pod-work events, default 1\n"
                    "  --detailed-tail-seconds S Also save a bounded full-detail final replay, default 0\n"
                    "  --tail-record-every N     Detailed-tail interval in steps, default 10\n"
                    "  --seed N                  World seed, default 7\n"
                    "  --controller X            spiking (default), reactive, or random\n"
                    "  --founder-brain X         random (default) or sparse-ancestor\n"
                    "  --habitat X               generated (default), ancestor-nursery, or nursery-frontier\n"
                    "  --companions N            Last N creatures use --companion-controller\n"
                    "  --companion-controller X  reactive (default), random, or spiking\n"
                    "  --food-assignment X       random (default), a-rich, or b-rich\n"
                    "  --reproduction 0|1        Enable automatic local reproduction, default 1\n"
                    "  --no-reproduction         Shortcut for --reproduction 0\n"
                    "  --communication 0|1       Enable calling and hearing, default 1\n"
                    "  --storms 0|1              Enable weather cycle and storm effects, default 1\n"
                    "  --no-storms               Keep storm sensor but hold it at zero and skip storms\n"
                    "  --establishment 0|1       Support population with archive-based immigration, default 0\n"
                    "  --immigration-auto-stop 0|1  Taper and withdraw support after sustained breeding, default 1\n"
                    "                             Split: 40% clones, 40% slight mutations, 20% strong\n"
                    "                             Archive parents use niche-preserving tournament selection\n"
                    "                             Floor 0 means half the initial population (at least 1)\n"
                    "  --resume FILE             Continue full state from a checkpoint (no world overrides)\n"
                    "  --founders FILE           Copy/reset living brains from a checkpoint into a new world\n"
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
            else if (arg == "--archive-eval-workers") evaluation_workers=integer(value,arg);
            else if (arg == "--stable-mutations") stable_mutations_override=boolean(value,arg);
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
            else {
                config_changed=true;
                if (arg == "--seed") cfg.seed=integer(value,arg);
                else if (arg == "--sensorimotor") {
                    if (value != "legacy" && value != "calibrated")
                        throw std::invalid_argument("--sensorimotor requires legacy or calibrated");
                    const bool calibrated = value == "calibrated";
                    cfg.extended_senses = cfg.brain.calibrated_io = calibrated;
                    cfg.brain.input_count = cfg.brain.sensory_input_count = calibrated
                        ? neuroevo::eco_input_count : neuroevo::eco_legacy_input_count;
                    cfg.motor_gain = calibrated ? 1.0 : 8.0;
                    cfg.actuator_tau = calibrated ? 0.30 : 0.0;
                    cfg.brain.conduction_speed = calibrated ? 6.0 : 1.5;
                    cfg.brain.max_delay_steps = calibrated ? 8 : 24;
                }
                else if (arg == "--calibrated-io") cfg.brain.calibrated_io=boolean(value,arg);
                else if (arg == "--outdoor-food-relocates") cfg.outdoor_food_relocates=boolean(value,arg);
                else if (arg == "--nursery-food-relocates") cfg.nursery_food_relocates=boolean(value,arg);
                else if (arg == "--controller") cfg.controller=neuroevo::parse_controller(value);
                else if (arg == "--founder-brain") {
                    founder_brain_explicit=true;
                    if (value!="random" && value!="sparse-ancestor")
                        throw std::invalid_argument("--founder-brain requires random or sparse-ancestor");
                    founder_brain=value;
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
                else if (arg == "--establishment") cfg.establishment=boolean(value,arg);
                else if (arg == "--immigration-auto-stop") cfg.immigration_auto_stop=boolean(value,arg);
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
        if (evaluation_workers==0 || evaluation_workers>32)
            throw std::invalid_argument("--archive-eval-workers must be 1..32");
        if (habitat=="ancestor-nursery") {
            if (cfg.initial_creatures!=1) throw std::invalid_argument("The ancestor nursery requires --creatures 1");
            if (companions!=0) throw std::invalid_argument("The ancestor nursery does not accept companion founders");
            if (cfg.controller!=neuroevo::ControllerKind::Spiking)
                throw std::invalid_argument("The ancestor nursery requires --controller spiking");
            founder_brain="sparse-ancestor";
        }
        cfg.nursery_frontier=habitat=="nursery-frontier";
        if (cfg.nursery_frontier) {
            cfg.establishment=false; cfg.archive_eval_trials=0;
            if (!founder_brain_explicit && founders.empty() && cfg.controller==neuroevo::ControllerKind::Spiking)
                founder_brain="sparse-ancestor";
        }
        if (!resume.empty() && (config_changed || !founders.empty()))
            throw std::invalid_argument("--resume restores the full configuration; use a new world with --founders to change it");
        if (!founders.empty() && founder_brain!="random")
            throw std::invalid_argument("--founders and --founder-brain select two different genome sources");
        if (founder_brain=="sparse-ancestor" && cfg.controller!=neuroevo::ControllerKind::Spiking)
            throw std::invalid_argument("The sparse ancestor is a spiking brain and requires --controller spiking");
        if (resume.empty() && cfg.initial_creatures==0) throw std::invalid_argument("--creatures must be at least 1");
        if (companions>cfg.initial_creatures) throw std::invalid_argument("--companions cannot exceed --creatures");
        for (const char* name : {"ecosystem.jsonl","ecosystem_tail.jsonl","ecosystem_stats.csv","events.csv","summary.json","checkpoint.eco","initial.eco","archive.csv","newborn_evaluations.csv"})
            if (std::filesystem::exists(out/name)) throw std::runtime_error("Run output already exists; choose a fresh --out directory: "+out.string());
        auto world=resume.empty()?(habitat=="ancestor-nursery"
            ?neuroevo::make_ancestral_nursery(cfg):neuroevo::EcosystemWorld(cfg)):load(resume);
        world.evaluation_workers=evaluation_workers;
        if (stable_mutations_override>=0) world.config.mutation.stable=stable_mutations_override!=0;
        if (remove_neuron_explicit) world.config.mutation.remove_neuron_probability=remove_neuron_override;
        if (resume.empty() && founder_brain=="sparse-ancestor") {
            const auto ancestor=neuroevo::make_sparse_ancestral_brain(world.config);
            const auto ancestral_genome_id=world.creatures.empty()?0:world.creatures.front().id;
            for (auto& creature:world.creatures) {
                creature.brain=ancestor;
                creature.brain.reset_state();
                creature.controller=neuroevo::ControllerKind::Spiking;
                creature.genome_id=ancestral_genome_id;
            }
        }
        if (!founders.empty()) {
            auto source=load(founders);
            if (source.creatures.empty()) throw std::invalid_argument("Founder checkpoint contains no living creatures");
            std::map<std::uint64_t,std::uint64_t> genome_ids;
            for (std::size_t i=0;i<world.creatures.size();++i) {
                const auto& source_creature=source.creatures[i%source.creatures.size()];
                const auto& brain=source_creature.brain;
                if (brain.config().dt != world.config.brain.dt) throw std::invalid_argument("Founder brains require matching --brain-dt");
                auto brain_config = world.config.brain;
                brain_config.hidden_count = brain.config().hidden_count;
                auto neurons = brain.neurons();
                auto synapses = brain.synapses();
                if (brain.config().input_count != brain_config.input_count) {
                    if (brain.config().input_count != neuroevo::eco_legacy_input_count
                        || brain_config.input_count != neuroevo::eco_input_count)
                        throw std::invalid_argument("Founder interface cannot be reduced; use --sensorimotor calibrated");
                    const auto added = neuroevo::eco_input_count - neuroevo::eco_legacy_input_count;
                    neurons.insert(neurons.begin()+neuroevo::eco_legacy_input_count,added,neuroevo::Brain::Neuron{});
                    for (std::size_t input=neuroevo::eco_legacy_input_count;input<neuroevo::eco_input_count;++input)
                        neurons[input].position={0.05,(static_cast<double>(input)+0.5)/neuroevo::eco_input_count};
                    for (auto& edge:synapses) {
                        if (edge.pre>=neuroevo::eco_legacy_input_count) edge.pre+=added;
                        if (edge.post>=neuroevo::eco_legacy_input_count) edge.post+=added;
                    }
                }
                world.creatures[i].brain=neuroevo::Brain::from_components(brain_config,std::move(neurons),std::move(synapses));
                world.creatures[i].brain.reset_state();
                const auto entry=genome_ids.emplace(source_creature.genome_id,world.creatures[i].id);
                world.creatures[i].genome_id=entry.first->second;
            }
            founder_brain="checkpoint";
        }
        for (std::size_t i=0;i<companions;++i)
            world.creatures[world.creatures.size()-1-i].controller=companion_controller;
        std::filesystem::create_directories(out);
        if (std::filesystem::exists(out/"performance.csv"))
            throw std::runtime_error("Performance output already exists; choose a fresh --out directory");
        std::ofstream performance(out/"performance.csv");
        if (!performance) throw std::runtime_error("Cannot open performance output");
        performance << "step,time,population,wall_seconds,step_wall_seconds,evaluation_wall_seconds,evaluation_calls,slowest_step_seconds\n"
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
                << step_wall_seconds << ',' << world.evaluation_wall_seconds << ','
                << world.evaluation_calls << ',' << slowest_step_seconds << '\n';
            performance.flush();
            if (!performance) throw std::runtime_error("Failed to write performance output");
        };
        std::signal(SIGINT,request_stop); std::signal(SIGTERM,request_stop);
        std::uint64_t last_recorded=world.step_index;
        std::cout << "Ecosystem: " << world.creatures.size() << " creatures, " << world.config.brain.input_count
            << " sensory inputs, " << neuroevo::eco_output_count << " motor outputs\nOutput: " << out.string() << '\n';
        if (!resume.empty()) std::cout << "Founder genomes and controllers were restored from the checkpoint.\n";
        else if (founder_brain=="sparse-ancestor") std::cout
            << "Founders use the seven-hidden-neuron sparse ancestral spiking brain.\n";
        else if (founder_brain=="checkpoint") std::cout
            << "Founder genomes were imported from a checkpoint and reset for new lifetimes.\n";
        else std::cout << "Founders use " << neuroevo::to_string(world.config.controller)
            << " controllers. Random spiking brains may not survive; survival must evolve.\n";
        if (world.config.establishment) std::cout << "Establishment support: floor=" << world.population_floor()
            << ", archive=" << world.archive.size() << ", 40/40/20 clone/slight/strong immigration\n";
        std::cout << "Archive evaluation workers: " << evaluation_workers << '\n';
        std::cout << "Mutation policy: " << (world.config.mutation.stable ? "stable (local edits, weak growth)" : "legacy") << '\n';
        for (std::size_t i=0;i<steps && (!world.creatures.empty() || world.immigration_enabled()) && !stop_requested;++i) {
            const auto step_started=std::chrono::steady_clock::now();
            const auto previous_evaluations=world.evaluation_calls;
            world.step();
            const double step_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-step_started).count();
            step_wall_seconds+=step_seconds;
            slowest_step_seconds=std::max(slowest_step_seconds,step_seconds);
            for (const auto& e:world.events) {
                const bool routine=e.type=="ingestion" || e.type=="digestion" || e.type=="pod_work";
                if (record_routine_events || !routine) {
                    events << e.time << ',' << e.type << ',' << e.creature << ',' << e.other << ',' << e.resource << ',' << e.amount << '\n';
                    pending.push_back(e);
                }
                if (tail_frame_limit) tail_pending.push_back(e);
            }
            if ((i+1)%record_every==0) { record(); last_recorded=world.step_index; }
            if ((i+1)%tail_record_every==0) record_tail();
            if (world.evaluation_calls!=previous_evaluations || (i+1)%1000==0) record_performance();
            if ((i+1)%1000==0) std::cout << "t=" << world.time() << "s population=" << world.creatures.size()
                << " births=" << world.totals.births << " deaths=" << world.totals.deaths
                << " immigrants=" << world.totals.immigrants << " archive=" << world.archive.size()
                << " evaluations=" << world.evaluation_calls << " evaluation_wall_s=" << world.evaluation_wall_seconds << std::endl;
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
        std::ofstream archive(out/"archive.csv");
        archive << "genome_id,source_id,niche,score,trials,mean_food_energy,mean_operating_energy\n" << std::setprecision(12);
        for (const auto& entry:world.archive) {
            double gained=0,spent=0;
            for (const auto& trial:entry.trials) { gained+=trial.energy_gained; spent+=trial.energy_spent; }
            const double count=static_cast<double>(entry.trials.size());
            archive << entry.genome_id << ',' << entry.source_id << ',' << neuroevo::to_string(entry.niche) << ','
                << entry.score << ',' << entry.trials.size() << ',' << gained/count << ',' << spent/count << '\n';
        }
        archive.close();
        if (!archive) throw std::runtime_error("Failed to write archive summary");
        std::ofstream evaluations(out/"newborn_evaluations.csv");
        evaluations << "genome_id,score,trial,seed,offspring,descendant_births,mature_offspring,elapsed,focal_age,"
            "food_energy,operating_energy,first_birth,second_birth,matured,focal_alive,capacity_limited\n" << std::setprecision(12);
        for (const auto& [id, evaluation] : world.newborn_evaluations) {
            for (std::size_t i=0;i<evaluation.trials.size();++i) {
                const auto& t=evaluation.trials[i];
                evaluations << id << ',' << evaluation.score << ',' << i << ',' << t.seed << ',' << t.offspring << ','
                    << t.descendant_births << ',' << t.mature_offspring << ',' << t.elapsed << ',' << t.focal_age << ','
                    << t.food_energy << ',' << t.operating_energy << ',' << t.first_birth << ',' << t.second_birth << ','
                    << t.matured << ',' << t.focal_alive << ',' << t.capacity_limited << '\n';
            }
        }
        evaluations.close();
        if (!evaluations) throw std::runtime_error("Failed to write newborn evaluations");
        std::vector<double> archive_scores;
        for (const auto& entry:world.archive) archive_scores.push_back(entry.score);
        std::sort(archive_scores.begin(),archive_scores.end());
        const double archive_best=archive_scores.empty()?0:archive_scores.back();
        const double archive_median=archive_scores.empty()?0:
            (archive_scores[(archive_scores.size()-1)/2]+archive_scores[archive_scores.size()/2])/2;
        std::size_t max_hidden_neurons=0;
        double mean_hidden_neurons=0;
        for (const auto& creature:world.creatures) {
            const auto hidden=creature.brain.config().hidden_count;
            max_hidden_neurons=std::max(max_hidden_neurons,hidden);
            mean_hidden_neurons+=static_cast<double>(hidden);
        }
        if (!world.creatures.empty()) mean_hidden_neurons/=static_cast<double>(world.creatures.size());
        const char* status=stop_requested?"interrupted":
            world.creatures.empty()?(world.immigration_enabled()?"awaiting_immigration":"extinct"):"completed";
        std::ofstream summary(out/"summary.json");
        summary << "{\n  \"status\":\"" << status << "\",\n  \"steps_run\":" << world.step_index-starting_step
            << ",\n  \"founder_brain\":\"" << (resume.empty()?founder_brain:"checkpoint") << "\""
            << ",\n  \"habitat\":\"" << (world.config.nursery_frontier?"nursery-frontier":resume.empty()?habitat:"checkpoint") << "\""
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
            << ",\n  \"immigrant_births\":" << world.totals.immigrant_births
            << ",\n  \"descendant_births\":" << world.totals.descendant_births
            << ",\n  \"births_first_100s\":" << world.totals.births_first_100s
            << ",\n  \"births_after_100s\":" << world.totals.births-world.totals.births_first_100s
            << ",\n  \"deaths\":" << world.totals.deaths
            << ",\n  \"immigrants\":" << world.totals.immigrants
            << ",\n  \"immigrant_mutations\":" << world.totals.immigrant_mutations
            << ",\n  \"immigrant_slight_mutations\":" << world.totals.immigrant_slight_mutations
            << ",\n  \"immigrant_strong_mutations\":" << world.totals.immigrant_strong_mutations
            << ",\n  \"immigrant_clones\":" << world.totals.immigrant_clones
            << ",\n  \"immigrant_random\":" << world.totals.immigrant_random
            << ",\n  \"archive_fallbacks\":" << world.totals.archive_fallbacks
            << ",\n  \"archive_empty_checks\":" << world.totals.archive_empty_checks
            << ",\n  \"immigrant_energy\":" << world.totals.immigrant_energy
            << ",\n  \"food_energy\":" << world.totals.energy_gained
            << ",\n  \"operating_energy\":" << world.totals.metabolism+world.totals.movement+world.totals.turning
                +world.totals.foraging+world.totals.calling+world.totals.neural+world.totals.exposure
            << ",\n  \"archive_entries\":" << world.archive.size()
            << ",\n  \"newborn_evaluated_genomes\":" << world.newborn_evaluations.size()
            << ",\n  \"performance\":{\"evaluation_workers\":" << evaluation_workers
            << ",\"evaluation_calls\":" << world.evaluation_calls
            << ",\"evaluation_wall_seconds\":" << world.evaluation_wall_seconds
            << ",\"step_wall_seconds\":" << step_wall_seconds
            << ",\"slowest_step_seconds\":" << slowest_step_seconds << "}"
            << ",\n  \"sensorimotor\":{\"inputs\":" << world.config.brain.input_count
            << ",\"calibrated_io\":" << (world.config.brain.calibrated_io?"true":"false")
            << ",\"sensory_rate_hz\":" << world.config.brain.sensory_rate_hz
            << ",\"motor_rate_tau\":" << world.config.brain.motor_rate_tau
            << ",\"motor_reference_hz\":" << world.config.brain.motor_reference_hz
            << ",\"actuator_tau\":" << world.config.actuator_tau << "}"
            << ",\n  \"archive_best_score\":" << archive_best
            << ",\n  \"archive_median_score\":" << archive_median
            << ",\n  \"archive_policy\":{\"min_age\":" << world.config.archive_min_age
            << ",\"min_energy\":" << world.config.archive_min_energy
            << ",\"min_feeding_bouts\":" << world.config.archive_min_feeding_bouts
            << ",\"min_efficiency\":" << world.config.archive_min_efficiency
            << ",\"tournament_size\":" << world.config.archive_tournament_size
            << ",\"evaluation_trials\":" << world.config.archive_eval_trials
            << ",\"evaluation_seconds\":" << world.config.archive_eval_seconds
            << ",\"evaluation_seed\":" << world.config.archive_eval_seed << "}"
            << ",\n  \"natural_spiking_breeders\":" << world.totals.natural_spiking_breeders
            << ",\n  \"mature_offspring\":" << world.totals.mature_offspring
            << ",\n  \"mean_hidden_neurons\":" << mean_hidden_neurons
            << ",\n  \"max_hidden_neurons\":" << max_hidden_neurons
            << ",\n  \"immigration_active\":" << (world.immigration_enabled()?"true":"false")
            << ",\n  \"immigration_withdrawn\":" << (world.immigration_withdrawn?"true":"false")
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
            << ",\n  \"mutation\":{\"stable\":" << (world.config.mutation.stable?"true":"false")
            << ",\"weight_sigma\":" << world.config.mutation.weight_sigma
            << ",\"bias_sigma\":" << world.config.mutation.bias_sigma
            << ",\"threshold_sigma\":" << world.config.mutation.threshold_sigma
            << ",\"mutate_weight_probability\":" << world.config.mutation.mutate_weight_probability
            << ",\"mutate_neuron_probability\":" << world.config.mutation.mutate_neuron_probability
            << ",\"add_synapse_probability\":" << world.config.mutation.add_synapse_probability
            << ",\"add_neuron_probability\":" << world.config.mutation.add_neuron_probability
            << ",\"max_hidden_neurons\":" << world.config.mutation.max_hidden_neurons
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
