# Audio workflow

Ludus audio authoring keeps source audio, persistent definitions and runtime
preview state separate. Begin with a loaded game project; the editor's Audio
workspace uses its `content` root.

## A practical authoring loop

1. Compose and keep editable projects in your DAW.
2. Export supported WAV or FLAC source audio.
3. Import/register content and create a sound or music definition.
4. Edit playback settings and audition using the supported runtime-backed preview.
5. Save, reopen and validate the definition.
6. Include the resources in the game's explicit package installation rules and
   test a real runtime trigger.

An editor preview alone does not prove packaged playback or device behavior on
another machine. Check the linked evidence for accepted native paths and pending
device/platform tests.

## Identity and ownership

Content catalogs associate stable logical IDs with project-relative paths.
Definitions save IDs and authored playback data; transient device, clip, voice
and lease handles remain runtime state. Reimporting an asset should retain its
intended logical identity.

Keep source masters and authoring files under your own asset workflow. Ludus does
not replace DAW projects, instrument plugins or composition tools. For stems in
a music section, deliberate rate/length/start/loop alignment matters; validation
should report problems rather than silently trim or normalize them.

## Save and switch safely

Before changing projects or launching work that owns audio preview, stop audition
and save or discard the audio draft explicitly. Cancellation and failed save
leave the authored draft available for recovery.

References: [authoring contracts](../../architecture/audio-authoring.md),
[implemented content/preview evidence](../../development/audio-content-evidence.md),
and [resource formats](../../architecture/content-resources.md).
