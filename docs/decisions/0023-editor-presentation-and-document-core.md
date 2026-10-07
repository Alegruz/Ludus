# ADR 0023: Editor presentation and portable document core

Status: Proposed, 2026-10-06. Architecture target; implementation and platform
qualification are separate work.

## Context

Ludus has a Linux Qt Widgets workspace and a bounded Qt/Wasm document preview.
The browser UI inherits desktop chrome and fixed panels. More importantly, its
private document/controller models use Qt types, so sharing the shell has not
created a toolkit-independent authoring system. Native macOS/Windows tools and
browser scene authoring need explicit host and rendering integration.

No delivery deadline requires us to preserve a toolkit at any cost. The decision
must prioritize reliable authoring, usable text/accessibility, maintenance,
debugging and measured performance over cosmetic novelty or migration avoidance.

## Decision

Select Qt 6 Widgets for native Linux/macOS/Windows presentation, with a small
shared semantic design system and task-focused layout. Keep normal native window
and menu conventions; remove simulated OS decoration from browser presentation.
Use standard model/view controls and custom 2D canvas controls where needed.
Ludus rendering owns 3D authoring previews and gizmos, rather than all GUI drawing.
Dear ImGui may serve developer diagnostics; runtime UI remains independent.

Extract a private Qt-free document/application core with typed schemas, stable
IDs, validated edit transactions, one history per document, saved-content identity
and revision-tagged deltas. GUI, tests and later batch tools invoke the same
operations. Qt view models are projections. File/process services are explicit
host capabilities. Preserve existing engine, project tooling and GameHost policy.

Retain the Qt/Wasm preview while qualifying full browser authoring. Require real
IME/text, accessibility, large-model, canvas/WebGPU composition and persistence
acceptance. If Qt/Wasm cannot meet required browser tasks through maintainable
public seams, select a DOM browser frontend over the same core. Do not maintain
two browser frontends indefinitely. Full browser builds/debugging additionally
need an execution strategy; presentation does not solve that service problem.

Keep gameplay out of process. Trusted authoring rendering may run in process from
draft snapshots after an explicit surface/ownership prototype. Current Platform/
RHI handles do not already implement this general embedding or multiple-surface
contract. A separate preview window is a supported fallback.

Do not introduce a universal widget abstraction, general event bus, reflection
framework, dynamic plugin framework, new scripting language or extra UI toolkit
as prerequisites. Start with one workflow extraction and runnable risk probes.

## Alternatives and consequences

Qt Quick is an alternative for a demonstrated custom surface, not a blanket
style replacement. DOM for the entire desktop application would unify browser
presentation but add bindings, native service and GPU-composition responsibilities.
An engine-owned toolkit adds long-term text, IME, accessibility and desktop
interaction work. Per-OS native layouts duplicate authoring integration. ImGui
is well suited to diagnostic surfaces but its upstream accessibility/text
limitations are a poor default for the primary authoring interface.

The core extraction is real work, and a conditional browser frontend can require
an additional adapter. This cost purchases testability and a reversible
presentation decision. Qt's maturity is not proof of browser task parity, and
existing code is not a comparative benchmark. Measure startup, memory, input
latency, idle cost, large-list behavior and task success before enforcing budgets.
A modern visual style must preserve semantics and platform behavior.

Revisit selection if Qt fails required native/browser user tasks, renderer
composition proves unmaintainable, a prototype shows material workflow or
performance advantages, or maintenance evidence favors another frontend. Use
the same correctness and usability gates rather than screenshot preference.

## References and implementation owner

See [GUI systems](../architecture/editor-gui-systems.md) for ownership,
transactions, jobs, surfaces, host matrix and staged acceptance, and the
[reference review](../architecture/editor-gui-reference-review.md) for actual
book excerpts and primary toolkit documentation. The review thanks Wihlidal,
Lightbown, Hirst and Nystrom, identifies adopted ideas and rejects incompatible
historical implementations. The current workspace guide remains authoritative
for shipped features; this ADR does not enable new runtime capabilities.
