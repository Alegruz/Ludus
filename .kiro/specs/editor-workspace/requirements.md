# Ludus Editor first milestone requirements

Status: proposed; implementation and acceptance remain outstanding.
This is the first project workspace milestone E0, not a scene-authoring editor.
Read [design](design.md), [tasks](tasks.md), and the committed
[research](../../../docs/architecture/editor-workspace-research.md).

## User outcome

A developer opens a project, changes its native build and launch settings,
saves them, restarts Editor, and builds/runs the same project. They can inspect
errors, repair a build failure, retry, stop a running application, and close
Editor without abandoning a supported child workload. The game has its own
window, keeping Editor responsive when gameplay pauses or crashes.

## Behavioral requirements

| ID | Requirement and acceptance |
| --- | --- |
| E01 | Editor lives in apps/editor with an optional OFF-by-default build. With Editor OFF, configure/build/install and browser targets do not find/link/export Qt or Editor components. |
| E02 | The reference Linux C++23 application uses Qt 6.4-compatible Widgets, explicit errors, Ludus aliases, warnings-as-errors and no engine/application exceptions. A dependency spike demonstrates this before feature implementation. |
| E03 | Open parses a bounded version-1 descriptor and resolves paths relative to its location/source, with no command execution. Invalid/missing files leave the previous workspace unchanged and produce useful errors. |
| E04 | Name, provider/source, native preset, executable target, cwd and an exact argument list can be inspected/edited. Missing/nonintegral version, wrong types, unsupported values, NUL/invalid Unicode, unknown fields and documented size-limit violations fail validation. |
| E05 | Saved and draft state are distinct. Successful Save survives restart. A failed/short write, commit failure or disk conflict retains the old saved file and dirty draft. Dirty Open/Reload/Close offer Save/Discard/Cancel; failed Save aborts that action. |
| E06 | Configure/Refresh Targets is explicit. It uses the appropriate provider, managed tools and an owned File API query; lists only executable targets; and never guesses executable output paths. An invalid stored target remains visible until corrected. |
| E07 | Build and BuildRun snapshot a clean saved descriptor, verify its digest, configure, validate the selected executable and build only that target. BuildRun launches only after success and post-build artifact validation. Failed/cancelled builds cannot launch an old binary. |
| E08 | Native Run preserves argument boundaries, including empty strings, Unicode, whitespace, quotes and shell-looking text. It uses the resolved absolute artifact, explicit cwd and relevant inherited session environment. No shell, RAD-specific argument restrictions or detached launch. |
| E09 | Exactly one operation is owned by a workspace. Legal transitions/capabilities are centralized; duplicate starts, reentrant widget updates and stale callbacks cannot create another job or corrupt settings. Stop accepted before spawn prevents launch. |
| E10 | Configure/build/run do not block the GUI. Stop is idempotent, uses deadlines, stops ordinary descendants, drains streams and confirms cleanup. Normal Close waits asynchronously. Parent-pipe loss stops the workload; abrupt supervisor loss becomes CleanupUnknown with operator recovery details. |
| E11 | Output and protocol storage/work are bounded even for a producer with no newlines, invalid UTF-8, simultaneous stdout/stderr or an absent/slow reader. Truncation is visible. Logs never overwrite last-result information or starve cancellation. |
| E12 | Every operation provides stage, exact argv/cwd, descriptor digest, job ID, exit/signal detail, cleanup status and omission counts through Copy Job Details. No environment dump, automatic upload, telemetry or deterministic-game-replay claim. |
| E13 | Engine-checkout and external installed-SDK sample projects use the same descriptor/launch semantics. External projects follow the documented two-preset, single-config Ninja build-tree convention, and have no engine-source/private-header coupling. |
| E14 | A missing tool, stale bootstrap, configure/build error, malformed File API reply, failed spawn, nonzero runtime exit, runtime signal, protocol error or supervisor loss is distinguishable and actionable. There is no automatic retry/installation or claim that Running proves a rendered frame. |
| E15 | Real regression fixtures, native GUI acceptance and pinned build/test/format/tidy/sanitizer/header/budget/SDK gates verify the delivered implementation. Unavailable/skipped checks remain incomplete. This documentation package does not itself pass future implementation gates. |

## Completion demonstration

1. Explicitly prepare the reference toolchain and Qt, build Editor, and launch
   with the trusted tooling checkout/interpreter.
2. Open the committed engine sample descriptor. Opening causes no configure,
   build, download or runtime launch.
3. Choose Development and a native executable; edit argument/cwd settings,
   save, close and reopen. All settings match the saved document.
4. Configure to discover targets, then Build and Run. A separate native runtime
   window presents a visible frame. Editor remains usable and logs/status update.
5. Close the runtime window normally; Editor reports its exit and can run again.
6. In a disposable sample copy, introduce a compiler error with an old successful
   binary still present. BuildRun shows the error and starts no runtime. Repair
   the error, retry, and see the new runtime launch.
7. Stop during configure/build and during runtime. Verify no late launch and
   no remaining ordinary descendants. Repeat Stop and a rapid subsequent job.
8. Close while running, choose Stop and Close, and verify cleanup. Cancel a close
   request and confirm Editor stays open. Kill a test GUI owner and verify EOF
   cleanup by its healthy supervisor.
9. Produce noisy/no-newline output and a signal/nonzero exit using process
   fixtures. Observe bounded memory, responsive Stop and useful result details.
10. Open the external SDK sample with an explicitly installed matching SDK;
    configure/build/run successfully without compiling engine sources as part
    of that project's build.

## First acceptance baseline

Linux x64, local project files, Clang/LLVM 18, pinned CMake/Ninja/Python tooling,
Qt 6.4-compatible system packages, native Debug/Development settings, a real
Wayland desktop and usable runtime graphics device. Offscreen Qt logic/widget
checks run without GPU/compositor. External SDK preparation is explicit.

This first implementation does not promise containment of daemonizing projects,
recovery from supervisor SIGKILL or machine loss, power-loss durability on every
filesystem, or coordination with independent CLI builds. Those limits must be
visible in the implementation evidence and usage documentation.
