# Shader feasibility probe

See [the handoff](../shader-toolchain-handoff.md) for pins,
commands, independent binding/layout contracts, evidence, and hardware gaps.

`probe.slang` is the only shader source. `scripts/shader-probe bootstrap` acquires
verified host tools; `compile` is offline and writes generated artifacts under
`out/`. Enable `LUDUS_BUILD_SHADER_PROBE` for the native executable and CTests.
The browser harness consumes generated WGSL through real WebGPU. The test runner
uses the existing Playwright lock. Neither target is installed into Ludus's SDK.
