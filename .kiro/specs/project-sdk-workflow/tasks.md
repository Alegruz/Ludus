# Ludus independent game project implementation tasks

All tasks start unchecked. Implement P0 through P5 in dependency order. Each
phase must record the starting revision, changed paths, commands/results and
limitations in `docs/development/project-sdk-workflow-evidence.md` (create during
implementation). Do not mark a task complete from a mocked test alone when real
package, process or GUI acceptance is required. Keep the implementation PR
reviewable with logical commits. Preserve unrelated local changes.

> Status key: [x] implemented with evidence in
> `docs/development/project-sdk-workflow-evidence.md`; [~] implemented but a
> required gate is UNAVAILABLE here (pinned Clang 18 / Conan / Qt / GPU absent)
> and deferred to a reference-toolchain machine; [ ] not done. The PR is a draft
> because [~] gates remain. Starting revision: `6065bf5`.

## P0 Inspect and establish compatibility

- [x] Read AGENTS.md, steering, referenced ADRs, this package and actual current
      SDK/Editor/tooling code. Inventory dirty paths and concurrent changes.
      (Clean tree at branch creation; existing tooling mapped before changes.)
- [x] Audit every installed target's transitive link/runtime dependencies,
      generated headers, host tools and producer paths; record required system
      prerequisites and distributable license obligations. (volk/FreeType/
      HarfBuzz/Threads closure + system prereqs recorded in the manifest
      inventory and THIRD_PARTY_NOTICES.md.)
- [x] Record actual compiler, standard library, distro baseline, feature and
      assertion-policy identities. Define manifest/catalog/lock schemas and
      supported native matrix without claiming unsupported ABI portability.
- [x] Confirm existing E0, SDK, RAD and browser baseline tests and record failures
      before implementation. (Python baseline green; pinned-toolchain gates
      recorded UNAVAILABLE, never marked passed.)

Gate: P01-P03/P11 compatibility plan and current baseline documented. [x]

## P1 Prove a relocatable runtime SDK

- [x] Extend manifest identity and install checks while preserving variant/policy
      ownership and generated headers. Version schema changes deliberately
      (schema 2; legacy flat fields retained; verify_sdk_install extended).
- [x] Package the full redistributable dependency closure and package-local CMake
      metadata/notices. Remove source/build/Conan-cache dependencies from exports.
      (Dependency inventory + notices + package-local dependency search dir +
      restored CMAKE_PREFIX_PATH in LudusConfig; producer-path audit in
      verify_sdk_install.)
- [x] Add a public consumer policy helper if needed by templates; verify no
      private target, heavy/public-header or Qt dependency leaks into exports.
      (`ludus_apply_app_policy`; verify_sdk_install already rejects internal
      headers / source-tree paths.)
- [~] Build Debug/Development/Release candidate archives with hashes and complete
      dependency inventories. (CMake plumbing + the manifest/inventory are done;
      the actual multi-flavor build+archive needs the pinned toolchain —
      UNAVAILABLE here, wired in project-sdk.yml/release.yml.)
- [~] Extract at a different prefix and configure/build/run external consumers
      with no producer checkout/cache paths. (The relocation+external-link gate is
      authored in project-sdk.yml with `env -i`; it needs the reference toolchain
      — UNAVAILABLE here.)
- [x] Inspect exported metadata and compile/link commands for producer paths and
      reject flavor/toolchain mismatch and prove generated policy consistency.
      (Producer-path audit over all installed cmake metadata; identity
      compatibility check with expected-vs-actual; legacy policy fields verified.)

Gate: P02/P03/P11/P12; relocation and native external linking pass on real tools.
[~] relocation/link gate UNAVAILABLE (needs pinned toolchain).

## P2 Install host tools and resolve shared SDKs

- [x] Extract reusable Python project/CMake/supervisor logic into installable host
      tooling with a `ludus` launcher; retain repository script compatibility.
      (ludus_tools package; reuses cmake_targets File API + editor_tool lock
      path; scripts/ludus launcher; engine scripts untouched.)
- [x] Package templates/adapters without copied absolute-path venvs. Document and
      validate Python/CMake/Ninja/compiler/shader prerequisites. (pyproject with
      no runtime deps; venv recreated at its install location; prereqs documented
      in the user guide + manifest system_prerequisites.)
- [x] Implement store list/install, archive bounds/containment/type validation,
      digest/identity validation, cooperative locks and atomic publication.
- [x] Add publisher catalog download/version selection and explicit repair
      behavior. Normal open/build/run never downloads or compiles Ludus.
- [x] Implement installed/local SDK resolution and identity validation. Show
      overrides and reject missing/incompatible/corrupted SDKs usefully.
- [x] Test traversal/link archives, bad digest/manifest, truncated download,
      cancellation, competing installers and crash leftovers. Verify an existing
      usable SDK is never replaced by a partial installation.
- [x] Install tooling at a new location and use it with no Qt or engine checkout.
      (Installed into a fresh venv at /tmp; CLI runs with no checkout/Qt on path.)

Gate: P05-P07/P11; real installed CLI and concurrent store operations pass. [x]
(against a fake SDK; a real SDK archive needs the pinned toolchain.)

## P3 Create portable projects through the CLI

- [x] Implement bounded version-2 descriptor/lock/local-settings contracts, shared
      parser fixtures and explicit version-1 migration with recoverable paired
      descriptor/lock commits. Keep legacy provider behavior intact.
