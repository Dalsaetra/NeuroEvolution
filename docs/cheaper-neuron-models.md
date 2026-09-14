# Cheaper models for persistent activity and two-cell competition

Date: 2026-09-14. This report describes the research prototypes and experiments.
Filtered LIF was subsequently [integrated into the simulation](filtered-lif.md)
at 5 ms with a 2 ms option. Adaptive LIF and rate units remain research prototypes.

## Recommendation

**Use LIF with decaying synaptic currents as the next spiking candidate. Use
continuous rate units if computational function matters more than individual
spikes.** Both are much cheaper than the current Izhikevich implementation in
small circuit benchmarks. A small adaptation current was inexpensive, but did
not improve the selected circuit's switching and reduced its retention margin.

The experiments do not establish a model that automatically gives perfect
memory, arbitrarily sensitive decisions, and noise rejection. A persistent
state resists change, including changes in which external input is stronger.
All three selected circuits switched at a 3:1 input contrast but could retain
the previous winner at a 2:1 contrast. For small changes to reliably choose a
new winner, use weaker recurrence for decision units and separate memory units,
or supply an explicit reset/update gate. This is a proposed architecture, not
an experimentally validated additional circuit in this report.

## Models and what actually changes

### LIF with a persistent synaptic current

The existing implementation supplies one-update current pulses. Incoming pulses
can be discarded while voltage is held at reset during the refractory period.
The alternative stores a synaptic current `s` that continues to decay during
refractoriness. Excitation and inhibition therefore have effects that last
beyond their arrival step. This addresses timing sensitivity directly.

The research model uses dimensionless voltage with threshold 1 and reset 0:

```text
tau_m dv/dt = -v + external_drive + s - adaptation
tau_s ds/dt = -s
on a delayed incoming spike: s += signed_weight
on an outgoing spike: v = 0; refractory = 10 ms
tau_m = 50 ms; tau_s = 100 ms
```

