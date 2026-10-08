// Thanks to Emscripten contributors, "Interacting with code", Implement a C API
// in JavaScript: export copied bytes through a linked library and explicit names.
// https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html
addToLibrary({
  LudusBrowserDownloadDocument__deps: ['$UTF8ToString'],
  LudusBrowserDownloadDocument: function(name, bytes, size) {
    const url = URL.createObjectURL(new Blob([HEAPU8.slice(bytes, bytes + size)], {type: 'application/octet-stream'}));
    const link = document.createElement('a');
    link.href = url;
    link.download = UTF8ToString(name);
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
