// RG1 emitted WGSL/GLSL stage-admission fixture. Device rendering belongs to RG2.
import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve} from 'node:path';
import {createRequire} from 'node:module';
const require = createRequire(process.env.LUDUS_RG1_PLAYWRIGHT || new URL('../../tools/web-browser-tests/package.json', import.meta.url));
const {chromium} = require('playwright');
const evidenceRoot = resolve(process.argv[2]);
const acceptance = JSON.parse(await readFile(resolve(evidenceRoot, 'acceptance.json')));
const packageRoot = acceptance.package;
const manifest = JSON.parse(await readFile(resolve(packageRoot, 'package.json')));
const pin = JSON.parse(await readFile(new URL('../../config/shader_toolchain.json', import.meta.url))).wgsl_validator;
const stages = await Promise.all(manifest.programs.filter(p => p.profile === 'wgsl' || p.profile === 'glsl').map(async p => ({
  ...p, code: await readFile(resolve(packageRoot, 'programs', p.key, p.descriptor.artifact), 'utf8')
})));
const server = createServer((_, response) => response.writeHead(200, {'Content-Type': 'text/html'}).end('<!doctype html><canvas></canvas>'));
await new Promise(done => server.listen(0, '127.0.0.1', done));
let browser;
try {
  browser = await chromium.launch({headless: true, executablePath: process.env.LUDUS_RG1_CHROMIUM,
    args: ['--enable-unsafe-webgpu', '--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--use-webgpu-adapter=swiftshader']});
  assert.equal(browser.version(), pin.chromium_version);
  const page = await browser.newPage();
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  const results = await page.evaluate(async ({stages, recipes}) => {
    const results = [];
    const wgsl = stages.filter(p => p.profile === 'wgsl');
    if (wgsl.length) {
      const adapter = await navigator.gpu?.requestAdapter();
      if (!adapter) throw Error('Pinned Chromium WGSL admission needs an adapter');
      const device = await adapter.requestDevice();
      for (const p of wgsl) {
        const info = await device.createShaderModule({code: p.code}).getCompilationInfo();
        const errors = [...info.messages].filter(m => m.type === 'error').map(m => m.message);
        if (errors.length) throw Error(p.id + ': ' + errors.join('\n'));
        results.push({program: p.id, profile: 'wgsl', valid: true});
      }
      device.destroy();
    }
    const gl = document.querySelector('canvas').getContext('webgl2');
    const essl = stages.filter(p => p.profile === 'glsl');
    if (essl.length && !gl) throw Error('Pinned Chromium GLSL admission needs WebGL 2');
    const shaders = new Map();
    for (const p of essl) {
      const shader = gl.createShader(p.descriptor.stage === 'vertex' ? gl.VERTEX_SHADER : gl.FRAGMENT_SHADER);
      gl.shaderSource(shader, p.code); gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw Error(p.id + ': ' + gl.getShaderInfoLog(shader));
      shaders.set(p.key, shader);
      results.push({program: p.id, profile: 'glsl', valid: true});
    }
    for (const recipe of recipes.filter(r => r.profile === 'glsl')) {
      const program = gl.createProgram();
      gl.attachShader(program, shaders.get(recipe.vertex)); gl.attachShader(program, shaders.get(recipe.fragment));
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw Error(recipe.technique + ': ' + gl.getProgramInfoLog(program));
      results.push({technique: recipe.technique, profile: 'glsl', linked: true});
      gl.deleteProgram(program);
    }
    for (const shader of shaders.values()) gl.deleteShader(shader);
    return results;
  }, {stages, recipes: manifest.recipes});
  const evidence = {chromium: browser.version(), generation: acceptance.generation, kind: 'software-adapter shader admission; no rendering claim', results};
  await mkdir(evidenceRoot, {recursive: true});
  await writeFile(resolve(evidenceRoot, 'browser.json'), JSON.stringify(evidence, null, 2) + '\n');
  console.log(JSON.stringify(evidence));
} finally {
  if (browser) await browser.close();
  await new Promise(done => server.close(done));
}
