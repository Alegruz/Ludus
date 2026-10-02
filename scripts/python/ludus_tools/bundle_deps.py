"""Bundle the redistributable dependency closure into an SDK prefix (P02).

A relocatable SDK must link from a fresh extraction with no producer Conan cache
on the search path. The engine's exported ``LudusConfig.cmake`` therefore calls
``find_dependency(volk/freetype/harfbuzz)`` against a **package-local** search
root, ``<prefix>/lib/cmake/Ludus/dependencies``. This module populates that root
at install time by copying each dependency's CMake package files, static
libraries and public headers out of the Conan-provided packages, and rewriting
any absolute producer path (Conan cache / build dir) to a path relative to the
bundled location.

The producer-path rewrite is the error-prone part and is unit-tested directly;
the file-copy orchestration runs during ``install-sdk`` on a machine with Conan.
Nothing here depends on Qt or the engine; it uses only the standard library.
"""

from __future__ import annotations

import re
import shutil
from pathlib import Path
from typing import Iterable, Optional

# The dependencies whose closure must travel with the SDK (see conanfile.py and
# the manifest inventory). volk is a public CONFIG dependency; freetype/harfbuzz
# are PRIVATE deps of Ludus::Text that remain in the final static link line.
BUNDLED_DEPENDENCIES = ("volk", "freetype", "harfbuzz")

# A CMake variable that bundled config files use to locate themselves. Conan's
# generated configs compute an absolute package root; we replace that root with a
# path computed relative to the installed config file so the bundle relocates.
_SELF_DIR = "${CMAKE_CURRENT_LIST_DIR}"


def rewrite_producer_paths(text: str, producer_roots: Iterable[str], package_root_token: str = _SELF_DIR) -> str:
    """Replace absolute producer roots in CMake text with a relocatable token.

    ``producer_roots`` are absolute path prefixes (the Conan cache package root,
    the repo checkout, the build/install dirs) that must not survive into the
    bundled, relocatable config. Each occurrence is replaced with
    ``package_root_token`` so the config resolves relative to its own location.
    Longer roots are replaced first so a nested root is not partially rewritten.
    """
    for root in sorted((r for r in producer_roots if r), key=len, reverse=True):
        text = text.replace(root, package_root_token)
    return text


def find_producer_roots(text: str) -> list[str]:
    """Heuristically find absolute Conan-cache / build package roots in a config.

    Returns the distinct absolute directory prefixes that look like a Conan
    package root (``…/p/<hash>/p`` or a cache ``…/.conan2/…/p``) or an absolute
    path under a build/install tree. Used by the audit so a leaked path is
    reported precisely rather than silently shipped.
    """
    roots: set[str] = set()
    # Conan v2 package roots: /.../p/<pkgfolder>/p  (the trailing /p is the
    # package). The folder token is alphanumeric (a short name + hash).
    for m in re.finditer(r"(/[^\s\"';]*?/p/[0-9a-z]+/p)(?=/|\"|'|\s|$)", text):
        roots.add(m.group(1))
    # Conan v2 build roots: /.../b/<buildfolder>/p
    for m in re.finditer(r"(/[^\s\"';]*?/b/[0-9a-z]+/p)(?=/|\"|'|\s|$)", text):
        roots.add(m.group(1))
    return sorted(roots)


def _copy_tree(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if src.is_dir():
        shutil.copytree(src, dst, dirs_exist_ok=True)
    elif src.is_file():
        shutil.copy2(src, dst)


def _rewrite_cmake_files_in(root: Path, producer_roots: Iterable[str]) -> int:
    """Rewrite producer paths in every *.cmake under ``root``. Returns count."""
    rewritten = 0
    for cmake_file in root.rglob("*.cmake"):
        text = cmake_file.read_text(encoding="utf-8")
        new = rewrite_producer_paths(text, producer_roots)
        if new != text:
            cmake_file.write_text(new, encoding="utf-8")
            rewritten += 1
    return rewritten


def bundle_from_conan_packages(
    *,
    prefix: Path,
    package_dirs: dict[str, Path],
    producer_roots: Iterable[str],
    dependencies: Iterable[str] = BUNDLED_DEPENDENCIES,
) -> list[str]:
    """Copy each dependency's package into the SDK's bundled dependency root.

    ``package_dirs`` maps a dependency name to the root of its Conan package
    directory (the directory that contains ``lib/``, ``include/`` and the CMake
    package files). The copied CMake files have their absolute producer paths
    rewritten to be relative to the bundled location.
    Returns the names of the dependencies actually bundled.
    """
    dep_root = Path(prefix) / "lib" / "cmake" / "Ludus" / "dependencies"
    dep_root.mkdir(parents=True, exist_ok=True)
    bundled: list[str] = []
    for name in dependencies:
        src = package_dirs.get(name)
        if src is None or not Path(src).exists():
            continue
        dest = dep_root / name
        _copy_tree(Path(src), dest)
        _rewrite_cmake_files_in(dest, producer_roots)
        bundled.append(name)
    return bundled


def audit_no_producer_paths(prefix: Path, producer_roots: Iterable[str]) -> list[str]:
    """Return a list of (file: leaked-path) strings for any producer path that
    survived in the bundled dependency CMake files. Empty means clean."""
    dep_root = Path(prefix) / "lib" / "cmake" / "Ludus" / "dependencies"
    leaks: list[str] = []
    roots = [r for r in producer_roots if r]
    if not dep_root.is_dir():
        return leaks
    for cmake_file in dep_root.rglob("*.cmake"):
        text = cmake_file.read_text(encoding="utf-8")
        for root in roots:
            if root in text:
                leaks.append(f"{cmake_file}: {root}")
    return leaks
