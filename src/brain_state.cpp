#include "neuroevo/brain.hpp"
#include "checkpoint_fields.hpp"
#include <iomanip>
#include <limits>

namespace neuroevo {

void Brain::save_state(std::ostream& s) const
{
    s << std::setprecision(std::numeric_limits<double>::max_digits10);
    checkpoint::write(s,"NEUROEVO_BRAIN_3");
    checkpoint::write_tuple(s,checkpoint::brain_fields(config_));
    checkpoint::write_tuple(s,checkpoint::calibrated_brain_fields(config_));
    checkpoint::write(s,neurons_.size(),synapses_.size(),buffer_cursor_);
    for (const auto& n : neurons_)
        checkpoint::write(s,n.position.x,n.position.y,n.bias,n.potential,n.threshold,
            n.background_sensitivity,n.refractory_remaining,n.spiked);
    for (const auto& e : synapses_) checkpoint::write(s,e.pre,e.post,e.weight,e.delay_steps);
    for (const auto& row : current_buffers_) {
        for (double v : row) checkpoint::write_value(s,v);
        s << '\n';
    }
    for (double v : motor_traces_) checkpoint::write_value(s,v);
    s << '\n';
    if (!s) throw std::runtime_error("Failed to save brain state");
}

Brain Brain::load_state(std::istream& s)
{
    std::string version;
    checkpoint::read(s,version);
    if (version != "NEUROEVO_BRAIN_3")
        throw std::runtime_error("Unknown brain checkpoint version");
    BrainConfig c;
    checkpoint::read_tuple(s,checkpoint::brain_fields(c));
    checkpoint::read_tuple(s,checkpoint::calibrated_brain_fields(c));
    if (c.input_count > 10000 || c.output_count > 10000 || c.hidden_count > 10000
        || c.max_delay_steps < 1 || c.max_delay_steps > 4096 || c.dt <= 0
        || c.membrane_tau <= 0 || c.threshold <= 0 || c.conduction_speed <= 0
        || c.sensory_rate_hz <= 0 || (c.calibrated_io && c.sensory_rate_hz*c.dt > 1)
        || c.motor_rate_tau <= 0 || c.motor_reference_hz <= 0)
        throw std::runtime_error("Invalid brain checkpoint configuration");
    const auto n = checkpoint::count(s,30000);
    const auto edges = checkpoint::count(s,1000000);
    std::size_t cursor = 0; checkpoint::read(s,cursor);
    if (n != c.input_count+c.output_count+c.hidden_count || cursor > c.max_delay_steps
        || n*(c.max_delay_steps+1) > 5000000)
        throw std::runtime_error("Invalid brain checkpoint dimensions");
    Brain b(c);
    for (auto& v : b.neurons_) {
        checkpoint::read(s,v.position.x,v.position.y,v.bias,v.potential,v.threshold,
            v.background_sensitivity,v.refractory_remaining,v.spiked);
        if (v.threshold <= 0 || v.background_sensitivity < 0 || v.refractory_remaining < 0)
            throw std::runtime_error("Invalid neuron checkpoint");
    }
    b.synapses_.resize(edges);
    for (auto& e : b.synapses_) {
        checkpoint::read(s,e.pre,e.post,e.weight,e.delay_steps);
        if (e.pre >= n || e.post >= n || e.delay_steps < 1 || e.delay_steps > c.max_delay_steps)
            throw std::runtime_error("Invalid synapse checkpoint");
    }
    b.rebuild_runtime_state();
    b.buffer_cursor_ = cursor;
    for (auto& row : b.current_buffers_) for (auto& v : row) checkpoint::read(s,v);
    for (auto& v : b.motor_traces_) checkpoint::read(s,v);
    return b;
}
} // namespace neuroevo
