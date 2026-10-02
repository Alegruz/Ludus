// Reuse the repository's integrity-locked Playwright tool; never ship this runner.
import {chromium} from '../web-browser-tests/node_modules/playwright/index.mjs';
import {createServer} from 'node:http';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve,sep} from 'node:path';
import assert from 'node:assert/strict';
const root=resolve(process.argv[2]||'out/build/linux-clang-development/tools/shader-probe/generated');
const output=resolve(process.argv[3]||'out/shader-probe-browser');
await mkdir(output,{recursive:true});
const software=process.env.LUDUS_SHADER_PROBE_SOFTWARE==='1';
const flags=software ? ['--enable-unsafe-webgpu','--enable-features=Vulkan',
  '--use-webgpu-adapter=swiftshader','--use-angle=swiftshader',
  '--use-vulkan=swiftshader','--disable-vulkan-surface'] : [];
const server=createServer(async(req,res)=>{
  const pathname=new URL(req.url,'http://localhost').pathname;
  const file=resolve(root,'.'+(pathname==='/'?'/index.html':pathname));
  if (!file.startsWith(root+sep)) {res.writeHead(403).end();return;}
  try {res.writeHead(200,{'Content-Type':file.endsWith('.js')?'text/javascript':
    file.endsWith('.html')?'text/html':'text/plain'}).end(await readFile(file));}
  catch {res.writeHead(404).end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
const evidence={kind:software?'real software GPU; no hardware acceptance':'default browser adapter',flags,
  wgslSha256:createHash('sha256').update(await readFile(resolve(root,'probe.wgsl'))).digest('hex'),
  errors:[]};
try {
  browser=await chromium.launch({headless:true,args:flags});
  evidence.browserVersion=browser.version();
  assert.equal(evidence.browserVersion,'140.0.7339.186','Pinned Chromium required');
  const page=await browser.newPage();
  page.on('pageerror',e=>evidence.errors.push(String(e)));
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(()=>window.shaderProbe?.state!=='pending',{},{timeout:30000});
  evidence.report=await page.evaluate(()=>window.shaderProbe);
  assert.equal(evidence.report.state,'passed',JSON.stringify(evidence.report));
  assert.equal(evidence.report.cases.length,8);
  await page.waitForFunction(()=>window.shaderProbe.frames>=5);
  await page.screenshot({path:resolve(output,'probe.png')});
  evidence.report=await page.evaluate(()=>window.shaderProbe);
  assert.equal(evidence.report.errors.length,0);
  assert.equal(evidence.errors.length,0);
  if (software) assert.match(JSON.stringify(evidence.report.adapter),/swiftshader/i);
  evidence.outcome='passed';
} catch(error) {evidence.outcome='failed';evidence.failure=String(error);process.exitCode=1;}
finally {
  await writeFile(resolve(output,'report.json'),JSON.stringify(evidence,null,2)+'\n');
  console.log(JSON.stringify(evidence,null,2));
  await browser?.close();server.close();
}
