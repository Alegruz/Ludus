# Maintainability Review (Brutal but Actionable)

Status: authoritative
Owner: maintainers
Last updated: 2026-01-26

This review is intentionally direct. The codebase has many strengths, but it is currently *documentation-heavy and policy-light*. That combination makes it harder than necessary for newcomers to understand what matters, what is stable, and where to make changes.

Use this document as a prioritized roadmap rather than a critique for critique's sake.

---

## Executive summary

### What is already strong

- **Layered architecture is clearly intended and mostly respected.** The split between Core, Platform, and Editor is visible in both the directory layout and docs.
- **The project is unusually well documented for its size.** There is clear effort to teach, not just to ship.
- **Build presets and tooling integration show mature intent.** Static analysis and sanitizers are first-class concepts.

### What is currently risky

1. **The documentation surface area is large and fragmented.** Newcomers must guess which doc is authoritative.
2. **There is no contributor contract.** The repo lacks an explicit "how we work" and "how we keep layers clean" guide.
3. **The build system is doing too much *policy* work.** Auto-installing toolchains (including `sudo apt-get`) inside CMake makes builds less predictable and harder to reason about.
4. **Module boundaries are described, but not enforced.** There are few guardrails to prevent accidental cross-layer coupling over time.

---

## Critical observations and recommendations

### 1) Documentation is abundant but not curated

**Problem:** There are many docs, but no clear map of "read these in this order." This increases the cognitive load for both users and contributors.

**Recommendation:** Establish a documentation spine.

Suggested approach:

- Treat [`README.md`](../README.md) as the *front door*.
- Add a short "Start here" section that routes readers to:
  - [`docs/GETTING_STARTED.md`](GETTING_STARTED.md) for setup.
  - [`docs/ENGINE_ARCHITECTURE.md`](ENGINE_ARCHITECTURE.md) for conceptual mapping.
  - [`CONTRIBUTING.md`](../CONTRIBUTING.md) for developer behavior and guardrails.
  - This document for known maintainability issues.

**Why this matters:** Newcomers rarely fail because the code is hard; they fail because they cannot identify the *authoritative path* through the project.

---

### 2) The build system currently mixes configuration with environment management

**Problem:** `CMakeLists.txt` attempts to install developer tooling automatically, including invoking `sudo apt-get` on Linux. This is convenient, but it also:

- breaks the expectation that configure/build steps are side-effect-free,
- makes CI and local builds behave differently in subtle ways,
- creates unclear failure modes when tools are partially installed.

**Recommendation:** Move auto-install behavior behind an explicit script and make it opt-in.

A pragmatic path:

- Keep detection in CMake.
- Replace auto-install attempts with:
  - a clear warning message, and
  - a pointer to a dedicated setup script or documentation section.
- Provide a separate `tools/bootstrap.*` script that developers run intentionally.

**Why this matters:** Deterministic builds are a prerequisite for maintainability. If "configure" can modify the machine, debugging becomes much harder.

---

### 3) Architectural layering needs enforcement, not just documentation

**Problem:** The architecture is explained well, but there are no automated checks that prevent Core/Platform drift.

**Recommendation:** Add lightweight architectural tests.

Examples that scale well:

- A script that uses `compile_commands.json` to detect forbidden include patterns.
- CMake interface targets that do not expose platform headers to Core.
- A CI check that fails if `src/Engine/Core/` includes platform headers.

**Why this matters:** Architecture erosion is gradual. Documentation cannot stop it; tests can.

---

### 4) There is no single contributor workflow reference

**Problem:** The repo previously had no `CONTRIBUTING.md`. That omission forces contributors to reverse-engineer norms from scattered docs and code.

**Recommendation:** Maintain [`CONTRIBUTING.md`](../CONTRIBUTING.md) as the source of truth for:

- layer guardrails,
- expected testing behavior,
- API design heuristics,
- what documentation updates are required alongside code changes.

**Why this matters:** Maintainers need a document they can point to when reviewing PRs. Without it, every review becomes a debate.

---

## A concrete 30/60/90 day improvement plan

This plan is intentionally scoped to be realistic for a small engine project.

### First 30 days - reduce newcomer confusion

- Add a "Start here" section to the root README that curates the doc order.
- Create a short "docs index" inside `docs/README.md` that labels each doc as one of:
  - onboarding,
  - reference,
  - design notes,
  - historical/implementation detail.
- Ensure `GETTING_STARTED.md` covers the most common failure cases and how to recover quickly.

### 60 days - add guardrails and trim policy from CMake

- Move tool installation to `tools/bootstrap.*` and keep CMake side-effect-free.
- Add at least one architectural check (even a small one) to CI.
- Make a small set of "golden path" commands that always work and document them once.

### 90 days - codify subsystem boundaries

- Introduce per-subsystem overview docs (1 page each) for Core, Platform, and Renderer.
- Require a short "design note" for any new subsystem or public API.
- Audit public headers for unnecessary includes and tighten them.

---

## Maintainer heuristics for reviewing changes

When reviewing future changes, apply these questions aggressively:

1. **Is the change in the right layer?**
2. **Does it make the public surface area bigger than necessary?**
3. **Can a newcomer guess how to use this without reading the implementation?**
4. **What prevents this from being misused six months from now?**
5. **Which doc would a careful reader update alongside this change?**

If a change fails one of these tests, the fix is usually to:

- move code down a layer,
- narrow the API,
- add a test,
- add a short doc note near the change.

---

## Bottom line

Ludus already shows strong engineering intent. The next leap in maintainability will not come from more features; it will come from *curation, guardrails, and reducing ambiguity*.

This review plus [`CONTRIBUTING.md`](../CONTRIBUTING.md) is meant to make that shift explicit and operational.
