// Explicit test provider: real browsers always use their own DOM/navigator.gpu.
if (typeof window === 'undefined' && typeof process === 'object') {
  const scenario = process.argv[2] || 'success';
  const stats = {devices: 0, destroyed: 0, configured: 0, unconfigured: 0, acquired: 0, submitted: 0, drawn: 0, pipelines: 0};
  const colors = new Set();
  const status = {textContent: '', dataset: {}};
  const listeners = new Map();
  class Target {
    addEventListener(name, callback) {if (!listeners.has(this)) listeners.set(this, new Map()); const events = listeners.get(this); if (!events.has(name)) events.set(name, new Set()); events.get(name).add(callback);}
    removeEventListener(name, callback) {listeners.get(this)?.get(name)?.delete(callback);}
    dispatch(type, values = {}) {const event = {type, preventDefault() {}, ...values}; for (const callback of listeners.get(this)?.get(type) || []) callback(event);}
  }
  const canvas = new Target();
  Object.assign(canvas, {width: 640, height: 360, clientWidth: 640, clientHeight: 360, isConnected: true});
  canvas.getAttribute = () => null; canvas.setAttribute = () => {}; canvas.removeAttribute = () => {};
  canvas.getBoundingClientRect = () => ({left: 0, top: 0, width: canvas.clientWidth, height: canvas.clientHeight});
  let configured = false;
  const context = {canvas,
    configure(config) {
      assert(config.device && config.format === 'bgra8unorm', 'surface configuration');
      ++stats.configured; configured = true;
    },
    unconfigure() { ++stats.unconfigured; configured = false; },
    getCurrentTexture() {
      assert(configured && canvas.width > 0 && canvas.height > 0, 'acquisition while hidden/unconfigured');
      if (scenario === 'acquisition-failure') throw new Error('injected acquisition failure');
      ++stats.acquired;
      return {kind: 'frame texture', createView() {return {kind: 'frame view'};}};
    }
  };
  canvas.focus = () => {document.activeElement = canvas; canvas.dispatch('focus');};
  canvas.setPointerCapture = () => {};
  canvas.getContext = kind => kind === 'webgpu' ? context : null;
  globalThis.window = new Target();
  globalThis.document = new Target(); document.hidden = false; document.activeElement = null;
  document.querySelector = selector => selector === '#canvas' ? canvas : null;
  document.getElementById = id => id === 'status' ? status : null;
  globalThis.HTMLCanvasElement = class {static [Symbol.hasInstance](value) {return value === canvas;}};
  globalThis.ResizeObserver = class {observe() {} disconnect() {}};
  globalThis.getComputedStyle = () => ({visibility: 'visible', display: 'block'});
  globalThis.devicePixelRatio = 1;
  const realNow = performance.now.bind(performance); let timeJump = 0;
  Object.defineProperty(performance, 'now', {value: () => realNow() + timeJump});
  globalThis.requestAnimationFrame = callback => setTimeout(() => callback(performance.now()), 8);
  globalThis.GPUValidationError = class extends Error {};
  globalThis.GPUOutOfMemoryError = class extends Error {};
  globalThis.GPUInternalError = class extends Error {};
  let currentDevice;
  function device() {
    let lose;
    let scopes = 0;
    const result = {
      features: new Set(), limits: new Proxy({maxTextureDimension2D: 4096, maxUniformBufferBindingSize: 65536, maxBufferSize: 268435456}, {get: (o, k) => o[k] ?? 0}),
      queue: {
        submit(commands) {assert(commands.length === 1 && commands[0].ended, 'submit unfinished pass'); ++stats.submitted;},
        writeBuffer(buffer, offset, data, dataOffset, size) {
          assert(buffer.kind === 'uniform' && offset === 0 && size === 48, 'uniform upload contract');
          const bytes = new DataView(data.buffer, data.byteOffset + dataOffset, size);
          colors.add(bytes.getFloat32(8, true));
        }
      },
      createBuffer(desc) {assert(desc.size === 48, 'uniform buffer size'); return {kind:'uniform',destroy(){}};},
      createBindGroupLayout(desc) {assert(desc.entries.length === 1 && desc.entries[0].buffer.minBindingSize === 48, 'uniform layout'); return {kind:'bindings'};},
      createPipelineLayout(desc) {assert(desc.bindGroupLayouts.length === 1, 'pipeline layout'); return {kind:'layout'};},
      createBindGroup(desc) {assert(desc.entries.length === 1 && desc.entries[0].resource.buffer.kind === 'uniform', 'bind group'); return {kind:'group'};} ,
      lost: new Promise(resolve => {lose = resolve;}),
      pushErrorScope() {},
      popErrorScope() {
        const failure = (++scopes === 1 && scenario === 'surface-failure') || (scopes === 2 && scenario === 'shader-failure');
        return new Promise(resolve => setTimeout(() => resolve(failure ? new GPUValidationError('injected validation') : null), scenario === 'cancel-pipeline' && scopes === 5 ? 100 : 0));
      },
      createShaderModule(desc) {assert(desc.code.includes('@vertex') && desc.code.includes('@fragment'), 'WGSL missing'); return {kind: 'shader'};},
      createRenderPipeline(desc) {assert(desc.vertex.module && desc.fragment.targets[0].format === 'bgra8unorm', 'pipeline descriptor'); ++stats.pipelines; return {kind: 'pipeline'};},
      createCommandEncoder() {
        let ended = false;
        return {kind: 'frame encoder',
          beginRenderPass(desc) {
            assert(desc.colorAttachments.length === 1 && desc.colorAttachments[0].loadOp === 'clear', 'clear pass');
            assert(desc.colorAttachments[0].clearValue.r === 0, 'clear remains independent of animation uniforms');
            let bound = false;
            return {kind: 'frame pass',
              setViewport(x, y, w, h) {assert(x === 0 && y === 0 && w === canvas.width && h === canvas.height, 'invalid fullscreen viewport');},
              setBindGroup(index, group) {assert(index === 0 && group.kind === 'group', 'uniform binding');},
              setPipeline(pipeline) {assert(pipeline.kind === 'pipeline', 'wrong pipeline'); bound = true;},
              draw(vertices, instances) {assert(bound && vertices === 3 && instances === 1, 'triangle draw'); ++stats.drawn;},
              end() {ended = true;}
            };
          },
          finish() {assert(ended, 'unfinished pass'); return {kind: 'frame commands', ended};}
        };
      },
      destroy() {if (!result.destroyed) {result.destroyed = true; ++stats.destroyed; lose({reason: 'destroyed', message: 'test destroy'});}},
      testLose() {lose({reason: 'unknown', message: 'test loss'});}
    };
    ++stats.devices; currentDevice = result; return result;
  }
  const adapter = {features: new Set(), limits: {maxTextureDimension2D: 4096, maxUniformBufferBindingSize: 65536, maxBufferSize: 268435456}, requestDevice(desc) {
    assert(!desc.requiredFeatures?.length && !Object.keys(desc.requiredLimits || {}).length, 'elevated requirements');
    if (scenario === 'device-failure') return Promise.reject(new Error('device denied'));
    return new Promise(resolve => setTimeout(() => resolve(device()), scenario === 'cancel-startup' ? 100 : 0));
  }};
  Object.defineProperty(globalThis, 'navigator', {value: {gpu: {getPreferredCanvasFormat: () => 'bgra8unorm', requestAdapter: () => Promise.resolve(scenario === 'adapter-failure' ? null : adapter)}}});
  if (scenario === 'missing-webgpu') navigator.gpu = undefined;
  function assert(ok, message) {if (!ok) throw new Error(message);}
  function noFrameHandles() {
    // Read-only inspection of the pinned port's handle registry verifies C API
    // release, rather than just the JS mock's command methods being invoked.
    return !Object.values(WebGPU.Internals.jsObjects).some(object => object?.kind?.startsWith('frame'));
  }
  function noOwnedHandles() {
    return noFrameHandles() && !Object.values(WebGPU.Internals.jsObjects).some(object => ['shader','pipeline','uniform','bindings','layout','group'].includes(object?.kind));
  }
  let phase = 0, checkpoint = 0, oldX = 0;
  const timer = setInterval(() => {
    if (!Module._Stop || !status.dataset.state) return;
    const state = status.dataset.state;
    if (scenario.endsWith('failure') || scenario === 'missing-webgpu') {
      if (state === 'failed') {Module._Stop(); finish(stats.destroyed === stats.devices && stats.submitted === 0 && noOwnedHandles(), 'readable failure and cleanup');}
      return;
    }
    if (scenario === 'cancel-startup' || scenario === 'cancel-pipeline') {
      if (phase === 0 && state === 'loading' && (scenario === 'cancel-startup' || stats.pipelines > 0)) {
        Module._Stop(); phase = 1;
        setTimeout(() => finish(status.dataset.state === 'stopped' && stats.submitted === 0 && stats.destroyed === stats.devices && noOwnedHandles() && Module.ludusBrowserWindows.size === 0, 'late callback ignored and window detached'), 180);
      }
      return;
    }
    if (stats.submitted < 2) return;
    assert(noFrameHandles() && stats.submitted === stats.acquired && stats.drawn === stats.submitted, 'per-frame handle leak or incomplete submission');
    if (scenario === 'device-loss' || scenario === 'validation') {
      if (phase === 0) {
        phase = 1; checkpoint = stats.submitted;
        if (scenario === 'device-loss') currentDevice.testLose();
        else currentDevice.onuncapturederror({error: new GPUValidationError('test error')});
      } else if (phase === 1 && state === (scenario === 'device-loss' ? 'device lost' : 'failed')) {
        setTimeout(() => {Module._Stop(); finish(stats.submitted === checkpoint && stats.destroyed === stats.devices && noOwnedHandles(), 'rendering stopped');}, 30);
        phase = 2;
      }
      return;
    }
    if (scenario === 'input-suspend') {
      if (phase === 0) {
        canvas.focus(); canvas.dispatch('keydown', {code: 'KeyD'}); oldX = Number(status.dataset.x);
        timeJump += 60000; checkpoint = stats.submitted; phase = 1;
      } else if (phase === 1 && stats.submitted > checkpoint) {
        const x = Number(status.dataset.x); assert(x > oldX && x - oldX <= 0.12, 'resume delta not bounded or keyboard ignored');
        canvas.dispatch('keyup', {code: 'KeyD'});
        canvas.dispatch('pointerdown', {isPrimary: true, pointerId: 1, buttons: 1, clientX: 480, clientY: 90});
        checkpoint = stats.submitted; phase = 2;
      } else if (phase === 2 && stats.submitted > checkpoint) {
        assert(Number(status.dataset.x) === 0.5 && Number(status.dataset.y) === 0.5, 'pointer coordinates wrong');
        canvas.dispatch('pointerup', {isPrimary: true, pointerId: 1, buttons: 0, clientX: 480, clientY: 90});
        document.hidden = true; document.dispatch('visibilitychange'); phase = 3;
        setTimeout(() => {checkpoint = stats.submitted; setTimeout(() => {
          assert(stats.submitted === checkpoint, 'hidden canvas rendered'); document.hidden = false;
          document.dispatch('visibilitychange'); phase = 4;
        }, 40);}, 20);
      } else if (phase === 4 && stats.submitted > checkpoint) {
        Module._Stop(); finish(stats.destroyed === stats.devices && noOwnedHandles(), 'input/suspension/resume');
      }
      return;
    }
    if (scenario === 'resize-hidden') {
      if (phase === 0) {canvas.clientWidth = 300; canvas.clientHeight = 150; globalThis.devicePixelRatio = 1.5; checkpoint = stats.submitted; phase = 1;}
      else if (phase === 1 && stats.submitted > checkpoint) {
        assert(canvas.width === 450 && canvas.height === 225 && stats.configured >= 3, 'DPR resize did not reconfigure');
        Module._Stop(); finish(stats.destroyed === stats.devices && noOwnedHandles(), 'DPR resize');
      }
      return;
    }
    if (phase === 0) {Module._Restart(); phase = 1; checkpoint = stats.submitted;}
    else if (stats.submitted >= checkpoint + 2) {
      Module._Stop(); finish(stats.destroyed === stats.devices && noOwnedHandles() && colors.size > 1 && Module.ludusBrowserWindows.size === 0, 'animated clear/triangle/restart');
    }
  }, 10);
  function finish(ok, message) {clearInterval(timer); console.log('[W6:' + (ok ? 'passed' : 'failed') + '] ' + scenario + ' ' + message); process.exit(ok ? 0 : 1);}
  setTimeout(() => finish(false, 'timeout'), 3000);
}
