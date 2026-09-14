# Filtered LIF in the simulation

Filtered LIF is available for new runs. Existing LIF remains the default.

```powershell
# 5 ms (default for this model)
.\scripts\ecosystem.ps1 -NeuronModel filtered-lif -Steps 4800 -Build -Open

# 2 ms
.\scripts\ecosystem.ps1 -NeuronModel filtered-lif -BrainDt 0.002 -Steps 4800 -Open

# Executable equivalents
.\build\neuroevo_ecosystem.exe --neuron-model filtered-lif --out runs/filtered_5ms
.\build\neuroevo_ecosystem.exe --neuron-model filtered-lif --brain-dt 0.002 --out runs/filtered_2ms
```

`--brain-dt` and `--synaptic-gain` overrides apply regardless of argument order.
Model and timestep selection applies to new worlds. Resume restores the complete
saved configuration; founder imports must match the selected model and timestep.
There is no automatic conversion of an existing LIF/Izhikevich genome to filtered LIF.

In C++:

```cpp
EcosystemConfig config;
config.brain.select_model(NeuronModel::FilteredLif); // 5 ms
// Or choose the timestep explicitly:
config.brain = BrainConfig::filtered_lif(0.002);
```

The factory returns a fresh BrainConfig, so apply custom sensory layout and other
overrides afterward. `select_model` preserves the existing sensory layout, but
selects the model's timing, membrane/refractory constants, and synaptic gain.

| Parameter | Default |
| --- | ---: |
| Neural timestep | 5 ms; optional 2 ms |
| Membrane time constant | 50 ms |
| Synaptic time constant | 100 ms |
| Refractory period | 10 ms |
| Synaptic gain | 20 |
| Maximum delay | 160 ms: 32 slots at 5 ms, 80 at 2 ms |

The delay ring has one additional cursor slot. Sensory phase encoding, motor
rate decoding, and world time remain unchanged. Hidden and motor neurons use
filtered LIF; sensory neurons retain calibrated phase encoding. Legacy sensory
encoding is rejected for this model. For numerical experiments, smaller steps
down to 0.05 ms are accepted provided the refractory period is an integer number
of steps and both time constants are at least one step. The world timestep must
also be compatible with the neural timestep.

## Dynamics and units

```text
dv/dt = -v / membrane_tau + bias + synaptic_current
ds/dt = -s / synaptic_tau
on arrival: s += weight * synaptic_gain
on spike: v = reset_potential; start refractory period
```

During the refractory period voltage is held at reset, while the synaptic current
continues to receive events and decay. Inhibition arriving then can affect the
voltage afterward. Excitation and inhibition share the same time constant, so one
signed current accumulator per neuron is sufficient. Background events also add
to this accumulator, with the configured event-current amplitude and sensitivity.

Voltage is integrated exactly below threshold for a constant bias and exponential
current over each timestep. The decay and integration coefficients are cached at
construction/rebuild, including the finite limit when membrane and synaptic time
constants are equal. Spikes and resets still occur on timestep boundaries; this
retains the firing-rate plateaus discussed in the research report.

Current uses the existing simulation units of potential/second. To compare with
the dimensionless research equations, `drive = membrane_tau * bias`, and the
dimensionless synaptic trace is `membrane_tau * synaptic_current`. The default
gain 20 and membrane_tau 0.05 make a unit-weight spike add one unit of that trace.
For example, research DC drive 1.2 corresponds to simulation bias 24. Hidden bias
is still capped below the tonic firing threshold; test circuits supply strong
drive through sensory events or output-neuron DC inputs instead.

An event adds the same peak current at 5 ms and 2 ms; it is not rescaled by dt.
The synaptic decay determines its physical duration. The sparse ancestor keeps
its nominal filtered-LIF weights when gain changes, so the gain controls actual
delivered current. The previous compensation that cancelled gain changes for
filtered-LIF ancestors has been removed; pulse-model calibration is unchanged.
Its topology is retained, but its ecological behavior has not been retuned to
match the original pulse-driven ancestor.

## Reducing excessive firing

For a fresh, lower-activity trial:

```powershell
.\scripts\ecosystem.ps1 -NeuronModel filtered-lif -SynapticGain 8
# Optional: -BrainDt 0.002
# Executable equivalent: --neuron-model filtered-lif --synaptic-gain 8
```

