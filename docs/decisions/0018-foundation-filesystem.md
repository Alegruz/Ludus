# ADR 0018: FoundationFilesystem and revision-bound reads

Status: Accepted for the F1 slice.

Content currently owns path validation and Linux file-reader code. General
storage cannot depend on Content schemas or Platform windowing, and decoders
must keep the revision they opened when an author replaces a file.

Add Ludus::FoundationFilesystem, depending only on FoundationBase. Its small
public header exposes move-only trusted-root directories and regular-file
revisions, offset reads, typed results and native error codes. Linux uses
per-component descriptor-relative traversal with no child symlink following,
close-on-exec handles, 64-bit offsets and pread. Allocation is explicit and
fallible at Open/Clone; warm reads and validation allocate nothing. Unsupported
hosts compile an explicit Unsupported backend, including the browser build.

Content retains its public API and adapts reads and decoder cursors to this
module. The Content-owned save path remains until the separate persistence
contract and failure tests exist. No dependency is added from FoundationBase
or Logging to Filesystem, avoiding recursive infrastructure failure paths.

The design and reference-review decisions are in
[Filesystem architecture](../architecture/filesystem.md). It distinguishes
ordinary mutation detection from immutable/cryptographically verified data,
symlink rejection from hostile-process sandboxing, and publication from durable
persistence. Mount resolution, packs and async I/O remain later slices with
separate acceptance criteria; this ADR promises no unmeasured speedup.
