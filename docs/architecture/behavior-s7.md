# S7 reference performance and release acceptance

Status: **in progress**. This first slice adds reproducible Linux x86_64 evidence
for the installed-SDK scripted-game encounter. It does not complete the S7
production acceptance criteria in the [scripting architecture](scripting.md).
[S5](behavior-s5.md) owns the authoring workflow; [S6](behavior-s6.md) owns safety
and platform qualification. No runtime API, scheduler or additional language
provider is introduced here.

## Workload and measurement boundaries

`config/behavior_qualification.json` records the reference workload: 100 ready
encounters, 1,000 inactive records, 30 warm-up batches and 300 measured batches.
The harness dispatches only its ready list. These are externally stored declared
state records, not 1,100 scheduled tasks or proof of 10,000 waiting tasks.
Each encounter re-arms after 100 interactions. Execution uses the installed
production transaction and provider APIs, generated project bindings, and paired
offline cooker. The native handler independently implements the same effects.

Twelve cases compare native and Luau empty, scalar and operation handlers;
native, handwritten Luau and generated sequence encounters; and dormant versions
of those three encounter paths. Equivalence checks include boundary input/state
combinations, command contents, entity tokens and capabilities. Measured runs
also validate expected aggregate state and command counts and inactive records.

The timing executable records every batch in nanoseconds using FoundationTime,
with nearest-rank p50/p95/p99 and maximum. Startup, package loading and shutdown
are outside the measured interval. The separate counting executable replaces
only FoundationMemory's aligned, nothrow allocation boundary. It records requests
and requested bytes during the same batches, without failure injection or added
allocation headers. These counts do not represent every process heap allocation.
VM live bytes are sampled between batches; their maximum is neither a true
within-invocation heap peak nor a GC-pause distribution. Both executables require
zero provider-owned live bytes after close.

The proposed 833,333 ns budget (5% of a 60 Hz tick) is **report-only**. Shared CI
machines are not suitable for a deterministic timing gate. A report identifies
its OS, architecture, CPU, SDK manifest, source hashes, paired cook identity and
raw samples. A passing report means its correctness and packaging checks passed;
it does not certify a game's capacity or latency target.

## Run against an installed SDK

Prepare tools and dependencies explicitly; qualification never downloads them.
Use the pinned compiler and a Behavior-enabled SDK. The first Release payload
policy is a headless Linux x86_64 build without the Wayland dependency:

```bash
./init.sh --cli --preset-only linux-clang-release --locked --with-tests --no-system-install
./scripts/script-provider bootstrap
out/host-tools/venv/bin/cmake --preset linux-clang-release \
  -DLUDUS_USE_INIT_OPTIONS=OFF -DLUDUS_BUILD_BEHAVIOR=ON \
  -DLUDUS_BUILD_BEHAVIOR_ACCEPTANCE=OFF -DLUDUS_BUILD_TESTS=OFF \
  -DLUDUS_BUILD_EDITOR=OFF -DLUDUS_PLATFORM_ENABLE_WAYLAND=OFF \
  -DLUDUS_WARNINGS_AS_ERRORS=ON
out/host-tools/venv/bin/cmake --build --preset linux-clang-release --parallel 2
./scripts/install-sdk linux-clang-release
./scripts/script-qualification --sdk out/install/linux-clang-release \
  --output out/behavior-s7-release --check
```

`--verify-only` accepts a Development or sanitized SDK and runs the same consumer
acceptance tests without publishing timing, release or iteration evidence. For
example, after building a Behavior-enabled ASan/UBSan profile, install that SDK
with `cmake --install out/build/linux-clang-asan-ubsan`, then pass
`--sdk out/install/linux-clang-asan-ubsan --verify-only`. The benchmark targets
inherit the installed SDK's sanitizer flags and stay exception-free.

Each invocation creates a separate `run-*` directory beneath the requested
`out/` location. It copies and relocates the SDK and project, generates local
presets, lists selectable configure/build/test presets, then performs a real
configure/build/test. `--check` also checks benchmark formatting and every
consumer translation unit with the pinned clang-tidy. Successful runs publish
`evidence.json` and atomically update `current.json` with its SHA-256; failed runs
leave diagnostic files and preserve the previously published pointer. Reports
are consistency evidence for trusted local builds, not signed attestations.

## Iteration and shipping checks

A saved Luau edit changes the encounter's threshold from two interactions to
three. The installed cooker publishes a new immutable package, and a fresh
provider process loads it and observes the changed behavior. The reported metric
is **save → cook → fresh-process provider observation**, with cook and observation
subtimes. It is not native compilation, GameHost hot reload, or an Editor UI
latency measurement. An invalid strict edit must preserve the old candidate,
which is executed again. A layout-only graph edit must reuse its cook key.

The Release payload contains the statically dispatched GameHost player, a
shipping-source acceptance executable, notices and runtime licenses. The latter
executes the sample's actual `src/game.cpp`, sends two rising input events,
checks the door effect and declared state, and restores that state into a new
generation. The GameHost player independently proves headless startup.

Packaging strips native debug sections, checks all ELF dependencies against the
existing closed Linux system-library policy, and audits **unstripped** executable
symbols for unselected compiler, AST, CodeGen and Qt dependencies. The ZIP is
extracted and checked against the file/hash/mode inventory. Both executables run
from the extracted directory while copied SDK/project directories are hidden and
loader override variables are removed. This proves the declared system ABI plus
payload is sufficient on the qualification machine. The paired cook retains its
current S4 debug profile; this slice does not claim stripped Luau debug metadata.

## Remaining S7 acceptance

Production completion needs a named game's CPU and iteration budgets; actual
scheduler and waiting-task scale; GC pauses and heap peaks; representative human
designer author/review/debug/edit evidence; and release qualification on each
selected physical device and platform. S6's unimplemented backend/device gates
remain dependencies. Further language providers, JIT and VM worker pools still
require measured game needs. The first reference report establishes the tool and
methodology for those decisions, without turning fixture numbers into product
requirements.
