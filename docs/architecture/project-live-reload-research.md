# Project live editing research and design revision

Research date: 2026-10-02. Baseline main:
`3fadacecd4c3856e0456341ea05507459d479a38`. Read the
[final architecture](project-live-reload.md) and
[normative design](../../.kiro/specs/project-live-reload/design.md).
All conclusions below are original paraphrases and Ludus engineering judgments.
No historical sample code, private PDF, extracted text or screenshots are needed
by Kiro or included in the source package. This is a focused review, not a survey
proving that one architecture is universally state of the art.

## Design recorded before reading chapters

The initial design selected separate editor/GameHost processes, static engine
libraries, one C-compatible game module, immutable build generations and a safe
frame-boundary reload. It retained the old instance until candidate acceptance,
isolated candidate staging, and required explicit checkpoint migration. Property,
asset, source-code and SDK changes had separate paths. A small inspector/undo
seam and the existing CMake File API avoided a new ECS, GUI or RPC framework.
Linux/debugger-first delivery preceded browser dynamic linking or engine patching.
The baseline was recorded before chapter review; subsequent changes are below.

## Discovery and reading method

Searched local ignored `references/game-dev-gems-toc.md` for DLL/dynamic linking,
runtime plugin reloading, tuning, serialization, IPC and asset pipelines. The
index spans Gems and related engine books. Titles were discovery pointers only.
Actually read the two DLL articles and tuning/RPC chapters below by text
extraction, plus the stated asset/plugin sections. Rendered and inspected the
DLL article opening, tuning separation discussion/Figure 16.2 and pipeline
Figure 2.3. The additional toolset book was selected because the index contains
an unusually direct runtime-reload section. Page positions are one-based PDF
file positions; printed pagination is separate. Partial reading is explicit.

## Articles/sections read and decisions

### Herb Marselas: Exporting C++ Classes from DLLs

Game Programming Gems 2 (2001), 1.4, printed pp. 28-32, local PDF pp. 25-29.
The article describes exported functions/classes, instance construction and
deletion helpers, and the consequences of consuming classes across a DLL
boundary. Its examples depend on historical Windows/compiler behavior.

Adopt: an instance's creator supplies its destruction path; export the smallest
intentional surface and make ownership visible. Strengthened ABI allocation,
opaque-instance lifetime and destructor-generation tests (L04/L08).
Reject: public exported C++ classes/virtual inheritance as a stable reload ABI,
and historical Visual C++ inline workarounds. The function-table design is our
choice; the article does not prove modern cross-toolchain binary compatibility.

### Herb Marselas: Protect Yourself from DLL Hell and Missing OS Functions

Game Programming Gems 2 (2001), 1.5, printed pp. 33-37, local PDF pp. 30-34.
It contrasts implicit and explicit linking, resolves required/optional functions
at runtime and handles missing libraries/functions with explicit fallback.
It also discusses balancing library references and version uncertainty.

Adopt: preflight the exact package, resolve the one required entry, check actual
capabilities/build identity and report load failure without destroying the active
game. Strengthened expected/actual diagnostics and dependency/entry fixtures.
Reject: arbitrary DLL searches, assuming file/version strings imply compatibility,
or turning optional OS API fallbacks into silent state resets. Current platform
docs, not these historical snippets, define loader flags and dependency search.

### Wessam Bahnassi: Game Tuning Infrastructure

Game Engine Gems 2 (2011), chapter 16, printed pp. 263-277,
local PDF pp. 279-293. The chapter separates tool choice, data exchange,
schema/exposure and storage. It discusses iteration cost, typed/ranged tunables,
separate-process editor stability, explicit schema definitions, readable named
fields, logical file partitioning and persistence versus temporary experiments.

Adopt: a narrow explicit property schema; measure turnaround per change class;
keep authoring/session values distinct; use named versioned source documents
and explicit Apply to Document. Strengthened conditional property edits,
document/runtime undo separation and revision/conflict tests (L10/L11).
Retained separate GameHost because the chapter's instability tradeoff applies.
Reject: moving Qt into the game, C++/CLI/UnrealScript dependencies, shared mutable
global tunables and raw-address inspector access. Full reflected scene authoring
is unnecessary for the first live tuning fixture.

### Kurt Pelzer: Inter-Process Communication Based on Your Own RPC Subsystem

Game Engine Gems 1 (2010), chapter 28, printed pp. 445-457,
local PDF pp. 473-485. It explains marshalling, procedure/program/version
identity, transport independence, asynchronous work and the uncertainty of
whether remote work occurred when a response is lost. Editor/runtime interaction
is one concrete use case.

