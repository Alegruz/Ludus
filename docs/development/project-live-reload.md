# Native gameplay live editing

The first supported host is Linux x64 with the pinned Clang 18 toolchain and a
matching Debug or Development SDK. Native gameplay libraries use `.so` on this
host. Windows DLL loading remains a separate backend and is not implemented.

The editor reads project metadata without loading game code or preparing a
build. It reports missing/stale CMake setup through the shared setup validator.
Install tools/SDKs and repair configuration explicitly before building. The
game's ordinary CMake project must expose selectable configure, build and test
presets. Machine-specific SDK paths belong in ignored local settings.

## Set up an external project

Use [the SDK workflow](project-sdk-workflow.md) to install a compatible SDK.
For an engine-development prefix, run:

```bash
ludus project engine ./MyGame --sdk /absolute/path/to/sdk --profile linux-clang-development
ludus project repair ./MyGame --tools /absolute/path/to/Ludus --sdk /absolute/path/to/sdk
ludus project check ./MyGame --tools /absolute/path/to/Ludus
```

The reference `examples/live-edit-game` is an external consumer. Its committed
lock is deliberately unresolved until a real SDK release is selected; it does
not contain a fabricated archive digest. A local override is explicit and does
not rewrite that lock. The example includes a real shipping smoke test.

The Editor's **Initialize / Repair / Update Setup** uses this same backend.
It preserves custom presets/settings, creates marked local presets, refreshes
stale caches and verifies an actual configure/build/test. Play builds select
the same owned CMake and presets. Repair/project creation are disabled while a
play session owns the workspace; stop and confirm cleanup first.

Use `ludus_add_game(NAME sample_game SOURCES src/game.cpp)` from the installed
SDK to build the gameplay MODULE, a Qt-free project host and a statically linked
shipping executable from one gameplay implementation. Add `ludus.play.json`:

```json
{
  "version": 1,
  "host_target": "sample_game_host",
  "module_target": "sample_game_module",
  "startup_document": "game.tuning.json",
  "watch_roots": ["src"]
}
```

Targets are resolved through CMake File API. The tooling never guesses a library
filename. The host creates its own Platform/Input/RHI session and native window;
the editor communicates with a separate bounded supervisor. Gameplay libraries
link only the small GameApi contract and receive explicit host services.

## Play and change code

Open the saved project, then choose **Play → Build and Play**. The build publishes
a new immutable generation containing the module, matching host, embedded debug
information and verified manifest. It validates SDK identity, symbols, hashes
and declared source inputs before launching code.

**Build and Reload Code** builds while the current generation continues playing.
**Automatically Reload Source Changes** is opt-in. It debounces notifications
for 450 ms and retains one newest pending rebuild while a build is active. Stop
disables the watcher. Watch notifications are hints; publication and activation
still validate source inventories/digests. Watch roots must stay in the project;
the editor supports at most 4096 watched paths. The publisher separately bounds
declared inputs to 16384 files/64 MiB. Generated/external inputs are not a
hermetic dependency graph; declare the inputs the game actually consumes.

The host replaces code between frames through Validate, Quiesce, Snapshot,
Stage, Commit and Retire. An ordinary pre-commit error preserves the old instance
and its checkpoint. Explicit tagged checkpoint schemas define migrations;
unsupported changes require a restart. Paused state survives successful reload.

Modules must cooperate with retirement. Create/staging cannot start deferred
work. Acquire a work lease before publishing a code reference, join workers in
Quiesce, and release leases after the work has completed. Worker release alone
does not prove it has returned out of module code. Only the appended work-lease
services are thread-safe; check the service table's StructSize before reading
them. Failed quiescence or live references retain code, instance and service
storage until process exit; the host reports CleanupUnknown/RestartRequired.

## Edit values and configuration

The inspector receives copied typed values and descriptors. Edits carry the
module/schema generation and expected object revision. Rejected batches do not
partially mutate state. **Undo/Redo Session Edit** uses conditional revisions;
refresh after a conflict. Ordinary session edits do not write files.

**Apply to Tuning Document** explicitly copies a persistable value into the
independent tuning draft. Save, document Undo/Redo and Discard are explicit.
Atomic saves detect disk conflicts. Stop preserves an unsaved tuning draft and
does not silently persist simulation state. A saved document is validated and
encoded into a bounded versioned payload for the next Create.

**Reload Frame-clear Configuration** accepts a project-relative JSON file:

```json
{ "version": 1, "kind": "frame_clear", "rgba": [0.1, 0.65, 0.25, 1.0] }
```

Tooling validates/imports it into an immutable 24-byte cooked artifact, with
distinct source and cooked digests. The host validates its format/digest/ranges
and swaps a copied configuration between frames through the public RHI
SetFrameTarget path. GPU commands retain no pointer into that CPU configuration;
there is no old GPU resource to destroy. Invalid imports preserve the previous
configuration. Texture/model/audio/shader importers are unsupported until their
public resource APIs exist. The ABI's reserved Uniform channels currently do not
render a shader effect.

## Recover and debug

Pause/Step/Resume operate on the host's simulation clock, with no catch-up after
a pause. Stop cancels build/query work and supervises owned descendants. A native
crash or hang affects the game process; documents remain in the editor. A hang
produces a diagnostic and awaits explicit Stop. A debugger-stopped process is
not killed for a missing heartbeat.

Refresh Session Details and Copy Job Details record session/generation, actual
native host PID, SDK identity, presentation counts, outstanding allocations/work,
exact host argv/cwd, phases and outcomes
without uploading data. Duplicate mutation requests reconcile retained outcomes;
old session/schema requests reject. Busy requires draining/acknowledging or
retrying from refreshed state. CleanupUnknown requires an explicit host restart.

Debug symbols stay with leased generations. Separate GDB launch and attach
acceptance cases verify A/B source breakpoints and stepping in one inferior,
with A retained while its frame is stopped. The supervisor observes traced
stopped threads and rejects reload before or after candidate Query; Continue in
the debugger before requesting reload again. The editor disables live mutations
and reload while the stop is observed. Explicit Stop can still force process
cleanup. The attach harness respects Linux ptrace policy without changing
system settings. Editor-integrated debugger launch/attach, RAD and other OS
loaders require additional acceptance; use the existing executable/debugger
workflow or a manual restart where symbol refresh is limited. See the
[evidence ledger](project-live-reload-evidence.md) for actual verified scope.
