# Game world reference

This small game demonstrates the first implementation of the
[world architecture](../../docs/architecture/game-world.md). Move the orange player
with WASD, attack nearby red guards with Space, and reach the green exit to load
the other room. P pauses, N advances one neutral tick while paused, and R restarts.
Click or focus the browser canvas before using keyboard controls.

The reusable engine module contains only identity and component storage. This
application owns the level schema, component roster, simulation policies, input
bridge, session and renderer. The model builds and tests with the normal native
presets even when the graphical application is disabled.

## Run the application

Prepare the normal pinned native tools and dependencies with `./init.sh --cli`.
The optional renderer also requires the pinned Slang/SPIR-V tools:

```bash
./scripts/shader-probe bootstrap
out/host-tools/venv/bin/cmake --list-presets=configure
out/host-tools/venv/bin/cmake --list-presets=build
out/host-tools/venv/bin/cmake --list-presets=test
out/host-tools/venv/bin/cmake --preset linux-clang-development   -DLUDUS_BUILD_WORLD_DEMO=ON   -DLUDUS_SLANG_COMPILER="$PWD/out/shader-tools/slang/bin/slangc"   -DLUDUS_SPIRV_VALIDATOR="$PWD/out/shader-tools/spirv-tools/usr/bin/spirv-val"
out/host-tools/venv/bin/cmake --build --preset linux-clang-development --target ludus_world_demo
out/build/linux-clang-development/apps/world_demo/ludus_world_demo
```

The CMake cache stores local tool paths; do not commit those paths. Existing CMake
Tools settings select the pinned CMake and selectable presets. `--headless` runs
120 simulation ticks without a window. `--frames 10` presents ten native frames
and exits through ordinary shutdown.

For the browser, prepare `./init.sh --cli web-emscripten-development --preset-only`,
enable `LUDUS_BUILD_WORLD_DEMO=ON` in that preset's local CMake cache, and run
`./scripts/build web-emscripten-development`. Serve
`out/build/web-emscripten-development/apps/world_demo/` over HTTP. The graphical
application is opt-in so normal native onboarding does not require shader tools.
Browser builds use the existing pinned Slang, SPIRV-Tools and SPIRV-Cross helpers.

## Edit levels and gameplay

Edit [first-room.json](levels/first-room.json) or
[second-room.json](levels/second-room.json), then build. CMake embeds these files;
there is no duplicated C++ level literal. `ReadLevel` owns the result, rejects
unknown/duplicate fields, and validates kinds, references and procedural visual
keys. IDs use lowercase ASCII letters, digits and hyphens, with at most 31 bytes.
The reader accepts JSON escapes for this ASCII vocabulary. It bounds input to
64 KiB and does not allocate or recurse over an arbitrary JSON tree.

`Level` is also directly constructible in code. `Prepare` and `BuildNext` use the
same world semantics for compiled definitions and decoded JSON. `WriteLevel`
produces canonical JSON into fallible owned storage, preserving the output on
failure. Entity records sort by authored ID. The codec has no filesystem writes;
an editor must provide atomic source-file replacement through its own platform
boundary.

[world.h](internal/world.h) defines the game-owned component roster and observable
state. [world.cpp](world.cpp) has the explicit BeginTick, Intent, Motion, Gameplay,
Commit and publication sequence. Add a component pool's initialization, recipe
insertion and destruction removal together. Gameplay uses player/enemy/exit
capabilities rather than branching on recipe kinds after construction.

`RunTick` advances exactly 1/60 second. The driver accepts at most 250 ms per frame,
runs at most four ticks, counts discarded time and retains fractional debt for
interpolation. Input edges survive zero-tick frames and apply once during catch-up.
Cancellation takes priority over presses; the bridge waits for neutral controls
before rearming. Pause, focus changes, hidden frames and activation reset clock
debt. Focus changes preserve pause/fault mode. A simulation fault retains the
previous render frame until restart and never publishes that tick's presentation.

## Ownership and limits

