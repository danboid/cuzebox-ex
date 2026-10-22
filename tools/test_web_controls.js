// WEB_BROWSER=firefox selects Playwright Firefox; otherwise use installed Edge.
// Arguments: web directory or URL, optional HapticDemo.uze for AVR input.
const assert=require('node:assert/strict'),fs=require('node:fs'),http=require('node:http'),path=require('node:path');
const {chromium,firefox}=require('playwright');
(async()=>{
 let server,browser,url=process.argv[2];
 if(!/^https?:/.test(url||'')){
  const root=path.resolve(url||'_obj_/emscripten');
  server=http.createServer((req,res)=>{
   const file=path.resolve(root,'.'+decodeURIComponent(req.url.split('?')[0]));
   if(!file.startsWith(root+path.sep)){res.writeHead(403).end();return;}
   fs.readFile(file,(e,data)=>{if(e){res.writeHead(404).end();return;}res.setHeader('Content-Type',file.endsWith('.wasm')?'application/wasm':file.endsWith('.js')?'text/javascript':'text/html');res.end(data);});
  });await new Promise(r=>server.listen(0,'127.0.0.1',r));
  url=`http://127.0.0.1:${server.address().port}/${fs.existsSync(path.join(root,'index.html'))?'index.html':'cuzebox.html'}`;
 }
 try{
  browser=await (process.env.WEB_BROWSER==='firefox'?firefox:chromium).launch({headless:true,...(process.env.WEB_BROWSER==='firefox'?{}:{channel:'msedge'})});
  const page=await browser.newPage({viewport:{width:1100,height:1000}});
  await page.route('**/api/config',r=>r.fulfill({json:{browser_guests:false}}));
  await page.route('**/api/me',r=>r.fulfill({status:401,json:{error:'Sign in'}}));
  await page.route('**/api/roms',r=>r.fulfill({json:{roms:[]}}));
  await page.goto(url);await page.waitForFunction(()=>Module.ready);
  const config=()=>page.evaluate(()=>{Module._main_web_save();return Module.FS.readFile('/user/config.cfg',{encoding:'utf8'});});
  assert.match(await config(),/^VDevBindType0_0=1$/m,'P1 keyboard connected');
  assert.match(await config(),/^VDevType0=1$/m,'P1 is a pad, not a keyboard peripheral');
  if(process.argv[3]){
   await page.locator('#rom').setInputFiles(process.argv[3]);await page.waitForTimeout(4000);
   const b=await page.locator('canvas').boundingBox();
   const clip={x:b.x+60,y:b.y+25,width:500,height:170};
   const before=await page.screenshot({clip});
   await page.keyboard.press('ArrowRight',{delay:100});await page.waitForTimeout(300);
   const pressed=await page.screenshot({clip});
   assert.notDeepEqual(pressed,before,'AVR demo must advance its channel from keyboard input');
   await page.keyboard.press('ArrowLeft',{delay:100});await page.waitForTimeout(300);
   assert.deepEqual(await page.screenshot({clip}),before,'AVR demo must return to the previous channel');
  }
  await page.evaluate(async()=>{Module._main_web_save();let s=Module.FS.readFile('/user/config.cfg',{encoding:'utf8'});s=s.replace('DisplayFullscreen=0','DisplayFullscreen=1').replace('VDevName0=PLAYER 1','VDevName0=CUSTOM PLAYER');Module.FS.writeFile('/user/config.cfg',s);await Module.flushStorage();});
  await page.reload();await page.waitForFunction(()=>Module.ready);
  assert.equal(await page.evaluate(()=>!!document.fullscreenElement),false,'saved fullscreen must not apply on startup');
  assert.match(await config(),/^VDevName0=CUSTOM PLAYER$/m,'custom input survives reload');
  await page.locator('#audio').click();
  assert.equal(await page.evaluate(()=>!!document.fullscreenElement),false,'ordinary page buttons must not enter fullscreen');
  const before=await page.locator('canvas').boundingBox();
  await page.locator('#fullscreen').click();await page.waitForFunction(()=>document.fullscreenElement?.id==='screen');
  await page.evaluate(()=>document.exitFullscreen());
  await page.waitForFunction(()=>!document.fullscreenElement);await page.waitForTimeout(300);
  const after=await page.locator('canvas').boundingBox();
  for(const key of ['x','y','width','height']) assert.ok(Math.abs(before[key]-after[key])<2,`restore canvas ${key}`);
  const frame=await page.evaluate(()=>Module._main_web_frame_count());
  await page.keyboard.press('Escape');await page.waitForTimeout(150);
  assert.ok(await page.evaluate(()=>Module._main_web_frame_count())>frame,'Escape must not quit the emulator');
  console.log('PASS: browser keyboard defaults/input, custom config, explicit fullscreen and layout restoration, Escape');
 }finally{if(browser)await browser.close();if(server)await new Promise(r=>server.close(r));}
})().catch(e=>{console.error(e);process.exitCode=1;});
