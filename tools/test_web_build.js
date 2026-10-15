// Run after make TSYS=emscripten. Requires npm install playwright and its browser.
// WEB_BROWSER_CHANNEL=msedge can use an installed Edge instead of Chromium.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { chromium } = require('playwright');

(async () => {
  const root = path.resolve(process.argv[2] || '_obj_/emscripten');
  const server = http.createServer((req, res) => {
    if (req.url === '/favicon.ico') { res.writeHead(204).end(); return; }
    const name = path.resolve(root, '.' + decodeURIComponent(req.url.split('?')[0]));
    if (!name.startsWith(root + path.sep)) { res.writeHead(403).end(); return; }
    fs.readFile(name, (error, data) => {
      if (error) { res.writeHead(404).end(); return; }
      const mime = {'.wasm':'application/wasm', '.js':'text/javascript', '.html':'text/html'};
      res.setHeader('Content-Type', mime[path.extname(name)] || 'application/octet-stream');
      res.end(data);
    });
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  let browser;
  try {
    browser = await chromium.launch({headless:true, channel:process.env.WEB_BROWSER_CHANNEL || undefined,
      args:['--mute-audio']});
    const page = await browser.newPage({viewport:{width:1100,height:900}});
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack || String(error)));
    page.on('console', msg => {
      if (process.env.WEB_TEST_VERBOSE) console.log(msg.type(), msg.text());
      if (msg.type() === 'error') errors.push(msg.text());
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/cuzebox.html`);
    await page.waitForFunction(() => Module.ready && Module._main_web_frame_count() > 10, null, {timeout:60000});
    const size = await page.locator('canvas').evaluate(el => [el.width, el.height]);
    assert.ok(size[0] >= 310 && size[1] >= 228, 'SDL2 creates a usable canvas');
    await page.locator('#audio').click();
    assert.equal(await page.evaluate(() => Module.SDL2.audioContext.state), 'running');

    // Exercise the same pause/frame advance functions used by the GUI without a debugger build.
    await page.evaluate(() => Module._mainui_debug_set_paused(1));
    const paused = await page.evaluate(() => Module._main_web_frame_count());
    await page.waitForTimeout(150);
    assert.equal(await page.evaluate(() => Module._main_web_frame_count()), paused);
    await page.evaluate(() => Module._mainui_debug_step_frame());
    await page.waitForFunction(frame => Module._main_web_frame_count() === frame + 1, paused);

    // Valid AVR HEX containing an endless rjmp, independent of external game assets.
    await page.locator('#rom').setInputFiles({name:'browser-test.hex',mimeType:'text/plain',
      buffer:Buffer.from(':02000000FFCF30\n:00000001FF\n')});
    await page.waitForFunction(() => document.getElementById('status').textContent === 'Loaded browser-test.hex');
    assert.match(await page.evaluate(() => Module.ccall('mainui_get_current_rom_name','string',[],[])), /browser-test/);
    await page.evaluate(async () => {
      Module.FS.writeFile('/user/persistence-test.txt', 'earlier save');
      const first = Module.flushStorage();
      Module.FS.writeFile('/user/persistence-test.txt', 'browser storage works');
      await Promise.all([first, Module.flushStorage()]);
    });
    await page.locator('#save').click();
    await page.waitForFunction(() => document.getElementById('status').textContent === 'Saved in this browser.');
    assert.match(await page.evaluate(() => Module.FS.readFile('/user/config.cfg',{encoding:'utf8'})), /ResidentBootloader/);
    await page.reload();
    await page.waitForFunction(() => Module.ready && Module._main_web_frame_count() > 2);
    assert.equal(await page.evaluate(() => Module.FS.readFile('/user/persistence-test.txt',{encoding:'utf8'})), 'browser storage works');
    assert.ok(await page.evaluate(() => Module.FS.analyzePath('/user/roms/browser-test.hex').exists));
    await page.mouse.move(200, await page.locator('canvas').evaluate(el => el.getBoundingClientRect().top + 5));
    const canvasBox = await page.locator('canvas').boundingBox();
    await page.mouse.move(canvasBox.x + 120, canvasBox.y + 5);
    await page.waitForTimeout(250);
    await page.mouse.click(canvasBox.x + 170, canvasBox.y + 12, {delay:80});
    await page.waitForTimeout(150);
    await page.mouse.move(canvasBox.x + 140, canvasBox.y + 145);
    await page.waitForTimeout(100);
    await page.mouse.click(canvasBox.x + 140, canvasBox.y + 145, {delay:80});
    await page.waitForTimeout(150);
    await page.mouse.move(canvasBox.x + 22, canvasBox.y + 90);
    await page.waitForTimeout(100);
    await page.mouse.click(canvasBox.x + 22, canvasBox.y + 90, {delay:80});
    await page.locator('#save').click();
    await page.waitForFunction(() => document.getElementById('status').textContent === 'Saved in this browser.');
    assert.match(await page.evaluate(() => Module.FS.readFile('/user/config.cfg',{encoding:'utf8'})), /^AudioS16=0$/m,
      'MicroUI audio checkbox changes the saved preference');
    await page.screenshot({path:path.join(root,'browser-smoke.png')});
    await page.reload();
    await page.waitForFunction(() => Module.ready && Module._main_web_frame_count() > 2);
    assert.match(await page.evaluate(() => Module.FS.readFile('/user/config.cfg',{encoding:'utf8'})), /^AudioS16=0$/m);
    await page.locator('#audio').click();
    assert.equal(await page.evaluate(() => Module.SDL2.audioContext.state), 'running');
    assert.deepEqual(errors, []);
    console.log('PASS: SDL2 browser startup, audio, frames, pause/step, HEX upload, MicroUI audio checkbox and IndexedDB reload');
  } finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
