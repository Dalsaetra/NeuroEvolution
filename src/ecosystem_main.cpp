#include "neuroevo/ecosystem.hpp"
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

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
        std::map<std::string,std::size_t*> sizes{
            {"--creatures",&cfg.initial_creatures},{"--max-population",&cfg.max_population},
            {"--width",&cfg.width},{"--height",&cfg.height},{"--shelters",&cfg.shelters},
            {"--grazing-patches",&cfg.grazing_patches},{"--fruit-patches",&cfg.fruit_patches},
            {"--pods",&cfg.pods},{"--hidden",&cfg.brain.hidden_count}};
        std::map<std::string,double*> numbers{
            {"--dt",&cfg.dt},{"--brain-dt",&cfg.brain.dt},{"--radius",&cfg.radius},
            {"--max-speed",&cfg.max_speed},{"--turn-rate",&cfg.max_turn_rate},
            {"--vision-range",&cfg.vision_range},{"--fov-degrees",&cfg.fov_degrees},
            {"--hearing-range",&cfg.hearing_range},{"--interaction-range",&cfg.interaction_range},
            {"--energy-capacity",&cfg.energy_capacity},{"--founder-energy",&cfg.founder_energy},
            {"--basal-cost",&cfg.basal_cost},{"--movement-cost",&cfg.movement_cost},
            {"--turn-cost",&cfg.turn_cost},{"--forage-cost",&cfg.forage_cost},{"--call-cost",&cfg.call_cost},
            {"--neuron-cost",&cfg.neuron_cost},{"--synapse-cost",&cfg.synapse_cost},{"--spike-cost",&cfg.spike_cost},
            {"--rough-multiplier",&cfg.rough_multiplier},{"--ingestion-rate",&cfg.ingestion_rate},
            {"--digestion-delay",&cfg.digestion_delay},{"--graze-capacity",&cfg.graze_capacity},
            {"--fruit-capacity",&cfg.fruit_capacity},{"--pod-capacity",&cfg.pod_capacity},
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
            {"--mutate-weight-prob",&cfg.mutation.mutate_weight_probability},
            {"--mutate-add-synapse-prob",&cfg.mutation.add_synapse_probability},
            {"--mutate-remove-synapse-prob",&cfg.mutation.remove_synapse_probability}};
        std::size_t steps=4800,record_every=10,companions=0;
        bool record_brains=true,config_changed=false;
        auto companion_controller=neuroevo::ControllerKind::Reactive;
        std::string resume,founders;
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
                    "  --record-brains 0|1       Record neural activity, default 1 (graphs always saved)\n"
                    "  --seed N                  World seed, default 7\n"
                    "  --controller X            spiking (default), reactive, or random\n"
                    "  --companions N            Last N creatures use --companion-controller\n"
                    "  --companion-controller X  reactive (default), random, or spiking\n"
                    "  --food-assignment X       random (default), a-rich, or b-rich\n"
                    "  --reproduction 0|1        Enable automatic local reproduction, default 1\n"
                    "  --no-reproduction         Shortcut for --reproduction 0\n"
                    "  --communication 0|1       Enable calling and hearing, default 1\n"
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
            if (i+1>=argc) throw std::invalid_argument("Missing value after "+arg);
            const std::string value=argv[++i];
            if (arg == "--out") out=value;
            else if (arg == "--steps") steps=integer(value,arg);
            else if (arg == "--record-every") record_every=integer(value,arg);
            else if (arg == "--record-brains") record_brains=boolean(value,arg);
            else if (arg == "--resume") resume=value;
            else if (arg == "--founders") founders=value;
            else {
                config_changed=true;
                if (arg == "--seed") cfg.seed=integer(value,arg);
                else if (arg == "--controller") cfg.controller=neuroevo::parse_controller(value);
                else if (arg == "--companions") companions=integer(value,arg);
                else if (arg == "--companion-controller") companion_controller=neuroevo::parse_controller(value);
                else if (arg == "--reproduction") cfg.reproduction=boolean(value,arg);
                else if (arg == "--communication") cfg.communication=boolean(value,arg);
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
        if (steps==0 || record_every==0) throw std::invalid_argument("--steps and --record-every must be positive");
        if (!resume.empty() && (config_changed || !founders.empty()))
            throw std::invalid_argument("--resume restores the full configuration; use a new world with --founders to change it");
        if (resume.empty() && cfg.initial_creatures==0) throw std::invalid_argument("--creatures must be at least 1");
        if (companions>cfg.initial_creatures) throw std::invalid_argument("--companions cannot exceed --creatures");
        for (const char* name : {"ecosystem.jsonl","ecosystem_stats.csv","events.csv","summary.json","checkpoint.eco","initial.eco"})
            if (std::filesystem::exists(out/name)) throw std::runtime_error("Run output already exists; choose a fresh --out directory: "+out.string());
        auto world=resume.empty()?neuroevo::EcosystemWorld(cfg):load(resume);
        if (!founders.empty()) {
            auto source=load(founders);
            if (source.creatures.empty()) throw std::invalid_argument("Founder checkpoint contains no living creatures");
            for (std::size_t i=0;i<world.creatures.size();++i) {
                const auto& brain=source.creatures[i%source.creatures.size()].brain;
                if (brain.config().dt != world.config.brain.dt) throw std::invalid_argument("Founder brains require matching --brain-dt");
                world.creatures[i].brain=brain;
                world.creatures[i].brain.reset_state();
            }
        }
        for (std::size_t i=0;i<companions;++i)
            world.creatures[world.creatures.size()-1-i].controller=companion_controller;
        std::filesystem::create_directories(out);
        std::ofstream replay(out/"ecosystem.jsonl"),stats(out/"ecosystem_stats.csv"),events(out/"events.csv");
        if (!replay || !stats || !events) throw std::runtime_error("Cannot open run output files");
        replay << std::setprecision(10); events << std::setprecision(12);
        events << "time,type,creature,other,resource,amount\n";
        neuroevo::write_ecosystem_metadata(replay,world);
        neuroevo::write_ecosystem_stats_header(stats);
        save(out/"initial.eco",world);
        std::unordered_set<std::uint64_t> known;
        for (const auto& c:world.creatures) known.insert(c.id);
        std::vector<neuroevo::EcoEvent> pending;
        auto record=[&]() {
            neuroevo::write_ecosystem_frame(replay,world,record_brains,&known,&pending);
            neuroevo::write_ecosystem_stats(stats,world);
            pending.clear(); replay.flush(); stats.flush();
            if (!replay || !stats || !events) throw std::runtime_error("Failed to write simulation output");
        };
        record();
        const auto starting_step=world.step_index;
        const auto started=std::chrono::steady_clock::now();
        std::signal(SIGINT,request_stop); std::signal(SIGTERM,request_stop);
        std::uint64_t last_recorded=world.step_index;
        std::cout << "Ecosystem: " << world.creatures.size() << " creatures, " << neuroevo::eco_input_count
            << " sensory inputs, " << neuroevo::eco_output_count << " motor outputs\nOutput: " << out.string() << '\n';
        std::cout << "Founders use " << neuroevo::to_string(world.config.controller)
            << " controllers. Random spiking brains may not survive; survival must evolve.\n";
        for (std::size_t i=0;i<steps && !world.creatures.empty() && !world.capacity_limited && !stop_requested;++i) {
            world.step();
            for (const auto& e:world.events) {
                events << e.time << ',' << e.type << ',' << e.creature << ',' << e.other << ',' << e.resource << ',' << e.amount << '\n';
                pending.push_back(e);
            }
            if ((i+1)%record_every==0) { record(); last_recorded=world.step_index; }
            if ((i+1)%1000==0) std::cout << "t=" << world.time() << "s population=" << world.creatures.size()
                << " births=" << world.totals.births << " deaths=" << world.totals.deaths << std::endl;
        }
        if (last_recorded!=world.step_index || !pending.empty()) record();
        save(out/"checkpoint.eco",world);
        const double wall_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        const char* status=world.capacity_limited?"capacity_limited":world.creatures.empty()?"extinct":stop_requested?"interrupted":"completed";
        std::ofstream summary(out/"summary.json");
        summary << "{\n  \"status\":\"" << status << "\",\n  \"steps_run\":" << world.step_index-starting_step
            << ",\n  \"step\":" << world.step_index << ",\n  \"time\":" << world.time()
            << ",\n  \"population\":" << world.creatures.size() << ",\n  \"births\":" << world.totals.births
            << ",\n  \"deaths\":" << world.totals.deaths << ",\n  \"wall_seconds\":" << wall_seconds << "\n}\n";
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
