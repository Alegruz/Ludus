// Exercise browser download bytes/filename/lifetime without external files.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {test} = require('node:test');
const source = fs.readFileSync(require('node:path').join(__dirname, '../browser/files.js'), 'utf8');

for (const [filename, type] of [['project.json', 'application/json'], ['project.tar', 'application/x-tar']]) {
test(`download owns its bytes, supplies ${filename} and releases the URL`, async () => {
  let library, link, blob, clicked = false, removed = false, revoked;
  const heap = new Uint8Array([0, 123, 125, 0]);
  const timers = [];
  vm.runInNewContext(source, {
    addToLibrary: value => { library = value; },
    UTF8ToString: value => value,
    HEAPU8: heap,
    Blob,
    URL: {
      createObjectURL: value => { blob = value; return 'blob:test'; },
      revokeObjectURL: value => { revoked = value; },
    },
    setTimeout: (callback, delay) => timers.push({callback, delay}),
    document: {
      body: {appendChild: value => assert.equal(value, link)},
      createElement: name => {
        assert.equal(name, 'a');
        link = {click: () => { clicked = true; }, remove: () => { removed = true; }};
        return link;
      },
    },
  });
  library.LudusBrowserDownloadDocument(filename, 1, 2);
  heap.fill(0); // The Wasm allocation may be released or reused after the call.
  assert.equal(link.download, filename);
  assert.equal(link.href, 'blob:test');
  assert.equal(blob.type, type);
  assert.equal(await blob.text(), '{}');
  assert.ok(clicked && removed);
  assert.equal(timers[0].delay, 60000);
  assert.equal(revoked, undefined);
  timers[0].callback();
  assert.equal(revoked, 'blob:test');
});
}
