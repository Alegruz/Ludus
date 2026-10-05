# Platform detection

## Initial design

The engine needs one inexpensive answer to "what target is this translation unit
compiled for?". That answer belongs in FoundationBase and comes from the compiler's
target macros. It must work in SDK consumers, standalone probes, cross builds,
and builds without a precompiled header. Build-host information belongs to host
tools; it must never select engine implementations.

Keep four independently owned concerns:

| Concern | Authority | Consumer |
| --- | --- | --- |
| Target OS, architecture, pointer width, byte order | Compiler target and target SDK | FoundationBase configuration |
| Compiler frontend and extension availability | Compiler predefined macros and feature queries | FoundationBase compiler abstraction |
| Build flavor and compiled backend | CMake target definitions and source selection | Build configuration and owning module |
| Available devices, instruction sets, permissions, graphics features | Successful runtime queries/initialization | Owning subsystem |

Use a small macro-only foundational layer for selecting headers, intrinsics, and
source boundaries. Add an explicitly included, constexpr C++ descriptor for
ordinary code and debugger inspection. Avoid registries, dynamic dispatch,
allocation, startup probing, generated host architecture headers, and dependencies
on windowing/logging. Existing defined-only flags remain source compatible;
inactive flags stay undefined. New numeric selectors allow comparisons under
`-Wundef` without migrating every consumer.

Classifying a target is separate from shipping its engine backend. Linux/Clang 18
and pinned Emscripten remain the validated engine builds. Detection of another
target does not bypass CMake's existing backend restrictions. Unknown targets fail
at the detection boundary with a specific diagnostic instead of silently selecting
a desktop/null implementation. Do not permit command-line overrides of detected
facts. Test other targets by selecting a compiler target, not redefining its OS.

Compiler frontend and Microsoft ABI compatibility are separate: clang-cl is Clang
while using Microsoft spellings where necessary. Query builtin/attribute support
where possible rather than assuming every compiler version supplies an extension.

## Reference review and revisions

This section records the relevant chapters actually read from the local collection
indexed by `references/game-dev-gems-toc.md`. Historical recommendations are
adapted to C++23 and Ludus's existing include/build contracts.

