# ludus-tools

The Ludus host tooling: the single project/SDK operation backend shared by the
installed `ludus` CLI and the optional Editor. It installs independently of a
Ludus engine source checkout and has **no Qt dependency** (requirements
P07/P11), using only the Python standard library.

## Install

From the Ludus checkout root:

```bash
python3 -m venv out/ludus-cli-venv
out/ludus-cli-venv/bin/python -m pip install ./scripts/python
out/ludus-cli-venv/bin/ludus --help
```

See [independent game projects](project-sdk-workflow.md) for the
full installation, SDK override/refresh, project creation, migration, direct
CMake and recovery documentation.

## Native release packaging

The installed CLI supports optional `project create --release`, native
`project package`, `project package verify`, and offline `project publish plan`.
See [the release guide](game-packaging-publishing.md).
The wheel ships the standalone CMake File API resolver; these operations need
no Qt or engine checkout. Explicit uploads use `project publish upload`; see the release setup guide below.

Existing-project Editor setup, browser packaging and CI uploading: see
[Editor-managed game releases](editor-game-releases.md).
