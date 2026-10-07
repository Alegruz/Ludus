// Exercise wasm longjmp in Chromium as well as the SDK's Node runtime.
import { chromium } from '../web-browser-tests/node_modules/playwright/index.mjs';
import { createServer } from 'node:http';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import assert from 'node:assert/strict';

const root = resolve(process.argv[2]);
const server = createServer(async (request, response) => {
  const name = new URL(request.url, 'http://localhost').pathname.slice(1) || 'index.html';
  if (!/^[\w.-]+$/.test(name)) {
    response.writeHead(404).end();
    return;
  }
  try {
    const data = await readFile(join(root, name));
    response.writeHead(200, { 'Content-Type': name.endsWith('.wasm') ? 'application/wasm' :
      name.endsWith('.js') ? 'application/javascript' : 'text/html' });
    response.end(data);
  } catch {
    response.writeHead(404).end();
  }
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
let browser;
const logs = [];
const errors = [];
try {
  browser = await chromium.launch({ headless: true });
  const page = await browser.newPage();
  page.on('console', message => logs.push(message.text()));
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
  await page.waitForFunction(() => document.body.dataset.ready === 'true' || document.body.dataset.abort,
    null, { timeout: 120000 });
  assert(logs.some(line => line.includes('S2 ACCEPTANCE PASS')), logs.join('\n'));
  const result = await page.evaluate(async () => {
    const assert = (value, message) => { if (!value) throw new Error(message); };
    assert(Module.ccall('S2Start', 'number', [], []) === 1, 'start');
    let identity = {version: 1, session: 'f123456789abcdef', execution: 'a123456789abcdef',
                    revision: 1, stop: '0000000000000000'};
    const control = (action, fields = {}, accept = true) => {
      const text = Module.ccall('S2Control', 'string', ['string'], [JSON.stringify({...identity, action, ...fields})]);
      if (!accept) { assert(!text, 'rejection'); return; }
      assert(text, action);
      const result = JSON.parse(text);
      identity = {...identity, execution: result.execution, revision: result.revision, stop: result.stop};
      return result;
    };
    const base = control('inspect');
    control('breakpoint', {asset: '0000000000000100', line: 8, enabled: true});
    assert(Module.ccall('S2Advance', 'number', [], []) === 8, 'pause');
    identity.stop = '0000000000000001';
    const paused = control('inspect');
    let heartbeats = 0;
    const timer = setInterval(() => { ++heartbeats; }, 1);
    await new Promise(done => setTimeout(done, 80));
    clearInterval(timer);
    assert(heartbeats > 5 && control('inspect').tick === paused.tick, 'responsive partial tick');
    control('reload', {package: 'replacement', expected: base.package}, false);
    control('into');
    const locals = control('inspect');
    assert(locals.frames.length > 1 && locals.locals.some(value => value.name === 'amount' && value.number === 1), 'helper locals');
    control('out');
    const done = control('continue');
    assert(done.states[0].interactions === 1 && !done.partial, 'publish');
    control('reload', {package: 'failed', expected: base.package}, false);
    assert(control('inspect').revision === 1, 'failed candidate retention');
    const replaced = control('reload', {package: 'replacement', expected: base.package});
    assert(replaced.revision === 2 && replaced.states[0].interactions === 1, 'migration');
    assert(Module.ccall('S2Advance', 'number', [], []) === 0, 'new dependency');
    const current = control('inspect');
    assert(current.states[0].interactions === 3, 'changed behavior');
    return {heartbeats, paused, current};
  });
  assert.equal(errors.length, 0, errors.join('\n'));
  await mkdir('out/browser-qa/results', { recursive: true });
  await writeFile('out/browser-qa/results/luau-s2.json', JSON.stringify({ browser: browser.version(), result, logs, errors }, null, 2));
  console.log(`S2 Chromium ${browser.version()}: debugger heartbeat and transactional replacement passed`);
} finally {
  if (browser) await browser.close();
  server.close();
}
