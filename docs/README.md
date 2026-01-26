# Ludus Documentation Hub

Status: authoritative  
Owner: maintainers  
Last updated: 2026-01-26

This is the canonical index of all docs. If a doc exists, it must be listed here with a short purpose and audience.

---

## Start Here (Recommended Reading Order)

1. [GETTING_STARTED.md](GETTING_STARTED.md) - setup, first build, and troubleshooting
2. [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md) - layered architecture and public API map
3. [CONTRIBUTING.md](../CONTRIBUTING.md) - workflow, layering guardrails, and expectations
4. [MAINTAINABILITY_REVIEW.md](MAINTAINABILITY_REVIEW.md) - blunt risks and priorities

---

## Documentation Map

### Onboarding

| Document | Purpose | Audience |
|----------|---------|----------|
| [GETTING_STARTED.md](GETTING_STARTED.md) | First build, presets, and recovery from common failures | New contributors |

### Architecture and Design

| Document | Purpose | Audience |
|----------|---------|----------|
| [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md) | Layer model, public API, and module boundaries | Everyone |
| [MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md) | Memory strategy, pools, mimalloc integration | Engine developers |
| [COMMAND_LINE_PARSER_DESIGN.md](COMMAND_LINE_PARSER_DESIGN.md) | Design notes for CLI parser | Engine developers |

### Build, Tooling, and CI

| Document | Purpose | Audience |
|----------|---------|----------|
| [BUILD_SYSTEM.md](BUILD_SYSTEM.md) | CMake philosophy, presets, and build policy | Maintainers |
| [CI_CD_TESTING.md](CI_CD_TESTING.md) | CI behavior and test policy | Maintainers |
| [STATIC_ANALYSIS.md](STATIC_ANALYSIS.md) | clang-tidy, cppcheck, MSVC /analyze | Code quality |

### Testing and Quality

| Document | Purpose | Audience |
|----------|---------|----------|
| [UNIT_TESTING.md](UNIT_TESTING.md) | Writing and running tests | Developers |

### Debugging and Profiling

| Document | Purpose | Audience |
|----------|---------|----------|
| [LEAK_DETECTION.md](LEAK_DETECTION.md) | Leak detection system and sanitizers | Debug-focused developers |
| [LEAK_DETECTION_QUICK_REFERENCE.md](LEAK_DETECTION_QUICK_REFERENCE.md) | Macro reference | Developers |
| [MIMALLOC_INTEGRATION.md](MIMALLOC_INTEGRATION.md) | Mimalloc setup and profiling | Performance engineers |
| [MEMORY_LEAK_DETECTION_IMPLEMENTATION.md](MEMORY_LEAK_DETECTION_IMPLEMENTATION.md) | Implementation details | Engine developers |

### Command-Line Parser (Subsystem)

| Document | Purpose | Audience |
|----------|---------|----------|
| [COMMAND_LINE_PARSER_README.md](COMMAND_LINE_PARSER_README.md) | Canonical usage and behavior | Developers |
| [COMMAND_LINE_PARSER_QUICK_REF.md](COMMAND_LINE_PARSER_QUICK_REF.md) | Quick reference | Developers |
| [COMMAND_LINE_PARSER_DESIGN.md](COMMAND_LINE_PARSER_DESIGN.md) | Design decisions | Maintainers |

### Project Process

| Document | Purpose | Audience |
|----------|---------|----------|
| [BRANCHING.md](BRANCHING.md) | Branching strategy and flow | Contributors |
| [DELIVERABLES.md](DELIVERABLES.md) | Project deliverables and scope | Maintainers |

### History and Logs

| Document | Purpose | Audience |
|----------|---------|----------|
| [devlog/2026-01-Week04.md](devlog/2026-01-Week04.md) | Weekly devlog | Anyone |
| [IMPLEMENTATION_COMPLETE.md](IMPLEMENTATION_COMPLETE.md) | Historical milestone | Maintainers |

---

## Quick Navigation by Task

- Engine structure and modules: [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md)
- Public API vs internal: [ENGINE_ARCHITECTURE.md#public-api-organization](ENGINE_ARCHITECTURE.md#public-api-organization)
- First build: [GETTING_STARTED.md](GETTING_STARTED.md)
- Unit tests: [UNIT_TESTING.md](UNIT_TESTING.md)
- Leak debugging: [LEAK_DETECTION.md](LEAK_DETECTION.md)
- Build presets and CI: [BUILD_SYSTEM.md](BUILD_SYSTEM.md), [CI_CD_TESTING.md](CI_CD_TESTING.md)
- Static analysis: [STATIC_ANALYSIS.md](STATIC_ANALYSIS.md)

---

## Doc Maintenance Rules

1. If you add or rename a doc, update this index in the same commit.
2. List each doc once, in the most appropriate category.
3. Use concise, factual summaries.
4. Keep diagrams ASCII-only to avoid encoding issues.

---

Ready to start? Begin with [GETTING_STARTED.md](GETTING_STARTED.md).
