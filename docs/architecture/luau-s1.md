# S1: generated native/Luau interaction

This opt-in headless slice implements the S1 contract in
[the scripting architecture](scripting.md): one door interaction, a shared
manifest, equivalent native/Luau effects, declared state, checked references,
phase ordering and preservation of unpublished effects on fault.

The experimental runtime lives in modules/runtime/scripting, the game-owned
adapter in tools/script-interaction, and the closed generator in tools/script-cook.
They have no RHI, Platform, Input, editor or managed dependency. Interfaces remain
private; no Scripting target or Luau dependency enters the installed SDK.
LUDUS_BUILD_SCRIPT_INTERACTION defaults to OFF.

## Profile and reproduction

S1 maintains the reviewed experimental profile
s0-interpreter-longjmp-loader-lifetime-v1, including its exact loader ownership
adjustment and both MIT notices. [The S0 audit](luau-s0.md) remains authoritative.
This is a decision to maintain that small profile for this opt-in integration,
not approval of the unmodified pin or a production dependency. cmake/LuauProfile.cmake
shares source verification and VM/Common configuration between S0 and S1.

After normal pinned native setup:

~~~sh
./scripts/script-interaction bootstrap
./scripts/script-interaction cook
./scripts/script-interaction run --preset linux-clang-development
./scripts/script-interaction check --preset linux-clang-development
./scripts/script-interaction run --preset linux-clang-asan-ubsan
python3 -m unittest discover -s tools/script-cook -p 'test_*.py' -v
~~~

Bootstrap explicitly acquires the pin and builds the upstream compiler/analyzer
in a separate host process containing no Ludus code. Cook emits ordinary C++
records/trampolines, Luau authoring definitions, a checked manifest, compiler
inputs, bytecode and fingerprint evidence under out/script-interaction/cooked.
The door behavior passes the paired strict analyzer before compilation.
Deliberately invalid fixtures are separately identified runtime acceptance
inputs; they do not bypass the authoring admission gate for game assets.
Configure and build reject stale/edited cook inputs, outputs and host tools.

With the pinned Emscripten SDK prepared, run the same script using the
web-emscripten-development and web-emscripten-release presets, then:

~~~sh
node tools/script-interaction/browser.mjs out/build/web-emscripten-release/tools/script-interaction
~~~

Chromium uses the existing pinned Playwright setup in tools/web-browser-tests.
Run disables unrelated Web smoke/probe targets in that cache and selects Info
logging for observable conformance. Reconfigure those targets through their
ordinary workflows afterward. The wasm profile requires wasm exception-handling
support for longjmp; all engine/VM/adapter C++ still compile without C++ exceptions.
No additional device/platform support claim follows from these executions.

## Shared game contract

interaction.json owns operation ID 200, configuration/state/event schema IDs
100/101/102, field IDs, defaults, ranges, phase, capability, effect and signature.
The generator accepts this first version's three record roles and one
entity/uint32 command signature. It rejects unsupported types/effects, duplicate
IDs/keys, unknown keys and unsafe identifiers. It never parses C++ or executes
snippets. Generated declarations/calls let the compiler check the native signature.

The behavior increments declared Interactions by an event's Amount. At the
configured threshold it requests door opening, recording OpenRequested when
admission succeeds. Native behavior uses the same generated records, rules and
command facade. Its executable's Ninja command closure contains neither Luau
nor the scripting runtime.

EntityRef is tagged opaque userdata carrying world, session, execution, slot and
entity generation. Every command checks that full tuple through the real
GameplayWorld EntityRegistry, including pending-destroy rejection. Equality
compares identities across distinct wrappers. No native pointer or uint64 identity
travels as a Luau number; fixtures use session/execution values above 2^53.
Number decoding rejects nonfinite/out-of-range values before narrowing, then
rejects fractions. Numeric strings and forged tables fail. State facades carry
invocation epoch/instance identity; a retained facade expires. Configuration,
event, API, result, globals and metatable tables are frozen. The library allowlist
excludes host I/O, dynamic loading, environment replacement, GC control and
script protected calls that could catch interruption. Strict types aid authors;
runtime tags and validation enforce the boundary.

