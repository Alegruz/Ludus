# Scene pipeline goldens (Phase 0)

Byte-exact goldens of the current Python asset pipeline, captured before it is ported to
C++ (the planned `modules/content/scene_cook`). The port must reproduce every byte recorded
here before `scene_cook.py` / `scene_assets.py` / `scene_editor.py` are removed.

| Path | Contents |
|---|---|
| `fixture.py` | Deterministic synthetic **staircase2-profile** PBRT/PLY/TGA source (CC0, no third-party data). It covers every accepted branch of `scene_cook._cook`: the wallpaper atlas clip, wood and tile coated diffuse, a conductor, solid colours, degenerate faces, quad and `s t` PLY, and the PLY-rectangle, trianglemesh-quad and disk emitters. |
| `source/` | The committed output of `fixture.py`, so a non-Python port can consume it directly. |
| `capture.py` | Runs `scene_cook.cook`, then `scene_assets` `new_level` → `import_scene` (twice) → `save_level` → `open_level` → `assemble` → `dump` → `publish`, plus `cmake/shaders/cook_texture.py` on the two in-repo texture sources and four synthetic cases. |
| `expected/goldens.json` | SHA-256 and size of every output file, the fixture tree, and the digests of the Python sources. |
| `expected/files/` | Full bytes of every output ≤ 64 KiB (manifests, catalog, levels, material/mesh/licence artifacts, texture headers). Generated C/C++ headers are stored with an extra `.golden` suffix so formatters and analysis tools leave their bytes alone. Larger outputs (the 1.1 MB `scene.luscn` and the wallpaper atlas texture) are checked by digest only. |
| `licensed/staircase2.json` | Digest-only goldens for the licensed staircase2 scene. Not captured in CI. |

## Normalization a port must apply

- `uuid4()` returns `UUID(int=1)`, `UUID(int=2)`, … in call order (`scene_assets.new_level`,
  import namespaces, object IDs).
- The resolved source origin stored in the scene manifest (`source_origin`) is
  `/ludus-golden/source`.
- The source directory is named `source` (the cook manifest records `source.name`).
- `build.importer_sha256` and `build.publisher_sha256` are the SHA-256 digests of
  `scene_cook.py` and `scene_assets.py` at capture time; they are recorded in
  `goldens.json["python_files"]`. A C++ port must emit these digests as frozen constants for
  `pbrt-reference-v1` to keep artifact keys stable.

- **Floating-point summation.** Python 3.12 changed the built-in `sum()` of floats to
  compensated (Neumaier) summation. `cook_texture.py` output depends on it: the 3×2
  `rg_data` case produces byte `127` on Python 3.10 and `128` on 3.12+. The goldens are
  captured with the compensated semantics (`goldens.json["normalization"]["float_sum"]`).
  On older interpreters the suite is skipped rather than reporting false failures. A C++
  port must implement Neumaier summation wherever the Python uses `sum()` over floats
  (`cook_texture.py`, and `scene_cook`'s `dot`, `point`, `vector` and `rectangle` area),
  and evaluate in double precision, in Python's operand order.

## Commands

```bash
python3 -m unittest discover -s tests/goldens/scene_pipeline -v   # check
python3 tests/goldens/scene_pipeline/capture.py --check            # same, CLI
python3 tests/goldens/scene_pipeline/capture.py --write            # deliberate recapture; review the diff
```

Licensed staircase2: on a machine that has the scene, run this to record its digests:

```bash
python3 tests/goldens/scene_pipeline/capture.py \
  --licensed references/resources/scenes/staircase2 --write-licensed
LUDUS_STAIRCASE2_DIR=references/resources/scenes/staircase2 \
  python3 -m unittest discover -s tests/goldens/scene_pipeline -v
```

Only digests and the input hashes are recorded; no licensed bytes are committed.
