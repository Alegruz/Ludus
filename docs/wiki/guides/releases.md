# Package a game

Development builds and player packages have different purposes. A Release
package contains the game's explicitly installed executable, runtime assets and
required notices. A completed build directory is not automatically a package.

## Prepare release files

Open a clean, saved compatible CMake project. In the editor choose
**Release → Set Up Releases**, select the supported platform and review the
generated release files. Setup preserves game source and refuses to overwrite
existing authored release files.

Complete the generated installation rules and asset/dependency notices. A
license file's presence does not prove the inventory is complete.

## Build and inspect

Choose **Release → Package Release**, select the configured release profile and
version, and provide a matching Release SDK when needed. Follow Output for the
verified package directory and archive digest.

Equivalent native CLI steps:

```bash
ludus project package /path/to/MyGame --profile linux-release --version 0.1.0
ludus project package verify /path/to/package-directory
```

Packaging verifies the current build and installed payload, stages privately,
and publishes a completed package. It does not upload it. Do not modify an
already verified package in place; rebuild and verify a new result.

## Publish deliberately

The installed CLI can plan and explicitly submit supported itch.io uploads.
Generated game-release workflows use separate build/verify/upload jobs and
environment-scoped credentials. Configure the game repository's destination,
SDK/tool inputs and release environment according to the detailed guide.

An upload submission is not proof of hosted processing or gameplay. Test the
extracted package and the hosted game separately.

## This wiki has its own deployment

Ludus Wiki uses GitHub Pages for documentation. Game packaging, game uploads and
the wiki's static deployment are independent workflows.

References: [native packaging](https://github.com/Alegruz/Ludus/blob/main/docs/development/game-packaging-publishing.md)
and [editor/native/browser release setup](https://github.com/Alegruz/Ludus/blob/main/docs/development/editor-game-releases.md).
