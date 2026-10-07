# S5: Editor authoring, behavior debugging and GameHost generations

S5 connects the optional installed Behavior provider to the actual Qt Editor and
project-owned GameHost modules. `examples/scripted-game` is an SDK-only project
with equivalent text and structured-sequence assets, an explicit contract, paired
cooking, an editor module, and a statically linked shipping player. Press Space
in its game window to invoke the sequence; the Editor's Interact button exercises
the same provider transaction. An accepted door command changes the clear color.

This milestone provides a bounded desktop workflow. Six-platform/device/browser
qualification remains S6; representative designer, scale, performance and release
qualification remains S7. C# and arbitrary visual graph vocabularies remain optional
future work. The browser Editor can read these widgets but cannot launch desktop
processes or acquire tools implicitly.

## Prepare and use

Prepare the optional paired compiler/analyzer, then enable both the Editor and
Behavior component with the reference tools. Explicitly disabling saved init
options is useful when an existing initialization recorded the Editor as disabled:

```bash
./scripts/script-provider bootstrap
./scripts/script-provider cook
out/host-tools/venv/bin/cmake --preset linux-clang-development \
  -DLUDUS_USE_INIT_OPTIONS=OFF -DLUDUS_BUILD_EDITOR=ON \
  -DLUDUS_BUILD_BEHAVIOR=ON -DLUDUS_BUILD_BEHAVIOR_ACCEPTANCE=ON \
  -DLUDUS_WARNINGS_AS_ERRORS=ON
./scripts/build linux-clang-development
./scripts/install-sdk linux-clang-development
./scripts/ludus project repair examples/scripted-game --tools "$PWD" \
  --sdk "$PWD/out/install/linux-clang-development" --no-web
out/build/linux-clang-development/apps/editor/ludus_editor
```

Open the project's `ludus.project.json`, then its Scripts tab. Loading only reads
bounded project metadata and assets. A missing or stale SDK/tool/preset setup is
reported by the existing read-only project setup check. Use explicit Setup/Repair
before cooking or playing. Machine paths remain in ignored local settings and
`CMakeUserPresets.json`; no paths belong in the committed project presets.

`ludus.scripts.json` names one cook target and project-relative contract/package
manifests. Its closed version-one schema is shared with the Python adapter.
The package chooses `source` or `graph` for each stable asset. Both use the same
installed paired cooker and `ludus_cook_behaviors` CMake helper. CLI callers can
use `ludus scripts cook` with the same six explicit path arguments documented in
[S4](behavior-s4.md). The Editor uses the selected SDK's cooker, compiler, analyzer
and profile, under its existing cancellable process supervisor and build-tree lock.

The text editor retains a draft and ordinary undo/redo. The sequence view exposes
stable node IDs, explicit child order and typed Increment/If/Repeat parameters,
with bounded undo/redo. The Editor document Save/Undo/Redo commands follow the
Scripts work area; pending graph-cell text is committed before save or draft
confirmation. Save rejects an external file change, uses atomic replace,
and retains the draft on failure. Switching assets/projects or launching a build
asks how to resolve a dirty draft. Structural graph editing remains in the
[S3 workbench](visual-s3.md); S5 does not introduce a second graph execution VM
or promise arbitrary graph/text round trips.

## Cook, activate and debug

Cook prepares an immutable candidate and reports strict-analysis/compiler errors
in the existing Output panel. It does not activate code. Build/Reload uses the
existing immutable native generation publication and GameHost reload protocol.
Invalid source, failed builds and incompatible state schemas retain the running
generation. State migration uses the S4 declared-state codec and exact contract
digest; the game checkpoints its counters and input edge state as explicit
little-endian fields, including the execution counter so prior restarts cannot
alias a replacement generation. Restart behavior is a separate operation that retires the
partial invocation, creates a fresh VM, resets declared state, and changes the
execution identity.

