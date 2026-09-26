# Sensory and motor interface

Nursery brains use three vision sectors, 88 local inputs, and six motor outputs. The non-predation extended interface has 66 inputs and five outputs; the reduced control interface has 60 inputs. Sensor labels and category groups are defined in `src/ecosystem_sensing.cpp`.

Inputs describe visible obstacles, food and creatures, hearing, contact, bodily state, weather, depleted food, and shelter. Predation appends meat sensing, observed body mass/health, and own health/damage. Typed food proximity distinguishes distance to individual food types. Walls occlude vision, and fields of view limit visible signals. Controllers do not receive global resource locations or population state.

Calibrated sensory neurons encode intensity as spike rate, with `sensory_rate_hz` as the reference. Their inherited thresholds adjust sensitivity. Motor spikes accumulate a rate trace controlled by `motor_rate_tau` and normalized by `motor_reference_hz`; `actuator_tau` smooths actions. Brain updates use `brain.dt`, and world `dt` must be an integer multiple of it.

Contact has four binary channels (front, left, back, right), recomputed from current
wall/boundary and creature proximity on every observation. They clear when contact
ends. A calibrated input neuron's recorded `potential` is instead a fractional
spike-encoding phase: nonzero input advances it, firing subtracts one, and zero input
leaves the remaining fraction unchanged without firing. This retained phase is
intentional and preserves sensitivity to weak or intermittent inputs; clearing it
would change simulation behavior. Recording precision can round a phase just below
one to `1`, making the flat trace look continuously activated.

The detailed viewer plots the actual sampled sensor value in blue alongside the
green input-phase trace and labels zero sensor values as off. Purple markers show
only spikes present at the recorded neural substep, not every spike between frames.
Observations describe the recorded world positions; brain state comes from the
controller update before that step's movement, so instantaneous transitions can
differ by a world step.

The six outputs drive forward movement, left/right turning, forage, call, and attack. Effort consumes the corresponding energy budget. The sparse ancestor's attack neuron is initially disconnected; its five hidden neurons implement the initial locomotion and forage circuit.

Tune neural gains, timing, and rates in `BrainConfig`, and bodily senses/actions in `EcosystemConfig`, both in [config.hpp](../include/neuroevo/config.hpp). Replay detail is controlled separately by `RunConfig`. Calibration tests cover rate coding, motor impulse responses, shelter visibility, and nutritional feedback.
