# S3: a visual encounter authoring workflow

S3 implements one bounded structured sequence workflow for the S1 two-door
encounter. A browser workbench owns authoring controls and a local POSIX tool
owns the real S2 Luau session. Designers can edit, review, save, test, pause at a
node, inspect copied locals, step, continue and replace behavior while retaining
both instances' declared state. `LUDUS_BUILD_VISUAL_SEQUENCE` defaults to OFF;
the workbench, bridge and graph model are private tools, never SDK exports.
The [scripting architecture](scripting.md) owns the broader visual design;
[S2](luau-s2.md) owns VM pause, state migration and retirement contracts.

## Use the workbench

From a prepared checkout:

```bash
./scripts/visual-sequence bootstrap
./scripts/visual-sequence cook
./scripts/visual-sequence run --preset linux-clang-development
./scripts/visual-sequence check --preset linux-clang-development
./scripts/visual-sequence serve
```

`serve` prints a loopback URL. Open it in a browser. Its default document is an
editable example copied once to `out/visual-sequence/encounter.json`; it preserves
that file on subsequent runs. Pass `--document /path/to/encounter.json` to own an
existing authored file. Serving checks for an already built native bridge; it
does not bootstrap or build implicitly. A cook uses the already prepared paired
compiler/analyzer. `bootstrap` explicitly prepares S1/S2 tooling and fixtures.

1. Select **Cook & replace**, then **Interact · Door 1**. Its count becomes one;
   the other instance remains unchanged.
2. Select **If count reaches threshold**, then **Break on selected node**.
   Interact again. The node highlights and the live state stays at one: changes
   in the paused invocation are unpublished. **Step over** reaches the native
   operation node. **Continue** publishes the tick and opens the first door.
3. Edit the threshold inline from two to three. **Review changes** reports its
   stable identity and value change. The running source remains revision one
   until **Cook & replace** succeeds. Existing state is retained. The second
   door now needs three interactions.
4. Drag a node, then cook. Layout changes preserve the executable key and VM
   revision. Copy a subtree and paste it into a selected block to create fresh
   node/port IDs. Undo/redo and earlier/later controls preserve explicit child
   order. Schema limits reject invalid edits without adding history.
5. **Save** atomically writes authored semantics/layout. Runtime counts, entities,
   command tokens, coroutine state and debugger snapshots never enter that file.
   **Reset preview** retires the owned process/VM and starts a fresh preview owner.

The running and draft Luau tabs are read-only explanatory output. Editing a
node while paused changes the draft; replacement remains disabled until the tick
finishes. A source stop maps through the active revision's spans even when the
draft differs. Source breakpoints are revision-scoped and need setting again
after replacement. Block nodes with no emitted executable line have no breakpoint.

## Canonical document and authoring ownership

The version-one JSON document contains a graph ID, root, two declared variable
IDs, flat node records and separate layout metadata. IDs are nonzero, lowercase
16-digit hex strings; graph, variable, node and port identities cannot collide.
The two state fields match S1's generated manifest: `Interactions` (uint32) and
`OpenRequested` (boolean). Ports carry structured control flow. The native call's
`EntityRef`/uint32 arguments, capability, phase, command effect and result are
shared with S1's operation manifest, which also feeds the workbench inspector.

Node records have kind/version, in/out port IDs, explicit ordered child IDs and
their typed literal. This vocabulary is `Sequence`, `Increment`, `If`, bounded
`Repeat`, `DoorOpen` (operation 200), and `Return`. Conditions test the count and
pending-open flag; the door call sets `OpenRequested` only on `Accepted`. Other
results leave that flag unchanged and appear in native result inspection.
Execution order is the structured child order shown in the outline and flow view.
A `Return` terminates the current handler, including from a nested repeat.

The validator admits at most 64 nodes, 16 children per block, depth eight,
128 KiB authored JSON, repeat counts 1–4 and two worst-case native commands per
invocation. It rejects duplicate/missing identities, cycles, multiply owned or
unreachable nodes, unknown fields, malformed types, range errors and excess
command budgets. The existing native numeric/state range checks remain active;
a handler fault discards unpublished effects and requires a preview reset.

Canonical serialization sorts records by stable ID and retains authored child
order. Positions, zoom and comments are outside executable semantics. Node moves
and record-array reorder preserve generated code/spans and the semantic key.
Paste recursively assigns fresh IDs to every node and port. Typed edits are
validated on a candidate document before publication; a bounded 64-entry command
history stores accepted before/after records for exact undo/redo. Rejected edits
preserve the draft, revision and history.

