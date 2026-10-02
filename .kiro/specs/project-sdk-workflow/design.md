# Ludus independent game project architecture

This specification extends the installed SDK and Editor E0 boundaries into a
distributable application workflow. New paths and commands below are contracts
to implement, not claims about the current checkout. Follow AGENTS.md, steering
and ADRs 0003/0004/0005/0007/0009. Keep production C++ exception-free.

## Ownership and dependencies

The runtime SDK owns public engine headers, static libraries, generated policy
headers, exported CMake targets and redistributable dependencies. Host tools own
project/SDK management, templates, build orchestration and target packaging
adapters. The optional Editor is a client of those tools. A game owns its code,
assets, CMake targets and application lifecycle.

Dependency direction is Game -> runtime SDK and Editor -> host tooling. Tooling
reads SDK metadata and invokes CMake; it does not require linking the target SDK.
The Editor may link its own native engine dependencies, independently of the SDK
selected for a game. Never load an arbitrary selected game's libraries into the
Editor process. Continue to launch the game as a separately owned process.

Game targets initially link statically. Do not introduce a dynamic game-module
ABI, ECS, reflection or hot reload for this milestone. Keep engine smoke and
unit tests in Ludus; use a standalone project as the application reference.

## Existing implementation to reuse

Inspect `cmake/LudusConfig.cmake.in`, `LudusSdkManifest.json.in`,
`EngineBuildFlavor.cmake`, `scripts/python/engine.py`, `editor_project.py`,
`editor_tool.py`, `editor_launch.py`, `cmake_targets.py`, `apps/editor`,
`tests/sdk_consumer` and `examples/editor-sdk-project` before changing them.

The SDK consumer currently receives both the SDK prefix and generated Conan
metadata. `LudusConfig.cmake.in` finds volk and optionally Wayland. Text's static
library privately links FreeType/HarfBuzz: PRIVATE does not eliminate the final
consumer's static link dependencies. Audit all exported modules, including
Threads and platform/graphics dependencies; do not fix only the minimal sample.
The current Editor launcher uses the checkout's managed Python and adapter.
Extract reusable logic rather than making installed tools simulate a checkout.

## Packages and supported compatibility

Produce three distributions: target runtime SDK, native host tools and optional
native Editor. They may be delivered together for convenience, but Qt and Editor
files never enter the runtime CMake export graph or headless CLI prerequisites.
Host tools include their adapter code and templates with declared Python/tool
requirements. An installed launcher locates resources relative to its own
installation. A Python environment must be recreated/installable at its final
location; do not archive a producer venv containing absolute paths.

Initial target packages support native Linux x64 Clang 18, C++23, the pinned
standard-library ABI and Debug/Development/Release flavors. Determine and record
the actual runtime ABI and distro baseline from the build environment; do not
infer binary portability from compiler major alone. Sanitizer builds are local
variants initially. Keep assertion headers paired with their libraries.

Emscripten is a separate target toolchain/SDK identity. Preserve current browser
consumers and record the Emscripten/WebGPU pins for web packages when supported.
A native editor is never an Emscripten executable, and a web SDK is linkable
input for a final application JS/Wasm build, not a native runtime library.

Extend the SDK manifest with a schema version, source revision, target triple,
toolchain/runtime identity, sorted features, sanitizer and policy configuration,
dependency versions and system prerequisites. Derive identity from the actual
build inputs. Hash the distributable payload; the hash lives in the release
catalog/lock or sidecar rather than recursively inside the bytes being hashed.
Reject incompatible/unknown identities before configure with expected/actual
details. Preserve CMake version checking and export package compatibility checks
for direct CMake consumers that bypass the CLI.

Bundle redistributable static dependencies and their CMake metadata in the SDK,
with package-relative locations and licenses. Explicitly list system libraries,
GPU drivers, development headers and host shader compiler prerequisites that
cannot reasonably be bundled. Provide a package-local dependency search helper
without leaking absolute producer paths or changing consumers' global search
state. Exported targets must resolve correctly even when modules are not used.
Do not use the producer Conan cache as a runtime package manager.

## Project files and schema evolution

Generated projects contain:

```text
MyGame/
  ludus.project.json
  ludus.lock.json
  CMakeLists.txt
  CMakePresets.json
  src/main.cpp
  assets/
  config/
  .gitignore
  .ludus/                 local overrides, locks and generated settings
  out/                    builds, logs and application packages
```

