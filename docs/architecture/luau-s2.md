# S2: dependency cook, source debugging and transactional replacement

S2 extends the private S1 two-instance door interaction with immutable dependency
bundles, real Luau breakpoints/locals/steps, a declared-state codec, whole-VM
replacement and native GameHost retirement. `LUDUS_BUILD_SCRIPT_SESSION` defaults
to OFF. The runtime and generated catalog are not installed in the SDK. The
optional GameApi 1.1 debugger callback is installed as a small provider-neutral
contract. S0's exception-free interpreter review and missing device/performance
acceptance remain production gates.

## Cook and trust boundary

Run from a prepared checkout:

```bash
./scripts/script-session bootstrap
./scripts/script-session cook
./scripts/script-session run --preset linux-clang-development
./scripts/script-session check --preset linux-clang-development
./scripts/script-session run --preset linux-clang-asan-ubsan
./scripts/script-session run --preset web-emscripten-development
./scripts/script-session run --preset web-emscripten-release
node tools/script-session/browser.mjs out/build/web-emscripten-release/tools/script-session
```

Bootstrap uses the pinned, paired S1 compiler/analyzer and reviewed S0 sources.
Cook accepts a closed bounded catalog: at most eight packages and eight programs
per package, two entrypoints per cohort, direct source basenames, no path escape,
64 KiB sources and 256 KiB bytecode. Imports must be literal lowercase 16-digit
asset IDs and exactly match declared edges. Missing, cyclic, duplicate and
non-module dependencies fail before initialization. The lexer ignores comments
and strings; aliases/dynamic imports are rejected. The analysis view rewrites
literal identities to local analyzer module paths; emitted source preserves
identities and line counts. Every program passes strict analysis before admission.

A program key includes source, transitive dependency keys, schema, binding
manifest, paired tools, runtime pin and interpreter O1/g2 profile. The package key
also includes revision and state range. Changing an imported constant invalidates
both unchanged entrypoint sources. Validated content-addressed bytecode can be
reused on subsequent cooks; strict analysis still runs. Cook builds in a temporary
directory and publishes only after success, retaining the previous cooked output
on analysis/compiler failure. Build verification fingerprints inputs, paired
host tools, bytecode, generated catalog and maps. Tampered cache output is rejected.

`out/script-session/cooked/<package>/bundle.json` records identities and bytecode
hashes. `maps.json` is a separate debug artifact mapping compiled lines to the
original authored file, source hash, line count and program key. This debug-only
profile retains g2 data; it is not a shipping stripping/transport profile.

The runtime accepts only the compiled trusted catalog through the Session owner;
a checksum is not authorization to load arbitrary bytes. Runtime loading checks
unique assets and dependency-first ordering again. Initializers receive no world
or service facade. `require` exists only during initialization and resolves only
the current program's declared earlier modules. S2 module exports are frozen,
flat primitive constants; nested mutable objects/functions are rejected. Root
functions can use local helpers. General module exports are future work.

## Pause and inspect

Each debug invocation owns a rooted Luau thread. Actual `lua_breakpoint`,
`debugbreak`, `debugstep`, `lua_break` and `lua_resume` implement source stops and
continue/into/over/out. Steps compare source, line and stack depth. A stopped
invocation keeps its POD transaction alive; authoritative state and command
publication wait until successful completion. A debug pause is a partial tick,
not a checkpoint or reload boundary. Closing a stopped VM cancels that transaction.

Inspection copies at most eight frames and sixteen top-frame locals inside the
protected VM callback. The bounded JSON reply exposes at most four frames/eight
locals and reports truncation. Nil, booleans, finite numbers and up to 64 raw string
bytes (hex) are copied; tables, functions and userdata are opaque. No pointers,
metamethods, getters or expression evaluation enter the response. Nonfinite values
have a separate kind and no JSON nonfinite number. Source maps identify authored
and compiled lines. Session, execution, stop, world, asset, instance and tick IDs
use fixed-width hex strings, preserving IDs above 2^53.

Requests carry version 1, action, session, execution, revision and stop. `stop`
must match the current stop sequence, or zero when not stopped. Supported actions
are inspect, breakpoint (asset/line/enabled), continue, into, over, out and reload
(package/expected). Unknown fields, malformed IDs, stale revisions/stops,
unsupported watches and unavailable commands are rejected before mutation.
Breakpoint replies return the actual resolved executable line. Runtime faults
return explicit diagnostics, discard unpublished effects and halt the fixture;
S2 does not implement an interactive break-on-error stack or arbitrary watches.

