# Loading projects and editing games during play

Design baseline: 2026-10-02, main `3fadacecd4c3856e0456341ea05507459d479a38`.
This package defines implementation work; it introduces no runtime code.
The normative contracts are [requirements](../../.kiro/specs/project-live-reload/requirements.md),
[design](../../.kiro/specs/project-live-reload/design.md) and
[tasks](../../.kiro/specs/project-live-reload/tasks.md). Read the
[research](project-live-reload-research.md) and [Kiro handoff](project-live-reload-kiro-handoff.md).

## Architecture choice

The editor owns project documents and build controls. A separate GameHost owns
the engine and play session. That host loads one active gameplay module through
a small versioned function table; engine libraries remain static. The shipping
game links the same gameplay implementation statically. This keeps editing and
debugging usable when gameplay stops or crashes, and confines rebuilds to game
code. It is a maintainability choice informed by current engine practice and the
Gems review, not a measured claim of universal best performance.

| Change | Handling | State/persistence |
| --- | --- | --- |
| Exposed parameter | Conditional typed command at frame boundary | Session-only; explicit Apply to Document |
| Game C++ implementation | Build immutable module generation; stage and swap | Explicit checkpoint restoration |
| Game state/schema layout | Declared migration or RestartRequired | No raw-object preservation |
| Supported asset | Validate replacement; swap handle at safe point | Source owns edits; cooked files are generated |
| SDK/engine ABI | Rebuild/restart host | Never swap incompatible engine libraries |
| Browser game | Existing build/page reload | Dynamic Wasm is deferred |

## The hard boundary

The gameplay module has one entry, opaque instances, explicit lifecycle/status
callbacks and a passed host service table. No C++ classes, STL objects, virtual
tables, exception handling or cross-module delete enter the ABI. Host-owned
resource handles carry session/generation identity. Small handwritten property
schemas supply a useful inspector without building an ECS/reflection framework.

Before a reload commits, the old instance stays alive. The host quiesces it,
captures a bounded tagged checkpoint, and creates the candidate under restricted
staging services. A recoverable rejection discards the candidate and resumes
the old instance. A successful prepared swap retires old callbacks/instances
before releasing the library. Native crashes and arbitrary external side
effects cannot be rolled back; they require host recovery/restart. Every new
engine service must define staging and retirement before it is reload-safe.

## Keeping authoring predictable

Open reads metadata and executes no project code. Build/Play are explicit.
Saved documents, dirty drafts and simulated state stay distinct. Live edits,
undo and Apply to Document use stable IDs and revision checks. Play/Stop never
silently saves simulation state. Switching projects cancels work and confirms
owned process cleanup before releasing the current workspace.

Module generations and symbol files are immutable and leased. A build lock lasts
through publication; a play lease protects the running copy. Consequently,
building while playing works without overwriting a loaded library or allowing
two writers into one CMake tree. Auto-build/reload is an optional convenience
over explicit actions; watcher events cannot activate incomplete/stale files.

## Existing work and sequencing

Editor E0 and installed SDK exports exist on the baseline. The broader
project-sdk-workflow implementation is concurrent work, not assumed merged.
This design works first with local installed SDKs and the existing canonical
tooling; integrate the installed backend when available, without implementing
the SDK store/release system again. Existing v1 executable Run stays supported.

Deliver an ABI/coexistence spike, restartable host, immutable builds, transactional
reload, live tuning/undo and one supported asset path, then real debugger/SDK/
sanitizer/CI acceptance. Qt stays optional and runtime/browser builds remain
independent. An embedded viewport, scene authoring, scripting and binary patching
are separate work, so none blocks useful live editing in a separate game window.
