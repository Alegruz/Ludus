// Execute the same production provider in Chromium and bind it to Node's artifacts.
import { chromium } from '../web-browser-tests/node_modules/playwright/index.mjs';
import { createServer } from 'node:http';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve, join } from 'node:path';
import assert from 'node:assert/strict';

const root = resolve(process.argv[2]);
const evidence = JSON.parse(await readFile(join(root, 'safety-evidence.json'), 'utf8'));
assert.equal(evidence.version, 1);
assert.equal(evidence.execution, 'node-wasm');
assert.equal(evidence.system, 'Emscripten');
assert.equal(evidence.architecture, 'wasm32');
assert.equal(evidence.pointer_bits, 32);
assert.equal(evidence.cases.length, 12);
for (const name of ['ludus_behavior_safety.js', 'ludus_behavior_safety.wasm']) {
  const digest = createHash('sha256').update(await readFile(join(root, name))).digest('hex');
  assert.equal(digest, evidence.artifacts[name], `changed qualified artifact: ${name}`);
}
const server = createServer(async (request, response) => {
  const name = new URL(request.url, 'http://localhost').pathname.slice(1) || 'index.html';
  if (!/^[\w.-]+$/.test(name)) { response.writeHead(404).end(); return; }
  try {
    const data = await readFile(join(root, name));
    response.writeHead(200, { 'Content-Type': name.endsWith('.wasm') ? 'application/wasm' :
      name.endsWith('.js') ? 'application/javascript' : 'text/html' });
    response.end(data);
  } catch { response.writeHead(404).end(); }
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
let browser;
const logs = [], errors = [];
let result = {};
let passed = false;
try {
  browser = await chromium.launch({ headless: true });
  const page = await browser.newPage();
  page.on('console', message => logs.push(message.text()));
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
  await page.waitForFunction(() => document.body.dataset.exitCode !== undefined ||
    document.body.dataset.abort !== undefined, null, { timeout: 90000 });
  result = await page.evaluate(() => ({ ...document.body.dataset }));
  assert.equal(result.exitCode, '0', JSON.stringify({ result, logs, errors }));
  assert.equal(result.abort, undefined);
  assert.equal(errors.length, 0, errors.join('\n'));
  for (const name of evidence.cases) {
    assert(logs.some(line => line.includes(`S6 PASS ${name}`)), name);
  }
  assert(!logs.some(line => line.includes('S6 FAIL')));
  assert(logs.some(line => line.includes('S6 TARGET os=Emscripten arch=wasm32 pointer_bits=32')));
  for (const [name, count] of Object.entries(evidence.allocation_points)) {
    assert(count > 0);
    assert(logs.some(line => line.includes(`S6 METRIC ${name}-allocation-points=${count}`)), name);
  }
  passed = true;
  console.log(`S6 Chromium ${browser.version()}: production provider safety passed`);
} finally {
  await mkdir('out/browser-qa/results', { recursive: true });
  await writeFile('out/browser-qa/results/behavior-s6.json', JSON.stringify({
    browser: browser?.version(), passed, result, logs, errors,
    qualification: 'headless Chromium; physical devices and other browsers pending',
    target: { system: evidence.system, architecture: evidence.architecture, pointer_bits: evidence.pointer_bits },
    artifacts: evidence.artifacts, allocation_points: evidence.allocation_points,
  }, null, 2));
  if (browser) await browser.close();
  server.close();
}
