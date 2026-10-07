# FoundationLogging

`Ludus::FoundationLogging` is the engine's custom logging subsystem. It
implements the logging redesign specified in
`.kiro/specs/logging-redesign/` (review phases 0–4): a corrected synchronous
core, a lightweight split frontend, a bounded no-heap typed formatter, an
independent emergency reporter in `FoundationBase`, and a bounded asynchronous
MPSC backend.

## Dependency direction

```
FoundationBase  ←  FoundationLogging  ←  (Platform / Rendering / Runtime / ...)
```

`FoundationBase` must never depend on `FoundationLogging`. The emergency
reporting primitive lives in `FoundationBase` (`base/diagnostic.hpp`) so Base and
the assertion subsystem can report failures without an upward dependency. The
logging module is platform-independent: the log directory is injected through
`LogConfig::Directory` (a borrowed UTF-8 path) rather than resolved inside this
module.

## Headers (split by cost and purpose)

| Header | Purpose | Heavy includes |
| --- | --- | --- |
| `logging/log.hpp` | Common call site: severities, categories, gating macros, **raw text** (`LUDUS_LOG_TEXT`), direct debug-critical delivery (`LUDUS_LOG_DEBUG_SYNC`), source capture | none (no `<format>`/`<filesystem>`/`<chrono>`) |
| `logging/log_format.hpp` | Opt-in **typed** formatting (`LUDUS_LOG_{TRACE..FATAL}` with `{}`) | none — uses the bounded engine, not `<format>` |
| `logging/log_system.hpp` | Lifecycle/config/flush/health (`LogConfig`, `LogSystem`, `FlushKind`, results) | none (no public `<filesystem>`) |
| `logging/level.hpp`, `logging/category.hpp` | Severity + category vocabulary | none |

A translation unit that only needs raw-text logging includes `log.hpp` only and
pays no formatting cost. Formatting sites additionally include `log_format.hpp`.

## Usage

```cpp
#include <ludus/foundation/logging/log.hpp>          // raw text + gating
#include <ludus/foundation/logging/log_format.hpp>   // typed "{}" formatting
#include <ludus/foundation/logging/log_system.hpp>   // lifecycle

using namespace ludus::foundation::logging;

LogConfig config{};
config.GlobalLevel = LogLevel::Info;
config.Directory   = userLogDir;      // borrowed UTF-8 path, copied on Initialize
config.Mode        = LogMode::Asynchronous;   // or Synchronous (tools/tests)
const LogInitResult init = LogSystem::Initialize(config);
// init.EffectiveMode reports the mode actually in effect.

LUDUS_LOG_INFO(LOG_CORE, "Ludus {} starting", version);          // typed
LUDUS_LOG_TEXT(LOG_CORE, Warning, driverMessage);                // raw (no format parse)
const auto v = LUDUS_LOG_DEBUG_SYNC(LOG_CORE, "before submit");  // direct, worker-independent
const auto f = LogSystem::Flush(FlushKind::Visible, 1000);       // acknowledged fence flush

LogSystem::Shutdown();   // closes admission, drains, joins the worker
```

Categories are declared/defined with process-lifetime names:

```cpp
// header
LUDUS_DECLARE_LOG_CATEGORY(LOG_RENDER);
// one .cpp
LUDUS_DEFINE_LOG_CATEGORY(LOG_RENDER, "Render");
// or, for a local constant:
inline constexpr LogCategory LogPlatform{"Platform"};
```

> Category **names** must have process lifetime (string literals / constant
> categories). The async backend stores the category name as a borrowed view; a
> name backed by a temporary would dangle.

## Delivery paths

1. **Ordinary** (`LUDUS_LOG_*`, `LUDUS_LOG_TEXT`): cheap gating; in async mode a
   bounded owned record is published to an MPSC queue drained by one worker that
   owns the sinks. In sync mode the record is written under an exclusive dispatch
   lock. `Error`/`Fatal` are flushed promptly in both modes.
2. **Direct debugger-critical** (`LUDUS_LOG_DEBUG_SYNC`): delivered synchronously
   to stderr/debugger before returning, **independent of the worker**, so a
   breakpoint immediately after still observes it. Returns a `DeliveryResult`.
3. **Emergency / Fatal**: reported first via the independent `FoundationBase`
   primitive (allocation-free, no engine locks), before the normal backend.

## Filtering cost

- **Compiled-out** level (below `LUDUS_COMPILED_LOG_LEVEL`): the whole statement
  is `((void)0)`; neither the category nor the arguments are evaluated and no
  formatting is instantiated.
- **Runtime-disabled** call: a lock-free scalar predicate (one atomic override
  load + one global threshold load) — no lock, no allocation, no map lookup, no
  timestamp, no argument evaluation.

## Formatter

A narrow, bounded, **no-heap** typed formatter (chosen over a `std::format`
erasure after measuring build cost and the no-allocation/no-terminate contract —
see the decision log). Grammar: string/`string_view`, `bool`, Ludus integers,
pointer-as-address, `float32`/`float64`; `{}` plus `{:x}` (hex) and brace
escaping (`{{`→`{`). Unsupported argument types are a **compile error**. Output
is bounded; oversize input is truncated with a flag, never allocated.

## Files and sessions

- One session file per process, created by **exclusive creation** (never
  truncates an existing/same-name file) named with UTC time + PID + nonce.
- Size-based rotation opens the replacement before releasing the old handle; on
  failure the sink is marked unhealthy (not silently disabled).
- Retention prunes only whole **inactive** session groups this logger created —
  never an active session and never unrelated files. Configurable session-count,
  total-byte, and age caps (0 means "no cap" for that dimension, explicitly).
- `Flush(FlushKind::Visible)` pushes buffers to the OS; `FlushKind::Durable`
  additionally issues an OS sync (`fdatasync`). Neither guarantees survival of
  arbitrary hardware failure.

## Compiled level per build

`LUDUS_COMPILED_LOG_LEVEL` (0=Trace … 5=Fatal) defaults by build type and is
overridable via the CMake cache / presets:

| Build (preset)            | Compiled minimum |
| ------------------------- | ---------------- |
| Debug                     | Trace            |
| Development (RelWithDebInfo) | Debug         |
| Profile (`linux-clang-profile`) | Info       |
| Release / MinSizeRel      | Warning          |

`Fatal` is never compiled out. Fatal **reports**; it does not terminate the
process (termination is owned by the caller / the assertion subsystem).

## Deferred (separately scoped follow-ups)

- Optional JSON-lines serialization and bounded typed fields.
- In-editor console subscriber; remote export/transport.
- Crash-reporter integration, native stack capture, offline symbolization
  (the emergency path is **not** async-signal-safe and is not a crash handler).
- Per-thread queues / deferred formatting — only if MPSC producer tails miss a
  measured budget.

See `.kiro/specs/logging-redesign/` for the full requirements, design, tasks, and
decision log.
