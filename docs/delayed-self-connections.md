# Delayed self-connections: feasibility experiment

Date: 2026-09-10. These results use the actual C++ `Brain::step` implementation.
This experiment adds explicit self-delay support to constructed circuits. Random
founders and evolutionary mutation still do not create self-connections.

## Decision

Delayed self-excitation is a useful candidate for compact persistent state and
for assisting externally driven firing. It does **not**, by itself, provide a
reliable input-sensitive winner-take-all primitive in the current impulse-based
neuron model. The requested properties separate as follows:

| Requirement | Result |
| --- | --- |
| Start with a pulse, continue without input | Yes, with sufficient self-weight and delay beyond refractory |
| Survive small inhibition at any arrival step | Yes, under an explicit aggregate-amplitude bound; analytical proof below |
| Support more than one persistent firing rate | Yes, demonstrated at 6.25 and 12.5 Hz with the same 160 ms loop |
| Increase firing rate reliably as external excitation increases | No general guarantee; broad plateaus and non-monotonic responses occur |
| Equal reciprocal inhibition reliably favors stronger excitation | No; ties, both-silent outcomes, and weaker-input winners occur |

Biological self-synapses are called autapses. Published models also find that
their effects depend strongly on delay and neuronal dynamics. That supports
testing this mechanism, not transferring a biological guarantee to this LIF
implementation. [Regulation of Irregular Neuronal Firing by Autaptic Transmission, 2016](https://www.nature.com/articles/srep26096)

## Runtime issue and minimal change

Previously, `from_components` replaced every supplied synaptic delay with one
computed from Euclidean distance. A self-edge has zero distance, giving the
minimum one-step delay. The current integrator consumes and discards incoming
current during refractory. Consequently, merely allowing self-edges would not
produce the intended feedback at the default settings.

`from_components` now preserves explicitly supplied self-edge delays, validating
them against `1..max_delay_steps`. It still derives other delays from geometry.
Neuron-position mutations preserve self delays. Neither the membrane equation,
synaptic impulse shape, default configuration, nor evolutionary connection
operators changed. Existing checkpoint fields already store delays; round-trip
tests preserve pending self-feedback and exact subsequent activity.

## Analytical retention and inhibition bounds

Assumptions: default hidden neuron, zero bias and reset potential, threshold 1,
background noise disabled, one established circulating spike, no continuing
external excitation, and only bounded external inhibition during retention.

The non-refractory update is

```text
V_next = a * V + q * (self_spike_weight - total_inhibitory_weight)
a = 1 - dt / membrane_tau = 0.8
q = dt * synaptic_gain = 0.64
```

After firing, two subsequent updates are refractory. Thus feedback delay `d`
must be at least 3 steps (60 ms). For a single seed to regenerate itself:

```text
q * w_self >= 1
w_self >= 1.5625
period = d * 20 ms
```

At equality the inhibition margin is zero. With `w_self = 2`, a returned spike
delivers a potential increment of 1.28, leaving 0.28 above threshold.

For **one isolated inhibitory arrival**, the worst timing is the same step as
the returned self-spike. The maximum aggregate inhibitory weight is therefore
`2 - 1.5625 = 0.4375`. Earlier eligible arrivals decay; arrivals during refractory
are discarded. A weight-0.6 inhibitory pulse at feedback arrival extinguishes
the loop, whereas the same pulse one step later is discarded during refractory.

For **arbitrary ongoing inhibition**, bound the aggregate arriving inhibitory
weight on every step by `h`. There are `m = d - 2` eligible updates between reset
and feedback return. The worst-case potential at the return is

```text
V_return >= q * (w_self - h * sum(a^j, j=0..m-1))
h <= (w_self - 1.5625) / sum(a^j, j=0..d-3)
```

For delay 5 (100 ms) and self-weight 2, the sum is 2.44 and the bound is
`h <= 0.179303...` on every step. A practical choice such as 0.1 leaves margin.
For delay 8, the corresponding bound is approximately 0.1186.

This is a timing-independent sufficient condition within the discrete simulator:
continuous maximum inhibition bounds every weaker arrival pattern. No early
spike occurs in this retention-only scenario, and the guaranteed returned spike
resets the cycle, so the argument repeats by induction indefinitely. The unit
test additionally exhausts all 504 binary inhibitory patterns of periods 3–8,
repeating each pattern with inhibition weight 0.1 and self-weight 2.

The bound concerns **total** inhibitory weight arriving in a step, not each
synapse separately. It does not cover arbitrary amplitude, simultaneous added
excitation that changes the spike schedule, all possible initial states, or
timing below the 20 ms resolution. Equality boundaries should not be used as
robust operating points.

## Rate response and multiple retained rates

One circulating spike gives approximately `1 / delay` Hz. Increasing self-weight
above threshold does not increase that rate: excess voltage is discarded at
spike reset. The shortest sustainable loop is already at the default maximum
firing rate, about 16.67 Hz, leaving no room to encode stronger input by rate.

Measured examples use self-weight 2 and external excitatory weight 1:

| Self delay | External input | During input | After withdrawal |
| --- | --- | --- | --- |
| 60 ms | 0–50 Hz | Approximately 16.67 Hz throughout | Approximately 16.67 Hz |
| 100 ms | 0–28 Hz in this phase condition | 10 Hz | 10 Hz |
| 100 ms | 50 Hz | 12.5 Hz | 10 Hz |
| 160 ms | 0 Hz | 6.25 Hz | 6.25 Hz |
| 160 ms | 20 Hz | 12.5 Hz | 12.5 Hz |

The longer loop can accommodate two circulating spikes separated by sufficient
recovery time. This proves more than one retained rate is possible, but gives
discrete, history-dependent states rather than a smooth measure of current
evidence. It also creates a reset issue: erasing one circulating spike need not
erase all of them.

Subthreshold self-feedback can instead facilitate input-driven activity. With a
60 ms delay, self-weight 1.2, external weight 1, and a 10 Hz external train, the
seeded neuron maintains 10 Hz. Removing the external train makes it stop. With
zero self-weight, the same 10 Hz train produces no steady firing. Thus the
proposal's weaker-input-to-maintain-activity variant also works in a tested
condition.

## Equal-weight competition

Each pair has identical intrinsic parameters and self-weights, equal reciprocal
inhibitory weights and delays, and equal external excitatory weights (0.5).
The neurons receive different external spike rates. Startup offsets and input
phases vary across the sweep.

A concrete timing counterexample:

| Configuration | Stronger-input neuron | Weaker-input neuron |
| --- | --- | --- |
| Both self-delays 100 ms, self-weight 2 | External input 15 Hz | External input 5 Hz |
| Reciprocal inhibition weight 0.75, delay 20 ms, simultaneous seeds | Output 10 Hz | Output 10 Hz |
| Same circuit, inhibitory delay changed to 60 ms | Output 10 Hz | Output 0 Hz |

In the first case, both neurons fire together and receive reciprocal inhibition
while refractory. Raising inhibitory weight cannot fix an impulse that is
discarded. Choosing a different delay can resolve this particular example but
does not establish phase-independent selection.

The sweep contains 34,560 pair cases, including 6,912 no-inhibition controls.
Among the 27,648 cases with nonzero reciprocal inhibition:

- 9,691 (35.1%): stronger-input neuron fires faster.
- 13,968 (50.5%): rates tie within 0.1 Hz, including both-silent cases.
- 3,989 (14.4%): weaker-input neuron fires faster.

These are fractions of a deliberately chosen parameter grid, **not** estimated
probabilities of evolutionary success. Faster firing is a weaker criterion than
strict winner-take-all; the first category does not imply complete suppression
of the other neuron. The grid is sufficient to disprove general reliable
selection, not to rule out useful parameter regions or evolved larger circuits.

## Protocol and reproducibility

`tools/autapse_fixture.hpp` constructs two hidden neurons with six event inputs
(seed, excitation, inhibition for each) and an unused output. Sensory thresholds
are calibrated so a scheduled binary event produces one input spike. All input
edges have a one-step delay. Reciprocal inhibitory delays are derived from
geometry. Hidden timing and gains remain the ecosystem defaults.

Noise is disabled to isolate causality. This is an open-loop circuit study,
not an ecological run or a test of learning.

- Retention: 112 cases, delays 1–8, seven self-weights, seeded and unseeded;
  40 s runs, rates measured over the final 20 s.
- Inhibition: 2,772 cases; delays 3–8, self-weights 1.6/1.8/2/2.5, seven
  inhibitory amplitudes, every phase, and isolated, periodic, or continuous
  arrivals. Perturbations start after 100 established cycles; 60 s runs,
  rates measured over the final 30 s.
- Rate response: 4,680 cases; delays 3/5/8, self-weights 0/1.2/1.6/2/2.5,
  external weights 0.25/0.5/1, rates 0–50 Hz in 2 Hz increments, four phases.
  Input lasts 40 s, then is absent for 40 s. Measure seconds 20–40 and 60–80.
- Competition: delays 3/5/8, self-weights 1.6/2/2.5, reciprocal weights
  0/0.25/0.75/1.5/3, reciprocal delays 1/3/5/8; weaker rates 0/5/10 Hz,
  stronger rates 2/5/10 Hz above those, every weaker-neuron seed offset from
  0 to `d-1`, four weaker-input phases. Stronger input starts at phase zero.
  Runs last 40 s and rates use the final 20 s.

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\neuroevo_autapse_experiment.exe runs/autapse
python tools/plot_autapse_experiment.py runs/autapse
```

The plot script requires matplotlib; the C++ experiments do not. Multi-config
generators may put the executable under `build/Release`.

All 13 CTest tests passed, including retention, all 504 bounded-inhibition
patterns, erasure timing, multiple persistent rates, the competition
counterexample, invalid-delay handling, and exact checkpoint continuation.

Generated data: `runs/autapse/{sustain,inhibition,rates,competition,traces}.csv`.
Figures: `runs/autapse/autapse-results.png` and `.svg`. The runs directory is
git-ignored; the source harness and this report preserve reproducibility.

## Recommended next experiment

Keep delayed self-excitation as a candidate memory/facilitation motif. Before
introducing it as a general evolutionary selection primitive, compare the
current impulses against synaptic currents that decay over several updates.
Such a current can remain available after refractory, and sustained excitation
can influence more of the inter-spike interval. It is a plausible way to reduce
the observed blind spots, not a proven fix.

A per-neuron excitatory/inhibitory current trace could keep additional state
proportional to neuron count. Its decay, reset behavior, and integrated impulse
strength must be calibrated explicitly; adding tails without adjusting gain
changes the total delivered excitation. Then rerun the same timing and rate
experiments, adding input reversals, inhibitory bursts, noise, and parameter
perturbations before ecological comparisons.

Introducing self-edges into evolution also requires deliberate choices for
delay inheritance/mutation, growth and pruning, and how persistent activity is
cleared. Those changes are outside this feasibility experiment.
