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
  for (const phase of [0,1,2]) {
  let oracle, resizedOracle;
  for (const scenario of ['webgpu','webgl2','auto','fallback']) {
    const images = [], resizedImages = [];
    for (const reference of [false,true]) {
      const context = await browser.newContext({viewport:{width:200,height:160},deviceScaleFactor:1});
      if (scenario === 'fallback') await context.addInitScript(() => Object.defineProperty(navigator,'gpu',{get:()=>undefined}));
      const page = await context.newPage();
      const errors = [];
      page.on('pageerror',error => errors.push(String(error)));
      page.on('console',message => {if (message.type() === 'error') errors.push(message.text());});
      await page.goto(`http://127.0.0.1:${server.address().port}/${phase > 0 ? 'renderer-views' : 'renderer'}.html?backend=${scenario === 'fallback'?'auto':scenario}${reference?'&reference=1':''}${phase===2?'&fit=fill':''}`);
      try { await page.waitForFunction(() => ['rendering','failed'].includes(document.querySelector('#status').dataset.state),{},{timeout:30000}); }
      catch (error) { throw new Error(JSON.stringify({scenario,reference,errors,state:await page.locator('#status').evaluate(n=>({...n.dataset}))}),{cause:error}); }
      const state = await page.locator('#status').evaluate(node => ({...node.dataset}));
      assert.equal(state.state,'rendering',JSON.stringify({scenario,state,errors}));
      assert.equal(state.frames,'5');
      assert.equal(state.culled,reference?'0':phase > 0 ? '2':'1');
      assert.equal(state.backend,scenario==='fallback'?'webgl2':scenario==='auto'?'webgpu':scenario);
      assert.deepEqual(errors,[]);
      const png = await page.locator('#canvas').screenshot();
      await writeFile(resolve(output,`l${phase===2?'1-fill':phase}-${scenario}-${reference?'reference':'optimized'}.png`),png);
      images.push(PNG.sync.read(png));
      await page.evaluate(() => {globalThis.__qaPause = false;});
      if (phase > 0) {
        await page.waitForFunction(() => document.querySelector('#status').dataset.frames === '45' || document.querySelector('#status').dataset.state === 'failed', {}, {timeout:30000});
        const resized = await page.locator('#status').evaluate(n=>({...n.dataset}));
        assert.equal(resized.state,'rendering',JSON.stringify({scenario,reference,resized,errors}));
        assert.equal(resized.width,'120'); assert.equal(resized.height,'80');
        assert.equal(resized.zero,'1'); assert.equal(resized.resized,'1');
        const png = await page.locator('#canvas').screenshot();
        await writeFile(resolve(output,`l1-${phase===2?'fill-':''}resized-${scenario}-${reference?'reference':'optimized'}.png`),png);
        resizedImages.push(PNG.sync.read(png));
        await page.evaluate(() => {globalThis.__qaPause = false;});
      }
      await page.waitForFunction(() => ['passed','failed'].includes(document.querySelector('#status').dataset.state),{},{timeout:30000});
      const final = await page.locator('#status').evaluate(node => ({...node.dataset}));
      assert.equal(final.state,'passed');
      assert.equal(final.frames,'120');
      assert.deepEqual(errors,[]);
      report.cases.push({phase,scenario,reference,...final});
      await context.close();
    }
    assert.deepEqual(images[0].data,images[1].data,scenario + ' direct image oracle');
    if (oracle) assert.deepEqual(images[0].data,oracle,scenario + ' cross-backend oracle');
    else oracle = images[0].data;
    if (phase === 0) {
    assert.equal(images[0].data[(20*96+24)*4],255,'red object');
    assert.equal(images[0].data[(20*96+72)*4+1],255,'green object');
    assert.equal(images[0].data[(45*96+48)*4+2],255,'error material');
    assert.deepEqual([...images[0].data.subarray((32*96+12)*4,(32*96+12)*4+4)],[25,26,52,255],'UNorm background');
    assert.deepEqual([...images[0].data.subarray((32*96+48)*4,(32*96+48)*4+4)],[70,134,13,255],'premultiplied painter overlays');
    } else {
      const check = (image,width,height) => {
        assert.equal(image.width,width); assert.equal(image.height,height);
        const pane = width/2, content = phase===2 ? height : pane, bar = phase===2 ? 0 : (height-pane)/2;
        const color = (x,y,expected,label) => {
          const offset=(y*width+x)*4;
          assert.deepEqual([...image.data.subarray(offset,offset+4)],expected,label);
        };
        for (const x of [pane/2,pane+pane/2]) color(x,height/2,[255,0,0,255],'near red survives later far green');
        color(4,bar+4,[0,255,0,255],'orthographic corner');
        color(pane+4,bar+4,[0,0,255,255],'perspective corner');
        for (const x of [Math.floor(pane/2+content*.325),pane+Math.floor(pane/2+content*.325)]) color(x,bar+Math.floor(content*.175),[255,0,255,255],'top-right overlay / top-left sampled UV');
        if (phase===2) { color(pane/2,2,[0,255,0,255],'Fill has no bars'); color(pane+pane/2,2,[0,0,255,255],'Fill perspective top'); }
        else for (const x of [pane/2,pane+pane/2]) color(x,2,[0,0,0,255],'letterbox bar');
        if (phase===2) color(Math.floor(pane/2-content*.3125),height/2,[0,255,0,255],'Fill crops once');
      };
      check(images[0],96,64);
      assert.deepEqual(resizedImages[0].data,resizedImages[1].data,scenario+' resized direct image oracle');
      if (resizedOracle) assert.deepEqual(resizedImages[0].data,resizedOracle,scenario+' resized cross-backend oracle');
      else resizedOracle=resizedImages[0].data;
      check(resizedImages[0],120,80);
    }
  }
  }

  let overlayOracle;
  for (const scenario of ['webgpu','webgl2','auto','fallback']) {
    const context = await browser.newContext({viewport:{width:200,height:160},deviceScaleFactor:1});
    if (scenario === 'fallback') await context.addInitScript(() => Object.defineProperty(navigator,'gpu',{get:()=>undefined}));
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror',error=>errors.push(String(error)));
    page.on('console',message=>{if(message.type()==='error') errors.push(message.text());});
    await page.goto(`http://127.0.0.1:${server.address().port}/renderer-overlays.html?backend=${scenario==='fallback'?'auto':scenario}`);
    await page.waitForFunction(()=>['rendering','failed'].includes(document.querySelector('#status').dataset.state),{},{timeout:30000});
    const state = await page.locator('#status').evaluate(n=>({...n.dataset}));
    assert.equal(state.state,'rendering',JSON.stringify({state,errors}));
    assert.equal(state.frames,'5');
    assert.equal(state.backend,scenario==='fallback'?'webgl2':scenario==='auto'?'webgpu':scenario);
    assert.deepEqual(errors,[]);
    const png = await page.locator('#canvas').screenshot();
    await writeFile(resolve(output,`l2-${scenario}.png`),png);
    const image = PNG.sync.read(png);
    if(overlayOracle) assert.deepEqual(image.data,overlayOracle,'L2 cross-backend painter/coverage image');
    else overlayOracle=image.data;
    const color=(x,y,expected)=>assert.deepEqual([...image.data.subarray((y*96+x)*4,(y*96+x)*4+4)],expected,`L2 pixel ${x},${y}`);
    color(8,48,[255,0,0,255]); color(24,48,[128,128,0,255]); color(40,48,[0,0,0,255]);
    color(80,4,[255,255,255,255]); color(80,28,[0,0,0,255]); color(60,16,[0,0,255,255]);
    color(72,32,[255,0,0,255]); color(24,26,[0,0,0,255]);
    await page.evaluate(()=>{globalThis.__qaPause=false;});
    await page.waitForFunction(()=>['passed','failed'].includes(document.querySelector('#status').dataset.state),{},{timeout:30000});
    const final=await page.locator('#status').evaluate(n=>({...n.dataset}));
    assert.equal(final.state,'passed',JSON.stringify({final,errors})); assert.equal(final.frames,'120'); assert.deepEqual(errors,[]);
    report.cases.push({phase:3,scenario,...final});
    await context.close();
  }
  await writeFile(resolve(output,'report.json'),JSON.stringify(report,null,2));
  console.log('L0/L1/L2 renderer SDK: WebGPU/WebGL2/Auto/fallback match direct and cross-backend pixels over 120 frames, including split views, zero extent and resize');
} finally { await browser?.close(); server.close(); }
