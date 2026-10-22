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
    await page.waitForFunction(() => Module.ready && Module._main_web_frame_count() > 10);
    function hex(words) {
      const bytes=words.flatMap(w=>[w&255,w>>8]);
      const data=[bytes.length,0,0,0,...bytes];
      return ':'+[...data,(-data.reduce((a,b)=>a+b,0))&255].map(x=>x.toString(16).padStart(2,'0')).join('')+'\n:00000001FF\n';
    }
    async function run(words) {
      await page.locator('#rom').setInputFiles({name:'speed.hex',mimeType:'text/plain',buffer:Buffer.from(hex(words))});
      await page.waitForTimeout(300);
      await page.evaluate(()=>Module._mainui_debug_set_paused(0));
      const first=await page.evaluate(()=>Module._main_web_frame_count());
      await page.waitForTimeout(800);
      return {frames:(await page.evaluate(()=>Module._main_web_frame_count()))-first,
        state:await page.evaluate(()=>Module._main_web_speed_state())};
    }
    const normal=await run([0xcfff]);
    const fast=await run([0xc004,0x4355,0x4558,1,0xfffe,0xcfff]);
    const restored=await run([0xc004,0x4355,0x4558,1,0xfffe,0xc004,0x4355,0x4558,0,0xffff,0xcfff]);
    assert.equal(normal.state,0,'normal pacing');
    assert.equal(fast.state,3,'guest request and host acceleration must both be active');
    assert.equal(restored.state,0,'normal marker restores pacing');
    assert.ok(normal.frames>20 && normal.frames<85,`normal frames: ${normal.frames}`);
    assert.ok(fast.frames>20,`fast mode must keep advancing: ${fast.frames}`);
    assert.ok(restored.frames>20 && restored.frames<85,`restored frames: ${restored.frames}`);
    // Unthrottled throughput depends on host CPU capacity, not just pacing policy.
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({normal,fast,restored}));
    console.log('PASS: browser speed markers enable acceleration and restore pacing');
  } finally {
    if(browser) await browser.close();
    await new Promise(resolve=>server.close(resolve));
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
