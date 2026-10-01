# Keyboard input requirements

Status: proposed implementation specification, 2026-10-01. Scope: keyboards
only, native Wayland plus backend-independent/headless tests. Read
[design.md](design.md), [tasks.md](tasks.md), and the
[research record](../../../docs/architecture/keyboard-input-research.md).
Repository standards remain authoritative. These APIs are proposed, not built.

| ID | Required observable behavior |
| --- | --- |
| K01 | Gameplay identifies physical key positions with a Ludus enum; native codes and backend headers remain private. Unknown/invalid values never index storage. Left/right modifiers and keypad keys remain distinct. |
| K02 | Ordered key down/up records are reduced into held state and per-step press/release flags. Down/up within a step yields both flags and final up state. Multiple taps remain visible in the ordered record view. Queries do not consume flags. |
| K03 | Exactly one owner pumps and consumes input on the main thread. Pumping is nonblocking. Events survive render iterations with zero simulation steps; each event belongs to one executed step, never all catch-up steps. |
| K04 | Repeated/duplicate down events never create additional presses; redundant up events create no releases. Keyboard input does not generate text. |
| K05 | Focus loss, window close, keyboard capability loss, seat removal, backend disconnect, and explicit reset clear held gameplay state and pending activations. Active actions cancel; cancellation is distinguishable from ordinary release. |
| K06 | Focus enter synchronizes the supplied held-key set without synthesizing press/release edges. Those keys cannot activate actions until released and pressed anew. Events for an unfocused or different attached window do not activate gameplay. |
| K07 | Pending storage is bounded. Overflow is observable, discards the incomplete pending batch, cancels actions and suppresses currently held keys until a fresh press. No dropped release can leave an action stuck. Recovery does not require dropping focus. |
| K08 | Integer action IDs support button bindings with multiple alternative keys and signed 1D digital axes. Releasing one alternative does not release an action while another remains held. Opposite directions cancel to zero. |
| K09 | One active binding map can be replaced between steps. Replacement is validated and atomic; invalid/capacity-exceeding input leaves the old map intact. Switching maps cancels old actions, clears pending activations, and suppresses held keys until fresh presses. |
| K10 | Wayland negotiates seat/keyboard versions, routes to the attached surface, handles enter/leave and capability lifecycle, closes every received keymap fd, and preserves existing display read/cancel/flush rules. Keymap failure cannot break physical gameplay input. |
| K11 | The core can run without Platform, a display, or a renderer. Synthetic tests use the production reducer. Headless builds retain their current window-pump termination behavior. |
| K12 | Runtime ingestion, step publication, queries, binding evaluation, and bounded trace capture allocate no heap memory and acquire no locks. No exceptions in engine code; fallible setup returns explicit status. |
| K13 | A bounded optional trace exposes order, source, key, transition, sequence, step assignment, and resets/loss. It reports wrap/truncation and supports reproducing complete test fixtures through the same ingestion interface. No filesystem work occurs in callbacks. |
| K14 | New public headers are self-sufficient, lightweight, exported and installable. FoundationBase remains independent. Builds remain warning-clean and satisfy pinned formatting, tidy, sanitizers, header and SDK checks. |
| K15 | An installed SDK consumer and smoke demonstration prove held input, short taps, action rebinding, focus cancellation, and step ownership. Manual compositor results and skipped checks are recorded honestly. |

## Deferred scope

Mouse, gamepad, touch, device-per-keyboard identity, multiple seats/windows as
simultaneous gameplay sources, text/IME, layout-based shortcuts/display names,
chords, tap/hold/double-tap interactions, UI repeat, context priority/consumption
stacks, input workers, durable replay files, rollback/network prediction,
timestamp-to-tick scheduling, and browser/Windows/macOS adapters are deferred.
The first backend can track multiple windows for safe routing but attaches one
gameplay window and selects one seat. Switching either uses the reset contract.

Rebinding is an in-memory API plus demonstration, not a config-file parser or UI.
No renderer changes or new general allocator, event bus, ECS, or string library
are needed. Physical debug labels such as `KeyW` do not claim to match the user's
layout. Do not advertise international text entry as implemented.
