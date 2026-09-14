# Switchable Izhikevich neurons: implementation and experiments

Date: 2026-09-10.

The alternative model is implemented and selectable. It provides adaptive,
history-dependent firing and useful two-neuron competition in selected parameter
regions. It is substantially more expensive per simulated second than the
existing 20 ms LIF model, and does not make arbitrary mutually inhibitory pairs
reliable winner-take-all circuits. LIF remains the default.

## Select the alternative

From the repository root:

```powershell
.\scripts\ecosystem.ps1 -NeuronModel izhikevich -Steps 4800 -Build -Open
# Or use the executable directly:
.\build\neuroevo_ecosystem.exe --neuron-model izhikevich --out runs/izh_trial
```

For C++ experiments:

```cpp
EcosystemConfig config;
config.brain.select_model(NeuronModel::Izhikevich);
// Or construct a standalone configuration:
auto brain_config = BrainConfig::izhikevich();
```

Selection sets `neuron_model`, `dt=0.001` seconds and `max_delay_steps=160`.
The maximum physical delay remains 160 ms. Sensory neurons retain calibrated
rate encoding; hidden and motor neurons use Izhikevich dynamics. World timing,
sensory reference rates and motor rate decoding remain in seconds. The model
requires calibrated IO. Do not simply change the enum while leaving dt at 20 ms;
invalid Izhikevich timing is rejected.

`--brain-dt` may explicitly refine the timestep, independently of argument order.
It must divide the fixed 1 ms synaptic pulse; delay-buffer and world-step limits
also apply. CLI overrides `--izh-a`, `--izh-b`, `--izh-c`, and `--izh-d` select
founder/default intrinsic parameters. New random neurons, sparse ancestral
neurons and structurally added neurons use these defaults. For
`Brain::from_components`, the caller supplies each neuron's `izhikevich` fields;
the function initializes its voltage/recovery from those parameters.

This selects a model for new brains. It does not convert saved LIF genomes into
behaviorally equivalent Izhikevich genomes. Founder imports require matching
model and timestep. Resume restores the saved model and disallows model overrides.
The ancestral circuit has not been retuned for equivalent ecological behavior.

## Model and numerical conventions

The implementation uses

```text
dv/dt_ms = 0.04 v² + 5 v + 140 - u + I
du/dt_ms = a (b v - u)
at v >= 30: v <- c; u <- u + d
```

