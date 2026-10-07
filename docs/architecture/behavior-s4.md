# S4: installed native/Luau behavior provider and project cooking

S4 promotes the reviewed interpreter bridge into the optional installed
`Ludus::Behavior` component. A project describes its own scalar configuration,
durable state, event fields and deferred operations in one closed manifest.
The paired cooker generates readable C++ wrappers, strict Luau declarations,
and an immutable package. Native callbacks and Luau handlers share the same
state validation and command admission path.

The default SDK remains C++/data only. S4 does not complete the Editor/GameHost
integration (S5), six-platform and remaining safety qualification (S6), or
representative performance and release acceptance (S7). The existing private
[S2 debugger](luau-s2.md) and [S3 graph workflow](visual-s3.md) remain useful
acceptance fixtures; their tools are not automatically part of this provider.
See the expanded [delivery roadmap](scripting.md#delivery-and-acceptance).

## Enable and install

Prepare the reviewed source and paired host compiler/analyzer explicitly:

```bash
./scripts/script-provider bootstrap
./scripts/script-provider cook
./scripts/script-provider run --preset linux-clang-development
./scripts/script-provider check --preset linux-clang-development
./scripts/install-sdk linux-clang-development
python3 tools/script-provider/verify_consumer.py out/install/linux-clang-development
```

`run` explicitly sets `LUDUS_BUILD_BEHAVIOR=ON` and
`LUDUS_BUILD_BEHAVIOR_ACCEPTANCE=ON`. For a provider SDK without fixtures,
configure with `LUDUS_BUILD_BEHAVIOR=ON` alone after preparing tools. Configuration
verifies existing source/tool receipts; it never downloads or cooks scripts.
The independent consumer test copies the SDK and project to separate directories,
checks real selectable configure/build/test presets using pinned CMake/Ninja/Clang,
cooks with installed tools, and executes a statically linked shipping program.
Machine paths live only in its temporary ignored `CMakeUserPresets.json`.

The optional payload includes Behavior's public header/archive, implementation-only
static archives, the portable cooker, paired host executables, profile fingerprint
and both upstream MIT notices under `share/Ludus/behavior` and
`share/Ludus/licenses/Luau` for the existing player release helper. Luau headers and private
runtime headers are not installed. Host tools execute on the SDK producer's host;
a cross-target SDK needs compatible host tools supplied explicitly to the helper.
Reinstalling with the component disabled prunes these owned paths. An ordinary
SDK cannot satisfy `find_package(Ludus REQUIRED COMPONENTS Behavior)`.

## Project-owned contract and build

The sample manifest in `tools/script-provider/fixtures/contract.json` declares
`Config.Threshold`, `State.Interactions`, `State.OpenRequested`, `Event.Target`,
`Event.Amount`, and two independently gated operations. IDs identify fields and
operations; names are stable authoring identifiers. The JSON schema is closed:
unknown keys, duplicate IDs/names, unsupported kinds and invalid numeric bounds
fail before compilation. Configuration and durable state support `uint32`/`bool`.
Events and operation arguments also support opaque `EntityRef`.

Limits are deliberate: sixteen fields per record, sixteen operations, four arguments
per operation, sixteen staged commands per invocation, eight programs per catalog,
128 KiB source per program, 256 KiB bytecode per program and 2 MiB per package.
A catalog has one contract and at least one entrypoint. These bounds make ownership
and failure reviewable; extending them requires a game requirement and measurement.

```cmake
find_package(Ludus CONFIG REQUIRED COMPONENTS Behavior)
ludus_cook_behaviors(NAME encounter
    CONTRACT "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/contract.json"
    PACKAGE "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/package.json")
add_executable(game main.cpp)
add_dependencies(game encounter_cook)
target_include_directories(game PRIVATE "${encounter_DIRECTORY}")
target_link_libraries(game PRIVATE Ludus::Behavior)
ludus_apply_app_policy(game)
```

The build target checks source/dependency closure each time, reusing a verified
completed cook on a cache hit. An explicit CLI path is also available:

```bash
./scripts/ludus scripts cook --contract contract.json --package package.json \
  --output out/cooked --profile /sdk/share/Ludus/behavior/profile.json \
  --compiler /sdk/share/Ludus/behavior/bin/luau-compile \
  --analyzer /sdk/share/Ludus/behavior/bin/luau-analyze
```

These commands operate on trusted project source and explicit existing tools;
project loading itself does not invoke them. Strict analysis precedes compilation.
Literal `require("0000000000000600")` imports must match the declared catalog,
resolve to modules, and form an acyclic graph. The bounded import profile rejects
backtick interpolation explicitly rather than guessing at imports inside nested
expressions; ordinary quoted/long strings and comments are lexed correctly. Source must remain under the package
root after symlink resolution. Cache identity covers the complete source/import
closure, contract, paired tool hashes, generator and compiler options.

A completed directory contains `contract.h`, `contract.d.luau`, `package.h`,
`behavior.lupack`, and `evidence.json`. Generated `MakeConfig/State/Event` and
`TryReadConfig/State/Event` copy typed records using stable field IDs, preserving
outputs on decoding failure. Each operation gets an ordinary inspectable C++
wrapper. `package.h` embeds the trusted package as a fixed byte array for static
shipping; compiler, analyzer, filesystem and authoring JSON are not runtime
requirements. Evidence includes source/bytecode hashes and source first-line
offsets after injected type declarations. Artifact readers acquire the completed
immutable directory via the atomically replaced `current.json`; failed analysis,
compilation or integrity checks leave the previous pointer and forwarding headers.
Concurrent cooks must use separate output directories or an owner-serialized build.

## Runtime ownership and effects

Include only `ludus/runtime/behavior/behavior.h` and the generated contract/package
headers. Create a `LuauProvider`, load a trusted package and borrowed immutable
contract, then invoke it synchronously on its owning thread. Contract descriptors,
strings and package bytes must outlive the active VM. No source acquisition occurs
inside `Load`. Candidate loading preserves the active VM on failure; old and new
VMs may coexist during preparation. The heap limit applies separately to each VM,
including realloc peaks; fixed provider metadata is outside that budget.

A handler returns a function with this signature:

```lua
--!strict
return function(config: Config, state: State, event: Event, api: Api)
    state.Interactions += event.Amount
    if state.Interactions >= config.Threshold then
        local result = api.RequestDoorOpen(event.Target, config.Threshold)
        if result.Status == "Accepted" then
            state.OpenRequested = true
        end
    end
end
```

Configuration and event tables are frozen copied values. State is an execution-scoped
facade over an unpublished declared record. Capturing the facade and using it in a
later invocation faults because its epoch expired. VM globals, closures and caches
are transient; only the declared record is a checkpoint. Native handlers use
`ExecuteNative` and the generated wrappers through the same `Transaction` checks.
Native C++ remains trusted and outside the VM sandbox.

Every invocation carries asset/revision, instance, world/session/execution, tick,
phase and capability bits. Entity identities stay opaque and never become floating
point numbers; host `Services.IsAlive` validates slot/generation after scope checks.
This service must be read-only, bounded, synchronous and nonreentrant. The provider
rejects nested invocation/load/close before accessing borrowed call data.

Operations stage copied commands only. Admission checks exact arity/type, numeric
bounds, live entity scope/generation, phase and capability. Rejection returns an
explicit status and appends nothing. Tokens are one-based within the invocation.
On success the owner receives a candidate state plus ordered commands and must
publish/apply them together at its safe point. The provider never mutates the world;
application failures and irreversible game side effects remain the owner's policy.
Every failure preserves the caller's `Outcome`. Script errors and safepoint
interruption retire the VM and discard all unpublished state/effects; an explicit
load is required to resume. A bounded copied diagnostic retains operation/status
and runtime error context, without exposing VM pointers.

`EncodeState`/`DecodeState` use versioned little-endian stable-ID scalar records with
the exact contract digest, never native object layout. Decoding rejects trailing
bytes, duplicate/missing IDs, kinds and bounds. `MigrateState` is an explicit owner
policy: new fields use defaults, unchanged fields retain validated values, and
removed fields/kind changes/out-of-range data reject the candidate. The owner must
validate and decode the old checkpoint against its old contract before migration.

## Trust and acceptance

Only owner-admitted bytecode produced by the paired cooker may enter this API.
The profile/contract fingerprints, package key and output hashes are compatibility
and corruption checks, **not authentication or a hostile-bytecode verifier**.
Package metadata is bounded and checked before VM allocation/loading. It retains
the S0 reviewed loader patch and S1 protected POD bridge; engine/runtime code has
C++ exceptions disabled. No JIT, arbitrary C callback registration, raw VM API,
engine pointers, coroutine continuation or language container crosses the public
surface. The S0 trampoline/device/profile audit gates remain S6 work; this opt-in
component does not grant production approval to unqualified platforms.

Acceptance includes native/Luau state and command equivalence, multiple operations
and dependency programs, high 64-bit entity scopes, invalid context/phase/capability,
command capacity, binding/state faults after staged effects, expired state facades,
loops, OOM and failed package retention, reentrant close, stable-ID codecs/migration,
strict cooker rejection, dependency invalidation, paired tool tampering and failed
publication. CI runs the provider on native Development and ASan/UBSan, plus pinned
Web Development/Release in Node. Node is Web runtime evidence, not browser/device
product acceptance. The relocated installed-SDK project proves offline cooking,
selectable presets, GNU linker dependency ordering and static shipping execution.
The player inspection also admits the already required glibc x64 interpreter when
it appears as a direct ELF dependency; unknown runtime libraries remain rejected
([Linux man-pages, ld.so(8)](https://man7.org/linux/man-pages/man8/ld.so.8.html)).
It installs a stripped player component, checks its Linux runtime dependencies and
notices, creates a ZIP without SDK/tools/source, verifies its inventory after clean
extraction, and runs that extracted executable. This Development-SDK smoke archive
is compatibility evidence; S7 still owns representative Release qualification.

## References and deliberate departures

Thanks to Waldemar Celes, Luiz Henrique de Figueiredo and Roberto Ierusalimschy,
“Binding C/C++ Objects to Lua”, *Game Programming Gems 6*, §4.2, pp.341–355:
checked copied identities and scoped facades separate native and language ownership.
Thanks to Julien Hamaide, “Automatic Lua Binding System”, *Game Programming Gems 7*,
§7.1, pp.503–516: generate inspectable bindings from an explicit contract. The adopted
ideas and consulted pages are recorded in the [Gems review](scripting-gems-review.md).
This implementation is original; it generates ordinary SDK-only wrappers and strict
types rather than exposing arbitrary native object methods or adopting legacy Lua
runtime code. Thanks to Roblox's [sandbox embedding guidance](https://luau.org/sandbox/):
the owner admits paired compiler output and runtime capabilities remain explicit.
