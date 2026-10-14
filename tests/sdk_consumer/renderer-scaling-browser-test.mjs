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
  try {
    const body = await readFile(path);
    response.writeHead(200, {'Content-Type':path.endsWith('.wasm')?'application/wasm':path.endsWith('.js')?'text/javascript':'text/html'}).end(body);
  }
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
  for (const scenario of [0,1,2]) {
    for (const ldr of [true,false]) {
      let oracle;
      for (const backend of ['webgpu','webgl2','auto','fallback']) {
        const images = [];
        for (const reference of [false,true]) {
          console.log(`L5 ${scenario} ${backend} ${ldr?'ldr':'hdr'} ${reference?'reference':'clustered'} starting`);
          const context = await browser.newContext({viewport:{width:200,height:160},deviceScaleFactor:1});
          if (backend === 'fallback') await context.addInitScript(() => Object.defineProperty(navigator,'gpu',{get:()=>undefined}));
          const page = await context.newPage();
          const errors = [];
          page.on('pageerror',e => errors.push(String(e)));
          page.on('console',m => {if (m.type() === 'error') errors.push(m.text());});
          await page.goto(`http://127.0.0.1:${server.address().port}/renderer-scaling.html?scenario=${scenario}&backend=${backend === 'fallback'?'auto':backend}${reference?'&reference=1':''}${ldr?'&ldr=1':''}`);
          console.log('L5 page loaded');
          for (const frame of ['5','80']) {
            try { await page.waitForFunction(frame => document.querySelector('#status').dataset.frames === frame || document.querySelector('#status').dataset.state === 'failed',frame,{timeout:30000}); }
            catch (error) { throw new Error(JSON.stringify({scenario,backend,reference,ldr,frame,state:await page.locator('#status').evaluate(n=>({...n.dataset})),errors}),{cause:error}); }
            const state = await page.locator('#status').evaluate(n => ({...n.dataset}));
            assert.equal(state.state,'rendering',JSON.stringify({scenario,backend,reference,state,errors}));
            if (frame === '5') {
              images.push(PNG.sync.read(await page.locator('#canvas').screenshot()));
              await writeFile(resolve(output,`l5-${scenario}-${backend}-${ldr?'ldr':'hdr'}-${reference?'reference':'clustered'}.png`),PNG.sync.write(images.at(-1)));
            }
            await page.evaluate(() => {globalThis.__qaPause = false;});
          }
          await page.waitForFunction(() => ['passed','failed'].includes(document.querySelector('#status').dataset.state),{},{timeout:30000});
          const final = await page.locator('#status').evaluate(n => ({...n.dataset}));
          assert.equal(final.state,'passed'); assert.equal(final.frames,'120');
          assert.equal(final.backend,backend==='fallback'?'webgl2':backend==='auto'?'webgpu':backend);
          assert.deepEqual(errors,[]);
          report.cases.push({scenario,backend,reference,ldr,...final});
          console.log(`L5 ${scenario} ${backend} ${ldr?'ldr':'hdr'} ${reference?'reference':'clustered'} passed`);
          await context.close();
        }
        const compare = (a,b) => {
          assert.equal(a.data.length,b.data.length);
          for (let i=0;i<a.data.length;i++) assert.ok(Math.abs(a.data[i]-b.data[i])<=1,`pixel ${i}`);
        };
        compare(images[0],images[1]);
        if (oracle) compare(images[0],oracle); else oracle=images[0];
        assert.ok(images[0].data.some((v,i) => i%4===0 && v>20),'nonblank lighting');
        if (scenario === 1) { const reds = images[0].data.filter((v,i)=>i%4===0); const peak = Math.max(...reds); assert.ok(peak>130 && peak<170,'all 128 directional lights contribute'); }
      }
    }
  }
  await writeFile(resolve(output,'report.json'),JSON.stringify(report,null,2));
  console.log(`L5 renderer browser: ${report.cases.length} cases passed`);
} catch (error) {
  // Report the original failure before browser shutdown can block on a wedged GPU.
  console.error(error);
  throw error;
} finally {
  await browser?.close();
  await new Promise(resolve => server.close(resolve));
}
