#!/usr/bin/env python3
"""Capture or check byte-exact goldens of the Python scene pipeline (design Phase 0).

These goldens freeze the observable bytes of:
  * scripts/python/ludus_tools/scene_cook.py   (pbrt-reference-v1 cook)
  * scripts/python/ludus_tools/scene_assets.py (catalog, level, assemble, dump, publish)
  * cmake/shaders/cook_texture.py              (build-time texture mip cooker)
so that a later C++ port can be verified byte-for-byte before the Python is removed.

Normalization (a port must reproduce these to compare):
  * uuid.uuid4() returns UUID(int=1), UUID(int=2), ... in call order.
  * The resolved source origin recorded in scene manifests is "/ludus-golden/source".
  * The source directory name passed to the cooker is "source".
  * build.importer_sha256/publisher_sha256 are digests of the Python files themselves and
    are recorded in goldens.json under "python_files"; editing those files intentionally
    changes the goldens (recapture deliberately with --write and review the diff).

Usage:
  capture.py --write                 # regenerate expected/ (review the diff!)
  capture.py --check                 # exit 1 on any difference
  capture.py --licensed DIR [--write-licensed]   # staircase2 digests; see README.md ("Licensed staircase2")
"""
import argparse
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path, PosixPath
import shutil
import subprocess
import sys
import tempfile
from unittest.mock import patch
import uuid

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "scripts/python"))
sys.path.insert(0, str(HERE))

import fixture  # noqa: E402
from ludus_tools import scene_assets as assets  # noqa: E402
from ludus_tools import scene_cook  # noqa: E402

EXPECTED = HERE / "expected"
SOURCE = HERE / "source"
VIRTUAL_ORIGIN = "/ludus-golden/source"
INLINE_LIMIT = 64 * 1024
TEXTURE_SOURCES = {
    "l3_color": ROOT / "tests/sdk_consumer/l3-color.texture.json",
    "l4_bake": ROOT / "tests/sdk_consumer/l4-bake.texture.json",
}
TEXTURE_SYNTHETIC = {
    "srgb_color_odd": {"width": 5, "height": 3, "format": "Rgba8Srgb", "semantic": "color",
                       "pixels": [(i * 37) % 256 for i in range(5 * 3 * 4)]},
    "rg_data": {"width": 3, "height": 2, "format": "Rg8Unorm", "semantic": "data",
                "pixels": [0, 255, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170]},
    "normal": {"width": 2, "height": 2, "format": "Rgba8Unorm", "semantic": "normal",
               "pixels": [255, 128, 128, 255, 128, 255, 128, 255, 0, 128, 128, 255, 128, 0, 128, 255]},
    "mask": {"width": 4, "height": 2, "format": "R8Unorm", "semantic": "mask", "cutoff": 0.5,
             "pixels": [255, 255, 255, 0, 0, 128, 200, 30]},
}


# Golden copies of generated C/C++ headers are stored with a ".golden" suffix so repository
# formatters, header self-sufficiency checks and static analysis never touch their bytes.
CXX_SUFFIXES = {".m", ".mm", ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}


def stored(name):
    path = EXPECTED / "files" / name
    return path.with_name(path.name + ".golden") if path.suffix in CXX_SUFFIXES else path


def float_sum_semantics():
    """Python 3.12 made float sum() compensated (Neumaier); cook_texture.py output depends on it."""
    return "compensated" if sum([1e16, 1.0, -1e16]) == 1.0 else "naive"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def tree(root):
    """Every regular file below root: relative path -> {sha256, bytes}."""
    root = Path(root)
    return {p.relative_to(root).as_posix(): {"sha256": sha(p.read_bytes()), "bytes": p.stat().st_size}
            for p in sorted(root.rglob("*")) if p.is_file()}


class _Uuids:
    def __init__(self):
        self.next = 0

    def __call__(self):
        self.next += 1
        return uuid.UUID(int=self.next)


def _origin_path(real_source):
    real = str(Path(real_source).resolve())

    class GoldenPath(type(Path())):
        if sys.version_info < (3, 12):
            _flavour = type(Path())._flavour

        def resolve(self, strict=False):
            # Only the provenance string computed in prepare_import/commit_import
            # (`origin = str(Path(source).resolve())`) is virtualized; file access is real.
            resolved = super().resolve(strict)
            caller = sys._getframe(1).f_code
            if str(resolved) == real and caller.co_name in ("prepare_import", "commit_import") \
                    and caller.co_filename == assets.__file__:
                return PosixPath(VIRTUAL_ORIGIN)
            return resolved
    return GoldenPath


def run_scene(source, work):
    """Run the Python pipeline on a staircase2-profile source; return outputs + collected files."""
    work = Path(work)
    files = {}
    with ExitStack() as stack:
        stack.enter_context(patch.object(assets.uuid, "uuid4", _Uuids()))
        stack.enter_context(patch.object(assets, "Path", _origin_path(source)))
        cook_out = work / "cook"
        manifest = scene_cook.cook(Path(source), cook_out)
        for name in ("scene.luscn", "manifest.json", "LICENSE.txt"):
            files["cook/" + name] = (cook_out / name).read_bytes()
        project = work / "project"
        project.mkdir()
        level = assets.new_level("Golden")
        level = assets.import_scene(project, Path(source), level)
        saved = assets.save_level(project, "scenes/main.lulevel", level)
        reopened = assets.open_level(project, "scenes/main.lulevel")
        if reopened != level:
            raise AssertionError("reopened level differs")
        # A second import of the same origin must reuse logical identities.
        level2 = assets.import_scene(project, Path(source), assets.new_level("Second"))
        assets.save_level(project, "scenes/second.lulevel", level2)
        files["assemble/main.luscn"] = assets.assemble(project, level)
        files["dump/main.json"] = assets.canonical(assets.dump(project, level))
        assets.publish(project, level, work / "release")
    for relative, record in tree(project).items():
        files["project/" + relative] = (project / relative).read_bytes()
    for relative in tree(work / "release"):
        files["release/" + relative] = (work / "release" / relative).read_bytes()
    files["saved/main.lulevel"] = saved
    return manifest, files


