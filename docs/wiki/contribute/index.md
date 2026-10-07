# Contribute to Ludus

Start with a concrete user problem or a bounded engine change. Find the owning
module, read its design/evidence and tests, and preserve its dependency direction.
Use a pull request to make the change and its validation reviewable.

## Required engineering rules

The repository's [AGENTS.md](https://github.com/Alegruz/Ludus/blob/main/AGENTS.md)
and [.kiro steering](https://github.com/Alegruz/Ludus/tree/main/.kiro/steering)
are authoritative. Read them before editing code. In particular:

- Use C++23 and the pinned reference tools; engine/app code has no C++ exceptions.
- Use Ludus fixed-width aliases and explicit error results.
- Keep heavy facilities and private headers outside public include boundaries.
- Credit actually consulted technical sources near the affected implementation.
- Build warning-clean and run appropriate unit, sanitizer, formatting and tidy gates.

## Standard native validation

Prepare tests with initialization, then use the pinned tools:

```bash
./scripts/build linux-clang-development
./scripts/test linux-clang-development
./scripts/check linux-clang-development --all
./scripts/build linux-clang-asan-ubsan
./scripts/test linux-clang-asan-ubsan
./scripts/install-sdk linux-clang-development
```

Report any skipped platform/manual acceptance explicitly. A successful offscreen
editor test is useful logic evidence, but it does not prove native high-DPI,
window decoration or device interaction.

## Keep the documentation honest

Update a task guide when a workflow changes. Separate current behavior from
architecture proposals, and state the environment needed by a command. Do not
turn a catalogued title into a claim that its chapter was consulted or adopted.

For documentation-only work, see [Improve this wiki](../../development/wiki.md). Include the built
site/link checks and browser evidence relevant to the change.

## Propose a change

[Open a pull request](https://github.com/Alegruz/Ludus/pulls) or
[report a reproducible issue](https://github.com/Alegruz/Ludus/issues).
Explain the resulting behavior, why it helps, what was validated and any remaining
acceptance limits. Keep secrets and machine-specific settings out of commits.
