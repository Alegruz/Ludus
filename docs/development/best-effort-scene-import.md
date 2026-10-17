# Best-effort pbrt-v4 scene import

Status: Authored-lighting import, October 13, 2026. Importer id `pbrt-best-effort-v1`,
report schema `1`.

`Ludus::SceneCook` has two pbrt paths. `pbrt-reference-v1` is the frozen,
byte-identical cooker for the recorded goldens; it rejects anything it does
not model exactly. `pbrt-best-effort-v1` accepts general pbrt-v4 scenes,
approximates what the runtime cannot represent, fits the result into the
LUSCN01 v3 limits and explains every compromise in a versioned JSON report.
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
  (tone-mapped and clamped for surface textures). Lighting PFM retains linear
  HDR values, byte order, scale and row orientation. EXR is not decoded; surface
  textures fall back to a swatch with `TEXTURE_UNREADABLE`, while an unreadable
  lighting environment rejects the import rather than discarding illumination.

## How it is approximated

| pbrt | Ludus |
| --- | --- |
| `diffuse`, `coateddiffuse` | base colour, roughness from the coat |
| `conductor`, `coatedconductor` | metallic, colour from the named eta/k table |
| `dielectric`, `thindielectric` | F0 0.04, transmission dropped |
| `mix` | the first material |
| other materials | grey default, `MATERIAL_DEFAULTED` |
| area lights | RGB/Y photometric emissive material, source sidedness and optional power; unequal resolved radiances stay separate |
| point, spot, distant lights | transformed world position/direction, RGB candela or lux, optional power/illuminance and spot cone |
| infinite light | authored constant or HDR environment, converted from PBRT equal-area mapping to engine equirectangular mapping |
| unsupported light types | skipped and counted |
| media | skipped |

Roughness follows pbrt: alpha is `sqrt(r)` when `remaproughness` is true and
`r` otherwise; the engine value is `sqrt(alpha)`, clamped to [0.045, 1].
Spectra are mapped to RGB through a small table, and blackbody emitters use a
cited curve fit. Every approximation adds a diagnostic code.

## Authored lighting profile

The shared scene profile is [LUSCN01 version 3](../architecture/content-resources.md), with stable
placed-object and source identities, emission sidedness, requested lumens,
analytic lights, HDR environment pixels and independent material/bake UVs.
Older v1/v2 bundles remain readable with their historical two-sided emission.

PBRT's spectral photometry is approximated by fixed Rec.709 RGB luminance:
source color is divided by `0.2126 R + 0.7152 G + 0.0722 B`, then multiplied by
`scale`. Requested area-light power resolves per placed shape after scaling as
`RGB/Y × scale × lumens / (π × world area × side count)`. Point power divides by
`4π`; distant illuminance is lux. The engine uses squared spot falloff, so power
normalization integrates that lobe rather than PBRT's smoothstep lobe. Outer
half angle is PBRT `coneangle`; inner is `coneangle - conedeltaangle`.

Constant environments normalize RGB/Y, with optional illuminance divided by π.
HDR PFM preserves linear energy; PNG/TGA environments decode sRGB. The source
light transform rotates the environment into world space. Optional HDR
illuminance uses bounded angular quadrature over the source light-space +Z
hemisphere. Lighting files enter the source digest closure, so changed sky bytes
invalidate the import preview. Values above the profile bounds reject, rather
than tone-map the lighting source. One infinite light is supported per PBRT
source scene; multiple directives reject explicitly. Separate imported scenes
combine through the durable scene-asset assembly contract.

The RGB proxy, material approximations, finite light range and coarse triangle
charts prevent pixel parity with PBRT's spectral path tracer. EXR, media,
transmission and textured area emission remain unsupported. See the
[renderer lighting contract](../architecture/renderer-systems.md) for viewport
area-light, shadow and indirect-transport limits.

## Budget planner

The cooked scene must fit 6 meshes, 8 materials, 8 textures, 500k vertices,
128 objects and 64 MiB. Each cooked mesh carries one material, so the planner:

1. reserves one slot per distinct emitter radiance, sidedness and power-normalization
   group; groups record their source shapes and area-light directives in the report;
2. ranks ordinary placements by world-space area and keeps the largest material
   groups in the remaining slots;
3. merges each remaining material into the nearest kept slot
   (`MATERIALS_MERGED`);
4. merges all geometry of a slot into one world-space mesh;
5. gives each triangle unique corners and a separate guttered bake chart, then
   drops the smallest ordinary placements if the expanded vertex budget is exceeded
   (`GEOMETRY_DROPPED`); required emitter geometry is never silently dropped;
6. ranks textures and halves the largest until the byte budget is met
   (`TEXTURE_DOWNSCALED`). Geometry and authored lighting bytes are reserved first.
   Untextured slots get a 4×4 sRGB swatch.

A scene rejects when distinct emitter groups cannot fit the six mesh slots,
required emitter geometry exceeds capacity, lighting exceeds 128 analytic lights,
or authored energy cannot fit the finite profile. It does not merge all emitters
into the brightest light or clamp their energy to make the import succeed.

The output is deterministic: the same input yields the same bundle and
report bytes. Paths in the report are relative to the entry directory.

## Importing in the Editor

World **Import scene…** (and the Level surface's **Import**) accepts any `.pbrt`
file. Ludus::SceneCook picks the importer from the file name:

- `scene-v4.pbrt` is cooked with `pbrt-reference-v1` first, so staircase2 keeps
  its byte-identical reference path. Only when the reference profile rejects
  the scene does the import fall back to `pbrt-best-effort-v1`; the preview
  says why.
- Any other `.pbrt` file uses `pbrt-best-effort-v1`.
- A source directory (the older flow and its tests) stays on the reference
  profile with no fallback.

Choosing a file first runs a dry-run preview on the scene worker thread, with
progress and **Cancel scene operation**. The preview panel lists source and
cooked counts, budget use against the runtime limits and every warning-level
fallback. Nothing is written until **Import previewed scene**; **Discard
preview** drops it.

A best-effort import stores its full report in the scene manifest, beside the
input digests that guard against a source changing mid-import. Opening or
reassembling a World returns these reports as `import_reports`, and the
**Import report** toggle beside the World path opens them in the scrollable
inspector. Reports start collapsed so their length does not squeeze the viewport
or hierarchy controls.
Reference imports have no such report; their approximations still appear as
warnings.

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
