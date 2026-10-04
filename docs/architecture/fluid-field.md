# CPU shallow-water field

`Ludus::PhysicsFluid` exports `<ludus/physics/fluid/field.h>` on native and
Emscripten targets. It extracts the reusable numerical core used by
[Ludus-Sandbox](https://github.com/Alegruz/Ludus-Sandbox), including conservative
height transport and a donor drainage limiter. The game owns authored sea modes,
splash envelopes, rock geometry, boat response, input mapping and rendering.

## Model and numerical choices

A centered rectangular basin has a constant flat bed, cell-centered elevation
and staggered face velocities. In continuous notation the model is
`du/dt + u·grad(u) = -g grad(h) - grad(p/rho) - damping*u`,
`dh/dt = -div((Depth+h)*u)`. This is depth-averaged shallow water.

Each substep midpoint-backtraces velocity, applies gravity and external pressure
on open faces, damps/clamps face velocity, transports the material map and foam,
and advances height using shared upwind depth fluxes. A cell's available water
above `MinimumDepth` limits its outgoing flux. Each shared face uses the donor's
scale once, so neighboring updates cancel and total volume is conserved to
roundoff. Height is never independently clipped. Closed faces carry zero normal
flow. Exact cell traversal rejects backtraces and stroke centerlines that cross
solids, including short corner intersections. Brush footprints affect nearby
open faces; the mask does not implement visibility shielding across a wall.

The substep policy is `0.3*min(dx,dy)/(MaxSpeed+sqrt(g*maxTotalDepth))`, recomputed
from the current height before each step. The speed cap bounds components;
using the smaller spacing with the 0.3 factor also bounds the two-axis transport
rate. This is a conservative engineering policy for this implementation, not a
proof of convergence for arbitrary forcing. Pressure and resolution admission
limits keep arithmetic finite; callers must handle `WorkLimit`.

Semi-Lagrangian velocity advection is dissipative and is not a conservative
momentum/shock solver. Foam and displacement are visual material diagnostics;
energy is a resting-depth reference diagnostic, not an exact invariant. The
model excludes variable bathymetry, wet/dry shores, deep-water dispersion,
overturning waves, moving-solid coupling, GPU solving and bitwise cross-platform
determinism. Gravity divergence is deliberately retained to change height.

## Public contracts and cost

All APIs are `noexcept` with explicit status. Initialization uses fallible Ludus
arrays and private storage; invalid configuration or allocation failure preserves
a previous field. Same-resolution initialization and all subsequent operations
reuse storage. Public views never expose ownership. Masks and pressure are
row-major cells; initial velocity uses `(Width+1)*Height` U faces and
`Width*(Height+1)` V faces. Any nonzero mask value is solid.

Reset and forcing validate whole inputs before mutation. Pressure remains held
until replaced, and its units are pressure divided by density (`m²/s²`). An empty
pressure view clears it. A stroke integrates a compact brush along its segment;
spacing controls quadrature error, while integrated strength depends on distance
rather than the number of pointer events. A rejected stroke applies no impulse.

`TryAdvance` accepts at most 0.25 s. On `WorkLimit`, `StepInfo` reports actual
partial progress; retry only the remaining duration. Invalid input preserves the
field and the output. `TryGetStepLimit` supports callers which update pressure
sources between substeps. Samples outside/inside solids preserve the output.
Configuration admission ranges live beside their checks in `field.cpp`.

Storage is O(cells + faces), roughly 113 bytes per cell, plus face-row padding.
Stepping, diagnostics and pressure validation are O(cells); stroke work is
bounded to 8192 brush samples and resolution is capped at 256 per axis.
Initialization/reset clears state. The field is single-owner, without internal
locks, callbacks or global simulation state.

## Validation

Native Catch2 coverage checks invalid-operation preservation, move ownership,
event-density forcing, opposite strokes, strong-pressure volume conservation and
positive total depth, solid basin isolation, a flat lake at rest, corner traversal,
all-solid masks and explicit partial advancement. The installed SDK consumer
links the exported target and exercises forcing, stepping, sampling and volume.
The web fluid probe executes that same corpus as wasm under pinned SDK Node.
Sandbox tests and browser playtests verify game integration separately.

See [the reference review](fluid-field-reference-review.md) for source scope and
which numerical decisions are Ludus adaptations.