The recovery variable supplies negative feedback and permits adaptation. Unlike
the current LIF path, this model does not impose a fixed 40 ms refractory period.
The value +30 mV is the spike apex used for reset, not a tunable voltage threshold
for spike initiation. [Original model, Izhikevich 2003](https://www.izhikevich.org/publications/spikes.pdf)

One neural update uses two half-sized voltage updates followed by one recovery
update. Voltage is stopped at the spike apex to prevent numerical overshoot from
feeding the quadratic term. Reset is applied in that update, and outgoing events
are scheduled afterward; there is no neuron-order-dependent immediate delivery.
This follows the structure of the author's pulse-coupled scheme, with explicit
apex handling. [Author's example implementation](https://www.izhikevich.org/publications/net.m)

Izhikevich synapses deliver a **fixed 1 ms rectangular current pulse** of amplitude
`weight * synaptic_gain`. At finer dt it occupies multiple buffer entries.
Background events use the same pulse duration. LIF retains its original one-update
current impulse and refractory semantics. Keeping physical pulse duration fixed
is essential when testing convergence; conserving only area while shortening
the pulse would change the circuit being tested.

Current units differ between the two models. The shared numeric weight/gain
representation does not establish equivalent physiological input strength.
Izhikevich bias is DC current; LIF bias is in the existing integrator's units.
LIF threshold remains meaningful for LIF neurons and sensory rate sensitivity;
Izhikevich hidden/motor neurons do not use that field as a firing threshold.

## Suggested mutation policy

Begin with regular-spiking parameters fixed. Allow evolution to explore wiring
and weights first. Intrinsic mutation is implemented but **disabled by default**.

| Parameter | Role | Regular-spiking default | Initial policy |
| --- | --- | --- | --- |
| a | Recovery speed | 0.02 | Optional small multiplicative mutations |
| b | Coupling of recovery to voltage | 0.2 | Inherit, keep fixed |
| c | Voltage after a spike | -65 | Inherit, keep fixed |
| d | Recovery increment after a spike | 8 | Optional small multiplicative mutations |

The roles and common firing regimes follow the original model. Changes in b or c
can move a neuron into substantially different excitability or bursting regimes;
they are better explored through explicit parameter families initially.
[Izhikevich 2003](https://www.izhikevich.org/publications/spikes.pdf)

To opt in, set `mutation.izhikevich_intrinsic_probability=0.1`, or use
`--izh-intrinsic-mutation 0.1`. Within an eligible Izhikevich neuron-parameter
edit, this probabilistically selects an intrinsic edit. One of a or d is selected
with equal probability and multiplied by `exp(N(0, log_sigma))`.

- Base `izhikevich_log_sigma=0.05`, approximately a 5% relative perturbation.
- Slight/strong birth profiles scale this sigma like other mutation sigmas.
- Mutation bounds: a in [0.005, 0.1], d in [0.5, 12].
- b and c are inherited unchanged. They can still be explicitly configured.
- A rare hidden-position edit retains the existing ability to evolve travel
  delays. Other non-intrinsic edits adjust bias or background sensitivity.
- Mutated hidden bias is bounded by `hidden_bias_min..3`. For the default RS
  family this stays below tonic-firing onset; it is not a universal subthreshold
  guarantee for arbitrary hand-selected b/c values.
- Voltage and recovery are **state**, not genes. Birth/reset clears them to
  `v=c, u=b*c`. Full continuation checkpoints preserve them.

The validation experiment perturbs a and d of both competitors independently
with 5% log-normal noise at once. That is a stronger change than one ordinary
intrinsic mutation. The selected fast-recovery circuit tolerated this better
than the selected RS circuit, so mutation tolerance should be evaluated together
with circuit structure, rather than inferred from parameter bounds alone.

## Experiment 1: profiling

Machine: AMD Ryzen 9 7900X, Windows, GCC/MinGW C++17 Release build. All workloads
are serial. Each condition has a discarded warmup and seven measured repetitions;
model order rotates. Construction and disk recording are excluded.

Three controls are compared:

1. Existing LIF at 20 ms.
2. LIF at 1 ms, keeping physical delay range and voltage impulse area unchanged.
3. Regular-spiking Izhikevich at 1 ms.

The same starting neurons, positions, weights and edges are used for each neural
workload. The five-hidden-neuron case uses the ancestor; 32/128-hidden cases use
the same seeded random topology. Inputs are identical deterministic intensity
vectors. Neural microbenchmarks have no background RNG. Each measured batch is
32 brains simulated for one second.

| Workload | LIF 20 ms median | LIF 1 ms median | Izh 1 ms median | Izh / current LIF | Izh / fine-step LIF |
| --- | ---: | ---: | ---: | ---: | ---: |
| 5 hidden, 83 inputs, 6 outputs | 1.379 ms | 30.375 ms | 32.569 ms | 23.6x | 1.07x |
| 32 hidden, 83 inputs, 6 outputs | 2.415 ms | 49.649 ms | 56.396 ms | 23.4x | 1.14x |
| 128 hidden, 83 inputs, 6 outputs | 9.147 ms | 143.156 ms | 232.004 ms | 25.4x | 1.62x |
| 24-creature world, 10 simulated seconds, no background events | 166.568 ms | 359.663 ms | 370.705 ms | 2.23x | 1.03x |
| Same world, default background events enabled | 168.163 ms | 375.492 ms | 386.393 ms | 2.30x | 1.03x |

An additional silent kernel removes inputs, connections and activity differences:
256 neurons, 5,000 updates. LIF at 1 ms takes 4.012 ms, Izhikevich 10.474 ms:
**2.61x per update**, or approximately 3.13 versus 8.18 ns per neuron-update in
this loop. This is an isolated runtime measurement, not an instruction-count
estimate.

Thus the richer update is about 2.6x as expensive in isolation, but the main
cost relative to today's simulation is the 20x increase in update frequency.
Sensor processing, routing and loop overhead dilute the equation-cost ratio
in small brains. Differences in firing and event delivery also affect the
active-network results, especially at 128 hidden neurons.

The world benchmark retains sensing, physics, resources and neural activity,
but disables reproduction to keep population size controlled. It is not a
long-run evolutionary performance estimate. Do not project its 2.3x factor to
larger populations or denser evolved brains; use the separate neural figures
to understand where that factor can grow. Runtime dispersion is preserved in
`analysis.json` and every repeat is in `profile.csv`.

The dense delayed-current buffer also grows from 9 to 161 doubles per neuron at
the two default timesteps, approximately 17.9x for that allocation. Per-neuron
parameters/recovery add five doubles to the neuron structure, including when
LIF is selected. Event-based delay storage and less frequent sensory updates are
possible later optimizations; this experiment does not implement them.

## Experiment 2: two-neuron winner-take-all

The motif contains exactly two computational neurons, each with excitatory
self-feedback and equal reciprocal inhibition. Six calibrated event sources
provide controlled seed and drive signals; there are no motor outputs or other
hidden neurons. Both candidates have identical intrinsic parameters unless a
perturbation test explicitly changes them. Mixed outgoing signs remain permitted
by the simulator; choosing a parameter family does not assign transmitter type.

For constant-drive tests, the stronger input initially belongs to A and switches
to B at 2 seconds. Output rates are measured over seconds 1–2 and 3–4. Success in
the robustness tables requires the correct neuron to dominate by at least 3:1
in **both** windows, with positive winner activity. Complete loser suppression
is additionally visible in the selected examples.

The broad sweep tests 4,320 circuits across LIF 20 ms, LIF 1 ms, RS, lower
adaptation, and fast-recovery neurons. It varies self/inhibitory weights, physical
delays, drive levels and seed offsets. It does not show a general Izhikevich
advantage for arbitrary circuits. In particular, LIF with strong inhibition and
no self-excitation already handles some constant-drive comparisons well.

A focused sweep adds 2,160 cases using 1–20 ms inhibitory delays and 5–60 ms
self-delays. It finds successful Izhikevich regions missed by the longer-delay
grid. The selected examples are:

| Candidate | a, b, c, d | Self weight / delay | Equal inhibitory weight / delay | Winner rate at 1 ms |
| --- | --- | --- | --- | ---: |
| Regular spiking | .02, .2, -65, 8 | 2 / 10 ms | 1 / 1 ms | about 77 Hz |
| Less adaptation | .02, .2, -65, 2 | 1 / 10 ms | 1 / 20 ms | about 83–84 Hz |
| Fast recovery | .1, .2, -65, 2 | .5 / 5 ms | .5 / 1 ms | about 111 Hz |

These examples receive DC drives **4.8 and 7.2**, which exchange neurons at the
reversal. Both drives exceed the approximate RS/FS tonic onset near 4, so loser
silence is not merely the absence of a suprathreshold input. Synaptic gain is 32.
The RS example yields roughly `(A,B)=(77,0)` before reversal and `(0,77)` after.

The model has a wider input-rate response than the current LIF ceiling; its
recovery state changes the effect of successive spikes. Nevertheless, recurrent
feedback still creates plateaus and attractors. Different input sensitivity
does not guarantee selection, reversibility or memory retention in every motif.

### Validation beyond the selected examples

| Test | Regular spiking | Less adaptation | Fast recovery |
| --- | ---: | ---: | ---: |
| Resolution and seed offsets | 12/12 | 9/12 | 12/12 |
| +/-10% weights and different input contrasts | 27/81 | 38/81 | 27/81 |
| Independent 5% a/d perturbations in both neurons | 60/100 | 54/100 | 100/100 |
| Sparse random input, 20 versus 40 Hz | 6/100 | 5/100 | 40/100 |

Resolution tests use dt=1, .5, .25 and .1 ms with the same physical pulse width,
and seed offsets 0, 5 and 15 ms. RS winner rates remain about 77–78 Hz. Fast
recovery preserves the selection outcome, but its winner rate changes from 111
to about 135 Hz as dt is refined, so 1 ms should not be considered precise for
that circuit's firing rate. The lower-adaptation candidate fails at .25 ms in
these tests, another indication of sensitive circuit dynamics.

Weight/contrast tests independently scale both shared self-weights and both
shared inhibitory weights by .9/1/1.1. Drive pairs are (4.4,5.2), (4.8,7.2),
and (7.2,9.6), with three seed offsets. They test a mixture of changes; their
success fractions are not estimates of isolated weight-mutation tolerance.

Sparse input tests use 100 seeds and independent Bernoulli spike arrivals per
millisecond, approximating Poisson trains at 20 and 40 Hz. Each external spike
has excitatory weight .5; the rate advantage reverses at 2 s. These inputs are
not matched in mean current to the tonic cases. They test whether those selected
circuits transfer to a more sensory-like drive without retuning. They often do
not. Counts also reflect the limited one-second readout windows and stochastic
evidence; no universal probability-of-success claim follows from them.

The positive conclusion is that useful input-sensitive, reversible competition
is achievable. The negative conclusion is that switching neuron equations alone
does not solve robust selection for the ecosystem's spike inputs. Longer
synaptic currents or small competing populations remain reasonable separate
experiments. More adaptation can also make winners relinquish control; whether
that is desirable depends on the behavior being evolved.

## Reproduce and inspect

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\neuroevo_izhikevich_experiment.exe all runs/izhikevich
python tools/plot_izhikevich_experiment.py runs/izhikevich
```

The executable also accepts `profile`, `wta`, `focused`, and `validate` to run
one part. Plotting requires matplotlib/numpy; simulation uses only C++.
Multi-config generators may place executables under `build/Release`.

Outputs in `runs/izhikevich`: `profile.csv`, `fi.csv`, `wta.csv`,
`wta_short_delays.csv`, `validation.csv`, `candidate_trace.csv`, `analysis.json`,
and `izhikevich-results.png` / `.svg`. Runs are git-ignored; source harnesses
preserve reproducibility.

All 14 CTest tests passed. New coverage includes quiet resting behavior,
adaptation, input-response ordering, comparison with an independent .01 ms
Euler reference, a convergent RS competition/reversal fixture, opt-in intrinsic
mutation, and exact checkpoint continuation with multi-update pulses and a
wrapped buffer cursor. CLI checks exercise selection, explicit founder
parameters, recording and continuation.

Ecosystem checkpoints now write format 23 and standalone brains format 4.
Format 22 worlds and format 3 LIF brains remain readable. A real format-22
checkpoint produced by the previous executable was resumed successfully and
produced a byte-identical final checkpoint to an uninterrupted LIF run.
Replay metadata records the model, timestep and intrinsic parameters; the viewer
distinguishes Izhikevich voltage and spike apex from LIF threshold and input phase.
