# Keyboard input: implementation decision log

Status: in progress. This log records the material decisions and deviations made
while implementing the committed spec
([requirements](../../.kiro/specs/keyboard-input/requirements.md),
[design](../../.kiro/specs/keyboard-input/design.md),
[tasks](../../.kiro/specs/keyboard-input/tasks.md)). Ordinary implementation
choices delegated to the implementer are summarized here for review.

## M0 — Revalidate checkout and freeze the small API

### Repository baseline reconciliation

The design and research record cite baseline
`6ebdf7c1f2d37840db4fcdce66489f3c263b8d2e`. The implemented checkout is a later
revision whose `main` tip is `a66f551` ("docs(input): add keyboard architecture
and Kiro implementation handoff"). The design explicitly says to treat its
repository baseline as historical. Reconciliation of what actually exists:

- `modules/platform/` is the only windowing module. Backends: Wayland
  (`src/window_wayland.cpp`), headless null (`src/window_headless.cpp`), and a
  browser canvas backend (`src/window_web.cpp`, Emscripten only).
- `WindowBase::Event` is still an empty struct, and `HandleEvent({})` pumps the
  display nonblockingly, returning `false` when the window closes or the pump
  terminally fails. The Wayland pump already implements the
  prepare/flush/poll/read-or-cancel discipline with EINTR/EAGAIN handling that
  the spec requires us to preserve. The headless pump returns `false`.
- There is **no** `wl_seat`/`wl_keyboard` listener, no input reducer, and no
  action map. The research record's description of the checkout matches.
- A browser canvas input path already exists: `ludus/platform/browser/window.h`
  defines a `browser::Key` enum (physical `KeyboardEvent.code` positions),
  `browser::InputEvent`, and a `browser::WindowState` snapshot with a held-key
  array and dropped-event counter, polled via `PollBrowserInput`. This is a
  **separate, working** canvas-only path and is preserved untouched. The new
  `ludus_input` module is the backend-independent reducer the browser path does
  not provide; a future browser adapter can feed the same `Ingest` contract
  (noted in the research record). We do not merge the two enums in this
  milestone — `browser::Key` is DOM-code-shaped (uint8, ~60 values) and the new
  `input::Key` is the broader physical domain the spec mandates (uint16, with
  distinct left/right modifiers, keypad, F1–F24). No competing reducer is
  created on the browser side.

Reused compatible functionality:

- `ludus::foundation::core::StaticArray` for all fixed storage (bounded rings,
  masks, binding tables). No new container is written.
- `LUDUS_LOG_*` for bounded lifecycle diagnostics; a dedicated `LOG_INPUT`
  category mirroring `LOG_PLATFORM`.
- The existing `WindowBase` sink-attach seam: we add an explicit
  `AttachKeyboardSink`/`DetachKeyboardSink` pair rather than repurposing the
  empty `Event`, exactly as the design requires.

### Module layout (frozen)

New static target `ludus_input`, export `Ludus::Input`, under `modules/input/`:

- Public headers in `modules/input/include/ludus/input/`:
  `key.h`, `keyboard_event.h`, `keyboard.h`, `actions.h`, `input_debug.h`.
- Private helpers in `modules/input/src/internal/`.
- Public dependency: `Ludus::FoundationBase`. Private: `Ludus::FoundationContainers`
  (StaticArray) and `Ludus::FoundationLogging`. No Platform, graphics, native OS,
  or XKB dependency. Platform depends on Input (one-way) for the normalized
  record/sink vocabulary.

### Key vocabulary (frozen)

`enum class Key : uint16` with explicit stable values. `Unknown = 0` reserved.
Dense supported domain with `Count` as the one-past-the-end sentinel; storage is
sized to `Count`, not a padded 512. The published table covers: letters A–Z,
digits 0–9 (top row), the US-position punctuation keys, Space/Enter/Escape/Tab/
Backspace, editing+navigation (Insert, Delete, Home, End, PageUp, PageDown, the
four arrows), F1–F24, the keypad (digits, operators, Enter, Decimal, NumLock),
and **distinct** left/right Shift/Ctrl/Alt/Super. Media/vendor keys are not in
the domain and normalize to `Unknown`. `IsValidKey()` validates every external
value (including fabricated enum values and negative native codes) before it is
used as an index. Debug labels like `KeyW` denote the US-reference physical
position, not the user's layout.

### Status enums and public signatures (frozen)

```cpp
enum class AdmissionStatus : uint8 { Accepted, Ignored, RejectedInvalid, RecoveredWithLoss };
enum class StepStatus      : uint8 { Ok, InvalidStep, SequenceExhausted };
enum class ActionStatus    : uint8 { Ok, InvalidAction };
enum class BindingStatus   : uint8 { Ok, InvalidAction, InvalidKey, CapacityExceeded,
                                     ConflictingType, DuplicateBinding };
enum class ResetReason     : uint8 { None, FocusLost, WindowClosed, CapabilityLost,
                                     SeatRemoved, BackendDisconnected, Overflow,
                                     MapReplaced, ExplicitNeutral, ExplicitLive };
```

Core instance operations on `ludus::input::InputSystem` (main-thread only, no
singleton; caller owns lifetime):

| Operation | Signature |
| --- | --- |
| Ingest | `AdmissionStatus Ingest(const KeyboardRecord&) noexcept` |
| Live state | `bool GetLiveDown(Key) const noexcept` |
| Step | `StepStatus ConsumeStep(uint64 stepId) noexcept` |
| Snapshot | `const KeyboardSnapshot& GetKeyboardSnapshot() const noexcept` |
| Step events | `StepEventView GetStepEvents() const noexcept` |
| Action query | `ActionStatus GetAction(ActionId, ActionState& out) const noexcept` |
| Rebinding | `BindingStatus ReplaceBindings(const BindingMap&) noexcept` |
| Reset | `void RequestReset(ResetReason, const FocusBaseline&) noexcept` |

`KeyboardRecord` carries source (`Native`/`Synthetic`/`Replay`), transition
(`Down`/`Up`), `Key`, an optional native timestamp (uint32 ms, diagnostic only),
and a focus/baseline marker for enter/leave. `GetKeyboardSnapshot()` returns a
reference stable until the next `ConsumeStep`/reset publication. `GetStepEvents()`
is borrowed until the next `ConsumeStep`.

### Fixed capacities (frozen, private constants)

- Pending transition ring: 256 accepted transitions.
- Output step-event buffer: 256 + 1 reset marker.
- Actions: 64. Bindings: 256.
- Trace ring: 256 entries (opt-in).

Capacities are template parameters on the internal storage so tests can
instantiate a tiny capacity for overflow/exhaustion coverage without touching
production sizes. Overflow returns `RecoveredWithLoss` and increments counters;
it never grows, drops-oldest silently, or blocks.

### Sequence and step semantics (frozen)

A monotonically increasing `uint64` ingestion sequence orders records; native
Wayland time is retained only as diagnostic metadata and never compared or
sorted on. Sequence exhaustion returns an explicit status and forces a reset
rather than wrapping. `ConsumeStep` requires a strictly increasing `uint64`
step id; a repeated or decreasing id returns `InvalidStep` and changes nothing;
`uint64` exhaustion returns `SequenceExhausted`. All pending events are assigned
to the next executed step in callback order (edges on the first catch-up step,
held state retained on later catch-up steps; the queue is retained across zero
simulation steps).

### Neutral/invalid results (frozen)

- `GetAction` for an id outside the configured domain → `InvalidAction`, output
  untouched / neutral.
- An id removed by map replacement that was previously active → neutral value
  with `Cancelled = true` for exactly the publication that computes the
  replacement, then neutral/no-flags afterward.
- Unknown/fabricated `Key` → record rejected (`RejectedInvalid` / `Ignored`),
  no out-of-bounds access, no state change.

### Zero-step and three-step walkthrough

Batch ingested while no step runs: `Down(W)`, `Up(W)`, `Down(W)` (sequences
1,2,3). Live `Down(W)` tracks the final physical state (true after seq 3).

- Zero steps between pumps → the three records stay queued; snapshot unchanged.
- `ConsumeStep(10)`: applies all three in order → `Down(W)=true`,
  `Pressed(W)=true`, `Released(W)=true` (two presses + one release are OR-folded
  into the step edge flags; `GetStepEvents()` preserves all three for consumers
  that need multiplicity).
- `ConsumeStep(11)`: no new events → edges cleared, `Down(W)=true` retained.
- `ConsumeStep(12)`: still no events → edges cleared, `Down(W)=true` retained.

### Seat selection and evdev mapping assumption (frozen)

One `wl_seat` is chosen deterministically: the lowest registry `name` that
advertises keyboard capability, held until it is removed; a replacement seat
starts from reset state (no held-state merge). The adapter maps native Linux
**evdev** key positions (the convention of the selected backend) to `Key` via an
explicit, tested table — no unchecked cast of Wayland numbers, and XKB's `+8`
offset is **not** applied for mask indexing. If the backend/keymap cannot
establish an evdev mapping, the adapter reports unsupported input and keeps
neutral state; it never guesses characters. Keymap fds are closed on every path.

### Toolchain / environment note (not a code deviation)

The sandbox shipped clang-15, cmake 3.22, and no conan; the repo pins clang-18,
cmake 3.29.6, ninja 1.11.1.3, conan 2.8.1. The pinned tools were provisioned
(system clang-18/lld-18/clang-tools-extra, Wayland dev packages, and
`init.sh --no-system-install` for the venv/conan graph). One local, gitignored
`out/host-tools/bin/clang-tidy` wrapper reorders `clang-tidy-18 --version` output
so the detector reads a parseable first line; the real binary is pinned
clang-tidy 18.1.8. No repository file was changed to accommodate the environment.

## M2 — Native Wayland keyboard adapter

### Sink seam

Rather than repurposing the empty `WindowBase::Event`, a new public header
`ludus/platform/keyboard_sink.h` defines a `KeyboardSink` of three `noexcept`
function pointers (`OnRecord`, `OnReset`) plus a borrowed `void* UserData`.
`WindowBase` gained `AttachKeyboardSink`/`DetachKeyboardSink`/`GetKeyboardSink`.
The sink is stored on the window; the Wayland backend routes native events to
the focused window's sink. Callbacks never retain borrowed Wayland payloads —
the focus-enter key array is normalized into a copied `FocusBaseline` before the
callback returns.

### Pump preservation

The original `HandleEvent({})` body (prepare/flush/poll(0)/read-or-cancel with
EINTR/EAGAIN) was extracted verbatim into `pumpDisplayOnce()`. `HandleEvent`
now calls it and, only when it returns false, submits a single `WindowClosed`
reset to the focused window's sink before returning false. Headless return
semantics are unchanged. The live `ludus_platform_wayland_tests` regression
(10440 assertions) confirms unchanged pump behavior against a real compositor.

### evdev mapping

`src/internal/evdev_keymap.hpp` maps the raw Linux evdev code carried by
`wl_keyboard.key` directly to `Key` via a compile-time table. The XKB `+8`
offset is deliberately NOT applied (it is only for a future XKB *keysym* lookup).
Out-of-range / unmapped / vendor / media codes normalize to `Key::Unknown`. The
table is zero-initialized and relies on `Key::Unknown == 0` (static_assert'd).

### Compositor validation tooling (environment)

The reference compositor used for live validation here is **weston 13.0.3**
(headless backend), installed into the sandbox. The sandbox has no uinput /
`/dev/input` virtual input device, and weston's headless desktop-shell does not
grant keyboard focus to a buffer-less surface, so synthetic key injection and a
focused key-delivery assertion could not be performed in-sandbox. The live test
therefore verifies seat/keyboard binding, listener registration, and pump
stability, and records a WARN when focus is not granted. Full focus + key
delivery + held-state-on-leave is an outstanding manual gate (M5 matrix).
