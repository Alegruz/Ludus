// Only this probe's Node tests install a controlled DOM/WebGPU implementation.
// Real browser builds always call their own navigator.gpu through the C API port.
if (typeof window === 'undefined' && typeof process === 'object') {
  const scenario = process.argv[2] || 'success';
  const stats = {adapter:0, devices:0, destroyed:0, configured:0, unconfigured:0};
  const listeners = new Map();
  class Target {
    addEventListener(name, callback) { if (!listeners.has(this)) listeners.set(this,new Map()); const events=listeners.get(this); if (!events.has(name)) events.set(name,new Set()); events.get(name).add(callback); }
    removeEventListener(name, callback) { listeners.get(this)?.get(name)?.delete(callback); }
  }
  const status = {textContent:'',dataset:{}};
  const canvas = new Target();
  canvas.width=640;canvas.height=360;canvas.clientWidth=640;canvas.clientHeight=360;canvas.isConnected=true;
  canvas.getAttribute=()=>null;canvas.setAttribute=()=>{};canvas.removeAttribute=()=>{};
  const context = {canvas,configure(config) { if (!config.device || config.format!=='bgra8unorm') throw new Error('bad configuration'); ++stats.configured; },unconfigure() { ++stats.unconfigured; }};
  canvas.getContext=kind=>kind==='webgpu'?context:null;
  canvas.getBoundingClientRect=()=>({left:0,top:0,width:640,height:360});
  globalThis.window=new Target();
  globalThis.document=new Target();document.hidden=false;document.activeElement=null;
  document.querySelector=selector=>selector==='#canvas'?canvas:null;
  document.getElementById=id=>id==='status'?status:null;
  globalThis.HTMLCanvasElement=class {static [Symbol.hasInstance](value) { return value===canvas; }};
  globalThis.ResizeObserver=class {observe(){} disconnect(){}};
  globalThis.getComputedStyle=()=>({visibility:'visible',display:'block'});
  globalThis.devicePixelRatio=1;
  globalThis.GPUValidationError=class extends Error {};
  globalThis.GPUOutOfMemoryError=class extends Error {};
  globalThis.GPUInternalError=class extends Error {};
  const devices=[];
  function device() {
    let lose;
    const result={features:new Set(),limits:new Proxy({maxTextureDimension2D:4096,maxUniformBufferBindingSize:65536,maxBufferSize:268435456},{get:(o,k)=>o[k]??0}),queue:{},
      lost:new Promise(resolve=>{lose=resolve;}),pushErrorScope(){},popErrorScope(){return Promise.resolve(scenario==='surface-failure'?new GPUValidationError('configuration rejected'):null);},
      destroy(){if (!result.destroyed) {result.destroyed=true;++stats.destroyed;lose({reason:'destroyed',message:'test destroy'});}},
      testLose(){lose({reason:'unknown',message:'test loss'});}};
    ++stats.devices;devices.push(result);return result;
  }
  const adapter={features:new Set(),limits:{maxTextureDimension2D:4096,maxUniformBufferBindingSize:65536,maxBufferSize:268435456},requestDevice(desc){
    if (desc.requiredFeatures?.length || Object.keys(desc.requiredLimits||{}).length) throw new Error('optional requirements requested');
    if (scenario==='device-failure') return Promise.reject(new Error('device rejected'));
    return new Promise(resolve=>setTimeout(()=>resolve(device()),scenario==='cancel-device'?80:0));
  }};
  Object.defineProperty(globalThis,'navigator',{value:{gpu:{getPreferredCanvasFormat:()=> 'bgra8unorm',requestAdapter(){
    ++stats.adapter;
    if (scenario==='adapter-failure') return Promise.resolve(null);
    return new Promise(resolve=>setTimeout(()=>resolve(adapter),(scenario==='cancel-adapter'||(scenario==='stale-restart'&&stats.adapter===1))?80:0));
  }}}});
  let ticks=0, phase=0;
  const timer=setInterval(()=>{
    if (!Module._Stop || !status.dataset.state) return;
    const state=status.dataset.state;
    if (scenario==='stale-restart') {
      if (phase===0) {Module._Stop();Module._Restart();phase=1;}
      else if (state==='ready'&&phase===1) {phase=2;setTimeout(()=>{Module._Stop();finish(stats.devices===1&&stats.destroyed===1&&stats.configured===1,'stale callback did not alter restart');},150);}
      return;
    }
    if (scenario.startsWith('cancel-') && phase===0 && (scenario==='cancel-adapter'||stats.adapter>0)) {
      if (scenario==='cancel-device' && stats.devices===0 && ticks++<2) return;
      Module._Stop(); phase=1;
      setTimeout(()=>{
        if (status.dataset.state!=='idle' || stats.destroyed!==stats.devices || (scenario==='cancel-adapter'&&stats.devices!==0)) finish(false,'stale request changed state or leaked device');
        else finish(true,'cancelled pending request');
      },150);
      return;
    }
    if (scenario.endsWith('failure')) {
      if (state==='failed') {Module._Stop();finish(stats.destroyed===stats.devices && (scenario!=='adapter-failure'||stats.adapter===2),'request failure cleanup');}
      return;
    }
    if (scenario==='device-loss') {
      if (state==='ready' && phase===0) {phase=1;devices[0].testLose();}
      else if (state==='device lost') {Module._Stop();finish(true,'device loss');}
      return;
    }
    if (scenario==='validation') {
      if (state==='ready' && phase===0) {phase=1;devices[0].onuncapturederror({error:new GPUValidationError('test error')});}
      else if (state==='failed') {Module._Stop();finish(true,'validation error');}
      return;
    }
    if (state==='ready') {
      if (phase===0) {if (!status.textContent.includes('4096') || stats.configured!==1) return finish(false,'ready before setup');Module._Restart();phase=1;}
      else {Module._Stop();setTimeout(()=>finish(stats.destroyed===stats.devices && stats.unconfigured===stats.configured,'restart cleanup'),25);}
    }
  },10);
  function finish(ok,message) {clearInterval(timer);console.log('[W4:'+ (ok?'passed':'failed') +'] '+scenario+' '+message);process.exit(ok?0:1);}
  setTimeout(()=>finish(false,'timeout'),3000);
}