Adopt: owned serializable values, version/request/session identities, asynchronous
UI operations and explicit outcome reconciliation. Added bounded retained-result
deduplication and Status/current-generation reads after a lost reply (L13),
instead of retrying an edit/reload that may already have committed.
Reject: transparent synchronous remote calls on the GUI thread, arbitrary RPC
registries, reconnect/replay or a remote network service. Local private channels
are sufficient; every native-game operation still has explicit latency/failure.

### Remi Arnaud: The Game Asset Pipeline, sections 2.2-2.3

Game Engine Gems 1 (2010), chapter 2; reviewed printed pp. 24-30,
local PDF pp. 52-58, not the entire chapter. These sections distinguish editor
viewing/authoring responsibilities, source/intermediate/final assets and push/pull
iteration. Editing generated intermediates can lose changes at the next export;
dependencies and source provenance support correct rebuilds and source navigation.

Adopt: source/cooked/runtime ownership, explicit asset ID/provenance and validating
replacement before retiring the old runtime resource (L12). Strengthened the
rule that editor Apply edits source documents, never build output. Asset failure
does not trigger a code reload or reset the running world.
Reject: a new general distributed asset build service, DCC integration or COLLADA
dependency. Section 2.4 and the remainder were not reviewed for this design.

### Graham Wihlidal: Reloading Plugins During Runtime

Game Engine Toolset Development (2006), chapter 38, focused review of printed
pp. 448-455/local PDF pp. 467-474. The passage uses .NET AppDomains,
file-change watching, delayed/coalesced reload, lifecycle notifications and
automatic-reload preferences; its implementation unloads the catalog before
reloading. The rest of the chapter/runtime compiler/security sections were
not reviewed or adopted.

Adopt: opt-in auto reload, coalescing watcher bursts and explicit teardown/events.
Strengthened newest-request scheduling and complete-artifact publication (L05/L06).
Reject: fixed long sleeps, broad exception suppression, unloading the active
catalog before replacement succeeds, and assuming native dlclose has AppDomain
isolation semantics. A watcher event proves neither a finished link nor safe
unload. Ludus retains A and stages B; that revision is our engineering decision.

## Current practice and API verification

Modern sources complement the books. Their API facts do not prove Ludus-specific
performance or safety; implementation must use pinned versions and real fixtures.

- [Epic Live Coding](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-live-coding-to-recompile-unreal-engine-applications-at-runtime)
  distinguishes small edits from structural changes requiring reinstancing and
  pointer/cache cleanup. This supports explicit recreation/migration. We defer
  machine-code patching and a universal object-reinstancing framework.
- [Unity serialization and reload](https://docs.unity3d.com/6000.0/Documentation/Manual/script-serialization-how-unity-uses.html)
  describes storing/restoring serializable state with limits on static fields.
  This supports defining intended persistent versus rebuilt state. Its C# runtime
  behavior is not a native C++ loader guarantee.
- [Linux dlopen/dlclose documentation](https://man7.org/linux/man-pages/man3/dlopen.3.html)
  describes immediate/local resolution, constructor execution, reference counts
  and conditions affecting actual unload. Use hidden symbols, audited dependencies
  and observable retirement; do not treat RTLD_LOCAL as security isolation.
- [Windows LoadLibraryEx](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)
  and [FreeLibrary](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibrary)
  define load/search and reference-release behavior. The Windows backend needs
  its own verified policy and DLL/PDB acceptance before support is claimed.
- [Emscripten dynamic linking](https://emscripten.org/docs/compiling/Dynamic-Linking.html)
  uses main/side modules with separate build/link constraints. Native hot reload
  does not automatically extend to existing browser builds.
- [CMake File API](https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html)
  supplies target type/artifact/source/build identity. Extend the existing
  resolver under managed CMake 3.29.6 rather than guess module paths.

## Final revisions versus baseline

The core host/module choice survived the review. The review made the design
more concrete: separate session edits from source authoring, use stable named
properties and explicit persistence, reconcile ambiguous mutation outcomes,
track asset source provenance and accept only complete/newest build generations.
Current engine docs strengthened migration/pointer cleanup and the distinction
between logical code retirement and physical unmapping. None justified copying
historical code, introducing exception handling or expanding into a generic
plugin/reflection/network framework.

No load/reload/debugger/latency claims are experimentally established by this
documentation commit. L0-L6 contain those gates. Kiro receives all normative
behavior and original findings here without access to ignored references.

Documentation validation: local relative links, fenced blocks, unchecked task
status and `git diff --check` passed. `scripts/check linux-clang-development
--all` passed pinned source formatting and the foundational include boundary;
tidy could not start because this clean isolated worktree has no bootstrapped
CMake/Ninja/Conan environment. No production/test code changed. Runtime builds,
sanitizers, SDK and hot-reload acceptance are implementation gates, all unchecked.
