# Ludus SDK — third-party redistributable notices

The Ludus runtime SDK links the following third-party components into the static
engine libraries. When you redistribute an application built against this SDK,
the applicable notices below travel with the redistributable link closure
(requirement P02). This file is installed to
`share/Ludus/licenses/THIRD_PARTY_NOTICES.md` inside every SDK prefix; the
authoritative, machine-readable inventory (names, versions, kinds) is the
`dependencies` array in `share/Ludus/LudusSdkManifest.json`.

## Bundled static dependencies (shipped inside the SDK)

| Component | Version | License | Linked by |
| --- | --- | --- | --- |
| yyjson | 0.10.0 | MIT | `Ludus::Content` (PRIVATE JSON parser) |
| miniaudio | 0.11.23 | MIT-0 | `Ludus::Audio` (PRIVATE decoder/device backend) |
| volk | 1.4.357.0 | MIT | `Ludus::GraphicsRhi` (Vulkan loader meta-loader) |
| FreeType | 2.14.3 | FTL **or** GPLv2 (dual) — see FreeType FTL | `Ludus::Text` (PRIVATE, part of the final link closure) |
| HarfBuzz | 14.5.1 | "Old MIT" / modern MIT-style | `Ludus::Text` (PRIVATE, part of the final link closure) |

> FreeType and HarfBuzz are **private** link dependencies of the static
> `Ludus::Text` library. `PRIVATE` on a static library does not remove them from
> the *final consumer's* static link line, so their object code and license
> obligations are part of any application that links `Ludus::Text`.

## System dependencies (NOT bundled — must exist on the target)

| Component | Obligation |
| --- | --- |
| C / C++ runtime (`libc`, `libstdc++` matching `cxx_runtime_abi`) | System runtime; must match the manifest `cxx_runtime_abi`. |
| POSIX threads (`pthread`, `Threads::Threads`) | System; pulled in by the assertion/concurrency paths. |
| Vulkan loader (`libvulkan.so.1`) + GPU/driver | Runtime requirement for RHI; not redistributed by the SDK. |
| Wayland client (`wayland-client`) | Only when the Wayland Platform backend is enabled. |

## Shader-toolchain prerequisite (build time, not redistributed)

`ludus_compile_shader` requires the pinned Slang compiler and `spirv-val`
(`config/shader_toolchain.json`). These are host build-time tools; they are not
part of a shipped game.

Each bundled component's full upstream license text is placed next to this file
in `share/Ludus/licenses/` by the release-packaging step. Where a build has not
yet produced those files (e.g. a developer `cmake --install` without the release
packager), this notices file plus the manifest inventory record the obligation.
