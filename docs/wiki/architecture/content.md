# Content and audio

Authored source files, catalog identity, acquired resource revisions and running
playback have different owners. Keeping them separate allows validation and
replacement without treating a live preview as a saved document.

`Content` and `AudioContent` are SDK modules. Content file reads and atomic saves
support Linux and macOS. The implemented composer,
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
[catalog/loading contract](../../architecture/content-resources.md).

## Read and save native content

Include `<ludus/content/content.h>` and link `Ludus::Content`. File operations are
synchronous; run them outside audio callbacks and render critical paths. Choose a
trusted existing root, then use relative UTF-8 paths. Parent directories must
already exist. Symlinks inside the root and non-regular destinations are rejected;
a symlink used to select the root itself is allowed.

Read into owned bytes with an explicit cap, then keep the digest of the saved
revision while editing your draft:

```cpp
ludus::content::Bytes saved;
auto status = ludus::content::ReadFile(root, "catalog.json",
                                     ludus::content::MAX_DOCUMENT_BYTES, saved);
if (status == ludus::content::Status::Ok)
{
    const auto expected = ludus::content::Hash(saved.Data());
    // draftBytes is the validated candidate document owned by the caller.
    status = ludus::content::SaveFile(root, "catalog.json", draftBytes, &expected);
}
```

`ReadFile` replaces its output only on success. `SaveFile` accepts up to 32 MiB
and publishes an empty file for an empty span. Pass `nullptr` instead of a digest
to require that a new file does not exist. After a successful save, retain the
digest of the new bytes for the next edit.

`Conflict` means the destination no longer matches your saved digest, the file
was expected but disappeared, or another cooperating writer holds the parent
directory lock. Retain the draft and offer reload/compare before retrying. I/O
and admission failures have separate statuses. Other platforms return
`Unsupported` for native saves.

The adapter stages an exclusive 0600 `filename.ludus-save` file beside the
destination, flushes and closes it, then atomically renames it. Existing open
readers retain their old revision. The suffix counts toward native filename
limits. A pre-existing temporary blocks a save with `IoError`; inspect it before
removing it. Failed saves attempt to clean up only their own temporary.

`Ok` means publication succeeded. Directory sync is best-effort after rename;
this API does not promise power-loss durability. Conflict checks serialize
cooperating writers only; external tools that ignore the lock can still race
publication. A save covers one file at a time. These Content capabilities do not
establish the full macOS composer or audio-device workflow.

See the [native save contract and validation](../../architecture/content-resources.md#native-saves)
for the platform boundary, failure tests and remaining persistence work.

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
[game packaging](../../development/editor-game-releases.md).

The [implementation evidence](../../development/audio-content-evidence.md)
records delivered C0–C5 slices, bounds and remaining acceptance gates. The
[authoring architecture](../../architecture/audio-authoring.md)
and [milestone plan](../../architecture/audio-content-milestones.md)
retain longer-term proposals and consulted references.
