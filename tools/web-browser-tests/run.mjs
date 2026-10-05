// CI only: real Chromium/SwiftShader, plus explicit fault injection in test
// contexts. Never load this harness into a user's browser or ship it in the ZIP.
//
// This harness drives the packaged, extracted smoke demo (exact ZIP payload)
// across the WebGPU/WebGL 2 fallback matrix. It exercises real shader
// compilation/linking and pixels on both browser backends, not stubs. Software
// GPU only (SwiftShader); physical-GPU and hosted acceptance are separate gates
// recorded in the handoff.
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve, sep} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import {chromium} from 'playwright';
import {PNG} from 'pngjs';

const root = resolve(process.argv[2] || '../../out/browser-qa/extracted');
const zip = resolve(process.argv[3] || '../../out/packages/ludus-web-smoke-release.zip');
const output = resolve(process.argv[4] || '../../out/browser-qa/results');
await mkdir(output, {recursive: true});
const report = {
  kind: 'software-GPU CI (SwiftShader), not hardware or hosted acceptance',
  zipSha256: createHash('sha256').update(await readFile(zip)).digest('hex'),
  // Software Vulkan + ANGLE/SwiftShader so Auto can pick WebGPU or WebGL 2 on a
  // host with no physical GPU. No experimental feature is used to claim a
  // flag-free pass; these flags only select the software backend.
  launchArgs: ['--enable-unsafe-webgpu', '--enable-features=Vulkan',
    '--use-webgpu-adapter=swiftshader', '--use-angle=swiftshader',
    '--use-vulkan=swiftshader', '--disable-vulkan-surface', '--enable-unsafe-swiftshader'],
  cases: [], requests: []};
