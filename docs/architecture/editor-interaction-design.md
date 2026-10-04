# Ludus editor interaction design

Status: S1 shell contract plus proposed interactions for subsequent stages.

## Default workspace

```text
File  Project  Build  Release  Play  Output  View
Build | Run | Debug | Build and Play | Pause | Step | Resume | Reload | Stop
+------------------+-------------------------------+--------------------+
| Recent Projects  | Project settings | Audio      | Live Inspector     |
| Open / browse    |                               | Session / revision |
|                  | Current authoring work        | Property/value     |
|                  |                               | Apply to session   |
|                  |                               | Copy to draft      |
+------------------+-------------------------------+--------------------+
| Output: bounded build, tool, and runtime diagnostics                  |
+----------------------------------------------------------------------+
| Project / draft / job status                                         |
```

The drawing describes roles, not pixel dimensions. Qt layouts use platform
fonts and logical pixels. Project settings scroll on smaller windows; Audio
uses the central width. Panels can move, float, and hide. View restores each
panel and the Game toolbar. Reset Layout returns the panes to their default
areas without discarding drafts, stopping Play, or changing project settings.
Use stable panel names and a versioned local preferences file. A preferences
write failure is logged and leaves document saves independent; invalid saved
layouts show a recovery message on startup.

## Consistency rules

- Name actions for their effect. “Build and Play,” “Copy live value to tuning
  draft,” and “Save Tuning Document” describe distinct operations.
- A menu and toolbar invocation share the same action/capability. Disabled
  controls must not start work; explain unavailable workflows in current status
  and existing setup diagnostics. No hidden second authority in a panel.
- Labels name the data, tooltips explain consequences, and status reports state.
  Use platform palettes/fonts and Qt focus indication. Never communicate errors,
  dirty state, or session scope through color alone.
- Keyboard focus follows visual order. Form labels have buddies/mnemonics;
  panels have accessible names. Ctrl+O opens and Ctrl+S currently saves the
  project descriptor. Keep document-specific save/undo actions explicit until
  S2 implements active-document shortcut routing with regression coverage.
- Reserve modal prompts for destructive decisions or required setup input.
  Progress, ordinary validation, and diagnostic output stay in the workspace.
  Preserve edit buffers, selection, and scroll position during background work.
- Mark scope visibly: session edits affect simulation; draft edits require save;
  imported artifacts can be replaced from source. Do not auto-save runtime state.

## Representative tasks and failure behavior

| Task | Expected interaction | Failure behavior |
| --- | --- | --- |
| Open recent project | Select and open, or double-click | Existing path/setup diagnostics; no automatic repair/download |
| Edit project | Project tab, change fields, Save | Validation leaves draft available; Build uses documented saved state |
| Tune Play | Start, pause, inspect, apply to session | Stale revisions rejected; session failure cannot overwrite source |
| Author audio | Audio tab, select/create, edit, save, preview | Explicit discard policy; preview stops before game launch |
| Recover workspace | View panel action or Reset Layout | Corrupt/incompatible preferences use defaults |
| Future asset import | Select source, see progress/result, cancel if needed | Retain last valid publication; retry with actionable error |
| Future scene transform | Select, drag preview, commit one operation, undo | Invalid target/revision cancels safely without losing other edits |

## Evaluation

S1 regression coverage verifies action identity, panel recovery, persisted
layout, corrupt preferences, and independence from project state. Capture the
real widget layout at desktop and smaller window sizes. Offscreen tests verify
structure and presentation, not native input, screen-reader behavior, Wayland,
or rendered game frames.

Before calling a stage production-ready, observe representative developers
perform its tasks without coaching. Record completion time, mistakes, recovery,
and subjective friction. Run keyboard-only, high-DPI, light/dark palette, and
native window-manager checks. Revisit the task flow before adding abstractions
or decorative UI. Measurement targets in the architecture require actual data.

The [post-design review](editor-design-review.md) adds a repeatable native task
script. Background updates must not reset focused fields or scroll/selection;
this is an S2 acceptance requirement, not an S1 implementation claim.
