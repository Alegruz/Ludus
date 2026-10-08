// Thanks to Emscripten contributors, "Interacting with code", Implement a C API
// in JavaScript: export copied bytes through a linked library and explicit names.
// https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html
addToLibrary({
  LudusBrowserPlaySample__deps: ['$UTF8ToString'],
  LudusBrowserPlaySample: function(player) {
    globalThis.ludusPlaySample(UTF8ToString(player));
  },
  LudusBrowserDownloadDocument__deps: ['$UTF8ToString'],
  LudusBrowserDownloadDocument: function(name, bytes, size) {
    const filename = UTF8ToString(name);
    const type = filename.endsWith('.tar') ? 'application/x-tar' : 'application/json';
    const url = URL.createObjectURL(new Blob([HEAPU8.slice(bytes, bytes + size)], {type}));
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  },
  LudusBrowserMarkEdited: function() {
    // A download request cannot confirm that the user saved the file.
    globalThis.ludusEditorEdited = true;
  },
});
