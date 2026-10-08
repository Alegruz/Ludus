import assert from 'node:assert/strict';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve, sep} from 'node:path';
import {createRequire} from 'node:module';
const require = createRequire(new URL('../../tools/web-browser-tests/package.json', import.meta.url));
const {chromium} = require('playwright');
const {PNG} = require('pngjs');
const root = resolve(process.argv[2]), output = resolve(process.argv[3] || 'out/renderer-browser');
await mkdir(output, {recursive:true});
const server = createServer(async (request,response) => {
  const path = resolve(root, '.' + new URL(request.url,'http://localhost').pathname);
  if (!path.startsWith(root + sep)) { response.writeHead(403).end(); return; }
  try { response.writeHead(200, {'Content-Type':path.endsWith('.wasm')?'application/wasm':path.endsWith('.js')?'text/javascript':'text/html'}).end(await readFile(path)); }
  catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
const args = ['--enable-unsafe-webgpu','--enable-features=Vulkan','--use-webgpu-adapter=swiftshader','--use-angle=swiftshader','--use-vulkan=swiftshader','--disable-vulkan-surface','--enable-unsafe-swiftshader'];
const report = {kind:'pinned Chromium software GPU; physical GPU qualification separate', cases:[]};
let browser;
try {
  browser = await chromium.launch({headless:true,args});
  const pin = JSON.parse(await readFile(new URL('../../config/shader_toolchain.json',import.meta.url))).wgsl_validator;
  assert.equal(browser.version(),pin.chromium_version);
  report.version = browser.version();
  let oracle;
  for (const scenario of ['webgpu','webgl2','auto','fallback']) {
    const images = [];
    for (const reference of [false,true]) {
      const context = await browser.newContext({viewport:{width:200,height:160},deviceScaleFactor:1});
      if (scenario === 'fallback') await context.addInitScript(() => Object.defineProperty(navigator,'gpu',{get:()=>undefined}));
      const page = await context.newPage();
      const errors = [];
      page.on('pageerror',error => errors.push(String(error)));
      page.on('console',message => {if (message.type() === 'error') errors.push(message.text());});
      await page.goto(`http://127.0.0.1:${server.address().port}/renderer.html?backend=${scenario === 'fallback'?'auto':scenario}${reference?'&reference=1':''}`);
      try { await page.waitForFunction(() => ['rendering','failed'].includes(document.querySelector('#status').dataset.state),{},{timeout:30000}); }
      catch (error) { throw new Error(JSON.stringify({scenario,reference,errors,state:await page.locator('#status').evaluate(n=>({...n.dataset}))}),{cause:error}); }
      const state = await page.locator('#status').evaluate(node => ({...node.dataset}));
      assert.equal(state.state,'rendering',JSON.stringify({scenario,state,errors}));
      assert.equal(state.frames,'5');
      assert.equal(state.culled,reference?'0':'1');
      assert.equal(state.backend,scenario==='fallback'?'webgl2':scenario==='auto'?'webgpu':scenario);
      assert.deepEqual(errors,[]);
      const png = await page.locator('#canvas').screenshot();
      await writeFile(resolve(output,`${scenario}-${reference?'reference':'optimized'}.png`),png);
      images.push(PNG.sync.read(png));
      await page.evaluate(() => {globalThis.__qaPause = false;});
      await page.waitForFunction(() => ['passed','failed'].includes(document.querySelector('#status').dataset.state),{},{timeout:30000});
      const final = await page.locator('#status').evaluate(node => ({...node.dataset}));
      assert.equal(final.state,'passed');
      assert.equal(final.frames,'120');
      assert.deepEqual(errors,[]);
      report.cases.push({scenario,reference,...final});
      await context.close();
    }
    assert.deepEqual(images[0].data,images[1].data,scenario + ' direct image oracle');
    if (oracle) assert.deepEqual(images[0].data,oracle,scenario + ' cross-backend oracle');
    else oracle = images[0].data;
    assert.equal(images[0].data[(20*96+24)*4],255,'red object');
    assert.equal(images[0].data[(20*96+72)*4+1],255,'green object');
    assert.equal(images[0].data[(45*96+48)*4+2],255,'error material');
    assert.deepEqual([...images[0].data.subarray((32*96+12)*4,(32*96+12)*4+4)],[25,26,52,255],'UNorm background');
    assert.deepEqual([...images[0].data.subarray((32*96+48)*4,(32*96+48)*4+4)],[70,134,13,255],'premultiplied painter overlays');
  }
  await writeFile(resolve(output,'report.json'),JSON.stringify(report,null,2));
  console.log('L0 renderer SDK: WebGPU/WebGL2/Auto/fallback match direct and cross-backend pixels over 120 frames');
} finally { await browser?.close(); server.close(); }
