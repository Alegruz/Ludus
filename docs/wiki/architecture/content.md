# Content and audio

Authored source files, catalog identity, acquired resource revisions and running
playback have different owners. Keeping them separate allows validation and
replacement without treating a live preview as a saved document.

`Content` and `AudioContent` are SDK modules. The implemented composer,
acquisition and output workflow described here is native Linux. Dynamic music,
browser acquisition/output and complete gameplay-world binding have separate
acceptance phases; linking a browser target does not establish that workflow.

## Who owns what?

| Owner | Holds | Boundary |
| --- | --- | --- |
| Content catalog | IDs, kinds, relative paths and revisions | Catalog identity is independent of a runtime handle |
| AudioContent loader | Typed definitions, reference resolution, preparation and leases | Acquire a validated revision before exposing it to playback |
| AudioSystem | Prepared clips/streams, commands, playback and retirement | Device/callback internals remain private |
| Game session | Active/pending leases and gameplay sound policy | Release resources after the associated work retires |
| Editor document | Mutable draft, saved digest, validation errors and revision | Preview is transient; Save is explicit |
| Packaging tools | Validated dependency closure and immutable release files | Publication rechecks source inputs |

Content stays below audio-specific behavior; AudioContent depends on Content
and Audio. Qt adapters live in the editor. Parser, OS and audio-device types do
not leak into installed public headers. See the
[catalog/loading contract](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/content-resources.md).

## Follow one sound through the system

1. Import or register a source under a stable resource ID.
2. Author a sound definition referencing that ID and validate its properties.
3. Save source intent with conflict checks against the saved digest.
4. Acquire a revision through the typed loader and prepare runtime audio.
5. Start playback through the audio owner, retaining its required leases.
6. On replacement or stop, cancel/drain work and observe retirement before reuse.

Decode/import/analysis work stays off the Qt GUI and audio callback paths.
Native preview uses its own AudioSystem control owner; the GUI submits commands
and observes bounded snapshots. Closing preview waits for quiescence before
releasing borrowed storage.

Resource IDs identify authored content; generations/revisions prevent stale
results from publishing into a replaced session. A successful cancellation
request does not by itself prove a worker or playback callback has stopped.

## Package the same validated intent

Packaging validates dependency closure and source decode, stages immutable
revisions, rechecks inputs and atomically publishes the current manifest. It
does not silently save a dirty editor draft or substitute a running preview.
For a task-oriented walkthrough, use [audio workflow](../guides/audio.md) and
[game packaging](../guides/releases.md).

The [implementation evidence](https://github.com/Alegruz/Ludus/blob/main/docs/development/audio-content-evidence.md)
records delivered C0–C5 slices, bounds and remaining acceptance gates. The
[authoring architecture](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/audio-authoring.md)
and [milestone plan](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/audio-content-milestones.md)
retain longer-term proposals and consulted references.
