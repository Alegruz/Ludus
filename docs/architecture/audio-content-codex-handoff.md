# Codex handoff for audio content

This package implements the path from a composer's exported audio to saved,
previewable, packaged game resources. It extends the existing audio runtime and
editor while sharing identity/loading rules with the proposed level architecture.
Start with [milestones](audio-content-milestones.md); architectural contracts live
in [content resources](content-resources.md) and [audio authoring](audio-authoring.md).
The initial C0–C5 implementation and pending acceptance gates are recorded in
[implementation evidence](../development/audio-content-evidence.md).

## Implement the first useful workflow

Paste this into an implementation chat:

```text
Implement Ludus's first composer workflow from:
- docs/architecture/content-resources.md
- docs/architecture/audio-authoring.md
- docs/architecture/audio-content-milestones.md
- docs/architecture/audio-content-codex-handoff.md

Read AGENTS.md and applicable .kiro/steering first. Inspect current code/diffs,
existing .kiro/specs/audio and .kiro/specs/editor-workspace, their evidence, and
world/level/frame docs. Work may have advanced since the documentation baseline;
reuse verified implementations and preserve unrelated work. Do not treat a public
API declaration or historical check mark as proof of a working feature.

Implement C0 through C5 in dependency order, including existing A4/A5 prerequisites
for streaming/native playback. C1/C2 and independent offline/editor document work
may proceed while hardware gates are unavailable. This is implementation scope,
not a request for another plan. Complete meaningful reviewable batches and record
paths, commands, actual outcomes and pending gates in
docs/development/audio-content-evidence.md.

Deliver stable logical resource IDs/catalog, bounded shared definition codec,
explicit asynchronous preparation and leases, resident SFX and simple streamed BGM,
safe reimport/replacement, editor import/property forms/waveform/preview/save/reopen,
game-owned audio presentation/session lifetime binding, and loose-file packaging.
Use the existing AudioSystem, seeded EventAdapter and Qt editor patterns. If the
world is still proposed, demonstrate integration with a standalone installed-SDK
sample and identify full world binding as pending. Do not implement an ECS or level
editor as an accidental prerequisite.

Prove cold parser/thread/decoder boundaries with no engine exceptions, explicit
failure, bounded memory, one AudioSystem control owner and no GUI/audio callback
blocking. Keep Qt/parser/miniaudio/OS types private. Validate paths/references/loop
conversion/capacity, reject unknown schemas, and preserve old files/playback on
failed saves or candidate preparation. Reimport preserves IDs; runtime handles
never appear in authored files or save games. Do not alter strict project v1 schema
silently. Do not weaken no-exception, warning, header or dependency standards.

Run appropriate pinned builds/tests/format/tidy/sanitizers/header/budget/SDK checks,
Editor ON/OFF isolation and existing regressions. Exercise real worker concurrency,
actual native output and interactive GUI. Unavailable device/display/toolchain
checks remain pending; finish independent work and describe the concrete block.
Do not claim streaming or preview based on offline/mocked tests. Do not silently
install prerequisites or modify unrelated build trees.

Exclude C6-C8 dynamic music/browser additions unless separately requested. Runtime
MIDI/tracker support, custom audio codecs, binary banks, live game IPC, node graphs,
allocator work and general job systems are outside this slice. Do not push, merge
or deploy without instruction. End with usage steps, verification and limitations.
```

## Continue the next batch

```text
Continue the next incomplete C0-C5 batch in
docs/architecture/audio-content-milestones.md. Read the three linked architecture
and handoff documents, current code and audio-content evidence. Verify prerequisites,
preserve unrelated work, implement the batch and its acceptance tests, and update
evidence. Do not stop at another proposal or mark unavailable gates complete.
```

## Add dynamic music

```text
Implement C6 and C7 from docs/architecture/audio-content-milestones.md using the
linked content/audio architecture and existing evidence. Verify C3/C5 first.
Freeze the complete v2 grammar and fixtures before coding its reader/editor.
Implement a shared authoritative musical transport, atomic stream-group admission,
sample-timed ramps/transitions, group starvation recovery, bounded MusicDirector
and matching editor audition forms. Preserve v1 simple BGM behavior and current
four-stream capacity unless a separately measured design revision is justified.
Use the production mixer and prove real streaming synchronization, not only
resident-clip scheduling. Run the required offline/concurrency/native/UI gates,
update evidence and report limits. Do not add MIDI/plugins/tempo maps/live IPC.
```

## Add browser delivery

```text
Implement C8 from docs/architecture/audio-content-milestones.md and existing A6
contracts. Verify content/native prerequisites. Support bounded asynchronous
resource fetch and real AudioWorklet playback under the pinned SDK. Test actual
Chromium/Firefox output, activation, isolation errors, fetch failures, stalls,
suspension and shutdown. Dynamic parity is required only if C6 is in this release.
Preserve unrelated web presets and report unverified platforms honestly.
```

## Review the result

```text
Review completed audio/content/editor milestones against their architecture and
acceptance contracts. Inspect actual source, fixtures and evidence. Focus on saved
identity versus runtime handles, malformed/conflicting content, failed candidate
rollback, active/candidate memory, stale completions, source/voice/worker retirement,
exactly-once game outbox delivery, owner generation reuse, GUI responsiveness and
preview shutdown. For dynamic music inspect atomic admission, boundary math,
stem starvation recovery and maximum overlapping stream capacity. Reproduce defects,
repair with focused tests, rerun affected checks, and report remaining unavailable
hardware/browser/SDK gates without turning them into passes. Preserve scope and
unrelated work; do not push, merge or deploy.
```
