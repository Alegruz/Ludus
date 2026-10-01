# W2 browser logging and profiling evidence

Validated 2026-10-01 on the merged W1 baseline (`ab078f70e9d7`). This is
Foundation infrastructure; the engine is not yet a playable browser game.

## Browser behavior

The Emscripten graph now builds FoundationBase, Containers, Logging and Profiling.
The web logger uses bounded typed formatting and direct synchronous browser
console calls; it does not compile the native async backend, file/debugger sinks,
thread identities, worker threads or timed-flush infrastructure. Messages are
bounded to 2048 bytes, category/file metadata to 256 bytes and thread names to
64 bytes. Severity selects console debug/info/warn/error; source accompanies
Warning+. Category overrides and the shared breadcrumb ring remain available.

Initialize supports repeated reconfiguration and shutdown/restart. Async or
file/debugger requests return Degraded with EffectiveMode=Synchronous; requested
unsupported sinks are unhealthy. Visible flush reports synchronous completion
or SinkFailed; Durable is Unsupported. Console exceptions are caught only at
JavaScript boundaries. Failed writes increase Dropped without increasing Written,
and a subsequent successful call restores console health. Console reentry drops
the nested record with a breadcrumb and rejects lifecycle/flush changes as
Incomplete, rather than waiting or recursing. Disabled sinks are intentional,
not failed writes. FoundationBase diagnostics remain independent of this logger.
Direct debug diagnostics use Base best effort delivery and report NoEndpoint,
because Base EmergencyReport exposes no endpoint acknowledgement.

The only retained logging mutex is CategoryRegistry's cold control-operation
mutex: it is uncontended in this main-thread-only build, never encloses a foreign
callback, and does not introduce startup waits. Enabling pthreads/workers will
require a new concurrency design; this port does not claim worker safety.

Profiling uses emscripten_get_now (performance.now) converted from milliseconds
into nanosecond units. Browser privacy settings can reduce actual resolution.
The existing recorder assigns one profiling-local thread id (0); no OS thread
identity is fabricated. Development records scopes, marks, frames and flows and
exports Chrome/Perfetto JSON into MEMFS. These files disappear with page teardown.
The probe copies bytes before runtime shutdown and offers a user-triggered Blob
download; it does not upload traces or automatically save host files. Web Release
links no-op control functions and omits the recorder/exporter, while instrument
macros do not evaluate their arguments. Native source selection stays unchanged.

## Validation

- `./scripts/test web-emscripten-development`: eight tests passed.
- `./scripts/test web-emscripten-release`: eight tests passed.
- Both web `./scripts/check <preset> --all`: format and clang-tidy 18 passed.
- Tests cover filtering/category overrides, formatting, failed console writes,
  recovery, breadcrumbs, independent CHECK, reentrant console callbacks,
  lifecycle rejection during reentry, 2048-byte truncation, unsupported sinks,
  durable flush, shutdown/restart, monotonic clock, capture restart and Release
  argument compile-out. JSON checks validate event order and thread identity.
- Compile policy and standalone public-header gates cover all four web modules.
  No pthread flags or shared-memory mode are used.
- Native Development: all 20 registered tests completed, no failures; Wayland
  display test skipped because no test display is available.
- ASan/UBSan: all 18 registered tests completed, no failures; same Wayland skip.
- Native format/tidy and installed SDK consumer passed. Profiling was rebuilt
  and retested after enforcing constant initialization of the capture flag.
- Browser automation: both final pages showed `W2 probe passed`. Development's
  download was enabled, Release's disabled. A clicked Development download
  produced `~/Downloads/ludus-trace.json`; parsed JSON contained the expected
  five ordered events on thread 0. Automation's download-event wait timed out,
  but the actual downloaded file was independently verified.

Clang-tidy 18 misclassifies three constant-initialized declarations with the
newer pinned web SDK libc++: the extern capture atomic and two dependent
constexpr container constants. Narrow documented suppressions apply only to
these declarations; constinit/constexpr enforce their initialization contracts.
The pre-existing legitimate Array pointer-element sizeof suppression is retained.

## Reproduce and continue

```bash
./scripts/test web-emscripten-development
./scripts/check web-emscripten-development --all
./scripts/test web-emscripten-release
./scripts/check web-emscripten-release --all
python3 -m http.server 8765 --bind 127.0.0.1 --directory out/build
```

Open `/web-emscripten-development/tools/web-observability-probe/` or the Release
counterpart. Use a fresh origin or clear cached JS/wasm after rebuilding. CI
publishes each probe's index.html/index.js/index.wasm as artifacts.

Next stage is W3 canvas platform and input. Re-read the implementation plan and
repository standards, check this PR's merge status, then branch from updated
main. W0 remains open for flag-free GPU support and animation verification on
the actual target browser; W2 does not close that GPU deployment gate.
