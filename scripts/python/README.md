# ludus-tools

The Ludus host tooling: the single project/SDK operation backend shared by the
installed `ludus` CLI and the optional Editor. It installs independently of a
Ludus engine source checkout and has **no Qt dependency** (requirements
P07/P11), using only the Python standard library.

## Install

```bash
python3 -m pip install .
ludus --help
```

See `docs/development/project-sdk-workflow.md` in the Ludus repository for the
full installation, SDK override/refresh, project creation, migration, direct
CMake and recovery documentation.

## Native release packaging

The installed CLI supports optional `project create --release`, native
`project package`, `project package verify`, and offline `project publish plan`.
See [the release guide](../../docs/development/game-packaging-publishing.md).
The wheel ships the standalone CMake File API resolver; these operations need
no Qt or engine checkout. Live itch.io uploads are a subsequent phase.
