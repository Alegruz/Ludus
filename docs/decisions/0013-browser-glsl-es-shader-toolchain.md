# ADR 0013: Browser GLSL ES 3.00 shader toolchain (WebGL 2 backend artifact)

- Status: Accepted for the shader feasibility stage of the WebGL 2 fallback
  (prompt 1), integrated with ADR 0014 and the browser demos. Hardware and
  hosted acceptance remain separate; see the current validation handoff.
- Depends on: ADR 0009 (browser toolchain), ADR 0010 (verified Slang compiler),
  ADR 0011 (public fullscreen rendering).
- Supersedes, for this slice only: the earlier WebGPU-only shader scope, per
  `docs/development/webgl-fallback-kiro-handoff.md`. Slang stays the authored
  language; GLSL ES is an additional generated backend artifact.

## Context

The browser demo and the Drift ocean need a WebGL 2 fallback when WebGPU cannot
initialize. WebGL 2 consumes GLSL ES 3.00, which the SPIR-V/WGSL shader helper
did not produce. The handoff flagged, but did not test, whether the pinned Slang
2026.1.2 can emit usable GLSL ES 3.00 for this slice. Desktop GLSL is not
acceptance. Before any production backend code, the feasibility gate had to prove
real WebGL 2 compile/link/render of changing mixed-layout uniforms, preserving
the single authored Slang source and the fullscreen rendering contract.

## Decision

### Direct Slang GLSL emission is unsuitable (measured)

Slang's `glsl` target is Vulkan-flavored GLSL. For the diagnostic shader it emits
`#version 450` (fragment) and `#version 460` + `#extension
GL_ARB_shader_draw_parameters : require`, `gl_VertexIndex`, `gl_BaseVertex`
(vertex). Slang exposes no ESSL / `#version 300 es` capability (only
`glsl_spirv_*`). None of this is valid GLSL ES 3.00 / WebGL 2. Direct emission is
rejected.

### Build-time SPIR-V -> GLSL ES translator (SPIRV-Cross), reproducibly pinned

Compile a GLSL-specific SPIR-V variant from the same Slang source with
`LUDUS_GLSL_ES=1`, validate it, then translate with SPIRV-Cross invoked as
`spirv-cross --version 300 --es --fixup-clipspace`. The authored fragment shader
normalizes the GL lower-left origin using its resolution uniform; generated
text is never patched. The pin is
the SPIRV-Cross **source** (`config/spirv_cross_toolchain.json`): tag
`vulkan-sdk-1.4.313.0`, commit `2275d0efc4f2fa46851035d9d3c67c105bc8b99e`, source
tarball SHA256 `7d1de24918bea9897753f7561d4d154f68ec89c36bb70c13598222b8039d4212`,
Apache-2.0. The CLI is a local Release build of exactly that verified source; the
bootstrap verifies the source archive hash and writes `<binary>.build.json`
with the pinned source identity and this host build's binary hash. The offline
helper verifies that sidecar and binary hash before executing the translator. No runtime download or runtime compilation is introduced.

### Authored-source fix for the one Vulkan-only feature

SPIRV-Cross refuses `BaseVertex`/`BaseInstance` in the ES profile. Slang maps
`SV_VertexID`/`SV_InstanceID` to D3D base-relative semantics requiring
`SPV_KHR_shader_draw_parameters` (shader-slang/slang#6992), unimplementable in
baseline WebGL 2. Rather than a handwritten ocean shader or an unchecked
generated-text patch, the single authored Slang vertex input uses
`SV_VulkanVertexID`, which maps to the plain Vulkan `VertexIndex` / WGSL
`vertex_index` / GLSL ES `gl_VertexID` with no draw-parameters capability. For the
contract's non-indexed, zero-base-vertex fullscreen triangle this is identical in
value on every backend; it leaves the SPIR-V entry (`main`), reflection, WGSL
entries and WGSL/std140 layout unchanged (verified).

### Helper and generated descriptions

`ludus_compile_shader` gains `LUDUS_SPIRV_CROSS` (required on web, optional natively);
`compile_shader.py` gains optional `--spirv-cross`/`--spirv-cross-lock`. When set,
each stage's validated SPIR-V is translated to `<name>.<stage>.essl`; the GLSL ES
std140 block layout is derived independently from the emitted block and checked
against the reflected uniform size and binding 0. `ShaderDescription` gains
backend-independent `GlslEs` / `GlslEsEntry` fields; the generated web branch
carries WGSL and GLSL ES side by side so the engine selects WebGPU or WebGL 2 at
startup. The manifest records GLSL ES artifact hashes, `glsl_es_entries`
(`main`/`main`), the derived `glsl_es_layout`, the `300 es` profile and translator
provenance. Depfiles are reused from the SPIR-V stage compiles, so include/source
edits rebuild ESSL. Native consumers may leave `LUDUS_SPIRV_CROSS` unset;
browser consumers require it so an Auto build cannot omit its fallback artifact.
Member offsets and sizes are checked across reflection targets and std140;
unsupported array/matrix/struct fields are rejected explicitly.

## Consequences

- The browser needs the pinned SPIRV-Cross source available to the build. Native
  packages gain no runtime dependency and keep working with `LUDUS_SPIRV_CROSS`
  unset. The SDK installs `spirv_cross_toolchain.json` next to the shader lock;
  shipping the host translator requires preserving its Apache-2.0 license.
- `ShaderDescription` stays backend-object-free; `GlslEs` is only populated for
  web builds. Consumers still verify std140 upload layouts against the real linked
  program (the WebGL 2 harness queries `UNIFORM_BLOCK_DATA_SIZE` /
  `UNIFORM_OFFSET`), never assuming cross-target packing equality.
- The WebGL 2 engine backend, canvas-ownership/fallback policy, context-loss
  handling and Emscripten link settings are defined by ADR 0014. Physical-GPU
  and hosted acceptance remain external gates.

## Observed limitations

- Slang cannot emit GLSL ES directly; the SPIRV-Cross translator is required.
- `SV_VertexID`/`SV_InstanceID` cannot be used for a WebGL-translatable shader;
  authors must use `SV_VulkanVertexID`/`SV_VulkanInstanceID`.
- SPIRV-Cross binaries are not reproducible across hosts/toolchains; only the
  source is a cross-machine integrity gate.
- Feasibility rendering was on ANGLE/SwiftShader (software) in pinned Chromium
  140.0.7339.186; hardware WebGL 2 and hosted/iframe acceptance are not covered.
- The pinned `spirv-val`/`spirv-dis` require glibc 2.38; a glibc-2.34 host cannot
  run them, so the full pinned helper and the probe's SPIR-V disassembly checks
  must run on a 2.38+ host. The recovered feasibility report used a source-built
  validator; current integration validation uses the unmodified pinned validator
  on Ubuntu 24.04. See `docs/development/webgl-fallback-sandbox-handoff.md`.

## References

- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) `vulkan-sdk-1.4.313.0`
- [Slang SV_VertexID / DrawParameters issue](https://github.com/shader-slang/slang/issues/6992)
- [GLSL ES 3.00 specification](https://registry.khronos.org/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf)
- [Emscripten OpenGL / WebGL 2 support](https://emscripten.org/docs/porting/multimedia_and_graphics/OpenGL-support.html)
- Evidence: `docs/development/webgl-shader-feasibility-evidence.json`