Inspect obtains a fresh cursor. Select a source line or sequence node, set a
breakpoint, and Interact or press Space. Continue, Into, Over and Out resume an
exact stop. The inspector copies bounded frames and primitive locals; active
source/node highlighting uses the cook's generated source spans. New stops have
monotonic identities across VM loads of a provider owner. No Lua object pointer
or stack reference escapes into Qt.

A pause holds copied invocation inputs, candidate state, command buffer and bridge
context until completion or Close. Simulation does not publish the partial tick.
GameHost exposes `script_paused`; native reload is unavailable until the invocation
completes or explicitly restarts. Completed invocations publish validated state
and admitted commands once. Faults discard the candidate and retire the VM;
explicit restart or native reload is required to run again.

The existing ABI 1.1 ScriptDebug channel fences the outer play session, project
epoch, request and native generation. The example also checks inner game-session,
execution and stop identities. Inspect is read-only; mutations require its exact
cursor. Payloads and replies stay within 4 KiB. The example replies with at most
four frames and eight primitive locals; the provider's copied snapshot has eight
frames and sixteen locals. Commands never transport source or bytecode.

Breakpoint/highlight mapping requires the active native package key to match the
completed cook and the authored source fingerprint to match the displayed asset.
Saved-but-uncooked edits and cooked-but-unreloaded candidates cannot direct stops
into another program version. Graph fingerprints exclude layout and sort identity
collections; executable child order remains significant. The shared lowering
currently adapts S3's two logical state slots to the generated named contract fields
`Interactions` and `OpenRequested`, and requires `Target`, `Amount` and operation
200 `RequestDoorOpen`. Other project contracts can use text handlers; a broader
visual vocabulary needs a separate contract/version change.

## Ownership and evidence

Native provider and JSON bridge archives own their position-independent link
closure; private feasibility probes are not prerequisites for installed game
modules. Each native module generation owns its provider and static cooked package/maps.
Prepared replacement builds a separate candidate, restores validated state, then
commits through GameHost. Destroy closes the VM and asserts zero live VM bytes
before returning; GameHost releases the native module lease only after callbacks
and the old owner retire. Qt never owns a VM or a native callback address.

The normal offscreen Editor suite tests drafts, undo/redo and external-save
conflicts. `tools/script-provider/test_cook.py` tests shared graph lowering, maps,
layout-only cache reuse, strict failures, vocabulary rejection and read-only path
planning. Provider tests exercise real pauses, retained copied inputs, unpublished
outcomes, stale stops across replacement and Close during a partial invocation.
The explicit S5 journey copies the SDK-only example into a fresh temporary project
and drives actual Qt controls, setup, cooking, play, stepping, failed-build retention,
native reload/state preservation, stale-command rejection and VM retirement:

```bash
QT_QPA_PLATFORM=offscreen LUDUS_SDK_PREFIX="$PWD/out/install/linux-clang-development" \
  out/build/linux-clang-development/apps/editor/ludus_editor_tests '[.script-journey]'
```

The Optional editor CI lane runs this journey after explicitly installing Behavior.
Its offscreen/headless scope proves integration and lifecycle, not compositor/GPU
rendering or physical-device acceptance. The same sample builds a static shipping
executable without Editor/GameHost debug transport. S6/S7 supply the remaining
platform, safety, performance and representative usability evidence.

## References

This implementation preserves the reviewed source and attribution in
[S2](luau-s2.md), [S3](visual-s3.md), [S4](behavior-s4.md) and the
[Gems design review](scripting-gems-review.md). In particular, the shared generator
continues the explicit, inspectable manifest approach credited to Julien Hamaide,
“Automatic Lua Binding System,” *Game Programming Gems 7*, section 7.1,
pp. 503–516. Real break/step uses the already reviewed pinned Luau debug interfaces;
thanks to Roblox and the Luau contributors. S5 adds owner lifetime and Editor
integration, without copying another scripting framework or expanding the public
VM ABI.