The reference world reserves 48 entities, 32 commands/outcomes, 64 optional
presentation records, 16 outstanding completion tickets and 64 render records.
Content permits 32 initial entities and 16 static boxes. These limits leave room
for every active entity plus static geometry in the render frame. Queue overflow
and commit preflight return explicit errors; optional presentation overflow counts
drops. Runtime spawning currently accepts guards targeting the existing player.

Queued destruction immediately excludes the entity from systems. The registry
and components remain alive until commit. Spawns publish complete bundles after
preflight; they appear in that tick's render frame and first simulate on the next
tick. Preflight conservatively ignores slots freed by that same batch. Spawn
outcomes remain available until the following BeginTick boundary. Combat uses
creation order for ties, and trigger contacts distinguish enter, stay and exit.
The first entering exit in stable order determines the next level.

A session parses an owned candidate, prepares all pool capacity, builds one recipe
per `PollLoad`, binds forward references and activates through a nonallocating move
at the frame boundary. Failure, cancellation and supersession preserve the active
world. The two procedural visual keys need no external asset loading or GPU leases.
The old render frame is an owned value and the renderer's one pipeline survives
level changes. Browser loading returns between construction steps.

`BeginRequest` returns an owned ticket and reserves completion capacity before
admission. `SubmitCompletion` is an owner-thread mailbox API; a worker would need
its own synchronized transport. It rejects foreign worlds or mismatched tickets,
and BeginTick sorts one bounded result batch by request ID. Superseded and destroyed
entities reject delayed results. Reservations persist until their matching result
or cancellation acknowledgement is consumed. This sample provides no worker pool.

Ludus profiling scopes measure ticks, Intent, Motion, Gameplay, Commit and render
extraction. A 64-record tick trace ring stores consumed input, phase/status and
queue counts.
Records carry tick numbers; the physical ring is not a chronological span after
wrapping. Const pool access exposes health, transforms and AI action phases to a
debugger. Same-build replay tests feed recorded tick inputs through `RunTick` and
compare canonical hashes that include content and gameplay state while excluding
world IDs, addresses, pool order and rendering history. Hashes include runtime
colliders and outstanding request reservations. `GetReplayHash` returns zero
outside an idle boundary or while commands/completions await the next tick.
A replay file format and
cross-platform floating-point determinism are future work.

## Rendering and validation

The game-owned renderer draws up to 64 procedural rectangles through the existing
public fullscreen RHI API. Its 3104-byte uniform block uses fixed vector arrays;
C++ offsets are asserted and independently checked against SPIR-V, WGSL and GLSL
ES reflection. Collider boxes are axis-aligned; visual rotation affects drawing. Narrow windows
expand the camera height to keep the authored horizontal bounds visible.
This adapter demonstrates render extraction without adding textures, arbitrary
meshes or a general sprite renderer to the RHI.

Native tests cover identity retirement, stale/foreign handles, sparse removal,
compiled and JSON definitions, canonical round trips, candidate ownership,
input/catch-up/pause, commands, combat/contacts, completions and replay. A separate
non-sanitized allocator test injects every candidate storage allocation failure
and proves prepared simulation/extraction paths allocate no storage.

The contributor workflow also checks native ASan/UBSan, format/tidy, header/include
boundaries and installed SDK consumption. Browser QA uses the repository's pinned
[Playwright/PNG prerequisites](../../tools/web-browser-tests/README.md):

```bash
node apps/world_demo/tests/browser-test.mjs   out/build/web-emscripten-development/apps/world_demo out/world-browser
```

Set `LUDUS_WORLD_WEBGL=1` to exercise the Auto fallback without WebGPU. The harness
verifies the selected backend and player/guard/exit pixels, captures canvas and
narrow-layout screenshots, and checks movement, pause, step,
restart and focus cancellation. Its Chromium/SwiftShader run is software GPU
acceptance; it does not establish hardware performance or public hosted acceptance.

Fiber scheduling, dependency graphs, render threads, generalized timer queues,
prefab reflection, external media preparation and live component patching remain
the conditional extensions described in the design docs. They are not prerequisites
for this jam baseline.

Recorded checks and remaining limits are in the
[validation record](../../docs/development/game-world-validation.md).
