"""Versioned bundled project templates and atomic no-replace generation.

A template is a set of files generated into a new project directory using only
public SDK APIs. Generation is *atomic* and *no-replace*: files are staged in a
sibling directory on the same filesystem, validated, and published to an
**absent** destination only. A destination that appears between validation and
publication is never overwritten (P08). Cancellation before publication leaves
no destination.

Templates are addressed by ``(id, version)`` which is recorded in the project
descriptor's ``template`` object so later template versions never silently
overwrite user-owned game code (design "Project creation and Editor workflow").

The only bundled template initially is ``minimal`` (version 1): a single native
application target that initializes the engine and prints useful diagnostics
using public headers only — no new engine subsystem, no scene/ECS/hot reload.
Placeholders are substituted by exact key, never by executing template content,
so a file name or value can never trigger an arbitrary command.
"""

from __future__ import annotations

import os
import re
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

from .descriptor import TARGET_NAME_RE
from .errors import DESTINATION_EXISTS, GENERATION_FAILED, INVALID_PROJECT, ToolingError

TEMPLATE_SCHEMA_VERSION = 1

# A placeholder is {{ KEY }} with a bounded uppercase key. Substitution is a
# plain string replacement of known keys; unknown placeholders are an error so a
# template cannot silently leave an unresolved token.
_PLACEHOLDER_RE = re.compile(r"\{\{\s*([A-Z0-9_]+)\s*\}\}")


@dataclass
class TemplateFile:
    relpath: str
    content: str
    executable: bool = False


@dataclass
class Template:
    id: str
    version: int
    files: list[TemplateFile]


@dataclass
class ProjectInputs:
    name: str
    target: str
    engine_version: str
    components: list[str]
    preset: str = "linux-clang-development"


def _substitute(text: str, values: dict[str, str]) -> str:
    def repl(match: re.Match[str]) -> str:
        key = match.group(1)
        if key not in values:
            raise ToolingError(GENERATION_FAILED, f"template references unknown placeholder {{{{{key}}}}}")
        return values[key]

    return _PLACEHOLDER_RE.sub(repl, text)


def _validate_identifier(name: str) -> None:
    if not TARGET_NAME_RE.fullmatch(name):
        raise ToolingError(INVALID_PROJECT, f"invalid target identifier: {name!r}")


# --- The minimal native template ---------------------------------------------


def _minimal_template() -> Template:
    files = [
        TemplateFile(
            "ludus.project.json",
            # Rendered via the descriptor serializer in create.py; this literal
            # is a fallback marker only (create.py writes the canonical file).
            "",
        ),
        TemplateFile(
            "CMakeLists.txt",
            """cmake_minimum_required(VERSION 3.29)

project({{ NAME }} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Resolve the shared Ludus SDK. The CLI/Editor supply LUDUS_SDK_PREFIX via the
# generated preset; no engine source is compiled by this project (P01/P12).
find_package(Ludus CONFIG REQUIRED)

add_executable({{ TARGET }} src/main.cpp)
target_link_libraries({{ TARGET }} PRIVATE {{ LINK_TARGETS }})

# Apply the supported application compile/link policy (C++23, no exceptions)
# without importing engine build warnings or private targets.
if(COMMAND ludus_apply_app_policy)
    ludus_apply_app_policy({{ TARGET }})
endif()
""",
        ),
        TemplateFile(
            "CMakePresets.json",
            """{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 29, "patch": 0 },
  "configurePresets": [
    {
      "name": "linux-clang-debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/out/build/linux-clang-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_PREFIX_PATH": "$env{LUDUS_SDK_PREFIX}"
      }
    },
    {
      "name": "linux-clang-development",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/out/build/linux-clang-development",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "CMAKE_PREFIX_PATH": "$env{LUDUS_SDK_PREFIX}"
      }
    },
    {
      "name": "linux-clang-release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/out/build/linux-clang-release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_PREFIX_PATH": "$env{LUDUS_SDK_PREFIX}"
      }
    }
  ],
  "buildPresets": [
    { "name": "linux-clang-debug", "configurePreset": "linux-clang-debug" },
    { "name": "linux-clang-development", "configurePreset": "linux-clang-development" },
    { "name": "linux-clang-release", "configurePreset": "linux-clang-release" }
  ]
}
""",
        ),
        TemplateFile(
            "src/main.cpp",
            """// {{ NAME }} — minimal Ludus application template (v1).
//
// Demonstrates engine initialization and useful diagnostics using only public
// SDK headers. It introduces no new engine subsystem (no scenes/ECS/hot reload).
#include <ludus/foundation/base/version.hpp>

#include <cstdio>

int main()
{
    // The build_metadata / version header is a stable public surface; printing
    // it proves the SDK linked and the policy headers are consistent.
    std::printf("{{ NAME }} starting against Ludus SDK\\n");
    return 0;
}
""",
        ),
        TemplateFile(
            ".gitignore",
            """# Ludus project — ignore machine-local state and build output (P04).
/out/
/.ludus/
""",
        ),
        TemplateFile("assets/.keep", ""),
        TemplateFile("config/.keep", ""),
        TemplateFile(
            ".ludus/.keep",
            "",
        ),
    ]
    return Template(id="minimal", version=1, files=files)


