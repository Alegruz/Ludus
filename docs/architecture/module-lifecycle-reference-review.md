# Module lifecycle reference review

The [final architecture](module-lifecycle.md) combines an explicit startup plan,
checked lifetime dependencies, attempted-start rollback, and module-owned
asynchronous retirement. This review records which readings from
`references/game-dev-gems-toc.md` actually informed
that design and which historical techniques were rejected.

Reviewed on 2026-10-04. The TOC and source PDFs are local reference material,
not distributed with the repository; printed pagination identifies the passages
in independently obtained copies. The index includes engine books and tool books as well as
Gems volumes. Titles were discovery pointers; recommendations below follow
reading the stated passages. All summaries are original paraphrases. The
architecture's completion, cancellation, and ownership rules are Ludus design
judgments, not claims that the historical articles prove modern concurrency or
performance guarantees.

## Design before the chapter review

The initial design selected an application composition root, typed dependency
injection, an immutable dependency DAG with deterministic serial startup,
exception-free explicit lifecycle results, inert constructors, begin/poll start
and stop operations, and a journal entered before every startup attempt.
Shutdown closed application admission and retired consumers before providers.
Process, engine, and play-session lifetimes remained distinct. Static engine
libraries, Base emergency reporting, and the existing gameplay reload transaction
were preserved. Parallel startup required measurements.

That baseline was recorded before searching the TOC. The review refined graph
ordering into a visible authored array with validation, and strengthened
cancellation, instance boundaries, and completion ownership.

## Discovery and reading method

Searched the local TOC for startup/shutdown, initialization, dependency,
singleton, plugin, and responsive-processing entries. Read selected chapters by
PDF text extraction. The scanned Bilas article required OCR; visually inspected
its opening and implementation pages to verify the article identity, printed
pagination, and historical pointer casts. PDF positions below are one-based file
pages, separate from printed page numbers. Unreviewed sections are not evidence.

The most direct chapter is Gregory's section 6.1. The two most useful Gems
articles are Bilas on controlled instance lifetime and Hamaide on job
dependencies. Wihlidal's cancellation discussion adds a concrete race case.

## Readings and decisions

### Subsystem Start Up and Shut Down

Jason Gregory, *Game Engine Architecture*, third edition, section 6.1,
printed pp. 417–425; local PDF pp. 436–444.
Local TOC line 5429.
Read the entire section, including its OGRE and Naughty Dog examples.

The section discusses cross-subsystem ordering, hidden work in lazy singleton
access, explicit lifecycle calls, and the debugging advantages of a visibly
ordered startup sequence. Its examples describe the engines at the time of
publication, not their current implementations.

**Design revision:** Author the plan's array directly and validate its dependency
edges. Keep constructors inert and lifecycle entry points ordinary named
functions. Generate reverse cleanup from the actual attempt journal, preserving
readability while removing duplicated teardown ordering. Do not adopt global
manager access or treat successful boot/shutdown examples as proof that partial
failure and asynchronous cleanup are safe.

### An Automatic Singleton Utility

Scott Bilas, *Game Programming Gems 1*, article 1.3, printed pp. 36–40;
local PDF pp. 34–38.
Local TOC line 6699.
Read the entire article using OCR and visual verification. The local catalog
does not contain the original printed contents pages; article numbering comes
from the article itself.

The utility tracks an instance separately from choosing when it is created and
destroyed. The discussion explains why first-use construction and exit-only
destruction are inadequate for controlled partial shutdown.

**Design revision:** Enforce uniqueness where a backend actually requires it at
the owning scope, while keeping construction and lifetime explicit. One instance
does not imply globally accessible state. Preserve RHI's current one-session
constraint without imposing singleton inheritance on every service. Reject the
historical pointer-to-`int` offset technique, fake pointer casts, global getter
macros, and registration through constructors. Those examples do not establish
portable modern C++ behavior or thread safety.

### Multithread Job and Dependency System

Julien Hamaide, *Game Programming Gems 7*, article 1.9, printed pp. 87–96;
local PDF pp. 120–129.
Local TOC line 7178.
Read the entire article, including its limitations and future work.

The article separates jobs, scheduling, workers, and dependency entries. It uses
reusable workers, explicit dependency completion, and identifiers that distinguish
reused table slots. It also notes limits when integrating external synchronization.

**Design revision:** Keep service lifetime edges separate from job completion.
Require a named completion/join condition for asynchronous cleanup and retain
operation identity across slot reuse. Future parallel preparation should reuse
an existing scheduler. Do not copy the standalone scheduler thread, unbounded
growing dependency table, priorities, or the convention that a missing dependency
counts as satisfied into engine startup. This article supplies no GPU-retirement
or cancellation proof and no Ludus startup speedup.

### Responsive UI During Intensive Processing

Graham Wihlidal, *Game Engine Toolset Development*, chapter 37,
printed pp. 423–430;
local PDF pp. 442–449.
Local TOC line 6447.
Read the entire chapter; the following Part VII introduction is outside scope.

The chapter discusses progress, cooperative cancellation, completion reporting,
UI-thread marshaling, and excessive progress-notification overhead. It explicitly
describes a race where work finishes before observing a cancellation request.

**Design revision:** A stop latch takes precedence over publishing startup
readiness once observed by the owner. A late success is disposed safely rather
than admitting new gameplay. Progress is bounded and emitted on changes; polls
yield to platform/browser dispatch. Separate a cancellation request from verified
completion. Reject .NET BackgroundWorker and exception-based reporting as Ludus
dependencies; use explicit statuses and owner-thread result consumption.

### Game Initialization and Shutdown

