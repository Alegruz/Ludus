# Keyboard input: research and decision record

Research date: 2026-10-01. Repository baseline:
`6ebdf7c1f2d37840db4fcdce66489f3c263b8d2e`.

This is the evidence accompanying [the keyboard spec](../../.kiro/specs/keyboard-input/design.md).
It is self-contained for a remote Kiro checkout. The PDFs are local research
inputs, not implementation dependencies. Summaries below are original paraphrases;
no scanned pages or book source code should be committed.

## Recommendation and limits

Use native events to maintain compact key state, preserve ordered transitions
until the next simulation step, and expose a small data-defined action map.
Keep the reducer independent of Wayland so tests and recorded sequences use
exactly the production state machine. Use one thread and bounded storage.

This is a synthesis of current production-library practices and the engine's
needs, not a claim that one universally best or fastest input architecture
exists. No benchmark has been run on a proposed implementation. Low latency,
maintenance cost, and correctness favor the simplest adequate design; further
optimization must follow measurements.

| Candidate | Strength | Cost or defect | Decision |
| --- | --- | --- | --- |
| Poll current state and diff once per rendered frame | Small API | Loses a down/up pair between samples; ties edges to render rate | Reject as sole input source |
| Call gameplay directly from native callbacks | Low apparent delay | Reentrancy, ownership, backend dependence, difficult replay | Reject |
| Ordered events plus derived state and integer action IDs | Fast state queries, preserves taps, easy injection | Requires explicit step/reset/overflow semantics | Select |
| General trigger/processor/context framework | Rich interactions | Too much scope for keyboard-only engine bootstrap | Borrow action separation; defer framework |
| Dedicated input thread and lock-free queues | Can decouple some work | Synchronization and native event-thread constraints | Defer until measured need |
| Replace native platform with SDL/GLFW | Mature portability facilities | Windowing/dependency migration unrelated to this feature | Learn from their contracts; retain native backend |

## Current primary sources

Sources were accessed on the research date. These justify specific ideas, not
every policy chosen for Ludus.

