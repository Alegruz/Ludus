# Editor workspace

Ludus keeps the central area for the current task. **Welcome** holds New/Open and
recent projects. Opening a project reveals **Project settings** and **Audio**;
recents remain in the File menu without occupying an authoring column.

## Work areas and panels

| Surface | Purpose |
| --- | --- |
| Project settings | Project name, provider, source directory, preset, target and run arguments |
| Audio | Content selection, audio definitions, save and preview |
| Configuration | Independent offline host configuration preview and preference editing |
| Live Inspector | Runtime state and supported live property operations |
| Output | Selectable build/tool/runtime diagnostics |

Use **View** to recover hidden panels and **Reset Layout** to restore the default
arrangement. Layout/history preferences are local to the user and independent of
project files.

## Project lifecycle

Opening reads project metadata and checks eligible SDK setup without building,
downloading or rewriting files. Build, Configure, Repair and Run are explicit
actions. A failed open preserves the current project.

**Close Project** returns to Welcome after draft confirmation. It keeps recent
history and does not quit the editor. Close is unavailable while owned operations
or runtime cleanup prevent switching projects; stop work first.

The Configuration workspace owns a separate, explicitly loaded offline preview.
It stays accessible and retains its draft across project changes. Quitting the
editor prompts for unsaved preferences.

## Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| Ctrl+N | New Project |
| Ctrl+O | Open Project |
| Ctrl+S | Save Project Settings |
| Ctrl+Shift+W | Close Project |
| Ctrl+Shift+B | Build |
| Ctrl+F5 | Build and Run |
| F5 | Build and Debug in RAD |
| F6 | Build and Play |
| Shift+F6 | Stop |
| F1 | Keyboard Shortcuts help |

F1 derives its list from the same action objects used by menus and the toolbar.
Disabled actions stay disabled through shortcuts. Ctrl+S currently names project
settings specifically; focused document save and undo routing are S2 work.

## Save intention explicitly

Project settings, audio documents, offline configuration preferences and live
runtime values have separate owners. **Apply to session** changes supported live
values. **Copy live value to tuning draft** requires a separate save to persist
it. A runtime heartbeat is not an instruction to save a document.

See [build and debug](build-and-debug.md), [configuration](configuration.md),
[audio](audio.md), and the
[full editor usage guide](https://github.com/Alegruz/Ludus/blob/main/docs/development/editor-workspace.md).
