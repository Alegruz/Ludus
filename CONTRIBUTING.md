# Contributing to Ludus

This document is intentionally opinionated. Ludus is an educational engine, so we optimize for *clarity, safety, and teachability* over cleverness and micro-optimizations.

If you are new here, follow this checklist in order:

1. Read the project overview in [`README.md`](README.md).
2. Read the architectural map in [`docs/ENGINE_ARCHITECTURE.md`](docs/ENGINE_ARCHITECTURE.md).
3. Skim the maintainability review in [`docs/MAINTAINABILITY_REVIEW.md`](docs/MAINTAINABILITY_REVIEW.md) to understand current pain points and priorities.
4. Configure, build, and run tests using [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md).

---

## What “good” looks like

A good change in Ludus usually has these characteristics:

- **Small surface area:** touches a focused set of files.
- **Clear ownership:** belongs obviously to one module/layer.
- **Teachable API:** a newcomer can guess how to use it from the name and types.
- **Symmetric design:** if we add `X::Create`, we should consider what `X::Destroy` or `X::Reset` means.
- **Documented intent:** non-obvious decisions are explained near the code or in `docs/`.

---

## Development workflow

### Branching and PR flow

We use a lightweight, release-friendly model:

- **`main`**: always release-ready. Only merged from `release/*` or `hotfix/*`.
- **`develop`**: integration branch for active work.
- **`feature/*`**: short-lived branches for most changes (target `develop`).
- **`hotfix/*`**: urgent fixes branched from `main` (merge back to `main` and `develop`).
- **`release/*`** (optional): stabilization branch cut from `develop` when preparing a release.

PR policy:

- Default PR target is **`develop`**.
- **`main`** accepts only `release/*` or `hotfix/*` PRs.
- Use **squash merge** for `feature/*` PRs to keep history clean.

### 1) Set up the build

Follow the platform-specific instructions in [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md).

### 2) Build targets explicitly

Prefer building specific targets instead of “everything”:

```bash
cmake --build --preset ninja_clang-debug -t LudusCore
cmake --build --preset ninja_clang-debug -t LudusPlatform
cmake --build --preset ninja_clang-debug -t LudusTests
```

This keeps iteration fast and makes module boundaries more obvious.

### 3) Run tests locally

```bash
cmake --build --preset ninja_clang-debug -t LudusTests
./build/bin/LudusTests
```

If you add a feature without a test, you should have a clear reason why.

---

## Architectural guardrails (must follow)

Ludus is organized in layers. Violating these rules makes the engine harder to teach and maintain.

- **Core must not depend on Platform.**
  - No OS headers or platform-specific branches in `Engine/Core`.
- **Platform may depend on Core, but not on Editor.**
- **Editor may depend on everything, but should act as a client of engine APIs, not a backdoor.**

When in doubt, consult [`docs/ENGINE_ARCHITECTURE.md`](docs/ENGINE_ARCHITECTURE.md) and prefer moving logic *down* the stack rather than *up* it.

---

## Code style expectations

### API design

- Prefer **nouns for types** and **verbs for functions**.
- Avoid abbreviations unless they are industry-standard (`CPU`, `GPU`, `ID`).
- Public APIs should read well at the call site.

### Error handling

- Fail fast in debug builds with assertions when invariants are violated.
- Avoid silently returning “default” values in core systems.

### Includes and dependencies

- Minimize includes in headers; prefer forward declarations when possible.
- Do not introduce new third-party dependencies without updating `docs/` with rationale and trade-offs.

---

## Documentation requirements

Changes that affect how newcomers understand the engine should update documentation in the same commit. Typical triggers:

- New module or subsystem.
- Public API changes.
- Changes to build flags, presets, or toolchain behavior.
- Any change that would surprise a careful reader of the current docs.

At minimum, update the most relevant file in `docs/`. If no good file exists, add one.

---

## Where to put things

Use this as a quick routing table:

- `include/Ludus/Engine/Core/`: Platform-independent types, utilities, math, containers, memory.
- `include/Ludus/Engine/Platform/`: Cross-platform interfaces and platform detection.
- `src/Engine/Core/`: Implementations of core systems.
- `src/Engine/Platform/`: Platform abstractions and OS integrations.
- `src/Editor/`: The application layer and platform entry points.
- `docs/`: Design intent, trade-offs, and onboarding material.

If you are unsure, prefer adding a short note in `docs/` that explains the decision.

---

## High-value starter tasks

If you want to contribute but are not sure where to begin, start with one of these:

- Add or improve tests for math, containers, and command-line parsing.
- Tighten module boundaries (remove unnecessary includes or cross-layer knowledge).
- Replace “magic numbers” and implicit assumptions with named constants and documentation.
- Improve error messages and diagnostics for command-line parsing and platform initialization.

These tasks have a strong maintainability payoff and are friendly to newcomers.
