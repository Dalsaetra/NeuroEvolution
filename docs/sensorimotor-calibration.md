# Sensory and motor interface

Nursery brains use three vision sectors, 91 local inputs, and six motor outputs. The non-predation extended interface has 66 inputs and five outputs; the reduced control interface has 60 inputs. Sensor labels and category groups are defined in `src/ecosystem_sensing.cpp`.

Inputs describe visible obstacles, food and creatures, hearing, contact, bodily state, weather, depleted food, and shelter. Predation appends meat sensing, observed body mass/health, and own health/damage. Typed food proximity distinguishes distance to individual food types. Walls occlude vision, and fields of view limit visible signals. Controllers do not receive global resource locations or population state.

Calibrated sensory neurons encode intensity as spike rate, with `sensory_rate_hz` as the reference. Their inherited thresholds adjust sensitivity. Motor spikes accumulate a rate trace controlled by `motor_rate_tau` and normalized by `motor_reference_hz`; `actuator_tau` smooths actions. Brain updates use `brain.dt`, and world `dt` must be an integer multiple of it.

Synapse-addition mutations sample available sensory categories equally. Selecting a category adds every missing member-to-destination connection in one structural mutation, using one shared initial weight. Existing connections and their weights are preserved; transmission delays are calculated separately from each source's position. This also applies when repairing a disconnected hidden neuron. The current brain has no synapse-count cap.

Synapse removal first selects an existing edge. For a sensory edge, a 50/50 choice removes either that edge alone or every edge from its sensory category to the same destination. Hidden-origin edges retain individual removal. Weight mutation and rewiring remain individual, allowing directional specialization after a category is discovered. These rules apply to subsequent mutations in both new and resumed runs; saved connections are not modified on load.

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

The three appended vision_N_creature_carnivory_total inputs sum all other visible creatures' carnivory within each sector. Walls, vision range, and field of view apply; distance within range adds no further falloff. For total S, the input is S / (1 + S): two 50% carnivores equal one 100% carnivore (0.5), while two 100% carnivores give 2/3. This bounded encoding keeps increasing beyond a total of 100%. The three directions form one mutation category and begin disconnected in the sparse ancestor. Version 41 writes this layout; supported older checkpoints retain their original 86 or 88 inputs. Importing older genomes into a new run appends the missing inputs.


## Evolved eyes

Each eye covers 150 degrees by default (the existing fov_degrees setting). The inherited eye_separation_degrees gene moves their axes symmetrically to minus/plus half the separation relative to heading. At the default separation of zero, both eyes face forward and preserve the previous 150-degree view. At separation 150, the axes are -75 and +75 degrees: their continuous combined view is 300 degrees, with a 60-degree blind spot behind.

All visual channels divide this combined view into three equal angular sectors. Separation 0, 75, and 150 therefore produce sector widths of 50, 75, and 100 degrees respectively. Wider coverage trades angular precision for coverage without adding input neurons. Range, wall occlusion, hearing, contact, and the forward attack/feeding arcs keep their own rules. Overlapping eyes do not double-count creatures or resources.

The ancestor template starts at founder_eye_separation=0. Initial founder diversification, if enabled, can mutate eyes along with the other body genes. On non-copy inheritance branches, eye_mutation_probability (default 0.2) and Gaussian eye_mutation_sigma (default 15 degrees) use the existing slight/strong body-profile scales. Separation is clamped to [0,150]; a custom per-eye FOV smaller than 150 also caps separation at that width so the combined view has no internal gap. Total coverage is capped at 360 degrees.

CLI controls: --founder-eye-separation, --eye-mutation-probability, --eye-mutation-sigma. Replays show each creature's separation and combined FOV, and draw the individual vision cone. Version 42 checkpoints preserve living and pending offspring eye genes and mutation settings. Older checkpoints retain zero separation and disabled eye mutation for unchanged continuation; genome imports into new runs preserve eye genes and use the new run's mutation settings.