- [SDL3 keyboard practices](https://wiki.libsdl.org/SDL3/BestKeyboardPractices):
  distinguishes physical positions, layout-dependent key meanings, and composed
  text; recommends configurable bindings. Adopt physical gameplay bindings and
  rebinding. Keep text input a separate future stream.
- [GLFW input guide](https://www.glfw.org/docs/latest/input_guide.html):
  documents callbacks, pollable state, missed transitions when polling, repeat,
  and distinct text callbacks. Adopt events plus state; repeats do not produce
  gameplay press edges. Sticky polling is insufficient for ordered multiple taps.
- [Unity Input System actions, package 1.14](https://docs.unity3d.com/Packages/com.unity.inputsystem@1.14/manual/Actions.html):
  separates actions from controls and bindings. Adopt integer action identities
  with button and signed-axis values. Its interactions and callback lifecycle
  are broader than the initial Ludus requirements.
- [Unreal Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine):
  action mappings, contexts, modifiers, and triggers illustrate a modern complete
  system. Adopt semantic actions and an explicit active map; defer prioritized
  context stacks and programmable trigger graphs.
- [Godot Input](https://docs.godotengine.org/en/stable/classes/class_input.html):
  exposes actions/state and describes accumulation and event propagation as
  distinct concerns. Ludus must make edge lifetime and consumption explicit,
  rather than confusing an event handled by UI with a physically released key.
- [Wayland protocol, wl_seat and wl_keyboard](https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_keyboard):
  defines focus enter/leave, held-key synchronization, keymap/modifier events,
  and repeat. Enter's held-key list must not be synthesized into key presses.
  Native timestamps have an undefined origin. New protocol versions can deliver
  repeated key states; negotiate versions and handle only supported semantics.
- [libxkbcommon keymap creation](https://xkbcommon.org/doc/current/group__keymap.html):
  a future layout/text adapter can interpret compositor keymaps. Physical-only
  input does not need to compile keymaps or add this dependency now.
- [Linux input event codes](https://www.kernel.org/doc/html/latest/input/event-codes.html):
  documents native key event types and repeat values. Do not confuse evdev codes,
  XKB codes, USB usages, and Ludus indices; the adapter needs an explicit mapping.
- [W3C KeyboardEvent.code](https://www.w3.org/TR/uievents-code/):
  gives browser physical-key names useful for a future adapter. The initial
  enum should express positions without depending on DOM or Wayland numbers.

## Gems search and locally inspected chapters

Searched `references/game-dev-gems-toc.md` for input, keyboard, recording,
playback, journaling, event scheduling, bit arrays, debugging, and clocks;
also inspected the Game Engine Gems sections. The TOC is a discovery index,
not evidence of chapter contents. Two directly relevant complete chapters were
read from the local PDFs. GPG3 is image-only: its pages were rendered and read
visually, rather than treating an empty text extraction as a successful review.

### GPG3, Greg Seegert, chapter 1.13

**Real-Time Input and UI in 3D Games**, printed pp. 109-116,
local PDF pages 112-119 (one-based), `Game Programming Gems 3.pdf`.

The chapter explains why state-only polling misses a press and release between
updates. Buffered events retain ordering and modifiers, while held-state queries
remain useful. It separates text generation from device input and supports
customizable hotkeys. Its UI, cursor, and network sections are outside this
milestone. Ludus adapts the keyboard principle to one ordered event stream and
compact masks, rather than copying its three queues or DirectInput implementation.
Native event ordering and bounded loss handling qualify any promise of never
missing input. XML and old Windows APIs are historical implementation details,
not proposed dependencies.

### GPG2, Bruce Dawson, chapter 1.16

**Game Input Recording and Playback**, printed pp. 105-111,
local PDF pages 102-108 (one-based), `Game Programming Gems 2.pdf`.

Centralizing input behind a replaceable source enables bug reproduction and
repeatable performance workloads. Accurate playback also requires known initial
state and simulation timing; rendering and unrelated random streams must not
change simulation behavior. Validate output state during playback to detect
divergence early. For this milestone, adopt injection fixtures and step IDs,
not a whole-game determinism claim. Always-on durable recording, crash saving,
network capture, random-number redesign, and movie export are separate projects.

### Other TOC candidates, not chapter-reviewed

| Candidate | Relevance | Disposition |
| --- | --- | --- |
| GPG1: Simple, Fast Bit Arrays | Dense keyboard masks | Candidate only; fixed masks chosen on simple size/cost reasoning |
| GPG3: Journaling Services | Event histories | Candidate only; no attributed implementation claims |
| GPG4: The Science of Debugging Games; The Clock | Diagnostics and time | Candidate only; no attributed implementation claims |
| GEG2: High-Performance Programming with Data-Oriented Design; Producer-Consumer Queues | Layout/ownership | Candidate only; no threading requirement inferred from titles |
| GEG3: Generic, Lightweight, and Fast Delegates in C++ | Backend sink | Candidate only; a function pointer and user data suffice |

GPG1/GPG4 extraction did not yield usable chapter text. They are not cited as
reviewed sources. The directly inspected chapters above provide the relevant
input evidence; Kiro need not obtain or OCR the remaining books.

## Ludus choices that are engineering judgments

The capacity defaults, focus suppression policy, reset behavior, one consumer,
single active action map, and assigning all pending events to the next executed
step are Ludus design choices. They do not come verbatim from a book or engine.
These choices deliberately trade advanced timestamp scheduling and context
arbitration for small, testable state machines.

The inspected checkout has an empty `WindowBase::Event`, a nonblocking Wayland
`HandleEvent({})` pump, no `wl_seat`/`wl_keyboard` listeners, a headless pump that
returns false, and a smoke app that pumps through the window. The new spec must
preserve pump behavior and avoid assuming an input service already exists.

The untracked local WebGPU plan has a future canvas/input milestone. It is not
assumed to exist in Kiro's checkout and is not required by this spec. Share the
neutral key/reducer contract when a browser backend is implemented later.