def run_textures(work):
    files = {}
    cases = dict(TEXTURE_SOURCES)
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    for name, document in TEXTURE_SYNTHETIC.items():
        path = work / f"{name}.texture.json"
        path.write_text(json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n")
        cases[name] = path
    for name, path in sorted(cases.items()):
        out = work / "out" / name
        subprocess.run([sys.executable, str(ROOT / "cmake/shaders/cook_texture.py"), "--source", str(path),
                        "--name", name, "--output", str(out)], check=True)
        for relative in tree(out):
            files[f"texture/{name}/{relative}"] = (out / relative).read_bytes()
        if name in TEXTURE_SYNTHETIC:
            files[f"texture/{name}/source.texture.json"] = path.read_bytes()
    return files


def capture():
    with tempfile.TemporaryDirectory(prefix="ludus-golden-") as directory:
        work = Path(directory)
        source = fixture.write(work / "source")
        generated = tree(source)
        _, files = run_scene(source, work / "scene")
        files.update(run_textures(work / "texture"))
    python = {p: sha((ROOT / p).read_bytes()) for p in (
        "scripts/python/ludus_tools/scene_cook.py", "scripts/python/ludus_tools/scene_assets.py",
        "cmake/shaders/cook_texture.py")}
    goldens = {
        "schema": 1,
        "normalization": {"uuid4": "UUID(int=n), n=1,2,... in call order", "source_origin": VIRTUAL_ORIGIN,
                          "source_directory_name": "source",
                          "float_sum": float_sum_semantics()},
        "python_files": python,
        "source": generated,
        "outputs": {name: {"sha256": sha(data), "bytes": len(data), "inline": len(data) <= INLINE_LIMIT}
                    for name, data in sorted(files.items())},
    }
    return goldens, files


def write(goldens, files):
    if EXPECTED.exists():
        shutil.rmtree(EXPECTED)
    EXPECTED.mkdir()
    for name, data in files.items():
        if len(data) <= INLINE_LIMIT:
            path = stored(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    (EXPECTED / "goldens.json").write_text(json.dumps(goldens, indent=1, sort_keys=True) + "\n")
    if SOURCE.exists():
        shutil.rmtree(SOURCE)
    fixture.write(SOURCE)


def differences(goldens, files):
    expected = json.loads((EXPECTED / "goldens.json").read_text())
    problems = []
    for key in ("schema", "normalization", "python_files", "source"):
        if expected.get(key) != goldens[key]:
            problems.append(f"{key} differs")
    old, new = expected["outputs"], goldens["outputs"]
    for name in sorted(set(old) | set(new)):
        if old.get(name) != new.get(name):
            problems.append(f"output {name}: expected {old.get(name)}, got {new.get(name)}")
        elif new[name]["inline"] and stored(name).read_bytes() != files[name]:
            problems.append(f"inline file {name} differs")
    if tree(SOURCE) != goldens["source"]:
        problems.append("committed source/ does not match fixture.py")
    return problems


def licensed(directory, write_back):
    """Digest-only goldens for the licensed staircase2; bytes are never committed."""
    directory = Path(directory).resolve()
    if directory.name != "staircase2":
        raise SystemExit("pass the staircase2 directory (references/resources/scenes/staircase2)")
    with tempfile.TemporaryDirectory(prefix="ludus-golden-licensed-") as work:
        manifest, files = run_scene(directory, work)
    record = {"schema": 1, "scene": "staircase2", "source_inputs": manifest["inputs"],
              "python_files": {p: sha((ROOT / p).read_bytes()) for p in (
                  "scripts/python/ludus_tools/scene_cook.py", "scripts/python/ludus_tools/scene_assets.py")},
              "outputs": {name: {"sha256": sha(data), "bytes": len(data)} for name, data in sorted(files.items())}}
    path = HERE / "licensed/staircase2.json"
    if write_back:
        path.write_text(json.dumps(record, indent=1, sort_keys=True) + "\n")
        print(f"wrote {path}")
        return 0
    expected = json.loads(path.read_text())
    if expected.get("outputs") is None:
        print("licensed/staircase2.json has no captured digests yet; run with --write-licensed")
        return 2
    if expected != record:
        print("staircase2 digests differ")
        return 1
    print("staircase2 digests match")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true")
    group.add_argument("--check", action="store_true")
    group.add_argument("--licensed", type=Path)
    parser.add_argument("--write-licensed", action="store_true")
    args = parser.parse_args()
    if args.licensed:
        return licensed(args.licensed, args.write_licensed)
    goldens, files = capture()
    if args.write:
        write(goldens, files)
        print(f"wrote {len(files)} outputs to {EXPECTED}")
        return 0
    problems = differences(goldens, files)
    for problem in problems:
        print(problem)
    print("goldens match" if not problems else f"{len(problems)} golden difference(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