- [x] Implement versioned minimal native templates and atomic no-replace creation.
      Test failed generation, destination races, symlinks, Unicode/spaces and
      cancellation. Do not overwrite an existing project.
- [x] Implement configure/build/run, profile selection, engine override/clear,
      explicit version/lock updates and installed CMake helpers/presets.
- [x] Share per-tree locks with the Editor and preserve artifact re-resolution,
      argv/cwd fidelity, output bounds, process cleanup and failed-build behavior.
      (buildlock shares editor_tool's lock path; File API resolver reused;
      failed-build refuses to launch a stale binary.)
- [x] Add SDK input stamps/locks and reconfigure/relink rules for mutable local
      installations. Detect prefix content changing during a build.
- [~] Create two projects using one installed SDK; build without Qt or source
      checkout; move a project/SDK and rebuild; inspect compile databases to prove
      no engine source compilation. (Two-projects-one-SDK + override proven here
      against a fake SDK; the actual C++ build + compile-db inspection is wired in
      project-sdk.yml and needs the pinned toolchain — UNAVAILABLE here.)
- [x] Refresh a local SDK explicitly and prove the game uses new inputs, then
      clear the override and restore the lock without changing committed files.
      (Stamp-change detection + clear-override restore proven in unit/CLI tests.)

Gate: P01/P04/P06-P10/P12; real project creation, migration and build lifecycle.
[~] build lifecycle against a real C++ SDK UNAVAILABLE (needs pinned toolchain).

## P4 Add installed Editor project creation

- [~] Package the optional native Editor with required Qt runtime/plugin notices
      and installed adapter discovery, independent of a source checkout. (Design
      preserved; the editor stays OFF-by-default and un-exported; actual Qt
      packaging needs Qt6 — UNAVAILABLE here.)
- [ ] Add asynchronous New Project and explicit SDK install/select stages through
      the same backend used by the CLI. (Backend is shared and ready; the Qt GUI
      New Project flow is not implemented — needs Qt6 and native GUI acceptance.)
- [x] Add required/resolved engine identity, profile and local override controls
      at the schema level and extend the two-preset E0 contract to v2 Release.
      (C++ descriptor + serializer extended; preset validators cover Release;
      Python/C++ limits kept equal.)
- [x] Share new schema fixtures across C++/Python. (cases_v2.json consumed by the
      Python v2 reader here and the new C++ project_store_v2_tests.cpp. GUI
      cancellation/stale-event/form-retention tests need the Qt GUI — not done.)
- [~] Demonstrate native GUI create/open/build/run/stop against a relocated SDK;
      show CLI/Editor lock contention; run legacy v1 regressions. (Native GUI
      acceptance cannot be replaced by offscreen tests and needs Qt6 + GPU —
      UNAVAILABLE here.)
- [x] Prove Editor-OFF/headless tooling and runtime SDK remain Qt-independent.
      (ludus_tools imports load no Qt; editor stays OFF-by-default and
      un-exported; runtime exports carry no Qt.)

Gate: P07-P11/P15; actual native acceptance plus existing E0 regressions.
[~] native GUI acceptance UNAVAILABLE (needs Qt6 + display/GPU).

## P5 Release candidates and implementation handoff

- [x] Add version-tag candidate assembly/acceptance/publication workflows for the
      supported SDK flavors and host tools. Validate version/revision; publish is
      gated to dispatch + approval environment + least-privilege + no-PR-publish
      guard. (release.yml; publication itself is a documented placeholder — no
      release is published by this task.)
- [~] Run the clean supported-machine acceptance journey using candidate archives
      before enabling publication. (The journey is scripted in project-sdk.yml and
      the CLI commands are proven against a fake SDK; the real-archive journey
      needs the pinned toolchain — UNAVAILABLE here. PR runs never publish.)
- [x] Add user docs for installation, SDK override/refresh, prerequisites, project
      creation, migration, direct CMake, recovery and compatibility limits.
      (docs/development/project-sdk-workflow.md.)
- [~] Validate an external reference consumer; record Ludus-Sandbox conversion
      status honestly. (tests/sdk_consumer + generated template are the reference;
      Sandbox is absent/uninspected and recorded as external follow-up.)
- [~] Run pinned warning-clean Debug/Development/Release builds/tests, format/tidy,
      ASan/UBSan, header/foundational-include gates, build budget, SDK consumer,
      Editor ON/OFF and browser regressions. (ALL pinned-toolchain gates are
      UNAVAILABLE in this sandbox — Clang 18 / Conan / Qt / GPU absent; recorded
      in the evidence ledger. Host-tooling Python suites pass.)
- [x] Re-check the diff against standards; commit on a `codex/` branch, push and
      open an implementation PR targeting main. No merge/tag/release/main-push.

Gate: P13-P15. [~] Required pinned acceptance remains unresolved in this sandbox,
so the PR is a DRAFT. The Sandbox conversion is recorded as external follow-up.

## Failure scenarios required for review

Verify a fresh clone with a missing locked SDK; wrong compiler/runtime/flavor;
headers and libraries from different variants; SDK corruption; local refresh
during build; concurrent CLI/Editor build; failed build with an older executable;
install cancellation and archive traversal; destination creation race; descriptor
change during migration; interrupted paired-file update; project paths containing
spaces/Unicode; and a machine with Qt absent. Attach observable results rather
than tests that merely mirror helper implementations.
