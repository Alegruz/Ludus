"""Ludus host tooling package.

This package is the installable host-tooling layer for the independent game
project / shared SDK workflow (``.kiro/specs/project-sdk-workflow``). It is the
single project-operation backend shared by the installed ``ludus`` CLI and the
optional Editor (requirement P07): both import these modules rather than
re-implementing resolution, descriptor parsing, SDK installation, CMake planning
or process supervision.

Design constraints that this package enforces:

* No engine-checkout dependency. Installed tooling locates its own resources and
  never imports adapters from a Ludus source tree (P11).
* No Qt dependency. The CLI and these modules work without a display (P07/P11).
* Pure-ish validation and explicit, bounded I/O. Downloads and engine builds are
  never triggered by ordinary open/build/run (P05/P09).

The modules intentionally mirror the existing repository scripts
(``editor_project.py``, ``cmake_targets.py``, ``editor_tool.py``,
``engine.py``) so repository scripts remain compatible entry points while the
installed package carries the same logic.
"""

from __future__ import annotations

__all__ = [
    "__version__",
]

# Host-tooling distribution version. Independent of the engine version: a game's
# engine requirement lives in its lock, this is the tooling/protocol surface.
__version__ = "0.1.0"

# Supported host-tool / template / protocol compatibility surface. The CLI and
# Editor reject an unsupported descriptor or backend protocol with an actionable
# version message (design "Shared backend and public commands").
TOOLING_PROTOCOL_VERSION = 1
SUPPORTED_DESCRIPTOR_VERSIONS = (1, 2)
TEMPLATE_SCHEMA_VERSION = 1
LOCK_SCHEMA_VERSION = 1
LOCAL_SETTINGS_SCHEMA_VERSION = 1
MANIFEST_SCHEMA_VERSION = 2
CATALOG_SCHEMA_VERSION = 1