Semantic review reports added/deleted records, literal/operation changes and
execution-order changes separately from layout metadata. Three-way merge uses
the saved base, current draft and uploaded branch document, matching records by
stable ID. Independent field/layout changes merge. Competing values/order,
identity collisions and delete-versus-edit conflicts are reported without changing
the draft. The merged document passes the full validator before admission.
If disk changed externally, merging that current disk document updates the saved
base/stamp; another branch cannot silently authorize overwriting unseen disk edits.

Save checks the last observed disk contents twice, rejects symlink leaves,
preserves file permission bits, and replaces a flushed temporary file in the same
directory. This is optimistic conflict detection, not a filesystem-wide writer
transaction. **Reopen saved** explicitly discards the draft when requested and
never replaces the running VM automatically.

## Lowering, cook, maps and trusted admission

The Python model is the authoritative authoring/compiler schema. The browser
sends typed commands; it does not independently compile or evaluate graphs.
Structured blocks lower directly to strict Luau handlers in authored order. A
repeat emits a bounded `for`; early return uses a local `do` block so later
siblings remain syntactically valid. Every emitted node line records graph/node/
port IDs and authored/compiled line numbers. S1's generated type definitions
precede source and are included in the compiled-line offset.

The key covers canonical executable semantics, lowering/cook source, paired
compiler/analyzer identity, runtime pin, generated definitions, binding manifest
and O1/g2 interpreter profile. Layout and the owner-supplied execution revision
are excluded. A successful strict-analysis/paired-compiler result publishes a
content-addressed cache record atomically. Cache reuse checks the code digest and
bounds; the cache is part of the trusted local tool filesystem. Neither that
checksum nor the artifact's key authenticates arbitrary bytecode.

Only the owned cooker supplies artifacts to the native bridge. Browser requests
cannot name source paths, process commands, bytecode or arbitrary packages. The
loopback server binds `127.0.0.1`, checks exact Host/Origin and a per-session bearer
token, exposes no CORS, and bounds input/time/replies. Every authoring action checks
the document revision; preview actions also check an owner epoch and active
execution/revision/stop/package cursor. A delayed control from a retired preview
cannot resume a new invocation. Server shutdown closes the VM and child pipes
before retiring the process.

S2's private Session now accepts a borrowed owner-admitted catalog, retaining its
original compiled S2 catalog by default. The S3 owner has two fixed bytecode/
package slots. Active/staged storage stays immutable and alive until VM retirement;
only a retired inactive slot can be reused. The bridge validates profile/contract,
revision, bounded artifact fields and expected active identity. It stages a fresh
VM, migrates both declared states, commits at a completed tick and then retires
old callbacks. Partial tick, stale cursor, incompatible pin, or candidate OOM
retains the active revision. No universal graph IR, graph interpreter, hot patching
of closures or installed runtime acquisition API is introduced.

## Evidence and scope

```bash
./scripts/visual-sequence run --preset linux-clang-asan-ubsan
./scripts/visual-sequence run --preset web-emscripten-development
./scripts/visual-sequence run --preset web-emscripten-release
node tools/visual-sequence/browser.mjs
```

The same bridge runs native and Node/Wasm acceptance. Tests compare separately
authored text and generated graph state, tick order, command identity/order,
target slot, power, token and native outcome. They cover real node stops, mapped
steps, partial-tick rejection, changed behavior with retained state, incompatible
candidate/OOM rejection, repeated storage-bank reuse, bounded repeat/early return,
zero VM bytes on close and disconnect while paused. ASan/UBSan covers native paths.
Owner tests cover stale draft/preview controls, fresh epochs, failed save/conflict/
symlink preservation, layout without VM replacement, and runtime state exclusion.

Chromium drives the real workbench and local native owner through author, review,
debug, replace, drag, copy/paste, undo, save and reset. Timers keep running during a
source pause while tick/state remain frozen. CI records screenshots and JSON
results under `out/browser-qa/results/visual-s3*`; native Development and sanitizer
jobs exercise S3, and browser builds validate both Wasm configurations with pinned
static analysis. This frontend is a standalone workbench; its local owner performs
compilation and execution. Node/Wasm tests prove the same generated artifacts and
VM behavior on the browser toolchain.

This milestone adds one useful synchronous encounter sequence. Named text helper
calls, waits/subscriptions, general state-machine authoring, parallel/data graphs,
project asset-browser integration, DAP/editor integration and shipping acquisition
remain future work. No C# provider, arbitrary-language roundtrip, SDK exposure or
mobile/device performance claim is made. S0's production gates still apply.

Thanks to Roblox Corporation, [Luau API Reference](https://luau.org/api/) and
[Sandboxing, Bytecode](https://luau.org/sandbox/), for the protected debugger and
paired-compiler trust contracts. Implementation is original and reuses the
attributed [S1](luau-s1.md)/[S2](luau-s2.md) native ownership/binding design.
