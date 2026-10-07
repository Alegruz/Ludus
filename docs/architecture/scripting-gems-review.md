# Scripting architecture Gems review

The [scripting architecture](scripting.md) selects native C++ authoring and strict
Luau as the initial scripting candidate, with focused visual sequences/state
machines lowered to Luau. A shared gameplay contract permits a later optional C#
provider and a C-compatible native facade. Six chapters selected from
[the local reading index](../../references/game-dev-gems-toc.md) strengthen its
binding, lifetime, task-management, and tooling contracts. They do not establish
that Luau is faster for Ludus or justify building a custom language.

## Reading scope

The review used the source chapters, including their examples and conclusions,
on October 6, 2026. The Gems 6 copy is scanned; text was extracted with OCR and
material headings, author names, and scheduler figures were checked against page
images. Gems 7 has an extractable text layer; its chapter opening was also checked
visually. No chapter implementation is copied into Ludus.

Printed page numbers and one-based PDF page numbers differ. This copy of Gems 6
omits some blank leaves, so a single offset does not work throughout the volume.
The table gives the substantive printed pages actually present and the exact
PDF pages read. The books remain the user's local reference packet and are not
redistributed by this change.

| Chapter and authors | Printed pages | Local source |
| --- | --- | --- |
| *Game Programming Gems 6*, 4.1, **Scripting Language Survey**; Diego Garcés | 323-340 | [PDF pages 304-321](../../references/Game%20Programming%20Gems%206.pdf#page=304) |
| *Game Programming Gems 6*, 4.2, **Binding C/C++ Objects to Lua**; Waldemar Celes, Luiz Henrique de Figueiredo, Roberto Ierusalimschy | 341-355 | [PDF pages 322-336](../../references/Game%20Programming%20Gems%206.pdf#page=322) |
| *Game Programming Gems 6*, 4.3, **Programming Advanced Control Mechanisms with Lua Coroutines**; Luiz Henrique de Figueiredo, Waldemar Celes, Roberto Ierusalimschy | 357-369 | [PDF pages 337-349](../../references/Game%20Programming%20Gems%206.pdf#page=337) |
| *Game Programming Gems 6*, 4.4, **Managing High-Level Script Execution Within Multithreaded Environments**; Sébastien Schertenleib | 371-381 | [PDF pages 350-360](../../references/Game%20Programming%20Gems%206.pdf#page=350) |
| *Game Programming Gems 6*, 4.5, **Exposing Actor Properties Using Nonintrusive Proxies**; Matthew Campbell and Curtiss Murphy | 383-392 | [PDF pages 361-370](../../references/Game%20Programming%20Gems%206.pdf#page=361) |
| *Game Programming Gems 7*, 7.1, **Automatic Lua Binding System**; Julien Hamaide | 503-516 | [PDF pages 536-549](../../references/Game%20Programming%20Gems%207.pdf#page=536) |

Other index entries on reflection, property systems, event scheduling, and
function-binding generation overlap Ludus's existing
[reflection review](reflection-serialization-gems-review.md) and
[world review](game-world-gems-review.md). Their existing contracts are retained;
this review does not claim a new direct reading of those chapters. Gems 5 is not
in this local index, so a citation to its *Building Lua into Games* chapter in
Gems 6 is a further reading lead, not evidence consulted here.

## Design before and after the review

The initial design already separated native systems from scripted orchestration,
selected an established runtime, lowered small visual documents to that runtime,
and required explicit state, fixed phases, safe references, staging, and debugging.
The following refinements are incorporated into the final architecture.

| Initial contract | Refinement after reading | Source |
| --- | --- | --- |
| Checked userdata instead of exposed pointers | Define equality by complete identity, weak wrapper-cache policy, and mandatory shipping checks. GC cannot determine native entity/resource destruction. | Gems 6, 4.2; Gems 7, 7.1 |
| Generated bindings from a selected API | Share immutable descriptors; keep generated private source inspectable; measure generated code size; keep native overload names explicit. | Gems 6, 4.1; Gems 7, 7.1 |
| Tick-based waits and cancellation | Define ready/running/waiting/paused/terminal states, stable ready order, indexed wakeups, and explicit failed resume handling. Avoid unordered table iteration and scanning every sleeper. | Gems 6, 4.3-4.4 |
| Explicit persistent state | Durable continuation records remain distinct from transient coroutine stacks. Cleanup ownership stays native even when script callbacks fail or never run. | Gems 6, 4.3-4.4, adapted to Ludus reload contracts |
| Reuse reflection and owner validation | Treat inspection projections separately from script write capabilities; resource edits prepare candidates instead of loading resources inside setters. | Gems 6, 4.5 |
| One Luau VM on the world owner thread | Make task inspection and safepoint pause essential. Add explicit sandbox/time/RNG rules; retain copied snapshots for any future worker work. | Gems 6, 4.1/4.4; modern Luau cross-check |

The coroutine chapters motivate a convenient sequencing facility; they do not
solve save/load or arbitrary hot migration of stacks. The explicit durable-state
choice is a Ludus inference from their execution model and the engine's existing
checkpoint requirements. Similarly, generated graph lowering is a Ludus choice,
not a technique demonstrated by these chapters.

## Multiple language integration

The multi-language extension is a Ludus design decision. The chapters support
explicit binding metadata, native lifetime ownership, observable tasks, and
measured integration cost; they do not demonstrate modern .NET hosting or prove
that several runtimes improve a game's workflow.

Carry those lessons into one operation/value/event/state manifest, generated
provider bindings, declared persistence, and ordered engine-mediated events.
Keep execution objects and debugger machinery inside each provider. Luau owns
its VM inside a native game generation; a future CoreCLR provider uses a
GameHost-owned process runtime and separately retired behavior contexts. Neither
one VM per session nor Luau's allocation/interrupt policies can be assumed for
managed code.

Implement C++/Luau first. An actual C# project must justify a separate runtime,
deployment, reload, debugger, and platform gate. This follows Garcés's emphasis
on integration and development support without using the historical survey as
a ranking of current languages. The visual backend remains Luau; native C/C++
remains compiled code. No custom language or universal compiler IR is introduced.

## Scripting language survey

Garcés evaluates authoring features, C/C++ integration, performance/memory behavior,
and development support. The useful lesson is that debugger quality and integration
cost can outweigh language feature count. The sections on wrappers and development
support also make binding generation and useful runtime inspection part of the
initial investment, rather than polish added after a language has shipped.

Ludus consequently gates adoption on an actual native/browser binding and debugger
spike. The delivery sequence starts with one useful behavior and its diagnostic
path. Memory/GC and native crossings appear separately in the benchmark plan.

The chapter's language feature comparisons describe historical versions of Python,
Lua, GameMonkey, and AngelScript. They are not a current ranking. In particular,
statements about Lua allocator customization and external debugger availability
must not override modern runtime documentation. Strict Luau is selected from the
current integration requirements, not from an old performance comparison. The
chapter gives no basis for declaring a proprietary language necessary.

## Binding C and C++ objects to Lua

Celes, de Figueiredo, and Ierusalimschy compare light userdata, checked values,
full userdata, extensible userdata, and table wrappers. They separate flexibility,
runtime checking, representation cost, and control of host-object lifetime. The
discussion of re-exporting full userdata also raises object uniqueness and weak
references explicitly. Their benchmark conclusion warns that isolated method-call
timings do not determine a real application's performance.

Ludus uses checked opaque references and copied value projections. Identity is
the full world/session, slot, and generation tuple. Equality and any wrapper cache
use that identity. The cache must not keep every object reachable forever. Shared
type descriptors replace per-instance inheritance metadata where practical.

The historical sample uses native addresses as identities and lets GC delete
host objects under some binding strategies. Ludus's owner-thread world and module
reload contracts make those choices unsuitable. Wrappers cannot own entity life,
and old destructor/callback code cannot survive module unload. Shipping retains
type/lifetime/capability/range checks; debug status is not permission to remove
them. Extensible wrappers that permit replacing native methods are also unnecessary
for the selected operation-oriented API.

## Programming advanced control mechanisms with Lua coroutines

De Figueiredo, Celes, and Ierusalimschy show filters, iterators, schedulers, and a
dispatcher with awake, sleeping, waiting, and dead tasks. Cooperative execution
can make sequences readable without creating an OS thread for each actor. The
chapter explicitly notes on printed page 364 that iterating a task table with
`pairs` gives undefined scheduling order and recommends an array when order matters.

Ludus adopts visible task state, explicit wake reasons, and stable scheduling.
Deadlines use simulation ticks; ready work and subscribers are indexed so inactive
tasks are cheap. Resume errors have an explicit status and invocation context.
Events are copied bounded records admitted at named phases, rather than arbitrary
Lua values passed through an unconstrained dispatcher.

The sample's wall-clock sleeping, repeated scan, and run-until-empty loop do not
fit Ludus's tick/browser contract. Coroutines are deferred convenience for transient
sequences. Durable sequences use declared fields and named continuations. A native
reference borrowed before a yield cannot be held afterward. The chapter does not
provide a serializable VM stack or a safe arbitrary-code reload mechanism.

## Managing high level script execution within multithreaded environments

Schertenleib distinguishes cooperative microthreads from heavyweight OS threads,
describes a controllable task manager, compares execution modes, and treats an
authoring console showing task status as necessary tooling. The cooperative mode
places script progress under the native update cadence. The chapter also discusses
the limitations of preprocessing custom yield/return keywords and of interruption.

Ludus retains one owner-thread Luau VM and an explicit task roster, pause state, and
native-owned cancellation. Every task has a bounded lifecycle and one terminal
outcome. Authoring tools show active state, last event, pending request, and reason
for suspension. Complex native work becomes an owned asynchronous request rather
than a script holding the world while it waits.

The Python generator, global lists, exception propagation, and concurrent update
mode are not adopted. Running the interpreter on a worker would not make world
access safe. A future parallel evaluator needs copied inputs, isolated output,
stable merge order, and equivalent serial results. The historical crowd benchmark
does not establish Ludus capacity. Custom textual keywords would add another
language compatibility layer; structured visual lowering supplies only the small
domain transformation the editor actually needs.

## Exposing actor properties using nonintrusive proxies

Campbell and Murphy separate existing actors from a projection that provides
property metadata and tool-facing access. Their main contribution is that generic
tools can work with legacy or specialized native objects without imposing one
reflective base class on every object. Labels and exposed concepts need not match
the underlying storage exactly.

Ludus already has a narrower generated scalar projection into GameApi. Extend
that boundary when a concrete scripting/tool consumer needs it. Keep units, bounds,
and editability explicit. Read projections, authored persistence, inspector edits,
and script capabilities have different purposes even when they share a schema.

Printed page 387 recommends exception-based getters/setters, and page 390 includes
a proxy setter that loads a texture resource. Those policies conflict with Ludus's
explicit-error and prepared-publication contracts. A scripting integration must
prepare values/resources privately and publish through the owner. It does not need
heap-allocated virtual objects for every property or an immediate setter for every
native field. The useful proxy idea is retained without copying that machinery.

## Automatic Lua binding system

Hamaide presents binding automation with performance, memory, ease of use, and
thread-safety goals. It consolidates binding metadata, explicitly discusses object
lifetime, offers renamed methods as an alternative to overload matching, and
provides debug helpers. Its final sections discuss sandbox access levels and the
code-size cost of generating a function for every method.

Ludus adopts a selected manifest and generated ordinary adapters. One manifest
produces selected providers' typed definitions, node catalog metadata, validation
rules, and private native call adapters. Explicit names eliminate expensive/ambiguous overload
search. Shared immutable tables and cached resolution reduce repeated setup.
Generated code remains visible to the debugger and is measured for code size
and public-header impact.

The sample's automatic registration uses global static constructors. Ludus instead
installs tables explicitly under a leased module generation. Its raw pointers,
GC/reference-counted native ownership, and optional removal of runtime checks are
also rejected. The proposed assembly-assisted generic dispatcher is unnecessary
and unsuitable for the native/browser portability goal. Access levels become
explicit capabilities checked in shipping, not trust inferred from an exposed
class name. Automation reduces repeated boilerplate; it does not authorize
exposing every native class or method.

## Current primary sources and adoption limits

Modern runtime details were checked separately from the historical chapters:

- [Luau types](https://luau.org/types/) documents gradual checking and strict mode.
  Native boundaries still validate runtime values.
- [Luau performance](https://luau.org/performance/) describes the portable interpreter,
  optional native code generation, and VM-supported debugging. Ludus performance
  claims still require its own workloads.
- [Luau C API](https://luau.org/api/) supplies allocator, error, and debugging
  mechanisms. Having those mechanisms does not deliver a Ludus debugger adapter.
- [Luau sandboxing](https://luau.org/sandbox/) identifies trusted compiler bytecode
  and limits of interrupt-based CPU control. Restrict native calls and isolate
  trust domains rather than assuming every embedded API is safe.
- [Luau source configuration](https://github.com/luau-lang/luau/blob/1eca9fda3e4753a1592000f6cfdf659aaa778b7d/VM/include/luaconf.h)
  and [error implementation](https://github.com/luau-lang/luau/blob/1eca9fda3e4753a1592000f6cfdf659aaa778b7d/VM/src/ldo.cpp)
  expose the longjmp build path. The [C++ nonlocal jump rules](https://eel.is/c++draft/csetjmp.syn)
  make destructor/lifetime auditing a mandatory adoption gate.
- Microsoft's [Write a custom .NET host to control the .NET runtime from your native code](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting)
  documents process hosting and deployment constraints. It informs the optional
  provider boundary, not a completed mobile/browser integration.
- Microsoft's [How to use and debug assembly unloadability in .NET](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)
  describes cooperative context retirement and surviving roots. Ludus must
  retire delegates, tasks, and native-generation leases before code replacement.
- Microsoft's [Native AOT deployment](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/)
  and [Building native libraries](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/libraries)
  identify dynamic-loading and library-unloading limits. An AOT profile cannot
  inherit the native GameHost module replacement contract.

The resulting design shares engine contracts while keeping runtime lifetimes
separate. Initial execution paths are native C++ and Luau, with optional C# gated
independently. Operation-sized native boundaries, declared persistent state,
and focused visual authoring remain central.
Implementations must add the contributor-guide reference comments near code
materially informed by these chapters. The runtime spike remains required before
selecting a dependency; document checks do not prove ABI, browser, OOM, or debugger
correctness.