Mike McShaffry and David Graham, *Game Coding Complete*, fourth edition,
chapter 5. Focused reading: printed pp. 130–132 and 147–152;
local PDF pp. 175–177
and [192–197](../../references/Game%20Coding%20Complete%20-%204th%20Edition.pdf#page=192).
Local TOC line 4959.
The intervening initialization checklist and remaining chapter were not reviewed.

These passages discuss fallible initialization before a game UI exists, repeated
quit requests, and destruction that needs still-live providers.

**Design revision:** Preserve a structured primary failure through cleanup,
coalesce stop requests, and keep emergency diagnostics independent of renderer
readiness. Treat saving before exit as explicit application policy. Reject global
application pointers, deletion macros, ad hoc modal message loops, and historical
Windows checks as requirements for Ludus. The journal supplies one cleanup path
for failed startup and normal exit; that extension is this design's choice.

### Exporting C++ Classes from DLLs and Protect Yourself from DLL Hell and Missing OS Functions

Herb Marselas, *Game Programming Gems 2*, articles 1.4 and 1.5,
printed pp. 28–32 and 33–37;
local PDF pp. 25–29
and [30–34](../../references/Game%20Programming%20Gems%202.pdf#page=30).
[TOC entries](../../references/game-dev-gems-toc.md#L6789).
Read both complete articles.

The first discusses exported objects and paired creation/destruction helpers.
The second distinguishes loader failures from use of resolved functionality and
checks library/function availability explicitly.

**Design revision:** Preserve creator-owned destruction and compatibility checks
at Ludus's existing gameplay function-table boundary. A loader handle is not proof
that jobs, callbacks, destructors, and function pointers have retired. Keep engine
modules static and gameplay replacement in its existing transaction. Reject
exported C++ classes as a universal stable ABI, old Visual C++ workarounds, and
inference that loader reference counting proves application-reference safety.

### Singleton maintainability discussion

Robert Nystrom, *Game Programming Patterns*, chapter 6. Focused reading:
the regret discussion, local PDF pp. 108–112,
and the opening alternatives on pp. 113–114.
Local TOC line 7829.
This is a partial chapter review; the Service Locator chapter was not reviewed.

The passages distinguish limiting instance count from providing global access,
and discuss coupling, shared mutable state, and unpredictable first-use costs.

**Design revision:** Inject the exact typed providers into each module adapter
and service; do not pass a universal EngineContext offering lookup. Keep the
runner's context type erasure private to lifecycle dispatch. Preserve existing
logging facades without generalizing them into a universal singleton pattern.
This supports the initial composition-root choice rather than adding a framework.

## Changes incorporated into the architecture

| Initial choice | Refined contract | Evidence that informed the refinement |
| --- | --- | --- |
| Deterministic serial execution of a dependency DAG | Visible authored array; validate dependencies before side effects; journal determines cleanup | Gregory section 6.1 |
| Explicit instance ownership | Backend uniqueness is scoped; instance count and global access are separate decisions | Bilas article and Nystrom discussion |
| Pending operations with cancellation | Stop disposition survives a completion race; late acquired resources must be disposed | Wihlidal chapter 37 |
| Async-safe stop | Completion is a durable correctness record; lifetime dependencies do not substitute for joining work | Hamaide article, extended by Ludus ownership requirements |
| Failed-start rollback | Retain the first cause, coalesce quit requests, and report without requiring graphical UI | Focused McShaffry/Graham passages |
| Existing gameplay reload boundary | Creator destruction and negotiated capability checks remain inside a separate code-lifetime transaction | Marselas articles and existing ADR 0012 |

No article justified parallelizing every startup operation, inventing a new
scheduler, or replacing Ludus's static engine libraries with plugins. The review
therefore simplified orchestration while strengthening the difficult lifetime
contracts.

## Verification against primary platform and engine documentation

These sources corroborate narrow design principles. They are not benchmarks
or evidence that the proposed Ludus implementation has passed validation.

- [Epic's StartupModule contract](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/Modules/IModuleInterface/StartupModule?application_version=5.5)
  states that dependencies loaded during startup remain available for shutdown.
  Ludus expresses that guarantee through explicit lifetime edges and consumer
  retirement. This is a versioned Unreal 5.5 API reference, not a claim about the
  latest Unreal release or a proposal to adopt its runtime module manager.
- [O3DE service dependencies](https://docs.o3de.org/docs/user-guide/programming/components/services/)
  distinguish required and conditional services and order providers before
  consumers. Its [component lifecycle](https://docs.o3de.org/docs/user-guide/programming/components/overview/)
  describes reverse deactivation. Ludus resolves conditional choices in profiles
  and validates the resulting lifetime edges; it does not adopt EBus or reflection.
- [Emscripten's runtime environment](https://emscripten.org/docs/porting/emscripten-runtime-environment.html)
  and [main-loop API](https://emscripten.org/docs/api_reference/emscripten.h.html#c.emscripten_set_main_loop)
  describe cooperative browser callbacks and why a native blocking main loop or
  assumed exit destructors cannot implement browser lifecycle completion.
  Implementation must validate the repository's pinned SDK, not infer feature
  availability from the current documentation version.
- [SDL's main callback explanation](https://wiki.libsdl.org/SDL3/README-main-functions)
  illustrates host-owned application iteration on platforms including the browser.
  It supports keeping the runner advance independent of a blocking outer loop;
  no SDL dependency is added by this proposal.

The remaining implementation questions are concrete: which existing acquisition
paths need fallible allocation repair, which native calls can block, how each
backend proves callback/GPU retirement, and which trace producers must join before
capture reclamation. The final architecture makes these acceptance requirements
instead of claiming that an orchestration abstraction already solves them.
