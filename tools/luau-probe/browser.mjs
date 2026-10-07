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
const browser = await chromium.launch({ headless: true });
const logs = [];
const errors = [];
try {
  const page = await browser.newPage();
  page.on('console', message => logs.push(message.text()));
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
  await page.waitForFunction(() => document.body.dataset.exitCode !== undefined ||
    document.body.dataset.abort !== undefined, { timeout: 120000 });
  const result = await page.evaluate(() => ({ ...document.body.dataset }));
  assert.equal(result.exitCode, '0', JSON.stringify({ result, logs, errors }));
  assert.equal(result.abort, undefined);
  assert.equal(errors.length, 0, errors.join('\n'));
  for (const name of ['protected-script-error', 'protected-binding-error', 'protected-stack-overflow',
    'bounded-loop-interrupt', 'constant-loader-oom-sweep', 'execution-oom-sweep',
    'break-step-locals-node-resume', 'dispatch-batch', 'fresh-vm-recovery']) {
    assert(logs.some(line => line.includes(`S0 PASS ${name}`)), name);
  }
  await mkdir('out/browser-qa/results', { recursive: true });
  await writeFile('out/browser-qa/results/luau-s0.json', JSON.stringify({ browser: browser.version(), result, logs, errors }, null, 2));
  console.log(`S0 Chromium ${browser.version()}: all interpreter probes passed`);
} finally {
  await browser.close();
  server.close();
}
