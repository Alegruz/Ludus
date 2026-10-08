// Qualify the actual packaged sample players in a real browser, including pixels.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {resolve, sep} from 'node:path';
import {chromium} from 'playwright';
import {PNG} from 'pngjs';

const root = resolve(process.argv[2] || '../../out/editor-pages');
const output = resolve(process.argv[3] || '../../out/web-sample-qa');
const samples = process.argv.slice(4);
if (!samples.length) samples.push('cornell-box', 'live-edit-game', 'scripted-game');
await mkdir(output, {recursive: true});
const server = createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  const file = resolve(root, '.' + (pathname.endsWith('/') ? pathname + 'index.html' : pathname));
  if (!file.startsWith(root + sep)) { response.writeHead(403).end(); return; }
  try {
    const bytes = await readFile(file);
    const type = file.endsWith('.wasm') ? 'application/wasm' : file.endsWith('.js') ? 'text/javascript' : 'text/html';
    response.writeHead(200, {'Content-Type': type}).end(bytes);
  } catch { response.writeHead(404).end(); }
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
const browser = await chromium.launch({
  executablePath: process.env.LUDUS_QA_BROWSER || undefined,
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan',
    '--use-webgpu-adapter=swiftshader', '--use-angle=swiftshader',
    '--use-vulkan=swiftshader', '--disable-vulkan-surface', '--enable-unsafe-swiftshader'],
});
async function shot(page, name) {
  const box = await page.locator('canvas').boundingBox();
  const bytes = await page.screenshot({clip: box});
  await writeFile(resolve(output, name + '.png'), bytes);
  const png = PNG.sync.read(bytes);
  const offset = (Math.floor(png.height / 2) * png.width + Math.floor(png.width / 2)) * 4;
  return {png, pixel: [...png.data.slice(offset, offset + 3)]};
}
try {
  for (const sample of samples) {
    const page = await browser.newPage({viewport: {width: 1000, height: 760}});
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', message => console.log(sample, message.type(), message.text()));
    await page.goto(`http://127.0.0.1:${server.address().port}/players/${sample}/index.html`, {waitUntil: 'domcontentloaded', timeout: 60000});
    await page.waitForFunction(() => document.getElementById('status').dataset.state === 'playing' &&
      Number(document.getElementById('status').dataset.frames) >= 3, undefined, {timeout: 60000});
    // Submission precedes browser composition. Let the compositor present the
    // completed frames before inspecting the canvas region.
    await page.evaluate(() => new Promise(done => requestAnimationFrame(() => requestAnimationFrame(done))));
    const first = await shot(page, sample + '-playing');
    if (sample === 'cornell-box') {
      const colors = new Set();
      for (let index = 0; index < first.png.data.length; index += 256) colors.add(first.png.data.subarray(index, index + 3).toString('hex'));
      console.log(sample, {width: first.png.width, height: first.png.height, colors: colors.size, center: first.pixel});
      assert.ok(colors.size > 20, 'Cornell must render a lit scene, not an empty/clear canvas');
    } else {
      await page.locator('#pause').click();
      await page.waitForFunction(() => document.getElementById('status').dataset.state === 'paused');
      const frozen = await shot(page, sample + '-paused');
      await page.waitForTimeout(350);
      assert.deepEqual((await shot(page, sample + '-still-paused')).pixel, frozen.pixel);
      await page.locator('#pause').click();
      await page.waitForFunction(() => document.getElementById('status').dataset.state === 'playing');
      if (sample === 'scripted-game') {
        await page.locator('canvas').click();
        // The authored contract requires two interactions before opening.
        for (let interaction = 0; interaction < 2; ++interaction) {
          await page.keyboard.down('Space');
          await page.waitForTimeout(100);
          await page.keyboard.up('Space');
          await page.waitForTimeout(150);
        }
        const changed = await shot(page, sample + '-interaction');
        assert.ok(changed.pixel[1] > changed.pixel[0] + 50, 'Two Space presses must invoke the real Luau interaction and turn the scene green');
      } else {
        await page.waitForTimeout(350);
        assert.notDeepEqual((await shot(page, sample + '-resumed')).pixel, frozen.pixel);
      }
    }
    assert.deepEqual(errors, []);
    await page.locator('#restart').click();
    await page.waitForFunction(() => document.getElementById('status').dataset.state === 'playing');
    await page.close();
    console.log('PASS', sample, 'rendering, controls and restart');
  }
} finally {
  await browser.close();
  await new Promise(done => server.close(done));
}