## Publication, faults and diagnostics

The game captures at most eight events, ordered by tick, phase, producer sequence
and instance creation order. It subscribes only to Gameplay; wrong tick/phase or
ambiguous ordering is rejected before invocation. It drains that captured batch
once. No recursive producer, async request, coroutine, timer, RNG operation or
structural command is exposed here.

Each handler receives copied configuration/event values and a candidate state.
Candidate command tokens do not advance the published sequence. Complete
state/reference/capacity preflight precedes allocation-free publication into fixed
buffers. A later native Gameplay application point rechecks references and
records explicit outcomes by token. Acceptance does not mean the door opened:
destruction before application reports InvalidEntity and preserves replacements.

A fault discards its invocation's unpublished state/commands, preserves earlier
successful publications and halts the incomplete tick. Subsequent handlers and
application stop. The VM is destroyed, including mutated transient upvalues.
Game checkpoint/restart policy must recover authoritative simulation; this
fixture creates a new world. S2 owns immutable program replacement and recovery.

Generated trampolines, contexts and wrappers are trivially destructible. Native
services are noexcept/non-reentrant and finish before the next VM call.
Allocating setup, marshalling and invocation are protected. No native resource
owner, lock, logger, destructor callback or scope guard lives in crossed frames.
The allocator uses FoundationMemory allocate/copy/free; rejection preserves the
old block and the 8 MiB limit/peak include the temporary old+new footprint.
This explicit first adapter is not a measured optimal realloc implementation.
Fault cleanup reclaims the heap before return. Diagnostics copy an existing
error string into 191 bytes without allocation/conversion, own their entrypoint
copy, and include asset, revision, execution, instance, entity, world, tick and
operation. Non-string errors can have empty text while status/context stay valid.

Acceptance covers equivalent effects/order, phase/capability checks, full
identity/stale generation, candidate capacity failure, later command rejection,
fault after staging, retained facades, numeric/forged inputs, immutable config,
nested VM entry, interruption, and rejection at every VM allocator call of a
baseline invocation with forced argument-stack growth and a 4096-element scratch
table; ordinary small calls can reuse existing VM pages without a host allocation.
This exercises marshalling and failure after effects are staged. Evidence retains
output, pin/cook/manifest hashes and executable sizes. These are conformance
fixtures, not representative performance measurements.

## Subsequent work and attribution

This slice supplies no installed scripting SDK, GameApi reload integration,
production package, persistence codec, general binding generator, DAP server,
visual editor or C# provider. S0 device evidence and workload/size budgets remain
production adoption gates. S2 owns package/dependency validation, source maps,
state capture/migration, debugger/reload and native-module retirement; see the
[implemented S2 slice](luau-s2.md) for its separate acceptance scope.
Authoritative mutable globals/upvalues remain unsupported authoring practice;
the strict checker alone does not prove that rule.

Thanks to Roblox Corporation for Luau's protected-call/sandbox contracts
([pinned source](https://github.com/luau-lang/luau/tree/1eca9fda3e4753a1592000f6cfdf659aaa778b7d),
[embedding documentation](https://luau.org/sandbox/)), and Waldemar Celes,
Luiz Henrique de Figueiredo and Roberto Ierusalimschy, “Binding C/C++ Objects to
Lua”, Game Programming Gems 6, §4.2, pp. 341–355, and Julien Hamaide, “Automatic Lua
Binding System”, Game Programming Gems 7, §7.1, pp. 503–516. Boundary checking,
host lifetime and generated bindings are adapted to copied identities and POD
trampolines. [The source review](scripting-gems-review.md) records consulted
chapters; no original binding implementation is copied.
