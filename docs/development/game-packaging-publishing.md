# Native game release packages

The installed `ludus` host tools can build and validate a native Linux x64 Release
package for a version-2 CMake game project. Packaging is independent of an
itch.io account. This implements the first part of the
[packaging and publishing architecture](../architecture/game-packaging-publishing.md).
Live upload, generated deployment workflows, Editor release controls and browser
game packaging are subsequent phases.

## Create a project with release files

Use an installed Release SDK that matches the project's engine requirement and
the consuming toolchain. Other SDK flavors remain valid for development but
cannot produce a Release package.

```bash
ludus project create MyGame --name MyGame --sdk /path/to/release-sdk --release
```

Add `--itch-target username/game` to generate an optional destination mapping
for offline upload planning. Nothing authenticates or uploads during creation.
Without `--release`, creation retains its ordinary project layout. The minimal
native template is now version 2; existing projects are never overwritten.

Generated `ludus.release.json` selects a release profile, executable target,
install component and entry point. `cmake/GameRelease.cmake` installs the
executable, `NOTICE.txt` and SDK license files. Edit its explicit install rules
to add runtime assets, dependencies and the corresponding notices. The generated
notice is a starting point: the game author must complete the license inventory.
CMake remains authoritative for the payload; no build-tree directory sweep occurs.

Existing projects can add these files manually, using a newly generated project
as a reference. There is no `release init` migration command yet.

## Build and inspect the package

Required host tools are Python 3.10+, the project's pinned CMake/Ninja/compiler
and GNU `readelf` from binutils. Linux with glibc's `renameat2` is the initial
publication platform. The target SDK manifest must identify `x86_64-linux-gnu`
and Release; the release profile's portable platform label is `linux-x64`.

```bash
ludus project package MyGame --profile linux-release --version 0.1.0
ludus project package MyGame --profile linux-release --version 0.1.0 --sdk /path/to/release-sdk
ludus project package verify /path/to/package-directory
```

The command holds the shared Editor/CLI build-tree lock throughout configure,
build and installation. It requires a successful current build, checks the CMake
File API artifact against the installed entry point, and validates SDK stamps
and project descriptor/lock/release configuration. Temporary staging is private
and cleaned on failure. Packages publish as complete directories through an
atomic no-replace rename. Identical verified packages are reused.

Output is `MyGame/out/packages/<profile>/<archive-sha256>/`, containing
`game.zip`, `package.json` and `validation.json`. The SHA256 identifies the exact
archive. ZIP entries are sorted with fixed timestamps and normalized permissions.
This is deterministic archiving of identical payloads, not a claim of reproducible
compilation. Keep `out/` ignored. Do not mutate a completed package: verify and
plan recheck its content rather than treating its pathname as proof of validity.

Extraction validation checks entry count, path lengths, expanded sizes, types,
case collisions, permissions, payload hashes, entry point, notices and native
runtime dependencies. It never executes the binary or invokes `ldd`. Regular
ELF libraries must be explicitly installed under loader search paths using
`$ORIGIN`; symlinks are rejected. Install libraries under the names actually
required by `DT_NEEDED`, including a regular copy for a versioned SONAME.

The initial external runtime policy allows only the Linux C/C++ ABI libraries:
libc, libm, libdl, libpthread, librt, libstdc++, libgcc_s, libc++, libc++abi and
libunwind, with specific SONAMEs recorded in `package_native.py`. Unknown native
dependencies fail until bundled or supported by a reviewed policy revision.
The manifest records the libraries actually required and the SDK's distro and
system prerequisites. SDK headers/static archives, debug sidecars, debug sections
and observed producer paths are rejected. License presence checks do not certify
that the author supplied every legally required notice.

## Inspect an itch.io plan

```bash
ludus project publish plan MyGame --package /path/to/package-directory --destination linux
ludus project publish plan MyGame --package /path/to/package-directory --destination linux --allow-local-inputs
```

Planning validates the archive, manifest, profile, current release configuration
and engine lock. It needs no installed SDK, butler, account login or network.
It reports destination/channel, version, exact digest, payload bytes and a future
butler argument plan with a placeholder for a verified private snapshot.
The reported plan is not an executable upload command.

Packages from dirty/unknown sources or local SDK overrides require the explicit
local-input option. This is a planning choice; no upload occurs. A channel named
`beta` or `staging` does not change the itch.io project's visibility.
`BUTLER_API_KEY` is stripped from project commands and compiler/native inspection
subprocesses. Do not store credentials in the project or its manifests.

Global `--json` emits schema-1 CLI results. Configuration and package failures
use stable `InvalidRelease`, `UnsupportedReleaseTarget`, `InvalidPackage`,
`Conflict`, `BuildFailed`, `SdkIncompatible` and `MissingTools` categories through
the existing host-tooling error boundary. A failed operation never uploads.

## Validation scope

Run `python3 -m unittest discover -s scripts/python -p 'test_ludus*.py' -v` with
the pinned tools on PATH. Release tests compile real native executables, exercise
CMake installation, corruption/failure cases and clean-extraction execution.
They use a small fixture SDK to avoid graphics dependencies and do not establish
relocation or license closure for a full production SDK. Full SDK acceptance,
itch.io transport and real hosted gameplay need their own evidence. See the
[evidence ledger](game-packaging-publishing-evidence.md).
