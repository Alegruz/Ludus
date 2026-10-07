---
description: Open the existing Ludus editor workspace in your browser.
---

# Browser editor

[Launch Ludus Editor in your browser →](../editor/index.html)

The browser preview runs the existing Qt Widgets application as WebAssembly.
It opens a bundled example project descriptor so you can explore the workspace
and edit its settings immediately. A desktop browser with WebAssembly and WebGL
is required. The first download includes Qt and the editor; it may take longer
than opening a guide.

## Supported workflows

- Edit the example project's name, provider, source directory, preset, target,
  working directory, and arguments.
- Open an existing project descriptor using **File → Open Project**. The browser
  uploads only that JSON document; it does not grant access to its project folder.
- Use **Save and Download Project** or **Download Project Descriptor** to export
  edits, then place the downloaded document back in your desktop project.
- Open the **Configuration** tab, load a cooked project bundle or preference
  document, inspect inherited values, edit/reset preferences, and download sparse
  preference overrides.
- Arrange the existing panels and inspect output and workflow status.

Imports are limited to 1 MiB per JSON document and 32 MiB per session. Invalid
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
