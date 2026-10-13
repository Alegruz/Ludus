# Cook and capture staircase2

Convert the locally supplied **Modern Hall / staircase2** scene and render it
through Ludus's L6 reference renderer. This native installed-SDK sample selects
**CPU-oracle traversal explicitly**. It produces a complete linear HDR capture;
its PNG preview is a separate, documented display conversion.

![Staircase2 captured by Ludus](images/staircase2-reference.png)

Ludus CPU-oracle capture: 512 × 512, 256 samples/pixel, depth four, seed 871.
Preview exposure one; no denoising. Modern Hall by NewSee2l035, CC BY 3.0;
conversion and display changes are described below.

This is a scene-specific offline vertical slice. The private sample bundle
does not itself implement the project workflow. Stage 5 now reuses its converter
inside the installed backend and integrates project-owned assets, saved levels and
the native Editor viewport; see the
[World scene import and saved-document workflow](../architecture/resource-management.md#project-scene-import-and-saved-level-workflow).
The standalone commands below are prototype evidence; they do not provide the
project → scene → import → viewport → save/reopen product workflow.

## Prepare the SDK and tools

Use the [supported native toolchain and SDK workflow](../development/building.md)
and [shader tool bootstrap](../development/tools/shader-probe.md). Install a
Development SDK with the L6 renderer, then set these host inputs:

```bash
export LUDUS_SDK_PREFIX="$PWD/out/install/linux-clang-development"
export LUDUS_SLANG_COMPILER="$PWD/out/shader-tools/slang/bin/slangc"
export LUDUS_SPIRV_VALIDATOR="$PWD/out/shader-tools/spirv-tools/usr/bin/spirv-val"
export LUDUS_SPIRV_CROSS="$PWD/out/shader-tools/spirv-cross/bin/spirv-cross"
```

The sample's selectable configure/build/test profiles are
`linux-clang-{debug,development,release}` and
`macos-clang-{debug,development,release}`. Select the profile matching your SDK.
On macOS, provide `LUDUS_OSX_SYSROOT` and `LUDUS_LIBCXX_INCLUDE` from the prepared
host tools and put its Clang 18, CMake and Ninja on `PATH`. For unbundled local
SDK installs, supply the matching Conan generator directory through an ignored
`CMakeUserPresets.json` extending the selected profile's `CMAKE_PREFIX_PATH`.
Machine paths belong in that local file, never in shared presets. Configuring
reports missing dependencies/tools and does not download or repair them.

From `examples/reference-scene`:

```bash
cmake --list-presets=configure
cmake --list-presets=build
cmake --list-presets=test
cmake --preset linux-clang-development
cmake --build --preset linux-clang-development
ctest --preset linux-clang-development
```

The tests use synthetic geometry; the third-party scene is not required for CI.
They check bounded source encodings, geometry/UV preservation, a complete
reference capture and malformed-bundle rejection. Enable
`LUDUS_SCENE_SANITIZERS=ON` to instrument the consumer with ASan/UBSan; its SDK
libraries must separately be built with sanitizers for full engine coverage.

## Convert and render

From the repository root, with the licensed local reference scene present:

```bash
python3 tools/scene_cooker/cook.py \
  references/resources/scenes/staircase2 out/staircase2
examples/reference-scene/out/build/linux-clang-development/reference_scene \
  out/staircase2 out/staircase2 256 64
python3 scripts/reference-hdr out/staircase2/capture.luhdr \
  --pfm out/staircase2/capture.pfm --manifest out/staircase2/capture.json
python3 tools/scene_cooker/preview.py \
  out/staircase2/capture.luhdr out/staircase2/preview.png --exposure 1
```

The final two capture arguments are square pixel extent (1–1024) and samples per
pixel (1–4096). Rendering uses four path vertices, roulette from depth three,
seed 871, the authored 70-degree camera and the authored area emitters with a
black environment. A capture has a 30-minute deadline and requires zero invalid
contributions before publication. It can take minutes on the CPU. Use `64 8`
for a quick framing check before increasing quality.

The output directory must already exist. The cooker creates it; a different
capture destination needs explicit creation. Failed captures preserve the
previous `capture.luhdr`. Exit steps distinguish usage (1), numeric arguments
(2), bundle acquisition (3), device creation (4), renderer setup (5), reference
setup (6), program readiness (7), bundle admission (8), capture creation (9),
completion/deadline (10), contribution validation (11), output allocation (12),
HDR encoding (13), and publication (14).

`manifest.json` records every consulted source file's SHA-256, the bundle hash,
source and cooked geometry counts, camera, attribution and approximations.
`LICENSE.txt` accompanies the converted assets. `scene.luscn` is an explicit
little-endian versioned sample format with bounded counts and finite binary32
fields; it is not a stable shipping asset format. The capture consumer admits
six meshes, eight materials, three textures and up to 128 scene items within a
64 MiB bundle. It rejects unsupported versions, truncation, trailing data,
nonfinite fields and invalid geometry references.

The PFM and LUHDR retain linear radiance. The PNG uses exposure multiplied into
radiance, `x / (1 + x)` compression, then sRGB encoding. It applies no denoising;
finite-sample noise is expected. Never compare that preview against a different
renderer without matching transport, materials, exposure and display transform.

## Conversion profile and limitations

The referenced PBRT scene has 19 room PLY meshes, four planar mesh lights, four
inline rectangle lights and five disk lights. Only referenced files are cooked.
The room surfaces are grouped into four meshes: diffuse, wood, floor tiles and
conductors. Shared quad and disk meshes make six renderer meshes and 17 scene
items. Three retained RGBA8 textures and four emitter colors fit eight materials.
Wallpaper triangles are clipped at repeated UV boundaries before atlas mapping,
so repetition survives interpolation. Wood and tile textures retain repeat UVs.

The local source contains 32,967 triangles after quad triangulation, including
2,432 zero-area room triangles. These contribute no surface area and are counted
and discarded. The resulting profile stores 28,561 triangles and renders 28,831
triangles after instancing the lights. Geometry is not decimated. Planar light
grids become equivalent rectangles; disks use 64 segments.

The converter rejects source directives, parameters and material/texture
profiles it cannot represent. It is deliberately restricted to this source's
PBRT subset, bounded binary little-endian PLY triangle/convex-quad meshes,
uncompressed opaque true-color TGA textures, rigid perspective camera and
identity room transforms. It is not a general PBRT importer.

Material approximations are explicit:

- PBRT coated diffuse becomes L6's RGB GGX/Lambert model, without coating layer
  multiple scattering. Direct GGX alpha is converted to perceptual roughness.
- Conductor eta/k becomes normal-incidence RGB F0 with Schlick angular behavior.
- Diffuse surfaces use the renderer's fixed dielectric F0 of 0.04.
- Textures use one retained mip with bilinear sampling, rather than PBRT's
  trilinear footprint filtering.
- The supplied PBRT material named `Glass` is a **conductor**, so this conversion
  does not introduce glass transmission. The included Tungsten preview can
  therefore differ substantially from the declared PBRT scene.
- L6 surface emission is two-sided; PBRT diffuse area lights default to one-sided.
- Source integrator, sampling and film choices are replaced by the explicit L6
  capture profile above.

## Attribution

Modern Hall is by **NewSee2l035**, distributed under
[CC BY 3.0](https://creativecommons.org/licenses/by/3.0/), originally shared on
[BlendSwap](http://www.blendswap.com/blends/view/51997) and obtained from
[Benedikt Bitterli's rendering resources](https://benedikt-bitterli.me/resources/).
Preserve the supplied license and attribution when sharing cooked assets or
renders; disclose conversion and display changes.

Thanks to Matt Pharr, Wenzel Jakob and Greg Humphreys for the
[pbrt-v4 input format documentation](https://pbrt.org/fileformat-v4), especially
camera transforms, graphics-state scopes, materials and shapes. The converter
is original code, with no PBRT implementation copied.

For interactive placement and gameplay fields use the single
[World authoring workflow](../development/editor-workspace.md#project-owned-3d-levels-and-reference-import).
The command-line level conversion/capture remains a runtime asset pipeline,
not a separate editor workspace. Existing `.lulevel` files open in World through
the documented conversion path.
