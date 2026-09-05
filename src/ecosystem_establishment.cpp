#include "neuroevo/ecosystem.hpp"
#include "ecosystem_mutation.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <numeric>
#include <stdexcept>

namespace neuroevo {
namespace {
constexpr double pi = 3.14159265358979323846;
double trial_score(const ArchiveTrial& t)
{
    const double coverage = t.energy_gained / std::max(1.0, t.energy_spent);
    return std::log1p(t.energy_gained) * (0.5 + std::min(2.0, coverage))
        + 0.25 * std::log1p(static_cast<double>(t.feeding_bouts))
        + 6.0 * std::log1p(static_cast<double>(t.offspring));
}
void score(GenomeArchiveEntry& entry)
{
    entry.score = 0;
    for (const auto& trial : entry.trials) entry.score += trial_score(trial);
    entry.score /= static_cast<double>(entry.trials.size());
}

}

const char* to_string(CreatureOrigin origin)
{
    switch (origin) {
    case CreatureOrigin::Founder: return "founder";
    case CreatureOrigin::Birth: return "birth";
    case CreatureOrigin::ArchiveMutation: return "archive-slight-mutation";
    case CreatureOrigin::ArchiveClone: return "archive-clone";
    case CreatureOrigin::RandomImmigrant: return "random-immigrant";
    case CreatureOrigin::ArchiveStrongMutation: return "archive-strong-mutation";
    }
    throw std::invalid_argument("Invalid creature origin");
}

std::size_t EcosystemWorld::population_floor() const
{
    if (config.immigration_floor != 0) return config.immigration_floor;
    return std::max<std::size_t>(1, std::min(config.max_population - 1, config.initial_creatures / 2));
}

bool EcosystemWorld::immigration_enabled() const
{
    return config.establishment && !immigration_withdrawn && !capacity_limited;
}

void EcosystemWorld::consider_archive(const EcoCreature& c, bool finished, double observed_at)
{
    if (!config.establishment || c.controller != ControllerKind::Spiking) return;
    const auto genome_id = c.genome_id ? c.genome_id : c.id;
    auto existing = std::find_if(archive.begin(), archive.end(), [&](const auto& e) { return e.genome_id == genome_id; });
    const double operating_spent = std::max(0.0,
        c.energy_spent - static_cast<double>(c.offspring) * config.reproduction_cost);
    const double efficiency = c.energy_gained / std::max(1.0, operating_spent);
    const bool partial_success = c.energy_gained >= config.archive_min_energy
        && c.feeding_bouts >= config.archive_min_feeding_bouts
        && efficiency >= config.archive_min_efficiency;
    const bool qualifies = c.age >= config.archive_min_age && (c.offspring > 0 || partial_success);
    // Failed repeats of a known genome count as evidence, even when they never eat.
    if (!qualifies && (existing == archive.end() || !finished)) return;
    ArchiveTrial trial;
    trial.creature_id = c.id;
    trial.feeding_bouts = c.feeding_bouts;
    trial.offspring = c.offspring;
    trial.energy_gained = c.energy_gained;
    trial.energy_spent = operating_spent;
    trial.age = c.age;
    trial.observed_at = observed_at < 0 ? time() : observed_at;
    trial.finished = finished;
    if (existing != archive.end()) {
        auto sample = std::find_if(existing->trials.begin(), existing->trials.end(), [&](const auto& t) { return t.creature_id == c.id; });
        if (sample != existing->trials.end()) *sample = trial;
        else {
            if (existing->trials.size() == 8) existing->trials.erase(existing->trials.begin());
            existing->trials.push_back(trial);
        }
        if (config.archive_eval_trials == 0) score(*existing);
        return;
    }
    GenomeArchiveEntry candidate;
    candidate.genome_id = genome_id;
    candidate.source_id = c.id;
    candidate.niche = static_cast<FoodKind>(std::max_element(c.eaten.begin(), c.eaten.end()) - c.eaten.begin());
    candidate.genome = c.brain;
    candidate.genome.reset_state();
    candidate.trials.push_back(trial);
    if (config.archive_eval_trials > 0) {
        auto evaluation = newborn_evaluations.find(genome_id);
        if (evaluation == newborn_evaluations.end()) {
            const auto started = std::chrono::steady_clock::now();
            evaluation = newborn_evaluations.emplace(genome_id, evaluate_newborn(candidate.genome)).first;
            evaluation_wall_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            ++evaluation_calls;
            events.push_back({trial.observed_at, "archive_evaluated", c.id, genome_id, 0, evaluation->second.score});
        }
        candidate.score = evaluation->second.score;
    } else score(candidate);
    if (archive.size() < config.archive_capacity) archive.push_back(std::move(candidate));
    else {
        std::array<std::size_t, 4> counts{};
        for (const auto& entry : archive) ++counts[static_cast<std::size_t>(entry.niche)];
        const auto niche = static_cast<std::size_t>(candidate.niche);
        const auto quota = config.archive_capacity / 4;
        std::size_t victim = archive.size();
        const bool reserve = counts[niche] < quota;
        for (std::size_t i = 0; i < archive.size(); ++i) {
            const auto other_niche = static_cast<std::size_t>(archive[i].niche);
            const bool eligible = reserve ? counts[other_niche] > quota : other_niche == niche;
            if (eligible && (victim == archive.size() || archive[i].score < archive[victim].score)) victim = i;
        }
        if (victim == archive.size() || (!reserve && candidate.score <= archive[victim].score)) return;
        archive[victim] = std::move(candidate);
    }
    const auto admitted = std::find_if(archive.begin(), archive.end(), [&](const auto& e) { return e.genome_id == genome_id; });
    events.push_back({trial.observed_at, "archive_admitted", c.id, genome_id, 0, admitted->score});
}

void EcosystemWorld::update_establishment()
{
    if (!config.establishment || capacity_limited) return;
    const auto floor = population_floor();
    if (creatures.size() < floor) support_stable_since = -1;
    else if (support_stable_since < 0) support_stable_since = time();
    const double cycle = config.calm_duration + config.warning_duration + config.storm_duration;
    if (!immigration_withdrawn && config.immigration_auto_stop && totals.natural_spiking_breeders >= 2
        && support_stable_since >= 0
        && time() - std::max(support_stable_since, last_immigration_time) >= static_cast<double>(config.withdrawal_cycles) * cycle) {
        immigration_withdrawn = true;
        events.push_back({time(), "immigration_withdrawn", 0, 0, 0, 0});
    }
    if (time() + 1e-9 < next_immigration_check) return;
    const double multiplier = config.immigration_auto_stop
        ? 1.0 + static_cast<double>(std::min<std::uint64_t>(3, totals.natural_spiking_breeders)) : 1.0;
    next_immigration_check = time() + config.immigration_interval * multiplier;
    // A fixed order keeps admission/tie handling independent of body vector order.
    std::vector<std::size_t> order(creatures.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return creatures[a].id < creatures[b].id; });
    for (auto i : order) consider_archive(creatures[i], false);
    if (immigration_withdrawn || creatures.size() >= floor) return;
    if (archive.empty()) {
        ++totals.archive_empty_checks;
        events.push_back({time(), "immigration_waiting_for_archive", 0, 0, 0, 0});
        return;
    }

