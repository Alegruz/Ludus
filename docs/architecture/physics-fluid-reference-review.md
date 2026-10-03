# Physics and fluid simulation reference review

The initial [simulation design](physics-fluid-simulation.md) was written before
this review. The local `references/game-dev-gems-toc.md` was then searched for
water, integration, collision and numerical chapters. The review supports the
small analytic implementation and adds specific numerical and extension rules.
The table distinguishes verified chapter content from index-only leads.

| Reference and evidence | Design consequence |
| --- | --- |
| Game Programming Gems 4, 3.6 Interactive Water Surfaces, PDF pp. 270–271, printed pp. 266–267; rendered and OCR checked | iWave uses a height grid and a convolution operator. Precomputed kernel size trades work for surface quality. Add iWave as a later reflection/dispersion option rather than confusing analytic gameplay rings with a full physical wave surface. Historical timing in the chapter is not a budget for current hardware. |
| Bridson, Fluid Simulation for Computer Graphics, second edition, 12.2–12.3, printed pp. 180–183; local PDF text checked | Shallow-water wave speed depends on depth. A discretization has a time-step restriction and height differs from depth on sloped terrain. Require an explicit CFL policy and a lake-at-rest test before introducing a grid/current solver. |
| GPU Gems 1, chapter 1, Effective Water Simulation from Physical Models; [author chapter](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models) checked | Analytic wave functions can supply height and derivatives continuously. If adding cosmetic bob/tilt, share wave definitions and derive normals; do not infer transport velocity from a moving surface pattern. |
| GPU Gems 1, chapter 38, Fast Fluid Dynamics Simulation on the GPU; [author chapter](https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu) checked | Its 2D velocity-field solver excludes a water/air free surface. Keep surface-wave and transport-current requirements distinct. GPU pressure/advection passes are a separate renderer and numerical project. |
| Physically Based Rendering fourth edition, mathematical infrastructure; [author book section](https://pbr-book.org/4ed/Utilities/Mathematical_Infrastructure#FindingZeros) checked | Replace the existing naive quadratic roots with cancellation-resistant q/a and c/q roots. Normalize geometry before coefficient products, handle exact linear/degenerate cases, and verify unsquared band overlap. The implementation does not copy the book's FMA discriminant helper; explicit roundoff tolerance and precise compiler flags follow Ludus policy. |
| Game Programming Gems 4, 1.3 The Clock; Game Engine Architecture third edition, time management; Video Game Optimization, benchmark lifecycle and AoS vs SoA; index entries only | These are useful follow-up reading leads. Existing fixed-tick ownership and profile-before-layout-change decisions remain ours; no claim that the unread chapters validate their detailed implementation. |

## Revisions adopted

The engine adds precise, checked numerical kernels rather than a boat/water
module. Stable contact roots, scale-aware arithmetic, explicit miss versus
failure, unchanged outputs on failure, and expm1 drag coefficients are acceptance
criteria. Sandbox validates physical tuning and caches drag coefficients outside
the tick. CPU and renderer continue to use one authoritative ring state.

A later interactive surface may use iWave for dispersive waves or a linear wave
grid for simpler interference. These models need distinct stability analysis;
there is no generic CFL constant transferable between algorithms. Full shallow
water needs depth-aware speed, positivity/wet-dry handling, conservation and a
lake-at-rest regression. Fluid-solid coupling in Bridson chapter 15 is an
index-only lead until that chapter is reviewed for a concrete coupling feature.

No reference establishes this design as universally optimal or current SOTA.
The decision is based on the game's one-boat, bounded-ring requirements, existing
SDK capabilities, numerical tests and measured implementation cost.
