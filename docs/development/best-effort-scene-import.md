# Best-effort pbrt-v4 scene import

Status: Phase 1b, October 13, 2026. Importer id `pbrt-best-effort-v1`,
report schema `1`.

`Ludus::SceneCook` has two pbrt paths. `pbrt-reference-v1` is the frozen,
byte-identical cooker for the recorded goldens; it rejects anything it does
not model exactly. `pbrt-best-effort-v1` accepts general pbrt-v4 scenes,
approximates what the runtime cannot represent, fits the result into the
LUSCN01 v2 limits and explains every compromise in a versioned JSON report.
Both are native C++; neither the cook nor its tests run Python.

## Running it

```sh
ludus-cook pbrt path/to/scene.pbrt out/scene-dir   # writes scene.luscn, import-report.json, LICENSE.txt
ludus-cook pbrt path/to/scene.pbrt --dry-run       # parses and plans; prints the report only
```

A dry run reads geometry (so counts and budgets are real) but decodes no
images and packs no bundle.

## What is read

- A streaming, locale-free tokenizer (64 KiB window, 4096-byte token limit)
  with `file:line` on every diagnostic.
- Graphics state: every transform directive, `AttributeBegin`/`End`,
  `TransformBegin`/`End`, `CoordinateSystem`/`CoordSysTransform`,
  `ReverseOrientation`, named materials, textures and media.
- `Include` and `Import`, contained in the entry file's directory, limited to
  a depth of 32 and with cycle detection. A failed include is a warning; a
  syntax error stops only the file that contains it.
- `ObjectBegin`/`ObjectInstance` are kept as objects and instances in the IR
  and expanded when meshes are baked.
- Shapes: `plymesh` (ASCII, binary little- and big-endian; cached per file),
  `trianglemesh`, `bilinearmesh`, `disk` and `sphere` (tessellated) and the
  `loopsubdiv` control cage. Curves and other shapes are skipped and counted.
- Images: TGA (including RLE), PNG through vendored `stb_image` and PFM
  (tone-mapped and clamped). EXR is not decoded; such textures fall back to a
  swatch with `TEXTURE_UNREADABLE`.

## How it is approximated

| pbrt | Ludus |
| --- | --- |
| `diffuse`, `coateddiffuse` | base colour, roughness from the coat |
| `conductor`, `coatedconductor` | metallic, colour from the named eta/k table |
| `dielectric`, `thindielectric` | F0 0.04, transmission dropped |
| `mix` | the first material |
| other materials | grey default, `MATERIAL_DEFAULTED` |
| area lights | emissive material (`L` × `scale`), clamped to 10000 |
| infinite light | environment fallback in the report |
| other lights | skipped and counted |
| media | skipped |

Roughness follows pbrt: alpha is `sqrt(r)` when `remaproughness` is true and
`r` otherwise; the engine value is `sqrt(alpha)`, clamped to [0.045, 1].
Spectra are mapped to RGB through a small table, and blackbody emitters use a
cited curve fit. Every approximation adds a diagnostic code.

## Budget planner

The cooked scene must fit 6 meshes, 8 materials, 8 textures, 500k vertices,
128 objects and 64 MiB. Each cooked mesh carries one material, so the planner:

1. ranks placements by world-space area;
2. keeps the largest material groups as slots (five when the scene emits
   light, the sixth reserved for the brightest emitter);
3. merges each remaining material into the nearest kept slot
   (`MATERIALS_MERGED`);
4. merges all geometry of a slot into one world-space mesh;
5. drops the smallest placements if the vertex budget is exceeded
   (`GEOMETRY_DROPPED`);
6. ranks textures and halves the largest until the byte budget is met
   (`TEXTURE_DOWNSCALED`). Untextured slots get a 4×4 sRGB swatch.

The output is deterministic: the same input yields the same bundle and
report bytes. Paths in the report are relative to the entry directory.

## Tests

`ludus_best_effort_import_tests` writes a synthetic fixture from C++ that
covers nested, cyclic and escaping includes, every shape and image format,
fallback materials, lights, media and instancing. It checks diagnostics,
runtime decoding, determinism, merging, budget drops and dry runs.

Configure with `-DLUDUS_BUILD_SCENE_COOK_FUZZERS=ON` (native Clang) to build
`ludus_scene_cook_pbrt_fuzzer`, a libFuzzer target over the pbrt reader, PLY
reader and image decoders.

### Local smoke test with licensed scenes

Licensed scenes are never committed. To import them locally:

```sh
LUDUS_PBRT_SCENES_DIR=~/Ludus/references/resources/scenes \
  ctest --test-dir out/build/linux-clang-development -R best_effort --output-on-failure
LUDUS_PBRT_SCENES_DIR=... LUDUS_PBRT_SMOKE_OUTPUT=/tmp/staircase \
  out/build/.../ludus_best_effort_import_tests "[licensed]"
```

The test skips when `staircase/scene-v4.pbrt` is missing under the directory.