    // Uniform placement over all physically valid cells, without knowledge of food.
    std::vector<Vec2> locations;
    for (std::size_t y = 0; y < config.height; ++y) for (std::size_t x = 0; x < config.width; ++x) {
        const Vec2 p{static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5};
        if (traversable(p)) locations.push_back(p);
    }
    const auto requested = std::min(config.immigration_batch, floor - creatures.size());
    std::size_t arrived = 0;
    while (arrived < requested && !locations.empty()) {
        const auto location = immigration_rng.uniform_index(locations.size());
        const Vec2 p = locations[location];
        locations[location] = locations.back(); locations.pop_back();
        if (std::any_of(creatures.begin(), creatures.end(), [&](const auto& c) {
            return length(c.position - p) < 2 * config.radius + 1e-9;
        })) continue;
        if (immigration_deck_cursor == immigration_deck.size()) {
            immigration_deck = {CreatureOrigin::ArchiveMutation, CreatureOrigin::ArchiveMutation,
                CreatureOrigin::ArchiveClone, CreatureOrigin::ArchiveClone, CreatureOrigin::ArchiveStrongMutation};
            for (std::size_t i = immigration_deck.size(); i > 1; --i)
                std::swap(immigration_deck[i - 1], immigration_deck[immigration_rng.uniform_index(i)]);
            immigration_deck_cursor = 0;
        }
        EcoCreature c;
        c.id = next_creature_id++;
        c.genome_id = c.id;
        c.origin = immigration_deck[immigration_deck_cursor++];
        c.position = p;
        c.heading = immigration_rng.uniform(-pi, pi);
        c.energy = config.founder_energy;
        c.controller = ControllerKind::Spiking;
        if (c.origin == CreatureOrigin::RandomImmigrant) {
            // Retained only so v2 checkpoints containing a live random
            // immigrant remain readable; v3 never deals this origin.
            Random genome_rng(immigration_rng.next_u64());
            c.brain = Brain::random(config.brain, genome_rng);
            ++totals.immigrant_random;
        } else {
            std::vector<FoodKind> niches;
            for (const auto& entry : archive)
                if (std::find(niches.begin(), niches.end(), entry.niche) == niches.end()) niches.push_back(entry.niche);
            const auto niche = niches[immigration_rng.uniform_index(niches.size())];
            std::vector<std::size_t> candidates;
            for (std::size_t i = 0; i < archive.size(); ++i) if (archive[i].niche == niche) candidates.push_back(i);
            const auto tournament_size = std::min(config.archive_tournament_size, candidates.size());
            std::size_t selected_index = archive.size();
            for (std::size_t round = 0; round < tournament_size; ++round) {
                const auto draw = round + immigration_rng.uniform_index(candidates.size() - round);
                std::swap(candidates[round], candidates[draw]);
                const auto challenger = candidates[round];
                if (selected_index == archive.size() || archive[challenger].score > archive[selected_index].score)
                    selected_index = challenger;
            }
            const auto& selected = archive[selected_index];
            c.source_id = selected.source_id;
            c.brain = selected.genome;
            if (c.origin == CreatureOrigin::ArchiveClone) {
                c.genome_id = selected.genome_id;
                ++totals.immigrant_clones;
            } else if (c.origin == CreatureOrigin::ArchiveMutation) {
                c.brain.mutate(detail::slight_mutation(config.mutation), immigration_rng);
                ++totals.immigrant_mutations;
                ++totals.immigrant_slight_mutations;
            } else {
                c.brain.mutate(detail::strong_mutation(config.mutation), immigration_rng);
                ++totals.immigrant_mutations;
                ++totals.immigrant_strong_mutations;
            }
        }
        c.brain.reset_state();
        c.neural_rng = Random(immigration_rng.next_u64());
        totals.immigrant_energy += c.energy;
        ++totals.immigrants;
        ++arrived;
        last_immigration_time = time();
        support_stable_since = -1;
        events.push_back({time(), std::string("immigration_") + to_string(c.origin), c.id, c.source_id, 0, c.energy});
        creatures.push_back(std::move(c));
    }
    if (arrived == 0) events.push_back({time(), "immigration_blocked", 0, 0, 0, 0});
}
} // namespace neuroevo
