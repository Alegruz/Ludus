# Graphics RHI

Link `Ludus::GraphicsRhi` and include `<ludus/graphics/rhi/rhi.h>`.
`ludus::graphics::rhi::Initialize(ApplicationInfo)` loads Vulkan entry points
through volk and creates a Vulkan instance. It returns `false` on failure;
development builds also assert on initialization failures. `Shutdown()` destroys
the instance and allows subsequent initialization. Serialize lifecycle calls;
the module does not provide synchronization. The loader remains loaded until
process exit. Devices, surfaces, and rendering resources are not implemented.

Conan pins volk and its matching Vulkan headers in `conan.lock`. No Vulkan SDK
or GPU is needed to build. A Vulkan loader is needed for initialization to
succeed at runtime. Vulkan and volk headers stay out of the public RHI API;
implementation files use `VK_NO_PROTOTYPES` and must not link a second Vulkan
loader. See the [volk integration guide](https://github.com/zeux/volk).

Installed static SDK consumers also need the Conan-generated dependency configs
on their CMake search path (or the matching Conan toolchain). `LudusConfig.cmake`
resolves `volk::volk`, including its platform loader library, for final linking.

## Vulkan diagnostics

Implementation files that format `VkResult` in assertions must include
`internal/vulkan_diagnostics.h` before the assertion call sites. Both
`LUDUS_LOG_*` and formatted assertion macros then accept the result directly
with `{}`, preserving its signed numeric code (including unknown result codes).
The adapter stays private to RHI; Foundation and the installed SDK do not gain
a Vulkan dependency from diagnostic formatting.