Native clients send protocol v1 `ScriptDebug` over the existing GameHost control
connection, through the shared supervisor as well, with `expected_generation`, extension 1 and a bounded UTF-8 JSON
`payload` string. The usual session/epoch/request ledger and generation check
apply. The response is a CommandResult whose `message` contains the provider
JSON. `Status` reports `script_paused`. The API tail is gated by
`Capability::ScriptDebug`, negotiated size and ABI minor 1. Providers use
`Status::ScriptPaused` from Update to suspend the host tick; the host continues
control/platform pumping, with no SimTicks/FrameIndex advance. Ordinary simulation
commands and native reload are Busy until script resume or Stop. Other providers
need not implement or link Luau.

## State and replacement

Only a completed EndTick with no pending invocation or commands is a safe point.
Prepare checks expected package key, increasing revision, compatible compiler/
binding pin and schema direction. It migrates both declared states by stable field
ID into POD candidate records, then initializes an entirely fresh VM. Init, OOM,
schema/range or trust failure destroys the candidate and retains the active VM,
execution identity and both states. An intervening authoritative tick cancels a
staged candidate. Commit rechecks expected identity/tick/execution, swaps the
whole cohort without allocation, advances execution identity and closes the old
VM. Every callback, upvalue, thread root and userdata from that VM is retired.
Breakpoints are revision-scoped and must be set again after replacement.

The state codec is explicit LE: magic/version/schema/count (four uint32s), then
stable ID/kind/value (three uint32s) per field, up to sixteen fields. Booleans are
exactly 0/1. Decode/migrate preserve outputs on failure; duplicate IDs, bad kinds,
invalid ranges, kind changes and removed fields are rejected. New declared fields
use validated defaults. This slice has uint32/boolean fields and schema 1→2;
logical asset/entity references, timers, waits and subscriptions are not yet
exposed and are not serialized as VM pointers or coroutine stacks.

The fixture checkpoint is 168 bytes: LE magic/version, session/tick, 64-byte
package key and two 40-byte tagged state records. Restore validates the entire
record into temporary state before publishing and rebinds fresh native entities.
GameHost adds its normal project/game/generation/schema/body-digest envelope.
Native modules own both VM banks inside their instance; Quiesce gates new work
without destroying a stopped invocation, and Destroy closes both before the
loader releases the module image. No callback or asynchronous lease escapes this
synchronous fixture. Future external workers must participate in native work
leases; this does not prove a general asynchronous provider implementation.

## Acceptance and limits

The same executable runs on Clang and Node/Wasm. It verifies stable-tag codecs,
output-preserving rejection, two-instance migration, changed import behavior,
failed initialization, narrowing rejection, forged-catalog rejection, stale
prepare/commit, genuine breakpoints/primitive locals/stepping, partial-tick gates,
repeated replace/stop and zero live VM bytes. Allocation failure is injected at
every observed candidate-staging allocation and every observed debugger setup/
local-capture allocation. S1's synchronous contract remains separately tested.

The native socket acceptance uses a real headless GameHost process, the same
control connection and two separately loaded module images. It checks responsive
Status/inspection with frozen host ticks, reload rejection while stopped, resume,
checkpoint migration into a new module, and VM retirement before module release
on reload, Stop and disconnect. ASan/UBSan covers these paths. Chromium drives the
same bounded JSON control API through Wasm exports, with a timer heartbeat during
a real source stop, unchanged tick/state, source locals and replacement behavior.
The Wasm acceptance executable reserves a checked 1 MiB native stack for its
multiple simultaneous test owners; this is a fixture setting, not a runtime
performance budget. Evidence is saved in `out/browser-qa/results/luau-s2.json`; CI retains the Web
executable, map/cook evidence and test output.

This milestone is an experimental vertical slice. It adds no installed scripting
runtime, DAP server/editor debugger UI, arbitrary binding/package loader, shipping
bytecode acquisition, asynchronous wait support, visual graph editor or C#
provider. The public ABI extension and private ownership proof are foundations
for those integrations. Performance/size claims require representative workloads
and device evidence beyond these conformance tests.

Thanks to Roblox Corporation for the pinned Luau protected-call/debug/sandbox
contracts ([source](https://github.com/luau-lang/luau/tree/1eca9fda3e4753a1592000f6cfdf659aaa778b7d),
[API](https://luau.org/api/), [sandbox](https://luau.org/sandbox/)); Waldemar Celes,
Luiz Henrique de Figueiredo and Roberto Ierusalimschy, “Binding C/C++ Objects to
Lua”, Game Programming Gems 6, §4.2, pp. 341–355, for host lifetime separation;
and Julien Hamaide, “Automatic Lua Binding System”, Game Programming Gems 7,
§7.1, pp. 503–516, for generated binding/schema discipline. The implementation is
original and adapts these ideas to bounded POD trampolines and a whole-VM swap.
See the [Gems review](scripting-gems-review.md) and
[GameHost reload design](../architecture/project-live-reload.md).