The default remains 20. At gain 20, a unit-weight filtered event has integrated
current `20 * 0.1 = 2`, versus `32 * 0.02 = 0.64` for old LIF. Gain 6.4 matches
that event integral, but does not equate firing: membrane decay, refractory
duration, and retained refractory inputs differ. Gain 8 is a moderate starting
trial, not a validated ecological optimum. It scales both excitatory and inhibitory
connections, preserving their relative weights; bias and background-current
amplitude are separate controls. Lower gain can eliminate persistent activity.

Raising hidden/output thresholds also lowers rates, but do not indiscriminately
scale sensory thresholds: those control the sensory spike encoder. In the
zero-reset linear dynamics, threshold scaling resembles reducing all input
currents; it affects bias and background drive too, unlike synaptic gain alone.

`neuroevo_filtered_gain_experiment` compares the same ancestral weights under
three constant sensory levels (0.1, 0.3, 0.6), with background activity on/off.
It runs 12 seconds and measures the final 10 seconds, excluding sensory spikes.
At level 0.3 with background enabled (seed 731):

| Setting | Mean hidden Hz | Mean motor Hz |
| --- | ---: | ---: |
| Old LIF | 3.44 | 3.53 |
| Filtered, gain 20 | 25.48 | 15.45 |
| Filtered, gain 8 | 11.90 | 4.88 |
| Filtered, gain 6.4 | 5.14 | 0.95 |
| Filtered, gain 20, hidden/output thresholds ×2.5 | 11.24 | 4.70 |

These synthetic inputs are diagnostic, not ecological trajectories. At weak
input without background, gains 8 and 6.4 silenced this circuit. Revalidate
memory, switching, and behavior before choosing a permanent gain. The earlier
two-neuron memory/WTA parameters were validated at gain 20, not at gain 8.
Run `cmake --build build --target neuroevo_filtered_gain_experiment` then
`.\build\neuroevo_filtered_gain_experiment.exe` to reproduce the CSV output.

Resume keeps the saved gain; this launcher override is for new runs. To retain
evolved weights while trying a new gain, use `-StartingGenomes runs/YOUR_RUN`
alongside the model and gain options. Founder import copies the weights into
the new world's brain configuration and resets transient state; the source
model and timestep must match. It is a new world, not a continuation.

## Evolution and persistence

Filtered LIF uses the existing LIF mutation path for weights, wiring, thresholds,
biases, positions, and background sensitivity. Membrane/synaptic time constants
and refractory duration are inherited configuration values and do not mutate.
No adaptation current is added. Existing restrictions on creating self-edges
remain: constructed/imported self-edges keep their explicit delays, while random
founders and structural mutation do not create them.

Brain checkpoint format 5 stores the filtered current of every neuron and the
synaptic time constant, along with pending delayed events, voltage, refractory
state, motor traces, and the existing model fields. It reads formats 3 and 4.
Ecosystem format 24 reads formats 22 and 23. Old versions cannot represent the
filtered model and are rejected if mislabeled as such. Reset/newborn initialization
clears the current accumulator along with the other transient brain state.

Replay metadata identifies `filtered-lif`, the neural timestep, and synaptic time
constant. Voltage and threshold visualization uses the existing LIF display.

## Validation

- Independent fine-step Euler reference for exponential-current voltage/decay,
  plus the analytic equal-time-constant limit.
- Quiet state, sustained self-feedback, input-dependent rates over a broad drive
  range, and inhibition retained during refractoriness and affecting later voltage.
- A sensory-driven pair with self weights 1, symmetric cross weights -0.45,
  60 ms recurrent delays, and sensory weights 0.6. A 60 Hz versus 20 Hz input
  reverses after four seconds. Measured at 2–4 and 6–8 seconds: **50/0 to 0/50 Hz**
  at 5 ms, and **62.5/0 to 0/62.5 Hz** at 2 ms. These are finite tests of a selected
  circuit, not a guarantee for arbitrary evolved networks or small input contrasts.
- Exact standalone continuation with nonzero currents, refractory state, and
  delayed recurrent spikes; exact CLI world continuation at both timesteps with
  random founders and background activity enabled.
- Fixed intrinsic constants under ordinary structural/parameter mutation.
- Previous-build version-23 LIF and Izhikevich checkpoints resume to byte-identical
  version-24 results compared with uninterrupted runs of the new build.

Run `ctest --test-dir build -C Release --output-on-failure`; the focused target is
`neuroevo_filtered_lif`. The standalone research benchmarks are not production
runtime measurements; switching the simulation does not promise their speedup.
