# Ludus independent game project implementation tasks

All tasks start unchecked. Implement P0 through P5 in dependency order. Each
phase must record the starting revision, changed paths, commands/results and
limitations in `docs/development/project-sdk-workflow-evidence.md` (create during
implementation). Do not mark a task complete from a mocked test alone when real
package, process or GUI acceptance is required. Keep the implementation PR
reviewable with logical commits. Preserve unrelated local changes.

## P0 Inspect and establish compatibility

- [ ] Read AGENTS.md, steering, referenced ADRs, this package and actual current
      SDK/Editor/tooling code. Inventory dirty paths and concurrent changes.
- [ ] Audit every installed target's transitive link/runtime dependencies,
      generated headers, host tools and producer paths; record required system
      prerequisites and distributable license obligations.
- [ ] Record actual compiler, standard library, distro baseline, feature and
      assertion-policy identities. Define manifest/catalog/lock schemas and
      supported native matrix without claiming unsupported ABI portability.
- [ ] Confirm existing E0, SDK, RAD and browser baseline tests and record failures
      before implementation. Resolve scope using evidence, never by weakening
      standards or marking unavailable checks passed.

Gate: P01-P03/P11 compatibility plan and current baseline documented.

## P1 Prove a relocatable runtime SDK

- [ ] Extend manifest identity and install checks while preserving variant/policy
      ownership and generated headers. Version schema changes deliberately.
- [ ] Package the full redistributable dependency closure and package-local CMake
      metadata/notices. Remove source/build/Conan-cache dependencies from exports.
- [ ] Add a public consumer policy helper if needed by templates; verify no
      private target, heavy/public-header or Qt dependency leaks into exports.
- [ ] Build Debug/Development/Release candidate archives with hashes and complete
      dependency inventories. Keep host tools distinct from target artifacts.
- [ ] Extract at a different prefix and configure/build/run external consumers
      with no producer checkout/cache paths in their environment or CMake search
      inputs. Exercise all available public module targets, not only Base.
- [ ] Inspect exported metadata and compile/link commands for producer paths.
      Reject flavor/toolchain mismatch and prove generated policy consistency.

Gate: P02/P03/P11/P12; relocation and native external linking pass on real tools.

## P2 Install host tools and resolve shared SDKs

- [ ] Extract reusable Python project/CMake/supervisor logic into installable host
      tooling with a `ludus` launcher; retain repository script compatibility.
- [ ] Package templates/adapters without copied absolute-path venvs. Document and
      validate Python, CMake, Ninja, compiler and shader tool prerequisites.
- [ ] Implement store list/install, archive bounds/containment/type validation,
      digest/identity validation, cooperative locks and atomic publication.
- [ ] Add publisher catalog download/version selection and explicit repair
      behavior. Normal open/build/run never downloads or compiles Ludus.
- [ ] Implement installed/local SDK resolution and identity validation. Show
      overrides and reject missing/incompatible/corrupted SDKs usefully.
- [ ] Test traversal/link archives, bad digest/manifest, truncated download,
      cancellation, competing installers and crash leftovers. Verify an existing
      usable SDK is never replaced by a partial installation.
- [ ] Install tooling at a new location and use it with no Qt or engine checkout.

Gate: P05-P07/P11; real installed CLI and concurrent store operations pass.

## P3 Create portable projects through the CLI

- [ ] Implement bounded version-2 descriptor/lock/local-settings contracts, shared
      parser fixtures and explicit version-1 migration with recoverable paired
      descriptor/lock commits. Keep legacy provider behavior intact.
- [ ] Implement versioned minimal native templates and atomic no-replace creation.
      Test failed generation, destination races, symlinks, Unicode/spaces and
      cancellation. Do not overwrite an existing project.
- [ ] Implement configure/build/run, profile selection, engine override/clear,
      explicit version/lock updates and installed CMake helpers/presets.
- [ ] Share per-tree locks with the Editor and preserve artifact re-resolution,
      argv/cwd fidelity, output bounds, process cleanup and failed-build behavior.
- [ ] Add SDK input stamps/locks and reconfigure/relink rules for mutable local
      installations. Detect prefix content changing during a build.
- [ ] Create two projects using one installed SDK; build without Qt or source
      checkout. Move a project/SDK and rebuild. Inspect compile databases to
      prove no engine source compilation.
- [ ] Refresh a local SDK explicitly and prove the game uses new inputs, then
      clear the override and restore the lock without changing committed files.

Gate: P01/P04/P06-P10/P12; real project creation, migration and build lifecycle.

## P4 Add installed Editor project creation

- [ ] Package the optional native Editor with required Qt runtime/plugin notices
      and installed adapter discovery, independent of a source checkout.
- [ ] Add asynchronous New Project and explicit SDK install/select stages through
      the same backend used by the CLI. Keep current workspace on failure.
- [ ] Add required/resolved engine identity, profile and local override controls.
      Extend the current two-preset E0 contract to v2 Release consistently.
- [ ] Share new schema fixtures across C++/Python. Test cancellation, duplicate
      actions, stale events, form retention, failed Open and cleanup states.
- [ ] Demonstrate native GUI create/open/build/run/stop against a relocated SDK.
      Show CLI/Editor lock contention. Run legacy v1 save/build/run regressions.
- [ ] Prove Editor-OFF/headless tooling and runtime SDK remain Qt-independent.

Gate: P07-P11/P15; actual native acceptance plus existing E0 regressions.

## P5 Release candidates and implementation handoff

- [ ] Add version-tag candidate assembly/acceptance/publication workflows for the
      supported SDK flavors and host tools/Editor. Validate version/revision and
      publish immutable catalogs/checksums/notices with minimal permissions.
- [ ] Run the clean supported-machine acceptance journey using candidate archives
      before enabling publication. PR runs never publish public releases.
- [ ] Add user docs for installation, SDK override/refresh, prerequisites, project
      creation, migration, direct CMake, recovery and compatibility limits.
- [ ] Validate an external reference consumer; record separate Ludus-Sandbox
      conversion instructions and its repository/access status honestly.
- [ ] Run pinned warning-clean Debug/Development/Release builds/tests, required
      format/tidy, ASan/UBSan build/tests, header self-sufficiency/foundational
      include gates, build budget and SDK consumer checks. Include Editor ON
      and independent OFF checks plus appropriate existing browser regressions.
- [ ] Re-check the entire diff against standards. Commit on a `codex/` branch,
      push that branch and open an implementation PR targeting main. Do not
      merge, tag, publish a release or push implementation directly to main.

Gate: P13-P15. Report failed/unavailable checks as incomplete and label the PR
draft if required acceptance remains unresolved. A separate Sandbox PR is not
a gate for the Ludus PR, but its pending conversion must remain clearly recorded.

## Failure scenarios required for review

Verify a fresh clone with a missing locked SDK; wrong compiler/runtime/flavor;
headers and libraries from different variants; SDK corruption; local refresh
during build; concurrent CLI/Editor build; failed build with an older executable;
install cancellation and archive traversal; destination creation race; descriptor
change during migration; interrupted paired-file update; project paths containing
spaces/Unicode; and a machine with Qt absent. Attach observable results rather
than tests that merely mirror helper implementations.