Implement descriptor version 2, preserving the version-1 name/provider/source/
preset/target/run fields and adding `engine` and `template` objects. Initially
`engine.version` is an exact release version, not a floating range;
`engine.components` lists requested public modules and `engine.features` lists
required features. `template.id` and `template.version` identify generation
inputs. Retain bounded strict parsing and shared C++/Python fixtures. Define
limits and unknown-field handling for the new objects. Only external provider
`cmake` projects need the engine declaration; existing provider `ludus` remains
the engine developer workflow and never acquires a downloaded engine dependency.

Keep version-1 readers supported in the updated tools. Old Editor versions
reject version 2 usefully. Migration is an explicit command/UI action: retain
relative paths and launch settings, require engine selection where absent,
compare on-disk digests, stage both descriptor and lock under a project lock,
and recover safely from partial multi-file commits. Never silently migrate Open.

The lock is versioned JSON recording exact engine version/revision and package
identities/checksums per target/flavor, plus template version. No installation
path belongs in the lock. Validate descriptor-lock agreement. Ordinary build
never updates it; explicit engine selection/lock update does. Initially locking
all three native flavors allows profile changes without changing dependency
intent. Release catalog entries provide their metadata and archive digest.
When no published release exists, use a local SDK override with an explicitly
unresolved release lock; build requires that override, and diagnostics say the
project is not reproducible from a release yet. Do not invent hashes/versions.
Represent that case with an explicit unresolved lock state and no fabricated
package entries. `create --sdk` derives the engine requirement from the validated
local manifest; it records the local prefix only in ignored settings. Publish a
stable lock schema and shared fixtures before adding commands that write it.

Ignored `.ludus/local.json` records target/flavor local SDK overrides. Changing
the override does not rewrite the committed descriptor/lock. Its SDK manifest
must match the project requirement and requested target/features/flavor; a local
revision may differ from the locked release but must be displayed. To test a
different engine version, explicitly update project intent instead of bypassing
compatibility validation. Keep UI layout and resolved machine paths local.

## SDK store and installation

Use the XDG user data directory (default `~/.local/share/ludus`) for installed
SDKs, with an explicit CLI option/environment override for CI and portable use.
Each final SDK directory is addressed by validated identity and payload digest.
Several projects use the same immutable files; generated build output stays in
the project. Removal is explicit and reports affected discoverable projects;
do not introduce automatic garbage collection or claim tracking of every project.

Support `ludus sdk install --archive <path>` first, then version selection through
a release catalog. Catalog schema records engine release/revision, package
identity, URL, size and SHA256. Use HTTPS for downloads and publisher-controlled
catalogs; project descriptors cannot supply arbitrary install scripts or hooks.
Checksums detect corruption, not authenticity when supplied by an untrusted
catalog. Fetch remote metadata only during explicit install/select operations.

Installation takes an SDK-specific cooperative lock, bounds compressed and
expanded sizes/entry counts, rejects traversal, absolute names, escaping links
and unsupported archive entry types, verifies digest and manifest identity,
extracts to a sibling staging directory and atomically publishes. An existing
verified identical installation is reused; a corrupt destination fails with an
explicit repair operation. Cancellation cleans staging and leaves previous SDKs
usable. Projects cannot observe an incomplete installation. Store writable
temporary downloads separately from immutable final SDKs.

## Resolution and CMake integration

For version 2, precedence is explicit per-operation local SDK option, saved
local override, then exact locked SDK in the store. Print the resolved identity,
path and override status. Missing SDKs stop with an install command; configure/
build/run do not download, refresh the lock or compile engine sources.
Version-1 external projects retain their existing preset/environment behavior.

Generate CMake presets for the three supported flavors with a documented
`LUDUS_SDK_PREFIX` input; the shared backend supplies the validated prefix and
compiler/tool requirements. Supply direct CMake instructions for using the same
presets without Ludus CLI. Keep the application's CMakeLists authoritative and
editable. An installed public CMake helper may apply the supported application
compile/link policy without exporting engine build warnings or private targets.
Do not reference checkout-only `ludus_apply_project_defaults` in templates.

Separate or invalidate build trees on SDK/toolchain identity changes. Use a
resolved-input stamp including manifest/header/library fingerprints for mutable
local SDK prefixes; path equality alone is insufficient. Refresh CMake and force
the required relink/recompile after local SDK updates. Detect a changed stamp
during an operation and fail rather than accepting mixed inputs. Local engine
install and game build must not run concurrently; document that limit and use
a shared prefix lock where the tools control both sides.

Extend the existing two-preset E0 contract deliberately to Release for version-2
native projects. Update validators, UI choices, command planner and fixtures
together; do not alter flavor assertion policy. Resolve requested executable
targets using the actual CMake File API artifact after configure and after
successful build. Never guess executable paths.