Excitation and inhibition share a time constant here, allowing one signed
current accumulator per neuron. Different time constants would require separate
accumulators. This is the exponential-current LIF construction, implemented
independently for this experiment. NEST documents the same type of synaptic
current and exact subthreshold integration; the deliberately slower time
constants here are a computational design choice, not a fit to cortical cells.
[NEST model documentation](https://nest-simulator.readthedocs.io/en/stable/models/iaf_psc_exp.html)

All decay coefficients are precomputed. For constant external drive over a step:

```text
em = exp(-dt/tau_m); es = exp(-dt/tau_s)
v_next = em*v + (1-em)*drive + tau_s/(tau_s-tau_m)*(es-em)*s
s_next = es*s
```

The optional adaptation current uses the analogous exact linear contribution.
There are no exponentials evaluated inside the per-neuron update. Threshold
crossings, resets, and transmission remain on the timestep grid; exact
subthreshold integration does not make spike times exact.

### Adaptive LIF

Add `tau_a da/dt = -a`, with `tau_a = 200 ms`, and increment `a` by 0.05 after
each spike. This adds negative feedback without the quadratic spike dynamics of
Izhikevich or the voltage exponential of AdEx. Linear adaptation variables are
a standard way to extend integrate-and-fire models.
[Neuronal Dynamics, adaptation equations](https://neuronaldynamics.epfl.ch/online/Ch6.S1.html)

Only this small adaptation setting was tested. It is not evidence that all
adaptive LIF models behave alike. Stronger adaptation may help release a winner,
but also threatens persistent memory.

### Bounded continuous rate unit

This replaces the individual spiking neuron with a continuous activity variable
`r` between 0 and 1, representing a population or computational state:

```text
x = max(0, external_drive + self_weight*r_self_delayed
           - cross_weight*r_other_delayed - 1)
target = x/(1+x)
r_next = decay*r + (1-decay)*target
decay = exp(-dt/100 ms), precomputed
```

It has no spike phase or refractory blind spot, and the stable active branch
changes smoothly with input. This particular rational transfer function is an
engineering choice. Recurrent population-rate models are a standard abstraction
for decision dynamics; this is not a claim that one biological neuron computes
this equation. [Neuronal Dynamics, decision circuits](https://neuronaldynamics.epfl.ch/online/Ch16.S3.html)

A phase accumulator could convert `r` to an output spike train, but that was not
tested. Feeding those discrete spikes back would change the system and invalidate
the stability argument below; the proved recurrence uses continuous activity.

## Reproducible experiment

`tools/cheap_neuron_experiment.cpp` contains all five kernels, the delayed pair,
input generation, sweeps, validation, and benchmark. Original LIF and regular
spiking Izhikevich serve as controls. Their isolated DC voltage trajectories and
spikes are checked against the production `Brain` implementation. This experiment
uses its own kernels; the subsequent production integration is documented in
[filtered-lif.md](filtered-lif.md).

The initial and focused searches contain **9,816 circuit trials**, including
five training noise seeds per configuration. This is a finite, selected search,
not a uniform measure of each model's useful parameter space. Delays for self
and cross connections are equal within each pair in this experiment, unlike
some circuits in the earlier Izhikevich report. Numeric weights are not portable
between models. LIF control drive maps to current `10*drive`; Izhikevich control
drive maps to `4*drive`, approximate DC-onset reference scales.

Selected configurations:

| Model | dt | Self weight | Equal cross-inhibition magnitude | Both delays |
| --- | ---: | ---: | ---: | ---: |
| Filtered LIF | 5 ms | 1 | 0.45 | 60 ms |
| Adaptive LIF | 5 ms | 1 | 0.45 | 20 ms |
| Rate unit | 20 ms | 5 | 2.5 | 20 ms |

Persistence: a 100 ms drive of 6 initiates an isolated neuron; then external
drive is removed. Activity is measured at 6–8 seconds. This can initiate a burst;
we do not claim that one seed spike is sufficient. With zero drive from the
start, all three models remain quiet. At their nominal dt, the two spiking
models retained about **29 Hz**; the rate unit retained **r = 0.723607**.

Competition: two neurons with identical parameters and symmetric cross weights
receive drive 3.6 vs 1.2 for four seconds, then 1.2 vs 3.6 for four seconds.
Both receive a finite 100 ms initialization kick. The pass criterion requires
the externally favored unit to have positive activity and at least 3:1 output
dominance in **both** the 2–4 second and 6–8 second measurement windows. It does
not require complete suppression throughout a transition or every noisy instant.

Noise: independent Poisson event counts on a 1 ms source grid, with 100 ms
exponential filtering, event rates 20/40 Hz and amplitudes chosen to preserve
the selected mean drive. Every model sees averages of the same pre-generated
waveform over its timestep. The 100 evaluation seeds (1001–1100) are disjoint
from the five search seeds. This is a different input protocol from the earlier
Izhikevich pulse experiment; their pass fractions should not be compared directly.

| Selected circuit | New noise seeds | Weights ±10%, 3 initial offsets | Timesteps × 3 offsets |
| --- | ---: | ---: | ---: |
| Filtered LIF | 100/100 | 27/27 | 12/12 |
| Adaptive LIF | 100/100 | 27/27 | 12/12 |
| Rate unit | 100/100 | 24/27 | 12/12 |

The weight tests independently scale the self weight and cross weight across
the pair, preserving symmetry between its neurons. They do not test unequal
intrinsic neuron parameters or asymmetric weight mutations. Initial offsets
are 0, 20, and 40 ms. Numerical checks use 10, 5, 2, 1 ms for spiking alternatives
and 20, 10, 5, 1 ms for rate units.

### Inhibitory timing and what can be proved

Each selected isolated cell survived a 20 ms inhibitory current pulse with
amplitude 0.1, 0.25, or 0.5, at every representable onset in a 200 ms window
starting two seconds after initiation. Across the four tested timesteps, that
is **1,080/1,080** pulse tests for each spiking model and **810/810** for the rate
unit. These are bounded numerical tests, not a proof for all timings or inputs.

With inhibition held continuously from 2–8 seconds, filtered LIF survived
amplitudes 0.1 and 0.25 but lost activity at 0.5. Adaptive LIF survived 0.1 but
lost activity at 0.25 and 0.5. Rate units survived all three. These magnitudes
are dimensionless external current, not synaptic weights or voltage kicks.

The rate unit also has a simple timing-independent guarantee. In isolation,
with self weight 5 and zero external drive, stable active equilibrium is

```text
r_active = (1 + sqrt(0.2))/2 = 0.723607
```

Suppose the present state and the complete delayed history are at least 0.6.
For any time-varying net inhibitory input `0 <= h(t) <= 0.5`:

```text
x >= 5*0.6 - 1 - 0.5 = 1.5
target >= 1.5/(1+1.5) = 0.6
```

The update is a convex combination of values at least 0.6, so by induction
activity cannot fall below 0.6, regardless of inhibitory timing or duration.
The same boundary argument applies to the continuous-time rate equation.
This guarantee is conditional on the established active history and on bounded
aggregate inhibition; it is not a guarantee against the stronger inhibition
needed to switch off a losing unit in the competing pair.

### Remaining problems

At the tested weak drive 1.2, all selected circuits passed reversal for strong
drive 3.6 and 4.2, but failed the two-window criterion at 1.8, 2.4, and 3.0.
Some failures involved both units firing; others retained the old winner.
Strong persistence creates hysteresis. Exact symmetry also supplies no reason
to choose a particular winner when both inputs and initial states are equal.

Coarse spiking timesteps still quantize rates. In the selected filtered LIF
cell, isolated rates at drives 0, 0.4, and 0.8 were 29, 33.5, and 40 Hz at 5 ms,
so it is not locked to one delay frequency. But extended plateaus remain.
During the strong-drive WTA trial, the winner fired at 50 Hz at 5 ms and 62.5 Hz
at 1 ms. Selection survives refinement; firing frequency is not fully converged
at 5 ms. At 10 ms even broad portions of the rate curve become nearly flat.
For a spiking implementation, start at 2 ms and validate the range the evolved
brains use. The rate unit avoids this integer-spike-period limitation and showed
matching steady activity across its four timesteps.

## Cost measurements

GCC Release on the local Ryzen 9 7900X. Each timing covers 1,024 independent
two-cell circuits for 20 simulated seconds. Construction, input generation,
disk output, and plotting are excluded. One warm-up and nine measurements per
variant, rotating execution order. Quiet and active workloads are separate;
quiet runs have identical zero activity. Active runs use the same heterogeneous
DC drive pattern, with model-dependent spike counts. The routing implementation
is shared, while feedback weights are scaled for the rate model. The benchmark
uses representative parameters, not all the selected validation parameters.

| Kernel | dt | Median active time | Speedup over Izhikevich | Cost / existing LIF |
| --- | ---: | ---: | ---: | ---: |
| Existing LIF | 20 ms | 5.63 ms | 44.2× | 1.0× |
| Izhikevich RS | 1 ms | 248.55 ms | 1.0× | 44.2× |
| Filtered LIF | 5 ms | 24.53 ms | 10.1× | 4.4× |
| Filtered LIF | 2 ms | 59.94 ms | 4.1× | 10.6× |
| Adaptive LIF | 5 ms | 25.00 ms | 9.9× | 4.4× |
| Adaptive LIF | 2 ms | 61.38 ms | 4.0× | 10.9× |
| Rate unit | 20 ms | 6.25 ms | 39.8× | 1.1× |

Per cell-update in the active workload, Izhikevich is about 6.1 ns, filtered
LIF about 3.0 ns, and rate units about 3.0 ns. Much of the per-simulated-second
gain comes from taking fewer steps, not merely cheaper equations. Quiet-workload
speedups were about 10.8×, 4.3×, and 44.5× for filtered LIF at 5 ms, filtered LIF
at 2 ms, and rate units respectively.

**These are minimal circuit benchmarks, not production Brain or ecosystem
speedups.** Graph traversal, sensory encoding, motor decoding, noise generation,
memory layout, and world simulation may dominate the actual application.
The earlier Izhikevich report includes production benchmarks; this report does
not justify predicting a 10× or 40× faster ecosystem.

## Parameter evolution and alternatives not benchmarked

For a first integration, keep the model family and time constants fixed and
mutate connection weights, wiring, and bounded bias. Different weights already
select persistent-memory and readily-switching operating regimes. Later allow
small multiplicative changes in positive time constants; recompute cached
coefficients only when parameters mutate. Mutating synaptic decay at fixed
peak weight also changes total delivered charge (`weight * tau_s`), so decide
whether evolution should preserve peak strength or integrated strength.
Treat these as proposals; no new mutation behavior was enabled here.

Do not independently randomize many intrinsic parameters just to obtain varied
cells. Optional adaptation should start near zero and be tested against memory
retention. Rate self weights near the isolated persistence boundary (4 in these
units) are especially fragile; symmetric pair-weight perturbations already
caused three failures in the selected rate circuit.

Other plausible methods include a hysteretic activation threshold (cheap
explicit memory), event-driven exact LIF integration (potentially useful for
sparse activity, with queue overhead), and piecewise-linear spiking models.
Those were not benchmarked here. QIF or AdEx alone do not address the discarded
inhibitory-pulse problem; changing synaptic dynamics remains a separate choice.

## Run and inspect

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target neuroevo_cheap_neuron_experiment -j 4
.\build\neuroevo_cheap_neuron_experiment.exe sweep
.\build\neuroevo_cheap_neuron_experiment.exe focused
.\build\neuroevo_cheap_neuron_experiment.exe validate
.\build\neuroevo_cheap_neuron_experiment.exe profile
.\build\neuroevo_cheap_neuron_experiment.exe verify
python tools/plot_cheap_neuron_experiment.py
ctest --test-dir build -C Release --output-on-failure
```

CSV data, machine-readable `analysis.json`, and PNG/SVG plots are written to
`runs/cheap-neurons/` (gitignored, reproducible). The added CTest verifies baseline
trajectories against production, the analytic LIF DC rate, filtered integration
against an independent fine-step Euler reference, the analytic rate fixed point,
and the selected persistence/reversal examples. The full suite passed 15/15.
