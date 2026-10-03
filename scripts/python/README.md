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
