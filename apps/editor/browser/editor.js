// Thanks to Qt Group, "Qt for WebAssembly", Using qtloader: load the existing
// Widgets application into a page-owned container, with explicit exit/failure UI.
// https://doc.qt.io/qt-6/wasm.html#using-qtloader
"use strict";

const loading = document.getElementById("loading");
const status = document.getElementById("status");
const retry = document.getElementById("retry");
const screen = document.getElementById("screen");
retry.addEventListener("click", () => location.reload());
window.addEventListener("beforeunload", event => {
  if (globalThis.ludusEditorEdited) {
    event.preventDefault();
    event.returnValue = "";
  }
});

function failed(message) {
  loading.hidden = false;
  status.textContent = message;
  retry.hidden = false;
  document.body.dataset.editorState = "failed";
}

async function start() {
  document.body.dataset.editorState = "loading";
  try {
    if (typeof qtLoad !== "function" || typeof ludus_editor_entry !== "function") {
      throw new Error("Editor assets are unavailable.");
    }
    if (typeof WebAssembly !== "object") {
      throw new Error("WebAssembly is unavailable.");
    }
    await qtLoad({
      qt: {
        entryFunction: ludus_editor_entry,
        containerElements: [screen],
        onLoaded: () => {
          loading.hidden = true;
          document.body.dataset.editorState = "ready";
        },
        onExit: () => failed("The editor closed. Reload to start another session. Download your changes before leaving."),
      },
    });
  } catch (error) {
    console.error("Ludus editor startup failed", error);
    failed("The editor could not start. Retry in a browser with WebAssembly and WebGL enabled, or use the desktop editor.");
  }
}
start();
