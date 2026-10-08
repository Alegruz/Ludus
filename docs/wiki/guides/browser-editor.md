---
description: Open the existing Ludus editor workspace in your browser.
---

# Browser editor

[Launch Ludus Editor in your browser →](../editor/index.html)

The browser preview runs the existing Qt Widgets application as WebAssembly.
It opens a writable Cornell Box sample with its sources and shaders so you can
explore a complete project immediately. A desktop browser with WebAssembly and WebGL
is required. The first download includes Qt and the editor; it may take longer
than opening a guide.

## Supported workflows

- Edit the example project's name, provider, source directory, preset, target,
  working directory, and arguments.
- Use **File → Open Project** and select the folder containing
  `ludus.project.json`. The browser copies the folder's files, including source,
  shaders and assets, into its session filesystem, preserving relative paths.
- Use **File → Open Sample** for writable copies of Cornell Box, Live Edit Game,
  Scripted Game or the installed SDK example. Each copy has its own folder;
  sample originals remain available for starting again.
- Use **Save and Download Project Folder** or **Download Project Folder** to
  export a `.tar` archive. The latter saves open script and scene edits before
  exporting. Extract the archive into a folder on your computer, then reopen
  that folder to continue. Save configuration preferences separately using their
  download action. Build outputs and `.ludus` machine settings are excluded.
- Open the **Configuration** tab, load a cooked project bundle or preference
  document, inspect inherited values, edit/reset preferences, and download sparse
  preference overrides.
- Arrange the existing panels and inspect output and workflow status.

Project imports admit at most 4096 files and 32 MiB per session; individual
configuration documents remain limited to 1 MiB. Generated `out`, `build`,
`.git`, `.ludus` and `node_modules` trees are omitted during project import and
export. Project exports are limited to
4096 regular files and 32 MiB. Symbolic links and paths exceeding ustar limits
cannot be exported. Select a source folder without generated build outputs. Invalid
imports preserve the current document. Document formats and validation are the
same as in the desktop application.

## Session storage

Files and edits live in the current browser session. Reloading or closing the
page discards them; browser history and the desktop filesystem do not recover
those edits. Download your changes before leaving. After an edit, the page asks
for confirmation before reload/close. The browser manages download destinations,
so the application cannot confirm whether you accepted or cancelled a download.

## Desktop actions

Local project creation/setup, CMake builds, native Run/Play, code reload, RAD
debugging, release packaging, and audio import/preview require the desktop
editor in this increment. Their tool adapters cannot start native processes in
a browser. The browser preview does not include a remote build service.

The same Qt shell can serve a future macOS editor. Native macOS support still
needs editor setup/launcher/build validation; a game viewport also requires a
working graphics backend. See the [desktop editor guide](../../development/editor-workspace.md).
