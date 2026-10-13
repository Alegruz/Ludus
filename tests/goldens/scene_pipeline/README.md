# Scene pipeline goldens (Phase 0)

Byte-exact goldens captured from the retired Python asset pipeline (`scene_cook.py`,
`scene_assets.py`, `scene_editor.py`) before it was ported to C++
(`modules/content/scene_cook`). The Python sources, `fixture.py` and `capture.py`
have been removed; the files here are frozen and are verified only by native tests.

| Path | Contents |
|---|---|
| `source/` | Deterministic synthetic **staircase2-profile** PBRT/PLY/TGA source (CC0, no third-party data), generated once by the retired `fixture.py`. It covers every accepted branch of the cooker: the wallpaper atlas clip, wood and tile coated diffuse, a conductor, solid colours, degenerate faces, quad and `s t` PLY, and the PLY-rectangle, trianglemesh-quad and disk emitters. |
| `expected/goldens.json` | SHA-256 and size of every output file, the fixture tree, and the digests of the Python sources at capture time. |
| `expected/files/` | Full bytes of every output ≤ 64 KiB (manifests, catalog, levels, material/mesh/licence artifacts, texture headers). Generated C/C++ headers are stored with an extra `.golden` suffix so formatters and analysis tools leave their bytes alone. Larger outputs (the 1.1 MB `scene.luscn` and the wallpaper atlas texture) are checked by digest only. |
| `expected/files/editor/` | `scene_editor.operate` responses for import → commit-import → save → open → assemble (renamed object) → save, the candidate catalog, and `scenes.json` with the digest of each assembled scene. |
| `licensed/staircase2.json` | Digest-only goldens for the licensed staircase2 scene. Not checked in CI. |

## Texture cooker goldens

`cmake/shaders/cook_texture.py` is still Python until it is ported, so
`test_texture_goldens.py` (ctest `ludus_texture_goldens`) re-cooks the texture cases and
checks `expected/files/texture/` byte for byte. It needs Python 3.12 or later and is
deleted together with the texture cooker once the C++ port checks the same files.

## Native consumers

- `ludus_scene_cook_tests` cooks `source/` and checks `cook/scene.luscn` (digest and
  size), `cook/manifest.json` and `cook/LICENSE.txt` byte for byte. The same check is
  available from the command line: `ludus-cook cook tests/goldens/scene_pipeline/source OUT`.
- `ludus_scene_assets_tests` runs new → import (twice) → save → open → assemble → dump →
  publish and checks every `project/`, `release/`, `saved/`, `assemble/` and `dump/`
  output, then replays the Editor sequence against `expected/files/editor/`.
- The `cook_texture.py` texture goldens (`expected/files/texture/`) currently have no
  native verifier; they remain as reference data until the texture cooker is ported.

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
ctest --preset linux-clang-development -L goldens --output-on-failure
```

The goldens are frozen. A deliberate format change must update the expected files and
explain the difference in review; there is no capture script any more.
