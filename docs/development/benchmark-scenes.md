# Benchmark scenes and system test suites

Status: continuation roadmap, October 7, 2026. The scene catalog below identifies
candidate tests; entries become validated only after a reproducible Ludus run
and recorded evidence.

Continue testing Ludus with recognizable reference scenes and controlled system
setups. Use each scene to check observable correctness as well as performance.
Cornell box and Sponza anchor the rendering work; stacks and tumblers, audio
fixtures, navigation maps and simulation cases extend coverage across the engine.
Keep small diagnostic cases alongside larger integrated levels.

The [engineering quality architecture](../architecture/engineering-quality.md#scenario-coverage-and-reproducibility)
owns scenario coverage, failure semantics and reproducibility policy. This guide
owns the scene catalog, priorities and continuation workflow. Existing
[CI behavior](continuous-integration.md) remains the authority for enabled gates.

## Next session

Start with a Cornell box capability review against the current
[public rendering slice](fullscreen-rendering.md). Choose a bounded representation,
define the camera, light, geometry, material assumptions and expected observations,
then implement and run that case on an available declared backend. Record the
representation: an application-owned analytic fullscreen shader tests a different
path from imported meshes. General mesh, texture and depth support require
separate feature work when absent; a familiar image does not prove those paths.

Before choosing an implementation, inspect current code, tests and capability
evidence. Use a task-specific `codex/` branch and separate worktree from freshly
fetched `origin/main`; inspect the primary checkout read-only and preserve its
existing changes. Reuse existing examples and
test infrastructure where appropriate, and keep scene-specific policies in the
application. Follow AGENTS.md and the owning module's contracts for every change.

After the first case, continue through the initial priorities below. If a case
requires an unavailable subsystem, record the concrete prerequisite and select
another admissible case. Keep the deferred scene in the catalog. Each follow-up
should add or improve a runnable case and its evidence rather than repeatedly
rebuilding the catalog.

## Initial priorities

| Priority | Suite and first cases | Admission and purpose |
| --- | --- | --- |
| 1 | Rendering: Cornell box, then Sponza | Establish a controlled lighting case first; admit Sponza when the chosen geometry/material path supports it. |
| 2 | Physics: box stack and tumbler | Require a contact solver and rigid-body lifecycle; measure resting stability and sustained contact churn separately. |
| 3 | Character movement: stairs, slopes and moving platforms | Require the chosen controller and collision queries; define step/slope/platform behavior explicitly. |
| 4 | Audio: overlapping effects, looping and streamed music | Extend the existing audio-content sample; check lifetime, loop seams, streaming progress and callback deadlines before advanced room acoustics. |
| 5 | World lifecycle: bounded spawn/despawn arena | Extend the world reference with entity generation, stale-reference and allocation checks. |
| 6 | Streaming: scripted traversal, reversal and teleportation | Start with supported filesystem reads; distinguish file acquisition from future renderer resource residency. |

Relevant starting points are the [world example](../examples/world-demo.md),
[world validation](game-world-validation.md),
[audio example](../examples/audio-content.md),
[audio evidence](audio-content-evidence.md),
[filesystem measurements](filesystem-benchmark.md), and
[Drift physics design](../architecture/physics-fluid-simulation.md).
Drift's analytic drag/ripple model does not establish a general rigid-body or
fluid solver. Navigation, crowds, animation and specialized simulation enter
the runnable suite as their required capabilities become available.

## Reference scene catalog

The linked resources include asset collections, sample applications, research
scenarios and formal benchmarks. They are references for constructing Ludus
cases, with different assumptions and acceptance criteria. The systems and
measurements below are proposed Ludus coverage, not claims that an external
collection supplies every check.

| Suite | Representative cases | Systems and observations |
| --- | --- | --- |
| Lighting and materials | Cornell box, material spheres, glass interiors | Direct/indirect lighting, shadows, materials, reflections, transparency and output encoding. |
| Rendering scale | Sponza, San Miguel, dense instanced geometry | Visibility, culling, batching, geometry throughput, texture residency and CPU/GPU frame time. |
| Asset import | Khronos assets isolating format features | Attributes, indices, transforms, cameras, materials, textures and supported extensions. |
| Animation | Fox, Animated Morph Cube, repeated animated characters | Skinning, morph weights, interpolation and looping; add explicit blending and event fixtures. |
| Rigid bodies | Stacks, pyramids, tumblers, convex piles, objects on triangle meshes | Broad/narrow phase, friction, restitution, sleeping, islands, penetration and jitter. |
| Continuous collision | Fast projectile against a thin wall | Tunneling across speed, angle, shape and timestep boundaries. |
| Constraints | Pendulums, chains, bridges, ragdolls and motors | Joint limits, constraint error, convergence and mass-ratio sensitivity. |
| Character movement | Stairs, ramps, ledges, ceilings, elevators and rotating platforms | Sweeps, ground detection, wall sliding, traversal boundaries and platform-relative movement. |
| Vehicles | Suspension course, slopes, uneven terrain, braking and turning | Wheel contact, suspension, traction, steering and chassis interaction. |
| Cloth and deformables | Hanging cloth, cloth over a sphere, rods and deformable solids | Stretch/bend constraints, collision, self-intersection, volume error and stability. |
| Fluids | Dam break, emitters and fluid interacting with solids | Neighbor search, pressure solving, boundary handling, density/volume error and coupling. |
| Simulation accuracy | Taylor–Green vortex; free fall, spring and pendulum fixtures | Integration error, numerical dissipation, timestep sensitivity and convergence against an applicable reference. |
| Pathfinding | Moving AI game maps, mazes, rooms and random obstacles | Path validity/quality, expanded nodes, query cost and unreachable goals. |
| Navigation meshes | Multi-level layouts, narrow passages and tiled terrain | Walkability, agent clearance, path queries, tile updates and crowd integration. |
| Crowds | ORCA circle crossing and evacuation; doorway counterflow | Neighbor queries, local avoidance, congestion, oscillation and arrival time. |
| Audio playback | Sound gallery, overlapping impacts, looping and streamed music | Decoding, mixing, routing, voice policy, playback lifetime, underruns and loop seams. |
| Spatial audio | Orbiting mono source, approach/recede, pass-by and connected rooms | Coordinate conventions, attenuation, HRTF, Doppler, occlusion, reflections and reverb where supported. |
| Networked simulation | Fiedler's interacting cubes and coupled physical groups | Replication, interpolation, synchronization, correction error and bandwidth under injected network conditions. |
| UI interaction | Widget gallery, long lists and overlapping windows | Layout, clipping, focus, input routing, text editing and keyboard/controller navigation. |

Reference collections and attribution:

- [PBRT scene distribution](https://www.pbrt.org/scenes-v3) and
  [Benedikt Bitterli's Rendering Resources](https://benedikt-bitterli.me/resources/)
  provide rendering scenes. Preserve each asset's author and license notices.
- [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets)
  provide examples of asset and animation features, including Fox. The
  [Animated Morph Cube](https://github.com/KhronosGroup/glTF-Sample-Models/blob/main/2.0/AnimatedMorphCube/README.md)
  isolates animated morph weights.
- [Bullet benchmarks](https://github.com/bulletphysics/bullet3/blob/master/examples/ExampleBrowser/ExampleEntries.cpp),
  [Erin Catto's Box2D samples](https://box2d.org/documentation/samples.html) and
  [pyramid/tumbler measurements](https://box2d.org/posts/2023/10/simulation-islands/),
  and [Jorrit Rouwe's Jolt samples](https://github.com/jrouwe/JoltPhysics/tree/master/Samples/Tests)
  provide physics scenarios and diagnostic applications.
- [PositionBasedDynamics](https://github.com/InteractiveComputerGraphics/PositionBasedDynamics)
  and [SPlisHSPlasH](https://github.com/InteractiveComputerGraphics/SPlisHSPlasH),
  by Jan Bender and contributors, provide deformable/constraint and fluid examples.
  Use the [dam-break setup](https://github.com/InteractiveComputerGraphics/SPlisHSPlasH/blob/master/data/Scenes/DamBreakModel.json)
  as one candidate, preserving its numerical assumptions.
- The [Taylor–Green benchmark project](https://benchmark.coria-cfd.fr/index.php/Main_Page)
  distinguishes analytical verification in two dimensions from numerical
  reference comparisons in three dimensions. Select a case matching the solver.
- [Google DeepMind's MuJoCo Menagerie](https://github.com/google-deepmind/mujoco_menagerie)
  supplies articulated models for later robotics/control tests when relevant.
- [Nathan Sturtevant's Moving AI benchmarks](https://www.movingai.com/benchmarks/grids.html)
  supply maps and pathfinding problems; they are not complete playable levels.
  [Recast/Detour](https://github.com/recastnavigation/recastnavigation) provides
  navigation-mesh examples.
- [Optimal Reciprocal Collision Avoidance](https://gamma.cs.unc.edu/ORCA/), by
  Jur van den Berg, Stephen J. Guy, Jamie Snape, Ming C. Lin and Dinesh Manocha,
  provides crowd scenarios. The
  [Reciprocal n-body collision avoidance paper](https://gamma.cs.unc.edu/ORCA/publications/ORCA.pdf)
  includes circle crossing.
- [Audiokinetic's Wwise samples](https://www.audiokinetic.com/en/library/Launcher_2023.2.4.3909/?id=samples&source=InstallGuide)
  provide educational audio projects, including Cube, Wwise Adventure Game and
  Wwise Audio Lab. [OpenAL Soft's HRTF example](https://github.com/kcat/openal-soft/blob/master/examples/alhrtf.c)
  and [Valve's Steam Audio documentation](https://valvesoftware.github.io/steam-audio/doc/unity/index.html)
  inform spatial audio fixtures; their presence does not establish Ludus support.
- [Glenn Fiedler's Introduction to Networked Physics](https://www.gafferongames.com/post/introduction_to_networked_physics/)
  motivates interacting-cube scenarios, including tightly coupled motion.
- [Omar Cornut and contributors' Dear ImGui demo](https://github.com/ocornut/imgui/blob/master/imgui_demo.cpp)
  provides a reference for the widget-gallery approach.

These authors and organizations supply the reference assets and scenario ideas;
Ludus-specific adaptations should document meaningful departures near the code.

## Ludus integration suites

| Suite | Controlled setup | Systems and observations |
| --- | --- | --- |
| World lifecycle | Repeated bounded spawning and destruction | Entity generations, component mutation, stale handles, events and allocation behavior. |
| Transform hierarchy | Deep chains, wide trees, moving parents and reparenting | Propagation, dirty tracking, hierarchy integrity and update cost. |
| Streaming and filesystem | Traversal, reversal and teleportation | Background reads, cancellation, cache behavior and stalls; resource residency when supported. |
| Save and replay | Checkpoint, mutate, restore and repeat inputs | Serialization, restored references, version handling, RNG state and scoped reproducibility. |
| Jobs and scheduling | Independent tasks, dependency chains and uneven work | Dependency correctness, synchronization, cancellation, shutdown and load balancing. |
| Time and pacing | Pause, single-step, slow motion and injected stalls | Fixed-tick accumulation, interpolation, timers, catch-up and dropped-time policy. |
| Platform and input | Resize, focus changes, fullscreen and device reconnects | Window lifecycle, DPI, input cancellation, device state and presentation recovery. |
| Editor and content | Import, edit, undo, save, reload, build and run | Document state, dependencies, persistence, setup repair and SDK integration. |

## Adding and running a case

1. Select one observable contract and inspect its current implementation and
   evidence. State the required backend, devices and engine features.
2. Pin the source revision and asset variant, verify license/attribution, and
   record hashes, units, coordinate conventions and import/conversion choices.
   Keep external acquisitions explicit and repeat runs independent of downloads.
3. Define initial state, ordered inputs, camera/listener trajectory, seed, tick
   policy, workload size, warm-up, measured duration and reset/termination rules.
4. Define expected observations and correctness tolerances before accepting a
   baseline. Use image comparisons, physical invariants, signal analysis or
   state checks appropriate to the tested contract. Listening/playtesting can
   supplement automated observations.
5. Build a small case using existing module boundaries and test helpers. Support
   interactive inspection and scripted execution; use headless execution where
   the contract allows it. For standalone SDK consumers, perform the project
   checks in [project setup](project-sdk-workflow.md).
6. Run supported correctness checks first, then repeated performance measurements
   with recorded build, hardware, backend and workload identity. Keep sanitizer
   measurements separate from ordinary performance baselines. Report distributions
   and workload-specific metrics rather than FPS alone.
7. Preserve failure artifacts and write an evidence record. Link it from this
   guide and the relevant implementation owner; update the case's status and
   name the next runnable case or concrete missing prerequisite.

Every evidence record should include the case/source/content identities, exact
commands, environment, expected and observed checkpoints, invariant results,
measurements, artifact paths and remaining limitations. Use ignored `out/` for
generated captures/reports; keep authored instructions and concise evidence in
`docs/`. Treat missing capabilities as unsupported/skipped, incomplete runs as
incomplete, and failed invariants as failed. Preserve the first failure before
retries. Calibrate performance thresholds on a declared runner before making
them required gates. See the owning engineering quality policy for details.

## Continuation status

| Item | Status | Next action |
| --- | --- | --- |
| Scene catalog and initial priorities | Recorded | Use this guide when selecting the next case. |
| Cornell box | Candidate | Review the rendering capability and define the representation and expected observations. |
| Sponza and San Miguel | Deferred | Admit a version when geometry/material import and rendering support the intended measurements. |
| Physics stack, tumbler and character course | Candidate | Confirm subsystem availability before adding runnable cases. |
| Audio and world fixtures | Candidate | Reuse the existing examples and add one controlled case at a time. |
| Other catalog entries | Backlog | Select according to implemented capabilities and a concrete validation need. |

Update this table with links to evidence as cases are implemented and executed.
An external sample running in its original engine is reference behavior; a
Ludus result requires the corresponding Ludus execution and observations.
