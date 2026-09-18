# FoundationLogging

`Ludus::FoundationLogging` is the engine's custom logging subsystem. This module
implements **Phase 1 (synchronous core)** of the approved logging specification.

## Dependency direction

```
FoundationBase  ←  FoundationLogging  ←  (Platform / Rendering / Runtime / ...)
```

`FoundationBase` must never depend on `FoundationLogging`. The logging module is
platform-independent: the log directory is injected through `LogConfig::directory`
rather than resolved inside this module (spec sections 2.1, 21).

## Usage

```cpp
#include <ludus/foundation/logging/log.hpp>

using namespace ludus::foundation::logging;

LogConfig config{};
config.global_level = LogLevel::Info;
config.directory = /* platform::GetUserLogDirectory() when available */;
LogSystem::initialize(config);

LUDUS_LOG_INFO(LogCore, "Ludus {} starting", version);
LUDUS_LOG_WARN(LogCore, "present mode {} unavailable; using {}", requested, fallback);

LogSystem::shutdown();
```

Engine code declares its own categories at namespace scope:

```cpp
inline constexpr LogCategory LogPlatform{"Platform"};
```

## What Phase 1 includes

- `LogLevel`, `LogCategory` (constexpr FNV-1a hashed id), `LogConfig`.
- `LUDUS_LOG_{TRACE,DEBUG,INFO,WARN,ERROR,FATAL}` macros.
- Compile-time filtering via `LUDUS_COMPILED_LOG_LEVEL` (arguments of a
  compiled-out level are never evaluated).
- Runtime global + per-category filtering.
- `std::format` formatting with compile-time format checking.
- `std::source_location` capture (no manual `__FILE__`/`__LINE__`).
- Synchronous console, file, and debugger sinks.
- Per-process session log files, size-based rotation, session retention.
- Emergency stderr/debugger path for pre-init, post-shutdown, and Fatal.
- Thread names (`set_current_thread_name`).

## Compiled level per build

`LUDUS_COMPILED_LOG_LEVEL` (0=Trace … 5=Fatal) defaults by build type and is
overridable via the CMake cache / presets:

| Build (preset)            | Compiled minimum |
| ------------------------- | ---------------- |
| Debug                     | Trace            |
| Development (RelWithDebInfo) | Debug         |
| Profile (`linux-clang-profile`) | Info       |
| Release                   | Warning          |

`Fatal` is never compiled out.

## Deferred to later phases / follow-ups

- **Asynchronous backend** (bounded MPSC queue, worker thread, overflow policy):
  Phase 2. The public API and `LogConfig` already reserve `LogMode`; calling
  `set_mode(Asynchronous)` currently notes that Phase 1 remains synchronous.
- **Spec section 33 (Wayland integration)** and **section 34 (remove the
  `UniquePtr` null-destruction logging comment)**: the Wayland platform code and
  `UniquePtr` do **not** exist on `main` (they live on the `develop` /
  `feature/rhi-vulkan` branches, which use a different source layout). These
  integrations should be applied when that platform code lands on `main`. Until
  then the only engine call site converted from `std::cout` is the smoke app.
