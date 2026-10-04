# CPU fluid field reference review

Reviewed October 4, 2026 for the reusable CPU field and its Sandbox adapter.
The local game-dev catalog was searched for interactive water and fluid chapters.
The older [physics review](physics-fluid-reference-review.md) records the prior
analytic ripple scope; this review concerns the subsequent grid simulation.

| Source actually reviewed | Consequence for this implementation |
| --- | --- |
| Robert Bridson, *Fluid Simulation for Computer Graphics*, second edition, chapter 3 (semi-Lagrangian tracing and CFL) and §§12.2–12.3, printed pp. 180–183; local PDF text inspected | Retain height-driven shallow-water pressure, use staggered velocities, recompute a depth-aware bound, and distinguish surface elevation from total depth. Semi-Lagrangian advection adds numerical diffusion. |
| Robert Bridson, [UBC shallow-water lecture](https://www.cs.ubc.ca/~rbridson/courses/533d-fall-2005/nov17-cs533d-slides.pdf), author slides | Supporting derivation of depth-averaged surface and velocity equations. The implementation assumes a flat bed and does not claim the full variable-bed model. |
| *Game Programming Gems 4*, §3.6, “Interactive Water Surfaces”, PDF pp. 270–271 / printed pp. 266–267; rendered and OCR checked | iWave's height convolution is a possible later dispersive/reflection model. Only these two pages were reviewed; the complete chapter and companion implementation were not audited. It is not the algorithm shipped here. |
| Mark J. Harris, [GPU Gems chapter 38](https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu), author chapter, particularly §38.1.3 and the projection discussion | Its incompressible 2D velocity solver omits the water/air free surface. Adding its projection here would erase the divergence which must evolve elevation. GPU textures, compute passes and pressure iterations are outside this CPU module. |

The 0.3 substep factor, speed cap, admission ranges, midpoint quadrature for
strokes, donor drainage limiter, foam heuristic and material displacement limits
are implementation choices. They are not quoted recipes or correctness claims
from these books. Shared donor-limited face fluxes conserve height algebraically;
tests check positivity, conservation and flat-rest behavior for supported cases.
The implementation does not establish convergence order, arbitrary-forcing
stability, scientific accuracy or superiority to other water solvers.

No book assets or companion source code are redistributed. References inform
the equations and scope; the engine exports a game-oriented numerical capability,
and Sandbox supplies the concrete source timing and gameplay use.
