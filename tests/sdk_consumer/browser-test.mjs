// Real browser checks through the installed C++ SDK; injected failures wrap real
// WebGPU objects. This harness is a development tool and is never packaged.
import assert from 'node:assert/strict';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve, sep} from 'node:path';
import {createRequire} from 'node:module';
const require = createRequire(new URL('../../tools/web-browser-tests/package.json', import.meta.url));
const {chromium} = require('playwright');
const {PNG} = require('pngjs');
const root = resolve(process.argv[2] || 'out/build/render-api-sdk-web');
const output = resolve(process.argv[3] || 'out/render-api-browser');
const pin = JSON.parse(await readFile(new URL('../../config/shader_toolchain.json', import.meta.url))).wgsl_validator;
await mkdir(output, {recursive:true});
const server = createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  const path = resolve(root, '.' + (pathname === '/' ? '/index.html' : pathname));
  if (!path.startsWith(root + sep)) {response.writeHead(403).end(); return;}
  try {response.writeHead(200, {'Content-Type':path.endsWith('.wasm') ? 'application/wasm' : path.endsWith('.js') ? 'text/javascript' : 'text/html'}).end(await readFile(path));}
  catch {response.writeHead(404).end();}
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const args = process.env.LUDUS_SHADER_PROBE_SOFTWARE === '1' ? ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-webgpu-adapter=swiftshader', '--use-angle=swiftshader', '--use-vulkan=swiftshader', '--disable-vulkan-surface'] : [];
const report = {kind:args.length ? 'real software GPU; hardware acceptance open' : 'default browser adapter', args, cases:[]};
let browser; let lastPage;
report.console=[];
try {
  browser = await chromium.launch({headless:true, args});
  assert.equal(browser.version(), pin.chromium_version);
  report.version = browser.version();
  for (const scenario of ['success', 'cancel-pending', 'invalid-shader', 'invalid-pipeline', 'device-loss']) {
    const context = await browser.newContext({viewport:{width:200,height:200}, deviceScaleFactor:1});
    await context.addInitScript(scenario => {
      window.__qaPause = false;
      window.__qaStats = {pipelines:0,buffers:0,submits:0,writes:0,devices:0};
      window.Module = {arguments:scenario === 'cancel-pending' ? ['cancel-pending'] : []};
      if (!navigator.gpu) return;
      const requestAdapter = navigator.gpu.requestAdapter.bind(navigator.gpu);
      navigator.gpu.requestAdapter = async (...args) => {
        const adapter = await requestAdapter(...args);
        if (!adapter) return adapter;
        const requestDevice = adapter.requestDevice.bind(adapter);
        adapter.requestDevice = async (...args) => {
          const device = await requestDevice(...args); ++window.__qaStats.devices;
          window.__qaDevice = device;
          for (const [name, counter] of [['createRenderPipeline','pipelines'], ['createBuffer','buffers']]) {
            const create = device[name].bind(device);
            device[name] = desc => {++window.__qaStats[counter]; return create(name === 'createRenderPipeline' && scenario === 'invalid-pipeline' ? {...desc, vertex:{...desc.vertex, entryPoint:'missing'}} : desc);};
          }
          const shader = device.createShaderModule.bind(device);
          device.createShaderModule = desc => shader(scenario === 'invalid-shader' ? {...desc, code:'invalid WGSL'} : desc);
          const scope = device.popErrorScope.bind(device);
          device.popErrorScope = () => {
            const promise = scope();
            return scenario === 'cancel-pending' ? promise.then(result => new Promise(resolve => setTimeout(() => resolve(result), 100))) : promise;
          };
          for (const [name,counter] of [['submit','submits'],['writeBuffer','writes']]) {
            const invoke = device.queue[name].bind(device.queue);
            device.queue[name] = (...args) => {++window.__qaStats[counter]; return invoke(...args);};
          }
          return device;
        };
        return adapter;
      };
      new MutationObserver(() => {
        const frames = Number(document.querySelector('#status')?.dataset.frames);
        if ([1,5,9,13,17,21,25,29].includes(frames)) window.__qaPause = true;
      }).observe(document, {attributes:true,subtree:true});
    }, scenario);
    const page = await context.newPage();
    lastPage = page;
    page.on("console", message => report.console.push({scenario,type:message.type(),text:message.text()}));
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    const result = {scenario, pixels:[]};
    if (scenario === 'success' || scenario === 'cancel-pending') {
      for (const frame of [1,5,9,13,17,21,25,29]) {
        await page.waitForFunction(frame => document.querySelector('#status').dataset.frames === String(frame), frame, {timeout:15000,polling:10});
        const bytes = await page.locator('#canvas').screenshot();
        const image = PNG.sync.read(bytes); const elapsed = (frame - 1) % 8 < 4 ? 0 : 2;
        let maximum = 0;
        for (let y = 0; y < image.height; ++y) for (let x = 0; x < image.width; ++x) {
          const cx = (x + .5 - image.width / 2) / image.height, cy = (y + .5 - image.height / 2) / image.height;
          const expected = [.6*(x+.5)/image.width+.01*elapsed, .4*(y+.5)/image.height+.02*elapsed, .2*(cx*cx+cy*cy<.04?1:0)+.03*elapsed, 1];
          for (let c = 0; c < 4; ++c) maximum = Math.max(maximum, Math.abs(image.data[(y*image.width+x)*4+c] - Math.round(expected[c]*255)));
        }
        assert(maximum <= 2, `frame ${frame}: maximum error ${maximum}`);
        result.pixels.push({frame,width:image.width,height:image.height,elapsed,maximum});
        await writeFile(resolve(output, `${scenario}-${frame}.png`), bytes);
        await page.evaluate(() => {window.__qaPause=false;});
      }
      await page.waitForFunction(() => document.querySelector('#status').dataset.state === 'passed',null,{polling:10});
      assert.equal(await page.locator('#status').getAttribute('data-frames'), '32');
      result.stats = await page.evaluate(() => window.__qaStats);
      assert.equal(result.stats.pipelines, 2); assert.equal(result.stats.buffers, scenario === 'cancel-pending' ? 3 : 2);
      assert.equal(result.stats.submits,32); assert.equal(result.stats.writes,32);
      assert.equal(result.stats.devices,scenario === 'cancel-pending' ? 3 : 2);
    } else {
      if (scenario === 'device-loss') {
        await page.waitForFunction(() => document.querySelector('#status').dataset.frames === '1',null,{polling:10});
        await page.evaluate(() => {window.__qaDevice.destroy(); window.__qaPause=false;});
      }
      await page.waitForFunction(() => document.querySelector('#status').dataset.state === 'failed', null, {timeout:15000,polling:10});
      result.stats = await page.evaluate(() => window.__qaStats);
      if (scenario !== 'device-loss') assert.equal(result.stats.submits,0);
    }
    assert.deepEqual(errors,[]); result.outcome = 'passed'; report.cases.push(result);
    await context.close();
  }
  report.outcome='passed';
} catch (error) {report.outcome='failed'; report.error=String(error); report.lastStatus = lastPage ? await lastPage.locator('#status').evaluate(element => ({text:element.textContent,...element.dataset})) : null; throw error;}
finally {await writeFile(resolve(output,'report.json'), JSON.stringify(report,null,2)+'\n'); await browser?.close(); await new Promise(resolve => server.close(resolve));}
