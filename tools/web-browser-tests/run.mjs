// CI only: real Chromium/SwiftShader, plus explicit fault injection in test contexts.
// Never load this harness into a user's browser or ship it in the game ZIP.
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
await mkdir(output, {recursive:true});
const report = {kind:'software-GPU CI, not hardware acceptance',
  zipSha256:createHash('sha256').update(await readFile(zip)).digest('hex'),
  // Match Chromium's software Vulkan pixel-test configuration so compositor and
  // WebGPU share the intended software backend on hosts without a physical GPU.
  launchArgs:['--enable-unsafe-webgpu', '--enable-features=Vulkan',
    '--use-webgpu-adapter=swiftshader', '--use-angle=swiftshader',
    '--use-vulkan=swiftshader', '--disable-vulkan-surface'],
  cases:[], requests:[]};
const server = createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  const file = resolve(root, '.' + (pathname.endsWith('/') ? pathname + 'index.html' : pathname));
  if (!file.startsWith(root + sep)) {response.writeHead(403).end(); return;}
  try {
    const bytes = await readFile(file);
    const type = file.endsWith('.wasm') ? 'application/wasm' :
      file.endsWith('.js') ? 'text/javascript' : file.endsWith('.html') ? 'text/html' : 'text/plain';
    response.writeHead(200, {'Content-Type':type, 'Cache-Control':'no-store'}).end(bytes);
    report.requests.push({path:pathname,status:200});
  } catch {response.writeHead(404).end(); report.requests.push({path:pathname,status:404});}
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}`;
let browser;
const observedPages = [];
async function until(read, accept, label) {
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    const value = await read();
    if (accept(value)) return value;
    await delay(30);
  }
  throw new Error('Timed out: ' + label);
}
const state = page => page.locator('#status').getAttribute('data-state');
const frames = page => page.locator('#status').getAttribute('data-frames').then(Number);
const x = page => page.locator('#status').getAttribute('data-x').then(Number);
async function playing(page) {await until(() => state(page), value => value === 'playing', 'playing');}
function triangle(bytes) {
  const image = PNG.sync.read(bytes);
  let count = 0, sumX = 0, sumY = 0;
  for (let y = 0; y < image.height; ++y) for (let x = 0; x < image.width; ++x) {
    const p = (y * image.width + x) * 4;
    if (image.data[p] > 220 && image.data[p+1] > 110 && image.data[p+1] < 210 && image.data[p+2] < 90) {
      ++count; sumX += x; sumY += y;
    }
  }
  assert(count > 100, 'No visible orange triangle');
  return {count,x:sumX/count,y:sumY/count,width:image.width,height:image.height,
    corner:Array.from(image.data.subarray(0,3))};
}
async function context(scenario = 'success', options = {}) {
  const context = await browser.newContext({viewport:{width:640,height:360},...options});
  context.on('page', page => {
    const consoleMessages = [];
    observedPages.push({page,consoleMessages});
    page.on('console', message => consoleMessages.push({type:message.type(),text:message.text()}));
    page.on('pageerror', error => consoleMessages.push({type:'pageerror',text:String(error)}));
  });
  // This test-only wrapper captures real resources or injects request failures.
  await context.addInitScript(scenario => {
    const nativeRAF = window.requestAnimationFrame.bind(window);
    window.requestAnimationFrame = callback => nativeRAF(time => {
      if (window.__qaSuspend) {
        window.__qaPending = callback;
        window.__qaPaused = {x:Number(document.querySelector('#status').dataset.x),
          frames:Number(document.querySelector('#status').dataset.frames)};
      } else callback(time);
    });
    if (scenario === 'missing-webgpu') {
      Object.defineProperty(navigator,'gpu',{value:undefined}); return;
    }
    const gpu = navigator.gpu;
    if (!gpu) return;
    const requestAdapter = gpu.requestAdapter.bind(gpu);
    gpu.requestAdapter = async options => {
      if (scenario === 'adapter-failure') return null;
      const adapter = await requestAdapter(options);
      if (!adapter) return adapter;
      window.__qaAdapter = {vendor:adapter.info?.vendor, architecture:adapter.info?.architecture,
        device:adapter.info?.device, description:adapter.info?.description};
      const requestDevice = adapter.requestDevice.bind(adapter);
      adapter.requestDevice = async descriptor => {
        if (scenario === 'device-failure') throw new Error('test device denied');
        const device = await requestDevice(descriptor);
        window.__qaDevice = device;
        window.__qaValidation = [];
        device.addEventListener('uncapturederror', e => window.__qaValidation.push(e.error.message));
        return device;
      };
      if (scenario === 'cancel-startup') await new Promise(resolve => setTimeout(resolve,300));
      return adapter;
    };
  }, scenario);
  return context;
}
async function run(name, task) {
  const entry = {name,status:'running'};
  report.cases.push(entry);
  try {await task(); entry.status = 'passed';}
  catch(error) {entry.status = 'failed'; entry.error = String(error);}
}
try {
  browser = await chromium.launch({headless:false, channel:'chromium', args:report.launchArgs});
  report.browserVersion = browser.version();
  await run('real software-GPU pixels, input, blur, resize, fullscreen, restart, loss', async () => {
    const c = await context();
    const page = await c.newPage();
    const errors = [];
    page.on('pageerror', e => errors.push(String(e)));
    await page.goto(base + '/index.html');
    await playing(page);
    report.adapter = await page.evaluate(() => window.__qaAdapter);
    assert(/swiftshader/i.test(JSON.stringify(report.adapter)), 'Expected software adapter');
    const before = await frames(page);
    await until(() => frames(page), n => n > before + 5, 'animated frames');
    const firstBytes = await page.locator('canvas').screenshot();
    await writeFile(resolve(output,'software-triangle.png'),firstBytes);
    const first = triangle(firstBytes);
    await until(async () => triangle(await page.locator('canvas').screenshot()).corner,
      color => color.some((value,i) => Math.abs(value-first.corner[i]) > 3), 'animated clear pixels');
    await page.locator('canvas').click();
    await page.keyboard.down('d');
    await until(() => x(page), value => value > 0.3, 'keyboard movement');
    await page.keyboard.up('d');
    const second = triangle(await page.locator('canvas').screenshot());
    assert(second.x > first.x + 15, 'Triangle pixels did not move');
    await page.keyboard.down('d');
    await page.locator('#fullscreen').focus(); // Blur clears held keys.
    const blurred = await x(page);
    await delay(160);
    assert(Math.abs(await x(page) - blurred) < 0.02, 'Blur kept a key held');
    await page.keyboard.up('d');
    // Controlled callback suspension: exercises real WASM timing, but does not
    // claim native tab background/freeze behavior (a hosted hardware gate).
    await page.locator('canvas').click();
    await page.keyboard.down('d');
    await page.evaluate(() => {window.__qaSuspend = true;});
    await until(() => page.evaluate(() => window.__qaPaused), Boolean, 'callback suspension');
    await delay(1200);
    await page.evaluate(() => {
      window.__qaSuspend = false;
      requestAnimationFrame(time => {
        window.__qaPending(time);
        window.__qaResumed = Number(document.querySelector('#status').dataset.x);
      });
    });
    const suspension = await until(() => page.evaluate(() => ({paused:window.__qaPaused,
      resumed:window.__qaResumed})), value => value.resumed !== undefined, 'callback resume');
    await page.keyboard.up('d');
    report.suspension = {kind:'controlled RAF callback suspension',...suspension};
    assert(suspension.paused.x < 0.8, 'Suspension started too near movement boundary');
    assert(suspension.resumed-suspension.paused.x <= 0.10001,
      'First resumed frame exceeded the simulation time-step bound');
    const bounds = await page.locator('canvas').boundingBox();
    await page.mouse.move(bounds.x + bounds.width*0.25, bounds.y + bounds.height*0.75);
    await page.mouse.down();
    await until(() => x(page), value => value < -0.45, 'CSS pointer mapping');
    await page.mouse.up();
    await page.setViewportSize({width:320,height:240});
    const resized = triangle(await page.locator('canvas').screenshot());
    assert(resized.width <= 320 && resized.height <= 240);
    const size = await page.evaluate(() => ({w:innerWidth,h:innerHeight,
      sw:document.documentElement.scrollWidth,sh:document.documentElement.scrollHeight}));
    assert(size.sw <= size.w && size.sh <= size.h, 'Embed overflow');
    await page.locator('canvas').evaluate(canvas => {canvas.style.display = 'none';});
    await delay(100);
    const hidden = await frames(page);
    await delay(160); assert.equal(await frames(page),hidden, 'Hidden canvas rendered');
    await page.locator('canvas').evaluate(canvas => {canvas.style.display = '';});
    await until(() => frames(page), value => value > hidden, 'visible resume');
    await page.locator('#fullscreen').click();
    await until(() => page.locator('#fullscreen').textContent(), text => text === 'Exit fullscreen', 'fullscreen');
    await page.locator('#fullscreen').click();
    await page.locator('#stop').click();
    await until(() => state(page), s => s === 'stopped', 'stop');
    const stopped = await frames(page);
    await delay(120); assert.equal(await frames(page),stopped);
    await page.locator('#restart').click(); await playing(page);
    assert.deepEqual(await page.evaluate(() => window.__qaValidation),[]);
    await page.evaluate(() => window.__qaDevice.destroy());
    await until(() => state(page), s => s === 'device lost', 'device loss');
    await page.locator('#restart').click(); await playing(page);
    assert.deepEqual(await page.evaluate(() => window.__qaValidation),[]);
    assert.deepEqual(errors,[]);
    await c.close();
  });
  await run('ordinary sandbox iframe and high-DPR canvas', async () => {
    const c = await context('success',{deviceScaleFactor:1.5});
    const page = await c.newPage(); await page.goto(base + '/iframe.html');
    const frame = await until(() => Promise.resolve(page.frames().find(f => f.url().endsWith('/index.html'))), Boolean, 'iframe');
    await playing(frame);
    const dims = await frame.locator('canvas').evaluate(canvas => ({w:canvas.width,h:canvas.height,
      cw:canvas.getBoundingClientRect().width,ch:canvas.getBoundingClientRect().height}));
    assert(Math.abs(dims.w-dims.cw*1.5) <= 1 && Math.abs(dims.h-dims.ch*1.5) <= 1);
    await writeFile(resolve(output,'software-iframe.png'),await page.screenshot());
    await c.close();
  });
  for (const scenario of ['missing-webgpu','adapter-failure','device-failure','cancel-startup']) {
    await run(scenario + ' in a real DOM', async () => {
      const c = await context(scenario); const page = await c.newPage();
      await page.goto(base + '/index.html');
      if (scenario === 'cancel-startup') {
        await until(() => state(page), s => s === 'loading', 'pending startup');
        await page.locator('#stop').click(); await delay(500);
        assert.equal(await state(page),'stopped'); assert.equal(await frames(page),0);
      } else {
        await until(() => state(page), s => s === 'failed', 'controlled failure');
        assert.match(await page.locator('#status').textContent(),/WebGPU|Graphics/);
        assert.equal(await frames(page),0);
      }
      await c.close();
    });
  }
  for (const file of ['index.js','index.wasm']) {
    await run('network abort ' + file, async () => {
      const c = await context(); const page = await c.newPage();
      await page.route('**/' + file, route => route.abort('failed'));
      await page.goto(base + '/index.html');
      await until(() => page.locator('#status').textContent(), text => text.includes('Please reload'), 'network failure');
      await c.close();
    });
  }
  const failed = report.cases.filter(entry => entry.status === 'failed');
  if (failed.length) throw new Error(failed.map(entry => entry.name + ': ' + entry.error).join('; '));
} catch(error) {
  report.failure = String(error); process.exitCode = 1;
  report.diagnostics = [];
  for (const {page,consoleMessages} of observedPages) {
    if (page.isClosed()) continue;
    const entry = {url:page.url(),consoleMessages};
    report.diagnostics.push(entry);
    try {
      entry.dom = await page.evaluate(() => {
        const canvas = document.querySelector('canvas');
        const status = document.querySelector('#status');
        return {visibility:document.visibilityState,status:status?.textContent,
          attributes:status ? Object.fromEntries(Array.from(status.attributes, a => [a.name,a.value])) : {},
          canvas:canvas ? {width:canvas.width,height:canvas.height,
            cssWidth:canvas.clientWidth,cssHeight:canvas.clientHeight} : null,
          validation:window.__qaValidation,paused:window.__qaPaused,resumed:window.__qaResumed};
      });
      await page.screenshot({path:resolve(output,`failure-${report.diagnostics.length}.png`)});
    } catch (diagnosticError) {entry.error = String(diagnosticError);}
  }
} finally {
  await browser?.close(); server.close();
  await writeFile(resolve(output,'report.json'),JSON.stringify(report,null,2)+'\n');
  console.log(JSON.stringify(report,null,2));
}
