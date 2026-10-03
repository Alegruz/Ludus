# Kiro handoff for independent Ludus game projects

Implement shared installed SDKs, portable independent game projects and one
project operation backend used by an installed CLI and optional Editor. The
initial target is native Linux x64 on Ludus's reference toolchain. Preserve
existing Editor E0, runtime and browser behavior. This package contains design
documents only; all implementation tasks start unchecked.

Read these five files together:

- `.kiro/specs/project-sdk-workflow/requirements.md`
- `.kiro/specs/project-sdk-workflow/design.md`
- `.kiro/specs/project-sdk-workflow/tasks.md`
- `docs/architecture/project-sdk-workflow.md`
- `docs/architecture/project-sdk-workflow-kiro-handoff.md`

The documents are self-contained. Ludus-Sandbox lives in a separate repository
and may be available in a sibling checkout. Inspect its current code and setup
before changing it; its local setup repair does not establish shared-SDK migration
acceptance. No private PDFs or conversation history are needed. Discover actual
current code and tools before implementation.

## Prompt 1 Implement and open the PR

```text
Implement the committed Ludus independent game project and shared SDK design.
Read AGENTS.md and applicable .kiro/steering instructions, then all five files
listed in docs/architecture/project-sdk-workflow-kiro-handoff.md.

Execute P0-P5 in dependency order, implementing rather than writing another plan.
Create a codex/project-sdk-workflow branch from current main, preserving unrelated
changes. Commit logical implementation batches, push the branch and open a PR
targeting main. This explicitly authorizes those branch/PR operations. Do not
push implementation directly to main, merge, create tags or publish releases.
Use a draft PR and identify incomplete gates if required acceptance is blocked.

Prove a relocatable runtime SDK first. Audit every installed module's static
link closure, including volk, FreeType/HarfBuzz, Threads and system dependencies.
Package redistributable dependencies, CMake metadata and notices. Test extraction
at a new prefix without producer checkout or Conan-cache access. Record actual
compiler/runtime ABI, platform baseline, flavor/features/policy and source
revision; never claim binary compatibility from a version string alone.

Install host tooling independently of the checkout and Qt. Extract existing
Python model/planning/CMake File API/supervision code rather than duplicating it.
Implement explicit bounded/checksummed SDK install into a shared immutable store,
atomic publication, cancellation and concurrency rules. Normal open/build/run
must not download SDKs, refresh locks or compile engine sources.

Implement bounded v2 descriptors, exact release locks, ignored local overrides,
explicit recoverable migration and versioned native templates. Retain v1 project
behavior. Project creation must stage complete output and publish without replacing
an existing destination, including destination races. Keep CMake authoritative.
Implement local installed-SDK refresh detection and visible override resolution;
never mix headers/libraries/flavors or run an old binary after failed builds.

Implement P16 setup checks shared by creation, Open, SDK update and explicit
repair: selectable configure/build/test presets via real CMake discovery,
SDK/dependency prefixes, compiler/Ninja/shader tools and IDE CMake selection.
Open reports actionable diagnostics without downloads, configure/build or writes.
Repair preserves custom presets/settings, refreshes stale caches and verifies
configure/build/test. Cover missing local presets, hidden/disabled/broken presets,
moved tools/SDKs, stale IDE paths, fresh clones and repeated repair.

Expose project create/configure/build/run and engine selection through CLI and
the same backend used by the Editor. Extend v2 native profiles to Release while
preserving assertion policy. Maintain exact argv/cwd, shared build locks, bounded
output, asynchronous GUI behavior, Stop/Close/EOF cleanup, stale-event rejection
and existing E0 conflict/atomic-save rules. Package the optional Editor separately;
headless tooling and runtime exports remain Qt-independent.

Add candidate-package CI and version-tag release workflows, without executing a
public release during this task. Prove two independent generated games share one
SDK and compile no engine sources. Inspect actual Ludus-Sandbox only if available
and separately authorized; otherwise document the external conversion follow-up
and validate the standalone reference in this repository.

Respect C++23, fixed-width aliases, no-exceptions, public include boundaries,
warning/format/tidy and build-budget rules. No scenes/ECS, embedded play, hot
reload, arbitrary template hooks or new general build/plugin framework. Preserve
current web packaging and browser targets; new web templates are deferred.

Create docs/development/project-sdk-workflow-evidence.md and record actual
commands/results, starting revision and limitations. Run the acceptance journey
and all applicable pinned build/test/sanitizer/format/tidy/header/budget/SDK gates,
Editor ON/OFF checks and relevant browser/RAD regressions. Native GUI acceptance
cannot be replaced by offscreen tests. Update tasks only with evidence. Finish
with the implementation PR URL, usage commands and honest remaining gaps.
```

## Prompt 2 Continue the next implementation batch

```text
Continue .kiro/specs/project-sdk-workflow on the existing implementation branch.
Read AGENTS.md, steering, all five handoff documents and current evidence/PR.
Inspect actual changes and verify checked tasks with missing evidence. Implement
the next incomplete phase whose prerequisites pass, including meaningful failure
tests and real package/process/GUI acceptance where required. Preserve unrelated
work and legacy E0/browser/RAD behavior. Do not weaken requirements to complete
checkboxes. Update the evidence ledger and existing implementation PR; do not
create duplicate PRs, merge, tag, publish releases or push implementation to main.
```

## Prompt 3 Review and repair before merge

```text
Review the project-sdk-workflow implementation PR against P01-P16 and design.md.
Use code, installed artifacts and real acceptance evidence. Check all exported
static dependency closures without checkout/Conan access, source-path leakage,
compiler/runtime/flavor identity, matching policy headers, archive containment
and checksum checks, concurrent/partial installs, descriptor-lock migration
recovery, no-replace generation races and SDK refresh detection.

Prove CLI and Editor share operations/build locks/lifecycle semantics, v1 remains
usable, headless tooling has no Qt dependency, build compiles no engine sources,
two projects share an SDK, and failed/cancelled builds cannot launch stale files.
Verify native New Project acceptance, Editor ON/OFF, required pinned gates and
browser/RAD regressions. Release CI must validate candidates before publication
and never publish during a PR. Do not claim Sandbox conversion without inspecting
that repository and its separate evidence.

Repair verified defects with focused regressions, update evidence/task status,
push the existing implementation branch and update its PR. Report actionable
findings with source locations and reproducers. Do not merge, create tags,
publish releases or push implementation directly to main.
```
