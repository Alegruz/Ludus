// Real designer journey through the S3 workbench and its owned native Luau VM.
import {chromium} from '../web-browser-tests/node_modules/playwright/index.mjs';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {readFile,writeFile,mkdir,mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {resolve,join} from 'node:path';
import assert from 'node:assert/strict';
const root=resolve(new URL('../..',import.meta.url).pathname);
const temporary=await mkdtemp(join(tmpdir(),'ludus-s3-browser-'));
const doc=join(temporary,'encounter.json');
await writeFile(doc,await readFile(join(root,'tools/visual-sequence/fixtures/encounter.json')));
const executable=resolve(process.argv[2]||join(root,'out/build/linux-clang-development/tools/visual-sequence/ludus_visual_sequence'));
const python=resolve(process.argv[3]||join(root,'out/host-tools/venv/bin/python'));
const child=spawn(python,['-c',`import sys;sys.path.insert(0,'tools/visual-sequence');import server;server.serve(sys.argv[1],sys.argv[2],__import__('pathlib').Path(sys.argv[3]))`,doc,executable,join(temporary,'cache')],{cwd:root,stdio:['ignore','pipe','pipe']});
let stderr='';child.stderr.on('data',data=>{stderr=(stderr+data).slice(-16384);});
const exit=new Promise(done=>child.once('exit',(code,signal)=>done({code,signal})));
let browser, testPage;
try {
  const lines=createInterface({input:child.stdout});
  const ready=await Promise.race([new Promise(done=>lines.once('line',done)),exit.then(()=>{throw Error('Server exited: '+stderr);}),new Promise((_,reject)=>setTimeout(()=>reject(Error('Server startup deadline')),15000))]);
  const {url}=JSON.parse(ready);assert.match(url,/^http:\/\/127\.0\.0\.1:\d+$/);
  browser=await chromium.launch({headless:true,args:['--no-sandbox']});
  const page=testPage=await browser.newPage({viewport:{width:1500,height:1000}});
  const errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.goto(url);
  await page.locator('#nodes .node').first().waitFor();
  assert.equal(await page.locator('#palette').evaluate(element=>element.scrollWidth<=element.clientWidth),true);
  const snapshot=()=>page.evaluate(async()=>{const reply=await fetch('/action',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+document.querySelector('meta[name=session]').content},body:JSON.stringify({action:'state'})});return (await reply.json()).state;});
  const waitReady=()=>page.waitForFunction(()=>document.body.getAttribute('aria-busy')==='false'&&!document.getElementById('message').classList.contains('error'));
  await page.locator('#cook').click();await page.waitForFunction(()=>document.getElementById('running-status').textContent.startsWith('Running revision 1'));
  await page.locator('#door0').click();await page.waitForFunction(()=>document.querySelector('#runtime .state').textContent.includes('1 interactions'));
  const condition='0000000000000030';
  await page.locator(`#nodes .node[data-node="${condition}"] .node-title`).click();
  await page.locator('#breakpoint').click();await waitReady();
  await page.locator('#door0').click();await page.waitForFunction(()=>document.getElementById('running-status').textContent.startsWith('Paused'));
  const paused=await snapshot();assert.equal(paused.preview.runtime.states[0].interactions,1);
  assert.equal(paused.preview.runtime.partial,true);assert.equal(await page.locator('#cook').isDisabled(),true);
  await page.evaluate(()=>{window.s3Heartbeat=0;window.s3Timer=setInterval(()=>window.s3Heartbeat++,10);});
  await new Promise(done=>setTimeout(done,100));assert.ok(await page.evaluate(()=>window.s3Heartbeat)>5);
  const still=await snapshot();assert.equal(still.preview.runtime.tick,paused.preview.runtime.tick);
  assert.equal(await page.locator(`#nodes .node[data-node="${condition}"]`).getAttribute('class').then(c=>c.includes('paused')),true);
  await page.locator('#over').click();await page.waitForFunction(()=>document.getElementById('debugger').textContent.includes('DoorOpen'));
  await page.locator('#continue').click();await page.waitForFunction(()=>document.querySelector('#runtime .state').textContent.includes('Open'));
  assert.equal((await snapshot()).preview.runtime.partial,false);
  const threshold=page.getByLabel('Threshold '+condition,{exact:true});
  await threshold.fill('3');await threshold.press('Enter');await page.waitForFunction(()=>document.getElementById('document-status').textContent.includes('Unsaved'));
  await page.locator('#review').click();await page.waitForFunction(()=>document.getElementById('changes').textContent.includes('threshold'));
  await page.locator('#cook').click();await page.waitForFunction(()=>document.getElementById('running-status').textContent.startsWith('Running revision 2'));
  assert.equal((await snapshot()).preview.runtime.states[0].interactions,2);
  for(let i=1;i<=3;i++){await page.locator('#door1').click();await page.waitForFunction(count=>document.querySelectorAll('#runtime .state')[1].textContent.includes(count+' interactions'),i);}
  assert.deepEqual((await snapshot()).preview.open,[true,true]);
  const beforeMove=await snapshot();
  const title=page.locator(`#nodes .node[data-node="${condition}"] .node-title`);const box=await title.boundingBox();
  await page.mouse.move(box.x+30,box.y+15);await page.mouse.down();await page.mouse.move(box.x+95,box.y+35,{steps:8});await page.mouse.up();
  await page.waitForFunction(revision=>document.getElementById('document-status').textContent.includes('revision '+(revision+1)),beforeMove.revision);
  await page.locator('#cook').click();await waitReady();
  const moved=await snapshot();assert.equal(moved.semantic,beforeMove.semantic);assert.equal(moved.preview.runtime.package,beforeMove.preview.runtime.package);assert.equal(moved.preview.runtime.revision,2);
  // Paste allocates fresh stable IDs, and undo restores the exact semantic hash.
  await page.locator('#copy').click();await page.locator('#nodes .node[data-node="0000000000000010"] .node-title').click();await page.locator('#paste').click();
  await page.waitForFunction(()=>document.querySelectorAll('#nodes .node').length===6);
  const pasted=await snapshot();assert.equal(new Set(pasted.document.nodes.map(n=>n.id)).size,6);
  await page.locator('#undo').click();await page.waitForFunction(()=>document.querySelectorAll('#nodes .node').length===4);
  assert.equal((await snapshot()).semantic,moved.semantic);
  await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('document-status').textContent.startsWith('Saved'));
  assert.equal(JSON.parse(await readFile(doc,'utf8')).nodes.find(n=>n.id===condition).threshold,3);
  // The file picker merges independent semantic edits by stable identity.
  const remote=JSON.parse(await readFile(doc,'utf8'));
  remote.nodes.find(n=>n.id===condition).threshold=4;
  await page.getByLabel('Amount 0000000000000020',{exact:true}).selectOption('2');await waitReady();
  await page.locator('#merge-file').setInputFiles({name:'branch.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(remote))});await waitReady();
  const merged=await snapshot();
  assert.equal(merged.document.nodes.find(n=>n.id===condition).threshold,4);
  assert.equal(merged.document.nodes.find(n=>n.id==='0000000000000020').amount,2);
  assert.equal(merged.preview.runtime.package,moved.preview.runtime.package);
  await page.locator('#undo').click();await waitReady();await page.locator('#undo').click();await waitReady();
  assert.equal((await snapshot()).dirty,false);
  // Closed HTTP boundary: no token/foreign Origin cannot author or load bytes.
  const denied=await fetch(url+'/action',{method:'POST',headers:{'Content-Type':'application/json',Origin:'https://example.invalid'},body:'{"action":"state"}'});assert.equal(denied.status,403);
  const currentRevision=(await snapshot()).revision;
  const endpoint=await page.evaluate(async revision=>{const response=await fetch('/action',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+document.querySelector('meta[name=session]').content},body:JSON.stringify({action:'load',revision,artifact:{code:'ff'}})});return {status:response.status,body:await response.json()};},currentRevision);assert.equal(endpoint.status,400);assert.match(endpoint.body.error,/unsupported authoring action/);
  await mkdir(join(root,'out/browser-qa/results'),{recursive:true});
  await page.screenshot({path:join(root,'out/browser-qa/results/visual-s3-workbench.png'),fullPage:true});
  await writeFile(join(root,'out/browser-qa/results/visual-s3.json'),JSON.stringify({pass:true,nodeStops:true,heartbeat:true,changedBehavior:true,layoutNoRecook:true,undoFreshIds:true,semanticMerge:true,save:true,pageErrors:errors},null,2)+'\n');
  assert.deepEqual(errors,[]);
  await page.locator('#restart').click();await page.waitForFunction(()=>document.getElementById('running-status').textContent==='Preview stopped');
  console.log('S3 Chromium author, review, debug, replace, undo, layout, save and retirement PASS');
} catch(error) {
  if(testPage){console.error(await testPage.evaluate(()=>({message:document.getElementById('message')?.textContent,status:document.getElementById('document-status')?.textContent,selection:document.getElementById('selection')?.textContent,busy:document.body.getAttribute('aria-busy')})));await mkdir(join(root,'out/browser-qa/results'),{recursive:true});await testPage.screenshot({path:join(root,'out/browser-qa/results/visual-s3-failure.png'),fullPage:true});}
  throw error;
} finally {
  if(browser)await browser.close();
  child.kill('SIGINT');
  const result=await Promise.race([exit,new Promise(done=>setTimeout(()=>{child.kill('SIGKILL');done({code:-1,signal:'timeout'});},10000))]);
  await rm(temporary,{recursive:true,force:true});
  assert.equal(result.code,0,stderr);
}
