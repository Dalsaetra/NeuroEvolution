# Sensory and motor interface

Nursery brains use three vision sectors, 83 local inputs, and six motor outputs. The non-predation extended interface has 66 inputs and five outputs; the reduced control interface has 60 inputs. Sensor labels and category groups are defined in `src/ecosystem_sensing.cpp`.

Inputs describe visible obstacles, food and creatures, hearing, contact, bodily state, weather, depleted food, and shelter. Predation appends meat sensing, observed body mass/health, and own health/damage. Typed food proximity distinguishes distance to individual food types. Walls occlude vision, and fields of view limit visible signals. Controllers do not receive global resource locations or population state.

Calibrated sensory neurons encode intensity as spike rate, with `sensory_rate_hz` as the reference. Their inherited thresholds adjust sensitivity. Motor spikes accumulate a rate trace controlled by `motor_rate_tau` and normalized by `motor_reference_hz`; `actuator_tau` smooths actions. Brain updates use `brain.dt`, and world `dt` must be an integer multiple of it.

The six outputs drive forward movement, left/right turning, forage, call, and attack. Effort consumes the corresponding energy budget. The sparse ancestor's attack neuron is initially disconnected; its five hidden neurons implement the initial locomotion and forage circuit.

Tune neural gains, timing, and rates in `BrainConfig`, and bodily senses/actions in `EcosystemConfig`, both in [config.hpp](../include/neuroevo/config.hpp). Replay detail is controlled separately by `RunConfig`. Calibration tests cover rate coding, motor impulse responses, shelter visibility, and nutritional feedback.
