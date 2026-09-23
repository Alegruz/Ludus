# Critical review: Squeezing More Out of Assert

Reviewed September 23, 2026, against the proposed [Ludus assertion design](assertions.md).

Source: Steve Rabin, “Squeezing More Out of Assert,” *Game Programming Gems*,
section 1.12, printed pages 109-114. In the repository's scanned
[GPG1 PDF](../../references/Game%20Programming%20Gems%201.pdf), these are PDF pages
107-112 (one-based). All six pages, including the final code and references,
were inspected visually; the PDF has no usable embedded text. The chapter cites
McConnell, Robbins, and Saltzman; this review assesses Rabin's printed chapter,
not those other works or the historical companion source archive.

## Verdict

This is a useful debugging-workflow article and an inadequate production
assertion specification. Its lasting contribution is that an assertion should
help a developer understand and reproduce a defect, including when a tester has
no debugger. That remains an excellent requirement. Its dialog-centered recovery
model, mutable ignore switches, and code samples are not suitable foundations
for a concurrent, cross-platform, no-exception C++23 engine.

That judgment is not “old code is bad.” The article deliberately offers seven
small tricks, not a complete engine subsystem. It deserves credit for identifying
practical debugging friction. The mistake would be promoting those sketches into
production contracts without addressing evaluation, threading, termination,
allocation failure, portability, and build cost.

## Assessment of the seven proposals

| Proposal | Assessment | Ludus decision |
| --- | --- | --- |
| #1: Add human-readable context to the condition, pp. 110-111 | The diagnostic goal is right. Attaching text through boolean operators mixes a message with program logic and adds no structured value capture | Keep context; use the separate message parameter and explicit typed values |
| #2: Explain an unconditional assertion, p. 111 | Better than an unexplained constant-false assertion. Negating a string is obscure, and the assertion still disappears in builds where assertions are disabled | Use always-enabled `LUDUS_FATAL` when continuation is invalid |
| #3: Wrap the message idiom, p. 111 | Better call-site ergonomics, but the shown wrapper remains tied to the standard assert and does not provide a robust macro contract | Keep a small macro front end; preserve expression text separately; use safe statement/expression forms |
| #4: Customize handling and break at the call site, pp. 111-112 | Owning behavior and improving debugger context are worthwhile. The shown macro calls the handler on passing conditions and couples failure policy to a dialog | Keep centralized cold handling and platform intrinsics; make caller-frame usability an acceptance test |
| #5: Remember an ignore choice, pp. 112-113 | Solves disruptive repeated dialogs, but suppresses future condition evaluation and allows continuation through a broken invariant | Reject for fatal assertions; throttle only recoverable Check report presentation |
| #6: Include a stack, p. 113 | Strong recommendation: a shared helper's source line often does not identify its bad caller | Capture a validated native crash context/raw PCs, symbolize outside the failing thread, and state when unavailable |
| #7: Make reports easy to copy, pp. 113-114 | Probably the highest-value workflow improvement per unit of complexity | Require a bounded transferable text report; leave clipboard/UI to a healthy consumer |

## Concrete technical problems

### 1. The API conflates observation with permission to continue

A human clicking Continue does not reestablish an invariant. If a destination
pointer is invalid or a resource lifetime is broken, execution after the dialog
can damage unrelated state and make subsequent evidence misleading. The chapter
does not establish a recovery precondition before permitting continuation.

There are legitimate exploratory debugging sessions where someone accepts that
risk, and benign diagnostics that should not terminate a game. Those do not
justify giving every invariant a universal ignore option. Ludus separates fatal
invariants from `CHECK`: the latter's caller must supply a valid recovery path.
Presentation policy cannot change that control-flow contract.

The per-site static boolean is also ordinary shared mutable state. Concurrent
reads and writes without synchronization can race; thread-safe initialization
of a local static does not make later accesses safe. Replacing it with an atomic
would address that race, but still adds a load/branch to passing sites, persistent
per-site state, and the wrong semantic permission. The right fix is not simply
to modernize the flag's type.

Our own 64-report Check budget has a related observability weakness: early noisy
sites can hide distinct later reports. The design already admits that limitation.
This review adds a suppression notice and explicit tests that conditions and
recovery still execute. It does not pretend a process budget is perfect, nor
introduce an unrequested per-site registry to solve it.

### 2. The custom macro is not a cheap failure-only path

On p. 112, the condition is passed to a handler function on every enabled call.
The condition is evaluated once, which is good, but the success path still crosses
the reporting interface unless optimization can remove it. A computed description
is also evaluated on success. Inline assembly at the breakpoint does not repair
either issue.

Ludus's branch must occur before the handler and diagnostic argument expressions.
This is a useful reason to retain a macro even in C++23; a function taking an
already-evaluated boolean and message cannot provide that contract by itself.

The printed integer cast is another unnecessary defect. Converting `0.5` through
an integer produces false, while converting it directly to bool produces true.
Pointer-to-integer conversion is also an inappropriate portability requirement.
Use direct boolean conversion and encourage explicit relational conditions.
An assertion implementation should not quietly change its condition's truth value.

### 3. The sample macros need basic correctness work before reuse

