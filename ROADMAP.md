# Overarching direction and goal

Create a simulator that uses evolutionary algorithms and concepts to evolve creatures with brains in an enviroment.w

## Subgoals

1. Create an environment that can be viewed graphically, showing how a generation performed.
2. Deploy challenges in the enviroment with the goal of evolving brains with intelligent behaviour such as decision making and memory.
3. Optimizing code such that simulation and evolution can happen as fast as possible.
4. Plenty of graphical summaries and displays, including showing the enviroment, population statistics and brain topologies

## Core development methods and values

- Optimized code written mostly in Python and C++, runnable on a Windows machine
- Focus on allowing for emergent behaviour, not hand-holding the evolutionary process
- Starting from simple enviroments, brains, challenges and algorithms, gradually increasing complexity as we see the possibility 
- Keeping tidy code and folder strucutres, such that it is easy to run and to do manual edits
- Use uv for the enviroment, find suitable packages for development and use git as version control

# Methods

The main methods in this projects relate to simulation the environment, 

## Current experimental foundation

- Sensorimotor, task, and external-fitness regimes are independently selectable.
- Available tasks include continuously visible food seeking and cue-occlusion foraging with variable hidden delays.
- Autonomous activity can be initiated by an episode-start pulse and maintained or restarted by low-rate Poisson background current with per-neuron evolvable sensitivity.
- Hidden-neuron bias changes excitability but is constrained below the isolated self-spiking threshold.
- Scalar and NEAT mutation can introduce a simple reciprocal hidden-neuron motif as one structural mutation.

## The "Brains"

- Use varying degrees of biological realism to simulate brains
- All brains should have some minimal characteristics
    - Neurons connected through synapse in a defined topology/graph/connectome
    - Neurons should be spacially embedded such that synapse travel time distance matters
    - Should be simulated as a spiking neural network
- Levels of brain modeling
    - Simplest: Leaky integrate-and-fire neurons with current-based synapses
    - More complex neuron types: Excitatory and inhibitory neurons
    - More complex neuron model: Izhikevich model of neurons
    - More complex synapse model: Conductance based synapses, where the synapse current depends on the post-synaptic neuron potential
        - Even more complex: Different synapse receptors model different timescales
- Levels of network / topology modeling
    - Simplest: static randomly / heavy-tailed initialized weights
    - Plasticity rules
        - Simple activity based rules
        - More complex "permanent" weight updates such as STDP or Clopath style plasticity

## Evolution simulation

The evolutionary fitness has two components: the impact of the external environment, versus the impact of internal factors, such as energy expenditure.

### External factors - enviromental simulation

- Creatures need to be able to respond to external stimuli through sensory input and respond accordingly
- The enviroment should give challenges that gradually increase the requirement for intelligence (higher rewards the more intelligent behaviour)
    - Such as remembering a temporal pattern or doing a computation
- Decision: 2D or 3D simulation
    - Start with 2D but have a backbone that can be converted to 3D
- Decision: 

### Internal factors

- Several internal regularization methods should be implemented in order to keep the population "healthy" and not drifting to bad solutions
    - Energy penalty for increasing amount of neurons or increasing amount of synapses
        - This penalty should gradually be decreased as more intelligent behaviour requires bigger and denser brains
    - Energy penalty for too many neuron spikes
    - Energy penalty for spending too much time on a task
    - Penalty for having unused neurons
        - Be careful of this not limiting the development of new brain topologies
    - Reward for trying challenges
