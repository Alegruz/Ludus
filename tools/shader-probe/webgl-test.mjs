// Reuse the repository's integrity-locked Playwright tool; never ship this runner.
// Feasibility-only WebGL 2 acceptance for the generated GLSL ES 3.00 stages.
import {chromium} from '../web-browser-tests/node_modules/playwright/index.mjs';
import {createServer} from 'node:http';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve, sep} from 'node:path';
import assert from 'node:assert/strict';
const root = resolve(process.argv[2] || 'out/shader-probe-webgl');
const output = resolve(process.argv[3] || 'out/shader-probe-webgl-report');
await mkdir(output, {recursive: true});
// Default: ANGLE's software path (SwiftShader) so this runs without a GPU.
// Hardware WebGL 2 acceptance is a separate gate and is recorded as such.
const software = process.env.LUDUS_SHADER_PROBE_SOFTWARE !== '0';
const flags = software ? ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] : [];
const server = createServer(async (req, res) => {
  const pathname = new URL(req.url, 'http://localhost').pathname;
  const file = resolve(root, '.' + (pathname === '/' ? '/index.html' : pathname));
  if (!file.startsWith(root + sep)) {
    res.writeHead(403).end();
    return;
  }
  try {
    res.writeHead(200, {
      'Content-Type': file.endsWith('.js') ? 'text/javascript'
        : file.endsWith('.html') ? 'text/html'
        : file.endsWith('.json') ? 'application/json'
        : 'text/plain',
    }).end(await readFile(file));
  } catch {
    res.writeHead(404).end();
  }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const evidence = {
  kind: software ? 'real software GPU (ANGLE/SwiftShader); no hardware acceptance' : 'default browser GL adapter',
  flags,
  vertSha256: createHash('sha256').update(await readFile(resolve(root, 'probe.vert.essl'))).digest('hex'),
  fragSha256: createHash('sha256').update(await readFile(resolve(root, 'probe.frag.essl'))).digest('hex'),
  errors: [],
};
let browser;
try {
  browser = await chromium.launch({headless: true, args: flags});
  evidence.browserVersion = browser.version();
  assert.equal(evidence.browserVersion, '140.0.7339.186', 'Pinned Chromium required');
  const page = await browser.newPage();
  page.on('pageerror', e => evidence.errors.push(String(e)));
  page.on('console', m => {
    if (m.type() === 'error') evidence.errors.push('console: ' + m.text());
  });
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(() => window.shaderProbe?.state !== 'pending', {}, {timeout: 30000});
  evidence.report = await page.evaluate(() => window.shaderProbe);
  assert.equal(evidence.report.state, 'passed', JSON.stringify(evidence.report, null, 2));
  assert.equal(evidence.report.cases.length, 8, 'Expected 8 pixel cases');
  await page.waitForFunction(() => window.shaderProbe.frames >= 5);
  await page.screenshot({path: resolve(output, 'probe.png')});
  evidence.report = await page.evaluate(() => window.shaderProbe);
  assert.equal(evidence.report.errors.length, 0, 'Page reported errors');
  assert.equal(evidence.errors.length, 0, 'Browser reported errors');
  evidence.outcome = 'passed';
} catch (error) {
  evidence.outcome = 'failed';
  evidence.failure = String(error);
  process.exitCode = 1;
} finally {
  await writeFile(resolve(output, 'report.json'), JSON.stringify(evidence, null, 2) + '\n');
  console.log(JSON.stringify(evidence, null, 2));
  await browser?.close();
  server.close();
}