The wrapper on p. 111 does not parenthesize its substituted operands separately.
The custom forms on pp. 112-113 do not use the usual single-statement do/while
wrapper: a normal invocation followed by a semicolon in an unbraced if/else can
break parsing. The p. 111 examples also use a different capitalization from the
new macro's definition. These are practical copy/paste hazards, not the main
architectural objection, but they rule out treating the samples as ready code.

The interface uses mutable `char*` for literal descriptions/file names, which is
not an acceptable modern C++ string-literal contract. Its x86/MSVC-style inline
assembly and `_DEBUG` switch are platform/build assumptions, not engine-wide
abstractions. None of these requires a framework to fix: const-correct borrowed
text, guarded private intrinsics, explicit build policy, and a few macro tests
are enough.

### 4. Release safety is asserted more strongly than it is justified

The normalization discussion on pp. 109-110 assumes release checking is too
expensive and hopes callers enforce preconditions. Neither is an adequate design
argument without workload measurements and an enforceable API contract.

A trusted inner-loop normalization operation can reasonably have a documented
nonzero/finite-input precondition and development assertions. A fallible boundary
operation should return failure for unsuitable data. A shipping-critical internal
invariant may warrant an always-enabled guard. The correct choice depends on
ownership, validation, and consequences, not just the function being low-level.

The example's nonzero-length check is also not a complete numerical validity
contract: a NaN is not equal to zero, and nonzero length alone does not handle
overflow/underflow or guarantee finite output. This is an illustration, not a
robust normalization algorithm. Do not enlarge the assertion subsystem to fix
math semantics; define and test those semantics in the math API.

The warning against state changes inside assertions is sound. The accompanying
blanket advice against function calls is too coarse: pure bounded queries are
perfectly legitimate invariant expressions. The relevant distinction is required
effects and cost, not whether parentheses denote a call.

### 5. Stack capture must not make the original failure harder to diagnose

The shared-helper/caller problem on p. 113 is real. Our design should not dismiss
caller context merely because it can print a file and line. This review therefore
adds explicit debugger caller-frame and no-debugger capture/handoff tests.

However, getting a stack and displaying symbols inside a failure dialog are
separate operations. The chapter does not specify allocator, loader-lock,
reentrancy, thread-safety, or damaged-stack constraints for capture. Ludus must
preserve the original report first and use an audited backend or native dump.
No safe stack is better than deadlocking while trying to manufacture one. Missing
stack data must be visible, not silently presented as a complete report.

### 6. The clipboard goal is excellent; the implementation is not failure-safe

The pp. 113-114 sample puts allocation and clipboard operations inside assertion
handling. That is an unsuitable dependency direction for Base even if every
Windows API call were correct. It also does not check `GlobalLock` before copying,
or handle clipboard publication failure and retained allocation ownership.
`GlobalLock` can return null, and clipboard ownership transfers only on successful
publication. See Microsoft's [GlobalLock contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globallock)
and [SetClipboardData contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclipboarddata).

More concretely, the sample opens the clipboard with a null window and then
empties it. Current Microsoft documentation explicitly identifies that sequence
as leaving a null owner and causing `SetClipboardData` to fail. This is a present-
day porting defect, not a claim about behavior on every historical Windows
version. See [OpenClipboard](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard).

The placeholder text fits its local buffer, so it would be dishonest to claim
that the printed constant itself overflows. But replacing it with real assertion
data through unbounded `sprintf` would not establish a safe capacity contract.
Moving copy/save into a healthy report viewer preserves Rabin's useful workflow
without requiring the failing engine to allocate or interact with desktop UI.

## Changes adopted in Ludus

The core architecture remains appropriate; the chapter does not justify replacing
it. The design now strengthens five concrete requirements:

1. Messages explain contracts and identify relevant state; expression text stays
   separate. Pure queries are allowed, required side effects are not.
2. Check suppression affects detail/breakpoints only, with a visible exhaustion
   notice. Every condition and recovery decision retains its original semantics.
3. A central breakpoint is acceptable only with usable original-site/caller
   context verified in supported debuggers. We softened the previous dismissal
   of a call-site breakpoint's value into a measurable tradeoff.
4. Reports form a bounded plain-text artifact containing build/flavor/policy,
   source/expression/context, and explicit capture status. UI/clipboard handling
   stays outside the failing runtime.
5. QA deployments must demonstrate that a no-debugger failure leaves retrievable
   evidence matched to the executable and symbols, including caller information
   when capture is configured. A disappearing stderr line is insufficient.

These add almost no call-site runtime or header cost. They mostly sharpen the
failure-record contract and operational tests. Actual collector/clipboard UI
remains outside this subsystem's implementation scope.

## Review validation and limits

A temporary Clang 18/C++23, no-exception probe verifies the integer-versus-bool
conversion example, the eager-handler success-path behavior, and continued Check
condition/recovery evaluation when presentation is suppressed. A compile-fail
probe verifies the unbraced if/else hazard of an analogous bare-block macro.
These are small independently written reproductions of language behavior, not
an attempted port of the chapter's Windows sample. Windows clipboard findings
are documentation-verified; no native Windows test was run. The engine remains
unimplemented: no claim of completed runtime, concurrency, or performance testing
is implied by this review.