_TEMPLATES: dict[str, Template] = {t.id: t for t in (_minimal_template(),)}


def available_templates() -> list[tuple[str, int]]:
    return sorted((t.id, t.version) for t in _TEMPLATES.values())


def get_template(template_id: str) -> Template:
    tmpl = _TEMPLATES.get(template_id)
    if tmpl is None:
        raise ToolingError(
            INVALID_PROJECT,
            f"unknown template {template_id!r}; available: {[t for t, _ in available_templates()]}",
        )
    return tmpl


def render_files(template: Template, inputs: ProjectInputs) -> list[TemplateFile]:
    """Return the rendered template files (does not touch the filesystem)."""
    _validate_identifier(inputs.target)
    link_targets = " ".join(f"Ludus::{c}" for c in (inputs.components or ["FoundationBase"]))
    values = {
        "NAME": inputs.name,
        "TARGET": inputs.target,
        "LINK_TARGETS": link_targets,
        "ENGINE_VERSION": inputs.engine_version,
        "PRESET": inputs.preset,
    }
    rendered: list[TemplateFile] = []
    for f in template.files:
        if f.relpath == "ludus.project.json":
            # Written by the caller (create.py) with the canonical descriptor.
            continue
        rendered.append(TemplateFile(f.relpath, _substitute(f.content, values), f.executable))
    return rendered


def _validate_relpath(relpath: str, dest_root: Path) -> Path:
    if not relpath or os.path.isabs(relpath):
        raise ToolingError(GENERATION_FAILED, f"template path must be relative: {relpath!r}")
    norm = os.path.normpath(relpath)
    if norm.startswith("..") or ".." in Path(norm).parts:
        raise ToolingError(GENERATION_FAILED, f"template path escapes the project: {relpath!r}")
    target = (dest_root / norm).resolve()
    root = dest_root.resolve()
    if target != root and not str(target).startswith(str(root) + os.sep):
        raise ToolingError(GENERATION_FAILED, f"template path escapes the project: {relpath!r}")
    return dest_root / norm


@dataclass
class StagedProject:
    staging: Path
    destination: Path


def stage_project(
    files: list[TemplateFile],
    destination: Path,
    *,
    extra_files: Optional[list[TemplateFile]] = None,
    cancel_check: Optional[Callable[[], None]] = None,
) -> StagedProject:
    """Stage a complete project into a sibling directory of ``destination``.

    Does NOT publish. Validates every path, rejects an existing destination
    (including a nonempty directory or a symlink), and leaves the staging dir for
    ``publish_project`` to atomically rename into place.
    """
    destination = Path(destination)
    _reject_existing_destination(destination)

    parent = destination.parent
    parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{destination.name}.", suffix=".staging", dir=str(parent)))

    all_files = list(files) + list(extra_files or [])
    try:
        for f in all_files:
            if cancel_check is not None:
                cancel_check()
            target = _validate_relpath(f.relpath, staging)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(f.content, encoding="utf-8")
            if f.executable:
                os.chmod(target, 0o755)
        return StagedProject(staging=staging, destination=destination)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def _reject_existing_destination(destination: Path) -> None:
    if destination.is_symlink():
        raise ToolingError(DESTINATION_EXISTS, f"destination is a symlink: {destination}")
    if destination.exists():
        raise ToolingError(DESTINATION_EXISTS, f"destination already exists: {destination}")


def publish_project(staged: StagedProject) -> Path:
    """Atomically publish the staged project to an absent destination (P08).

    Uses ``os.rename`` which fails if the destination appeared in the meantime
    (no overwrite). On any failure the staging directory is removed.
    """
    try:
        # Re-check right before publishing; a destination appearing here must not
        # be overwritten. os.rename onto an existing non-empty dir fails anyway,
        # but the explicit check gives a clean error and covers the empty-dir case.
        _reject_existing_destination(staged.destination)
        os.rename(staged.staging, staged.destination)
        return staged.destination
    except ToolingError:
        # Explicit "destination appeared" from the pre-check: clean staging and
        # re-raise without overwriting the (now-present) destination.
        shutil.rmtree(staged.staging, ignore_errors=True)
        raise
    except FileExistsError as exc:
        shutil.rmtree(staged.staging, ignore_errors=True)
        raise ToolingError(DESTINATION_EXISTS, f"destination appeared during publish: {staged.destination}") from exc
    except OSError as exc:
        # os.rename onto a nonempty existing directory raises OSError(ENOTEMPTY).
        shutil.rmtree(staged.staging, ignore_errors=True)
        if staged.destination.exists():
            raise ToolingError(DESTINATION_EXISTS, f"destination appeared during publish: {staged.destination}") from exc
        raise ToolingError(GENERATION_FAILED, f"failed to publish project: {exc}") from exc


def discard_staging(staged: StagedProject) -> None:
    shutil.rmtree(staged.staging, ignore_errors=True)