const server = createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  const file = resolve(root, '.' + (pathname.endsWith('/') ? pathname + 'index.html' : pathname));
  if (!file.startsWith(root + sep)) {response.writeHead(403).end(); return;}
  try {
    const bytes = await readFile(file);
    const type = file.endsWith('.wasm') ? 'application/wasm' :
      file.endsWith('.js') ? 'text/javascript' : file.endsWith('.html') ? 'text/html' : 'text/plain';
    response.writeHead(200, {'Content-Type': type, 'Cache-Control': 'no-store'}).end(bytes);
    report.requests.push({path: pathname, status: 200});
  } catch {response.writeHead(404).end(); report.requests.push({path: pathname, status: 404});}
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}`;
let browser;
const observedPages = [];
async function until(read, accept, label, ms = 15000) {
  const deadline = Date.now() + ms;
  while (Date.now() < deadline) {
    const value = await read();
    if (accept(value)) return value;
    await delay(30);
  }
  throw new Error('Timed out: ' + label);
}
const data = (page, key) => page.locator('#status').getAttribute('data-' + key);
const state = page => data(page, 'state');
const backend = page => data(page, 'backend');
const frames = page => data(page, 'frames').then(Number);
const x = page => data(page, 'x').then(Number);
const y = page => data(page, 'y').then(Number);
async function playing(page) {await until(() => state(page), v => v === 'playing', 'playing', 20000);}
// A WebGPU/WebGL canvas with preserveDrawingBuffer unset reads blank via an
// element screenshot in headless; capture the composited page region over the
// canvas instead, which reflects the presented frame on both backends.
async function canvasShot(page) {
  const box = await page.locator('canvas').boundingBox();
  return page.screenshot({clip: {x: box.x, y: box.y, width: box.width, height: box.height}});
}

// Count the orange marker pixels and their centroid. The generic demo shader
// draws an orange marker (circle) on a dark background on every backend.
function scan(bytes) {
  const image = PNG.sync.read(bytes);
  let count = 0, sumX = 0, sumY = 0;
  for (let y = 0; y < image.height; ++y) for (let x = 0; x < image.width; ++x) {
    const p = (y * image.width + x) * 4;
    if (image.data[p] > 220 && image.data[p+1] > 110 && image.data[p+1] < 210 && image.data[p+2] < 90) {
      ++count; sumX += x; sumY += y;
    }
  }
  return {count, x: count ? sumX / count : 0, y: count ? sumY / count : 0,
    width: image.width, height: image.height, corner: Array.from(image.data.subarray(0, 3))};
}
// Poll until the composited frame shows the marker; the first presented frame
// can lag the "playing" state by a compositor tick on either backend.
async function marker(page) {
  return until(async () => scan(await canvasShot(page)), r => r.count > 100, 'visible orange marker');
}

// A test-only context. `scenario` injects WebGPU/WebGL faults; `query` forces a
// backend selection via the demo's ?backend= URL param.
async function context(scenario = 'success', options = {}) {
  const ctx = await browser.newContext({viewport: {width: 640, height: 360}, ...options});
  ctx.on('page', page => {
    const consoleMessages = [];
    observedPages.push({page, consoleMessages});
    page.on('console', m => consoleMessages.push({type: m.type(), text: m.text()}));
    page.on('pageerror', e => consoleMessages.push({type: 'pageerror', text: String(e)}));
  });
  await ctx.addInitScript(scenario => {
    // Controlled requestAnimationFrame suspension for the late-callback test.
    const nativeRAF = window.requestAnimationFrame.bind(window);
    window.requestAnimationFrame = cb => nativeRAF(time => {
      if (window.__qaSuspend) {
        window.__qaPending = cb;
        window.__qaPaused = {x: Number(document.querySelector('#status').dataset.x),
          frames: Number(document.querySelector('#status').dataset.frames)};
      } else cb(time);
    });
    if (scenario === 'canvas-replacement-failure') {
      const create = document.createElement.bind(document);
      document.createElement = (name, ...args) => {
        if (name === 'canvas') throw new Error('QA rejected canvas replacement');
        return create(name, ...args);
      };
      Object.defineProperty(navigator, 'gpu', {value: undefined});
      return;
    }
    // Disable WebGL 2 so "both unavailable" and "forced WebGPU" can be exercised.
    if (scenario === 'no-webgl2' || scenario === 'both-unavailable' || scenario === 'surface-failure') {
      const getContext = HTMLCanvasElement.prototype.getContext;
      HTMLCanvasElement.prototype.getContext = function (type, ...rest) {
        if (type === 'webgl2' && scenario !== 'surface-failure') return null;
        if (type === 'webgpu' && scenario === 'surface-failure') return null;
        return getContext.call(this, type, ...rest);
      };
    }
    if (scenario === 'missing-webgpu' || scenario === 'both-unavailable') {
      Object.defineProperty(navigator, 'gpu', {value: undefined}); return;
    }
    if (scenario === 'both-uniform-limits') {
      const getParameter = WebGL2RenderingContext.prototype.getParameter;
      WebGL2RenderingContext.prototype.getParameter = function (key) {
        return key === this.MAX_UNIFORM_BLOCK_SIZE ? 32 : getParameter.call(this, key);
      };
    }
    const gpu = navigator.gpu;
    if (!gpu) return;
    const requestAdapter = gpu.requestAdapter.bind(gpu);
    gpu.requestAdapter = async options => {
      if (scenario === 'adapter-failure') return null;
      const adapter = await requestAdapter(options);
      if (!adapter) return adapter;
      window.__qaAdapter = {vendor: adapter.info?.vendor, architecture: adapter.info?.architecture,
        device: adapter.info?.device, description: adapter.info?.description};
      const requestDevice = adapter.requestDevice.bind(adapter);
      adapter.requestDevice = async descriptor => {
        if (scenario === 'device-failure') throw new Error('test device denied');
        const device = await requestDevice(descriptor);
        window.__qaDevice = device;
        if (scenario === 'uniform-limit' || scenario === 'both-uniform-limits') {
          // Override only the engine's queried device limit. The real device
          // still compiles shaders/renders on accepted paths.
          const limits = device.limits;
          Object.defineProperty(device, 'limits', {value: new Proxy(limits, {
            get(target, key) {
              return key === 'maxUniformBufferBindingSize' ? 32 : Reflect.get(target, key, target);
            }
          })});
        }
        window.__qaValidation = [];
        device.addEventListener('uncapturederror', e => window.__qaValidation.push(e.error.message));
        return device;
      };
      if (scenario === 'cancel-startup') await new Promise(resolve => setTimeout(resolve, 300));
      return adapter;
    };
  }, scenario);
  return ctx;
}
async function run(name, task) {
  const entry = {name, status: 'running'};
  report.cases.push(entry);
  try {await task(entry); entry.status = 'passed';}
  catch (error) {entry.status = 'failed'; entry.error = String(error);}
  console.log(JSON.stringify(entry));
}

try {
  // Use the pinned Playwright-bundled Chromium (not a branded channel): it is the
  // integrity-locked build the WGSL validator also pins, and it composites WebGPU
  // to the canvas in headless, which the branded channel does not here.
  browser = await chromium.launch({headless: true, args: report.launchArgs});
  report.browserVersion = browser.version();
  assert.equal(report.browserVersion, '140.0.7339.186', 'Pinned Chromium required');

  // 1. Auto with usable WebGPU: WebGPU selected, animates, pixels/input/resize/
  //    fullscreen/stop/restart/device-loss, WebGL not initialized.
  await run('auto webgpu: pixels, input, blur, resize, fullscreen, restart, loss', async entry => {
    const c = await context();
    const page = await c.newPage();
    const errors = [];
    page.on('pageerror', e => errors.push(String(e)));
    await page.goto(base + '/index.html');
    await playing(page);
    entry.backend = await backend(page);
    assert.equal(entry.backend, 'webgpu', 'Auto should select WebGPU here');
    report.adapter = await page.evaluate(() => window.__qaAdapter);
    assert(/swiftshader/i.test(JSON.stringify(report.adapter)), 'Expected software adapter');
    const before = await frames(page);
    await until(() => frames(page), n => n > before + 5, 'animated frames');
    const first = await marker(page);
    await writeFile(resolve(output, 'auto-webgpu.png'), await canvasShot(page));
    // Input moves the marker.
    await page.locator('canvas').click();
    await page.keyboard.down('d');
    await until(() => x(page), v => v > 0.3, 'keyboard movement');
    await page.keyboard.up('d');
    const second = await marker(page);
    assert(second.x > first.x + 10, 'Marker did not move with input');
    await page.keyboard.down('s');
    await until(() => y(page), v => v < -0.3, 'vertical keyboard movement');
    await page.keyboard.up('s');
    const vertical = await marker(page);
    assert(vertical.y > second.y + 10, 'WebGPU vertical coordinates inverted');
    // Blur clears held keys.
    await page.keyboard.down('d');
    await page.locator('#restart').focus();
    const blurred = await x(page);
    await delay(160);
    assert(Math.abs(await x(page) - blurred) < 0.02, 'Blur kept a key held');
    await page.keyboard.up('d');
    // Resize/DPR.
    await page.setViewportSize({width: 320, height: 240});
    const resized = await marker(page);
    assert(resized.width <= 320 && resized.height <= 240);
    // Fullscreen toggle.
    await page.locator('#fullscreen').click();
    await until(() => page.locator('#fullscreen').textContent(), t => t === 'Exit fullscreen', 'fullscreen');
    await page.locator('#fullscreen').click();
    // Stop halts frames; controls gating: Stop disabled when stopped.
    await page.locator('#stop').click();
    await until(() => state(page), s => s === 'stopped', 'stop');
    assert.equal(await page.locator('#stop').isEnabled(), false, 'Stop active while stopped');
    const stopped = await frames(page);
    await delay(120); assert.equal(await frames(page), stopped, 'Frames advanced while stopped');
    await page.locator('#restart').click(); await playing(page);
    // WebGPU device loss -> DeviceLost -> restart rebuilds.
    assert.deepEqual(await page.evaluate(() => window.__qaValidation ?? []), []);
    await page.evaluate(() => window.__qaDevice.destroy());
    await until(() => state(page), s => s === 'device lost', 'device loss');
    await page.locator('#restart').click(); await playing(page);
    assert.deepEqual(errors, [], 'Page errors');
    await c.close();
  });

  // 2-4. Auto fallback: missing WebGPU API, adapter rejection, device rejection
  //      each fall back to WebGL 2 and keep playing with real GLSL ES pixels.
  for (const scenario of ['missing-webgpu', 'adapter-failure', 'device-failure', 'surface-failure', 'uniform-limit']) {
    await run('auto fallback to WebGL 2 on ' + scenario, async entry => {
      const c = await context(scenario);
      const page = await c.newPage();
      const errors = [];
      page.on('pageerror', e => errors.push(String(e)));
      await page.goto(base + '/index.html');
      await playing(page);
      entry.backend = await backend(page);
      assert.equal(entry.backend, 'webgl2', scenario + ' should fall back to WebGL 2');
      const before = await frames(page);
      await until(() => frames(page), n => n > before + 5, 'WebGL 2 animated frames');
      const m = await marker(page);
      await writeFile(resolve(output, 'fallback-' + scenario + '.png'), await canvasShot(page));
      // Input works on the WebGL 2 path too.
      await page.locator('canvas').click();
      await page.keyboard.down('ArrowRight');
      await until(() => x(page), v => v > 0.3, 'WebGL 2 keyboard movement');
      await page.keyboard.up('ArrowRight');
      const moved = await marker(page);
      assert(moved.x > m.x + 10, 'WebGL 2 marker did not move');
      await page.keyboard.down('ArrowDown');
      await until(() => y(page), v => v < -0.3, 'WebGL 2 vertical movement');
      await page.keyboard.up('ArrowDown');
      assert((await marker(page)).y > moved.y + 10, 'WebGL 2 vertical coordinates inverted');
      assert.deepEqual(errors, []);
      await c.close();
    });
  }

  await run('forced WebGPU rejects unmet uniform requirement', async entry => {
    const c = await context('uniform-limit');
    const page = await c.newPage();
    await page.goto(base + '/index.html?backend=webgpu');
    await until(() => state(page), s => s === 'failed', 'forced requirement failure');
    entry.webgpuError = await data(page, 'webgpu-error');
    assert.equal(Number(entry.webgpuError), 11); // RequirementsUnsatisfied
    assert.equal(Number(await data(page, 'webgl-error')), 0);
    assert.equal(await frames(page), 0);
    await c.close();
  });
  await run('Auto rejects both backends with unmet uniform requirements', async entry => {
    const c = await context('both-uniform-limits');
    const page = await c.newPage();
    await page.goto(base + '/index.html');
    await until(() => state(page), s => s === 'failed', 'both requirement failures');
    entry.webgpuError = await data(page, 'webgpu-error');
    entry.webglError = await data(page, 'webgl-error');
    assert.equal(Number(entry.webgpuError), 11);
    assert.equal(Number(entry.webglError), 11);
    assert.equal(await frames(page), 0);
    await c.close();
  });

  // 5. Both unavailable: final readable error, no frames, no busy loop, status
  //    not covered by controls (narrow viewport).
  await run('both unavailable: readable final error, no overlap', async entry => {
    const c = await context('both-unavailable', {viewport: {width: 300, height: 240}});
    const page = await c.newPage();
    await page.goto(base + '/index.html');
    await until(() => state(page), s => s === 'failed', 'both-unavailable failure');
    const text = await page.locator('#status').textContent();
    assert.match(text, /WebGPU or WebGL 2|could not start/, 'Final error not readable');
    assert.equal(await frames(page), 0, 'Frames advanced with no backend');
    entry.webgpuError = await data(page, 'webgpu-error');
    entry.webglError = await data(page, 'webgl-error');
    assert(Number(entry.webgpuError) !== 0 && Number(entry.webglError) !== 0, 'Both attempts should be recorded');
    // Status must not be covered by the controls row (reserved area).
    const overlap = await page.evaluate(() => {
      const s = document.getElementById('status').getBoundingClientRect();
      const c = document.querySelector('.controls').getBoundingClientRect();
      return s.bottom > c.top + 1;
    });
    assert.equal(overlap, false, 'Controls overlap the status');
    await writeFile(resolve(output, 'both-unavailable.png'), await page.screenshot());
    await c.close();
  });

  await run('failed canvas replacement finalizes one fallback attempt', async entry => {
    const c = await context('canvas-replacement-failure');
    const page = await c.newPage(); await page.goto(base + '/index.html');
    await until(() => state(page), s => s === 'failed', 'canvas replacement failure');
    assert(Number(await data(page, 'webgpu-error')) !== 0);
    assert(Number(await data(page, 'webgl-error')) !== 0);
    assert.equal(await backend(page), 'none');
    assert.equal(await frames(page), 0);
    await c.close();
  });

  // 6. Forced WebGPU: WebGPU only; with WebGL disabled it must NOT fall back.
  await run('forced WebGPU selects only WebGPU', async entry => {
    const c = await context('no-webgl2'); // WebGL2 disabled; forced WebGPU must ignore it
    const page = await c.newPage();
    await page.goto(base + '/index.html?backend=webgpu');
    await playing(page);
    entry.backend = await backend(page);
    assert.equal(entry.backend, 'webgpu', 'Forced WebGPU did not select WebGPU');
    await c.close();
  });

  // 7. Forced WebGL 2: WebGL only; exercises real GLSL ES compile/link/pixels.
  await run('forced WebGL 2 compiles/links/renders real GLSL ES', async entry => {
    const c = await context();
    const page = await c.newPage();
    const errors = [];
    page.on('pageerror', e => errors.push(String(e)));
    await page.goto(base + '/index.html?backend=webgl2');
    await playing(page);
    entry.backend = await backend(page);
    assert.equal(entry.backend, 'webgl2', 'Forced WebGL 2 did not select WebGL 2');
    const before = await frames(page);
    await until(() => frames(page), n => n > before + 5, 'forced WebGL 2 frames');
    const m = await marker(page);
    assert(m.count > 100, 'No GLSL ES pixels');
    await writeFile(resolve(output, 'forced-webgl2.png'), await canvasShot(page));
    assert.deepEqual(errors, []);
    await c.close();
  });

  // 8. Late callback during pending WebGPU request: cancel is harmless and
  //    leaves exactly one (stopped) session; the late adapter callback is a no-op.
  await run('cancel during pending request leaves one session', async () => {
    const c = await context('cancel-startup');
    const page = await c.newPage();
    await page.goto(base + '/index.html');
    await until(() => state(page), s => s === 'loading', 'pending startup');
    await page.locator('#stop').click();
    await until(() => state(page), s => s === 'stopped', 'stopped after cancel');
    const settled = await frames(page);
    await delay(500); // let any late adapter/device callback arrive
    assert.equal(await state(page), 'stopped', 'Late callback resurrected a session');
    assert.equal(await frames(page), settled, 'Frames advanced after cancel');
    await c.close();
  });

  // 9. WebGL 2 context loss and restoration: drawing stops, restart rebuilds.
  await run('WebGL 2 context loss and restoration', async entry => {
    const c = await context('missing-webgpu'); // force the WebGL 2 path
    const page = await c.newPage();
    await page.goto(base + '/index.html');
    await playing(page);
    assert.equal(await backend(page), 'webgl2');
    const extension = await page.evaluate(() => {
      const gl = document.querySelector('canvas').getContext('webgl2');
      // getContext returns the engine's existing context; WEBGL_lose_context lets
      // the test inject a real loss. Record the dependency.
      const ext = gl && gl.getExtension('WEBGL_lose_context');
      if (ext) { ext.loseContext(); return true; }
      return false;
    });
    entry.loseContextExtension = extension;
    if (extension) {
      await until(() => state(page), s => s === 'device lost', 'WebGL 2 context loss');
      assert.equal(await frames(page) >= 0, true);
      await page.locator('#restart').click();
      await playing(page);
      assert.equal(await backend(page), 'webgl2', 'Restart did not rebuild on WebGL 2');
    } else {
      entry.note = 'WEBGL_lose_context unavailable on this build; loss not injected';
    }
    await c.close();
  });

  // 10. Repeated restart + resize does not accumulate listeners/resources.
  await run('repeated restart and resize stays stable', async () => {
    const c = await context();
    const page = await c.newPage();
    await page.goto(base + '/index.html');
    await playing(page);
    for (let i = 0; i < 4; ++i) {
      await page.locator('#stop').click();
      await until(() => state(page), s => s === 'stopped', 'stop ' + i);
      await page.setViewportSize({width: 400 + i * 20, height: 300});
      await page.locator('#restart').click();
      await playing(page);
    }
    const m = await marker(page);
    assert(m.count > 100, 'Marker missing after repeated restart');
    await c.close();
  });

  // 11. Ordinary sandbox iframe and high-DPR canvas.
  await run('ordinary sandbox iframe and high-DPR canvas', async () => {
    const c = await context('success', {deviceScaleFactor: 1.5});
    const page = await c.newPage(); await page.goto(base + '/iframe.html');
    const frame = await until(() => Promise.resolve(page.frames().find(f => f.url().endsWith('/index.html'))), Boolean, 'iframe');
    await playing(frame);
    const dims = await frame.locator('canvas').evaluate(canvas => ({w: canvas.width, h: canvas.height,
      cw: canvas.getBoundingClientRect().width, ch: canvas.getBoundingClientRect().height}));
    assert(Math.abs(dims.w - dims.cw * 1.5) <= 1 && Math.abs(dims.h - dims.ch * 1.5) <= 1, 'DPR backing size wrong');
    await writeFile(resolve(output, 'iframe.png'), await page.screenshot());
    await c.close();
  });

  // 12. Network asset failure stays readable.
  for (const file of ['index.js', 'index.wasm']) {
    await run('network abort ' + file, async () => {
      const c = await context(); const page = await c.newPage();
      await page.route('**/' + file, route => route.abort('failed'));
      await page.goto(base + '/index.html');
      await until(() => page.locator('#status').textContent(), t => t.includes('Please reload') || t.includes('could not load'), 'network failure');
      await c.close();
    });
  }

  const failed = report.cases.filter(entry => entry.status === 'failed');
  if (failed.length) throw new Error(failed.map(entry => entry.name + ': ' + entry.error).join('; '));
} catch (error) {
  report.failure = String(error); process.exitCode = 1;
  report.diagnostics = [];
  for (const {page, consoleMessages} of observedPages) {
    if (page.isClosed()) continue;
    const entry = {url: page.url(), consoleMessages};
    report.diagnostics.push(entry);
    try {
      entry.dom = await page.evaluate(() => {
        const canvas = document.querySelector('canvas');
        const status = document.querySelector('#status');
        return {visibility: document.visibilityState, status: status?.textContent,
          attributes: status ? Object.fromEntries(Array.from(status.attributes, a => [a.name, a.value])) : {},
          canvas: canvas ? {width: canvas.width, height: canvas.height,
            cssWidth: canvas.clientWidth, cssHeight: canvas.clientHeight} : null};
      });
      await page.screenshot({path: resolve(output, `failure-${report.diagnostics.length}.png`)});
    } catch (diagnosticError) {entry.error = String(diagnosticError);}
  }
} finally {
  await browser?.close(); server.close();
  await writeFile(resolve(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
}