- **David Etherton, Game Programming Gems 4, 1.4, Designing and Maintaining Large
  Cross-Platform Libraries**, printed pp. 35-41, [PDF pp. 52-58](../../references/Game%20Programming%20Gems%204.pdf#page=52).
  Separate interfaces from platform implementations and keep conditional
  compilation sparse. Centralize build rules, compile each target with warnings
  as errors, and make scalar sizes/byte order explicit. This reinforces the four
  authorities above and adds negative compile tests and pointer-width/endianness
  facts. Adopt numeric selectors with `-Wundef` for new target decisions. Retain
  the defined-only compatibility flags rather than defining inactive flags to zero,
  which would invert existing `#ifdef` callers. Keep the small foundational PCH
  optional; do not copy the chapter's forced-include or universal header approach.
  Its virtual factories, private-only data, and historical vtable layout advice
  are not required for immutable target metadata.
- **Jason Hughes, Game Engine Gems 1, 1, What to Look for When Evaluating
  Middleware for Integration**, sections 1.10-1.13, printed pp. 10-12,
  [PDF pp. 38-40](../../references/Game%20Engine%20Gems%201.pdf#page=38).
  Keep integration small, symbols scoped, and portability assumptions visible.
  The revised descriptor is opt-in and header-only; it introduces no middleware
  dependency, initialization, or global mutable state. Byte order describes native
  values, not a serialization format. Files/network packets continue to require
  their own explicit encoding and conversion contracts; use the bounded codecs in
  `byte_order.hpp` (ADR 0015), not native-order serialization. Target identity cannot
  establish thread safety or runtime feature availability.

Other TOC matches concern threading engines, graphics implementations, allocators,
and SIMD programming. They do not improve the detection boundary and are not
grounds to introduce those systems here.

## Implementation contract

`config.h` remains macro-only. It classifies Emscripten before native compatibility
macros and Android before Linux. Apple classification uses the target SDK's
`TargetConditionals.h`: macOS is `TARGET_OS_OSX`, iOS (including Catalyst and
simulator builds) is `TARGET_OS_IOS`. Other Apple targets fail explicitly.
`LUDUS_PLATFORM_DESKTOP` includes Windows, macOS, and desktop Linux only.

| Selector | Enumerants |
| --- | --- |
| `LUDUS_TARGET_OS` | `LUDUS_OS_WEB`, `WINDOWS`, `MACOS`, `LINUX`, `ANDROID`, `IOS` (each with the `LUDUS_OS_` prefix) |
| `LUDUS_TARGET_ARCH` | `LUDUS_CPU_WASM32`, `WASM64`, `X86_64`, `ARM64` (each with the `LUDUS_CPU_` prefix) |
| `LUDUS_TARGET_COMPILER` | `LUDUS_CXX_CLANG`, `GCC`, `MSVC` (each with the `LUDUS_CXX_` prefix) |
| `LUDUS_POINTER_BITS` | 32 or 64, from the compiler data model |
| `LUDUS_TARGET_ENDIAN` | `LUDUS_ENDIAN_LITTLE` or `LUDUS_ENDIAN_BIG` |

The enumerant numbers are classification values, not persistence/serialization
identifiers or ABI compatibility guarantees. Preserve their values for source
compatibility. Existing `LUDUS_PLATFORM_*`, `LUDUS_ARCH_*`, and
`LUDUS_COMPILER_*` identity flags remain defined to 1 only when active. The
`LUDUS_COMPILER_MSVC_ABI` compatibility flag is independent of frontend identity.
Clang-compatible frontends take precedence over GCC/MSVC compatibility macros.

The deliberately bounded recognition set has four architectures. ARM64EC,
32-bit native CPUs, WASI, and unnamed OS families fail with named diagnostics.
Adding recognition requires a branch and regression case. It does not enable
an engine backend. ARM64 big endian and x86_64's x32 data model are detection
test cases, not validated engine deployments. WASM64 is recognized for future
Emscripten memory64 work; the shipped browser preset/probes remain wasm32.

`cmake/EngineOptions.cmake` derives `LUDUS_EXPECTED_TARGET_OS` from
`CMAKE_SYSTEM_NAME`/Emscripten. Project options apply it to in-tree targets;
FoundationBase exports it as a public usage requirement to SDK consumers, without
exporting private warning or exception options. The header checks it against
compiler detection; an incorrectly selected cross toolchain fails when it
includes foundational configuration.
No architecture is inferred from CMake's host, or frozen into an installed
generated target header. The existing assertion SDK policy remains authoritative
for assertion behavior. One build-flavor input with value 1 is required; standalone
fallback remains Debug for `_DEBUG`/`DEBUG`, Release for `NDEBUG`, Development
otherwise. Sanitization is an independent marker. Do not derive optimization,
logging, assertion, or debugger availability from the flavor name.

`compiler.h` provides `LUDUS_HAS_BUILTIN`, `LUDUS_HAS_ATTRIBUTE`, and
`LUDUS_HAS_CPP_ATTRIBUTE` without redefining compiler-owned query macros. The
codegen abstractions use those queries or Microsoft compatibility where required.
Branch hints evaluate their argument once. Web debug breaks keep trapping behavior.

`target.hpp` explicitly exposes `TargetOs`, `TargetArch`, `TargetCompiler`,
`TargetEndian`, and the immutable `kTarget` descriptor. It includes only the
foundational compiler/types headers, instantiates no library templates, and adds
no link dependency, runtime probe, or initialization. `TargetOsName` and
`TargetArchName` return static literals for diagnostics; their `Unknown` case
handles an invalid caller-supplied enum, not successful detection of an unknown
target. `kTarget` asserts pointer width against `sizeof(void*)`. It is intentionally
absent from `core.h`. Its internal linkage keeps frontend facts local to each
translation unit when an external consumer mixes compatible compiler frontends.
The native smoke app logs target names, pointer width, and native
byte order once during startup, through the existing logger.

```cpp
#include <ludus/foundation/base/target.hpp>

// Ordinary C++ can use constexpr target facts without a runtime branch.
static_assert(ludus::foundation::kTarget.PointerBits == sizeof(void*) * 8);

// Use the numeric selector when selecting declarations/OS headers.
#if LUDUS_TARGET_OS == LUDUS_OS_LINUX
// Linux implementation boundary.
#endif
```

Platform `config.h` owns backend checks. Public code may include it without a
selected backend; the owning implementation must select one. More than one
backend, a selected flag other than 1, or an incompatible target/backend pair is
an error. Wayland/X11 require desktop Linux; the canvas backend requires
Emscripten; the native headless backend excludes Emscripten. X11 remains an
existing reserved flag rather than a new implemented backend. Backend selection
stays private in CMake. Neither a compiled Wayland backend nor Linux detection
proves that a compositor is reachable. Likewise, Emscripten can run under Node;
the compatibility name `WEB` is not a claim about browser DOM availability.

Engine OS-specific file/audio/logging paths use the centralized selectors and
explicitly include configuration where needed. They no longer repeat raw compiler
OS tests. Actual OS APIs remain private to implementation files. This revision
does not introduce platform subclasses or duplicate runtime dispatch.

## Runtime capability ownership and future additions

Keep process facts, device capabilities, and operation results separate. Add a
capability query only with its first concrete consumer and a tested fallback:

1. CPU dispatch belongs to a future CPU/math service. A compiled AVX/SIMD
   implementation is eligible only when runtime CPU and OS state checks succeed;
   x86_64 identity alone does not authorize AVX. Cache validated process-stable
   facts once outside hot loops and bind a function at subsystem initialization.
2. Graphics capabilities belong to the successfully created adapter/device.
   Scope cached values to that device generation and refresh after loss/recreation.
   Keep existing RHI WebGPU/WebGL fallback and selection policy in RHI.
3. Audio/windowing/input capabilities belong to their live sessions. Initialization
   returns an explicit status; OS names, environment variables, installed packages,
   and compiled flags cannot substitute for successful API calls.
4. Permissions, device attachment, filesystem access, and browser feature policy
   can change. Query/use with explicit failure handling; do not publish a timeless
   global `HasEverything` mask or user-agent classification.

Snapshots should be immutable, describe the owner/generation and confirmed facts,
and keep query failure distinct from confirmed unavailability when callers need
that distinction. Construct them on the subsystem's owner thread and publish
under its existing synchronization contract. No runtime capability is inferred
from this descriptor. No speculative CPU probing service is added before Ludus
has multiple instruction-set implementations to select between.

## Verification and maintenance

`ludus_target_detection` runs with pinned native Clang and real target triples,
without cross-target C++ libraries or a linker. Positive cases compile the macro
headers and codegen helpers with `-Wundef -Werror -fno-exceptions`; they check exact
active/absent legacy flags, target precedence, pointer width against the actual
cross-target `sizeof`, endian, build fallback, and backend validity. Negative cases
must contain their intended diagnostic: unknown target/CPU/compiler, ARM64EC,
forbidden output overrides, flavor/backend conflicts, invalid values, unknown
data model/byte order, and configured SDK disagreement.

Apple tests supply a minimal macro-only SDK fixture and use Clang's target
classification. They validate Ludus's dispatch, not Apple's real SDK packaging.
GCC/MSVC frontend dispatch cases are synthetic; real Clang Microsoft-target
extension cases are compiled. This does not claim native GCC/MSVC engine support.
The same test rejects new raw OS/CPU/frontend classification conditionals in
first-party engine/application C++ outside `config.h`/`compiler.h`, keeping future
edits centralized. Game API symbol visibility now queries the compiler's actual
attribute support instead of repeating frontend identity checks.

Native FoundationBase tests verify the C++ descriptor against `std::endian` and
the actual pointer size. Installed SDK and pinned Emscripten FoundationBase probes
exercise the exported typed header. Existing header self-sufficiency, include
boundary, format/tidy, sanitizer, and build-time-budget gates still apply.

When adding a platform, change detection and its compile matrix together, then
add actual backend source selection and SDK/runtime tests as a separate measured
step. When adding a runtime feature, test query failure and fallback as well as
the success case. Console-specific SDK facts belong behind a reviewed private
adapter; this public design makes no assumptions about confidential SDK macros.

## Primary toolchain references

- [Clang feature queries](https://clang.llvm.org/docs/LanguageExtensions.html)
  inform the builtin/attribute wrappers and frontend precedence.
- [Emscripten build macros](https://emscripten.org/docs/compiling/Building-Projects.html)
  identify the toolchain independently of its host.
- [Apple conditional compilation](https://developer.apple.com/documentation/xcode/running-code-on-a-specific-version/)
  uses the target SDK's platform conditionals rather than treating all Apple
  targets as desktop macOS.
- [Microsoft predefined macros](https://learn.microsoft.com/en-us/cpp/preprocessor/predefined-macros)
  distinguish `_WIN32`, pointer model, and ARM64EC compatibility.
- [CMake target system](https://cmake.org/cmake/help/latest/variable/CMAKE_SYSTEM_NAME.html)
  defines the configured target separately from the build host.