## Shared backend and public commands

Keep the existing Python tooling as the initial implementation language.
Extract installed package modules for project model/resolution, templates, SDK
installation, CMake planning and process supervision. The engine repository's
scripts become compatible entry points where useful. Avoid parallel CLI and
Editor implementations or a second supervisor. Preserve current E0 protocol,
backpressure, stale-event rejection, Stop/Close/EOF handling and cleanup states;
version protocol extensions explicitly when adding actions.

Public command contract:

```text
ludus sdk list
ludus sdk install --archive <sdk-archive>
ludus sdk install --version <release> --target linux-x64 --flavor development
ludus project create <destination> --name <name> --template minimal --engine <release>
ludus project create <destination> --name <name> --template minimal --sdk <prefix>
ludus project configure <project> --profile development
ludus project build <project> --profile development
ludus project run <project> --profile development
ludus project engine <project> --sdk <prefix> --profile development
ludus project engine <project> --clear-override --profile development
ludus project migrate <project> --engine <release>
```

Accept a project directory or descriptor consistently. `run` ensures a successful
current build first, using E0 build-and-run semantics. Add explicit lock-update
selection through `project engine --version <release>`; stage descriptor/lock
together. Missing other flavors require explicit install or override; never use
a Development SDK for a Release request silently. `create` uses installed SDKs
only; the Editor may offer a separately explicit install followed by creation.
No command string is passed through a shell. Human CLI output uses stable exit
statuses and optional versioned JSON result output; the Editor uses the private
structured streaming protocol, not parsing human text.
Record host-tool/template/protocol compatibility in the tooling distribution;
reject an unsupported descriptor or Editor/backend protocol with an actionable
version message. Engine version selection never implicitly changes the running
Editor's own libraries or downgrades the host tools.

Share build-tree locks across installed CLI and Editor. Raw CMake invocations
remain outside cooperative lock guarantees; document concurrent use as
unsupported. Headless means project tooling without Qt/display, not a promise
that a windowed game can run without a compositor/GPU.

## Project creation and Editor workflow

Bundle a versioned minimal native template using only public SDK APIs. Its first
target demonstrates initialization and useful diagnostics without introducing
new engine subsystems. Provide a headless integration fixture and a separate
native runtime acceptance path. Generated project files are user-owned after
creation; template updates never overwrite existing game code automatically.

Reject existing destinations in the first implementation, including nonempty
directories and symlink destinations. Under a parent/destination lock, stage on
the same filesystem, validate generated paths/identifiers/manifest, resolve the
SDK and publish with a no-replace operation. Handle a destination appearing
between validation and publication without overwriting it. Cancellation before
publication leaves no destination; after publication report success and allow
Open retry. File names/placeholders never enable arbitrary template commands.

Editor New Project collects location/name/template/engine and presents missing
SDK/tool prerequisites before creation. Install, Create and Open are separate
observable asynchronous stages. Opening does not start a download/configure.
Settings show required engine version, resolved revision, flavor and override.
Failures retain form input and the existing workspace; avoid GUI-thread waits.
Reuse the E0 state owner, operations, cancellation and diagnostic panels.

## Release and application packaging boundaries

CI builds candidates and relocates them before acceptance. Version-tag workflows
publish native SDKs for all accepted flavors, host tools, optional Editor,
dependency notices, catalogs/checksums and provenance. Validate that tag/version/
source revision agree and fail duplicate asset replacement. Ordinary PRs test
candidate packages without publishing a release. Use least-privilege workflow
permissions; test artifacts before release publication. Implementation authors
may open PRs but must not create tags/releases or merge them without authorization.

SDK distribution and game distribution are different outputs. The game's final
executable/assets and necessary dynamic runtime files ship to players; SDK
headers, compilers and Qt Editor do not. Future `project package` adapters should
reuse existing shader/web packaging logic with project-selected targets, not
copy the engine smoke packager's fixed file list. General asset importing and
game packaging are deferred here; preserve existing package-web behavior.

## Reference project and completion boundary

Add a standalone integration fixture outside the engine source tree during
tests. Prove two projects share one SDK and engine sources are absent from their
compile databases. Ludus-Sandbox should consume the same interface, remove its
mandatory nested engine build, and pin an engine release once available.

This repository does not contain Ludus-Sandbox. Do not guess its paths or commit
changes to another repository without access and explicit scope. The Ludus PR
can complete with a validated standalone reference and a recorded Sandbox
conversion follow-up. That is not evidence the external Sandbox was converted.
