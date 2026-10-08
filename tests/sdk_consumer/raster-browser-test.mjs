// Installed-SDK raster conformance. Real shader compilation, indexed instances,
// depth, texture origin and retained snapshots on both forced APIs and Auto.
import assert from 'node:assert/strict';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve, sep} from 'node:path';
import {createRequire} from 'node:module';
const require = createRequire(new URL('../../tools/web-browser-tests/package.json', import.meta.url));
const {chromium} = require('playwright');
const {PNG} = require('pngjs');
const root = resolve(process.argv[2]);
const output = resolve(process.argv[3] || 'out/raster-browser');
await mkdir(output, {recursive:true});
const server = createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  const path = resolve(root, '.' + pathname);
  if (!path.startsWith(root + sep)) {response.writeHead(403).end(); return;}
  try {response.writeHead(200, {'Content-Type':path.endsWith('.wasm') ? 'application/wasm' : path.endsWith('.js') ? 'text/javascript' : 'text/html'}).end(await readFile(path));}
  catch {response.writeHead(404).end();}
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const args = ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-webgpu-adapter=swiftshader', '--use-angle=swiftshader', '--use-vulkan=swiftshader', '--disable-vulkan-surface', '--enable-unsafe-swiftshader'];
const report = {kind:'real Chromium software GPU; physical GPU acceptance separate', args, cases:[], console:[]};
let browser, lastPage;
try {
  browser = await chromium.launch({headless:true, args});
  const pin = JSON.parse(await readFile(new URL('../../config/shader_toolchain.json',import.meta.url))).wgsl_validator;
  assert.equal(browser.version(),pin.chromium_version);
  report.version = browser.version();
  for (const scenario of ['webgpu','webgl2','auto','auto-fallback','webgpu-srgb','webgl2-srgb','webgpu-blend','webgl2-blend','invalid-shader','invalid-pipeline','invalid-upload','invalid-readback','slow-completion','transfer-loss','device-loss']) {
    const context = await browser.newContext({viewport:{width:200,height:160},deviceScaleFactor:1});
    await context.addInitScript(scenario => {
      window.__qaPause = false;
      window.__qaSlowTransfers = scenario === 'slow-completion';
      if (scenario === 'auto-fallback') Object.defineProperty(navigator,'gpu',{get:() => undefined});
      if (navigator.gpu) {
        const requestAdapter = navigator.gpu.requestAdapter.bind(navigator.gpu);
        navigator.gpu.requestAdapter = async (...args) => {
          const adapter = await requestAdapter(...args);
          if (!adapter) return adapter;
          const requestDevice = adapter.requestDevice.bind(adapter);
          adapter.requestDevice = async (...args) => {
            const device = await requestDevice(...args); window.__qaDevice = device;
            const shader = device.createShaderModule.bind(device);
            device.createShaderModule = desc => shader(scenario === 'invalid-shader' ? {...desc,code:'invalid WGSL'} : desc);
            const pipeline = device.createRenderPipelineAsync.bind(device);
            device.createRenderPipelineAsync = desc => pipeline(scenario === 'invalid-pipeline' ? {...desc,vertex:{...desc.vertex,entryPoint:'missing'}} : desc);
            const buffer = device.createBuffer.bind(device);
            device.createBuffer = desc => {
              const object = buffer(scenario === 'invalid-upload' && (desc.usage & GPUBufferUsage.VERTEX) && (desc.usage & GPUBufferUsage.COPY_DST) ? {...desc,size:4} : desc);
              if (scenario === 'invalid-readback' && (desc.usage & GPUBufferUsage.MAP_READ)) {
                const map = object.mapAsync.bind(object);
                object.mapAsync = (mode, offset, size) => map(mode, offset + 1, size);
              }
              return object;
            };
            const done = device.queue.onSubmittedWorkDone.bind(device.queue);
            device.queue.onSubmittedWorkDone = (...args) => {
              const promise = done(...args);
              if (scenario === 'transfer-loss') { device.destroy(); }
              return scenario === 'slow-completion' ? promise.then(() => new Promise(resolve => setTimeout(resolve,30))) : promise;
            };
            return device;
          };
          return adapter;
        };
      }
      new MutationObserver(() => {
        const frames = Number(document.querySelector('#status')?.dataset.frames);
        if (frames === 5) window.__qaPause = true;
      }).observe(document,{attributes:true,subtree:true});
    },scenario);
    const page = await context.newPage(); lastPage = page;
    const errors = [];
    page.on('pageerror',error => errors.push(String(error)));
    page.on('console',message => report.console.push({scenario,type:message.type(),text:message.text()}));
    const variant = scenario.endsWith('-srgb') ? 'srgb' : scenario.endsWith('-blend') ? 'blend' : '';
    const selection = scenario.startsWith('webgpu') ? 'webgpu' : scenario.startsWith('webgl2') ? 'webgl2' : scenario.startsWith('invalid') || scenario === 'device-loss' || scenario === 'slow-completion' || scenario === 'transfer-loss' ? 'webgpu' : 'auto';
    await page.goto(`http://127.0.0.1:${server.address().port}/raster.html?backend=${selection}&variant=${variant}`);
    const result = {scenario};
    if (scenario.startsWith('invalid') || scenario === 'transfer-loss') {
      await page.waitForFunction(() => document.querySelector('#status').dataset.state === 'failed',null,{timeout:20000});
      assert.equal(await page.locator('#status').getAttribute('data-frames'),'0');
    } else {
      await page.waitForFunction(() => document.querySelector('#status').dataset.frames === '5',null,{timeout:20000});
      result.backend = await page.locator('#status').getAttribute('data-backend');
      if (scenario === 'auto-fallback' || scenario.startsWith('webgl2')) assert.equal(result.backend,'webgl2');
      else assert.equal(result.backend,'webgpu');
      if (scenario === 'device-loss') {
        await page.evaluate(() => {window.__qaDevice.destroy(); window.__qaPause=false;});
        await page.waitForFunction(() => document.querySelector('#status').dataset.state === 'failed',null,{timeout:20000});
      } else {
        const box = await page.locator('#canvas').boundingBox();
        const bytes = await page.screenshot({clip:{x:box.x,y:box.y,width:box.width,height:box.height}});
        const image = PNG.sync.read(bytes);
        assert.equal(image.width,96); assert.equal(image.height,64);
        const high = variant === 'srgb' ? 55 : variant === 'blend' ? 160 : 255;
        const low = variant === 'blend' ? 32 : 0;
        for (const [i,x] of [12,36,60,84].entries()) {
          const left = i%2 === 0;
          for (const [y,expected] of [[16,left?[high,low,low,255]:[low,high,low,255]],[48,left?[low,low,high,255]:[high,high,high,255]]]) {
            const offset = (y*image.width+x)*4;
            assert.deepEqual([...image.data.subarray(offset,offset+4)],expected,`${scenario} pixel ${x},${y}`);
          }
        }
        await writeFile(resolve(output,`${scenario}.png`),bytes);
        await page.evaluate(() => {window.__qaPause=false;});
        await page.waitForFunction(() => document.querySelector('#status').dataset.state === 'passed',null,{timeout:20000});
        assert.equal(await page.locator('#status').getAttribute('data-frames'),'120');
      }
    }
    assert.deepEqual(errors,[]); result.outcome='passed'; report.cases.push(result);
    await context.close();
  }
  report.outcome='passed';
} catch (error) {
  report.outcome='failed'; report.error=String(error);
  report.lastStatus = lastPage ? await lastPage.locator('#status').evaluate(node => ({text:node.textContent,...node.dataset})) : null;
  throw error;
} finally {
  await writeFile(resolve(output,'report.json'),JSON.stringify(report,null,2)+'\n');
  await browser?.close(); await new Promise(resolve => server.close(resolve));
}
