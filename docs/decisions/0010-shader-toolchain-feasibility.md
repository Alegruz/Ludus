# ADR 0010: Bounded Slang shader-toolchain feasibility

## Status

Accepted for the isolated probe subset. Physical GPU acceptance and a public
Ludus shader/resource API remain open; this is not a production portability claim.

## Context

The installed RHI exposes lifecycle/frame operations but no shader, uniform,
resource, or pipeline API. W5/W6 use private probe interop and handwritten WGSL.
A separate game needs evidence that one authoring source can produce Vulkan and
browser WebGPU shaders before designing that public boundary.

## Decision

Pin Slang **2026.1.2** in `config/shader_toolchain.json`. Bootstrap its official
Linux x86_64 archive with SHA256 verification into `out/shader-tools/`, separately
from Clang 18, Conan, and Emscripten. Use `slangc` exclusively at build time.
No compiler library is linked or distributed with the engine or probe runtime.
The archive supplies Apache-2.0 WITH LLVM-exception licensing. Preserve its
license and upstream notices when redistributing developer tools.

The single `tools/shader-probe/probe.slang` source emits two SPIR-V 1.3 stage
modules and one WGSL module containing both entries. Pin SPIRV-Tools via a
checksum-verified Ubuntu package and pin Chromium's WGSL validator through the
repository's Playwright lock and Chromium revision/version. Compiler reflection,
SPIR-V decorations, and independent WGSL alignment calculation must agree with
separate CPU upload structs and compile-time offset/size/alignment checks.

Slang passed actual vertex/fragment compilation, Vulkan pipeline creation and
pixel readback on llvmpipe, and Chromium WebGPU pipeline creation and pixel
readback on SwiftShader. Eight cases per backend cover both aspect ratios,
changing uniforms, and UNORM/sRGB attachments. No second handwritten shader or
DXC fallback was needed. Slang's release documentation calls WebGPU support work
in progress; this decision only covers the exercised subset.

The opt-in executable and standalone browser harness stay under `tools/` and
have no install/export rules. The browser harness uses the actual WebGPU JS API,
not Emscripten or the existing smoke renderer. Native rendering is offscreen
Vulkan, not the RHI swapchain. These isolate shader feasibility without extending
the installed SDK or claiming application integration.

## Consequences

External games may use the pinned CLI recipe to produce assets. They still need
a separately designed public shader/resource/pipeline API to submit those assets
through Ludus. Private RHI probe handles and this direct-backend harness are not
that integration path. Textures, samplers, varyings, matrices, arrays, resource
lifecycle/recovery, broader shader features, and other compiler hosts need their
own evidence. Full-screen coverage does not test face winding/culling conventions.

See [the handoff and measured outcomes](../development/shader-toolchain-handoff.md).
Upstream: [selected release](https://github.com/shader-slang/slang/releases/tag/v2026.1.2),
[release WGSL documentation](https://github.com/shader-slang/slang/blob/v2026.1.2/docs/user-guide/a2-03-wgsl-target-specific.md).
