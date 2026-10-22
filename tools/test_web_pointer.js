// Browser pointer regression: run against a build directory or an HTTPS URL.
const assert=require('node:assert/strict'),fs=require('node:fs'),http=require('node:http'),path=require('node:path');
const {chromium}=require('playwright');
(async()=>{
 let server,browser;
 let url=process.argv[2];
 if(!/^https?:/.test(url||'')){
  const root=path.resolve(url||'_obj_/emscripten');
  server=http.createServer((req,res)=>{
   const file=path.resolve(root,'.'+decodeURIComponent(req.url.split('?')[0]));
   if(!file.startsWith(root+path.sep)){res.writeHead(403).end();return;}
   fs.readFile(file,(err,data)=>{if(err){res.writeHead(404).end();return;}res.setHeader('Content-Type',file.endsWith('.wasm')?'application/wasm':file.endsWith('.js')?'text/javascript':'text/html');res.end(data);});
  });await new Promise(r=>server.listen(0,'127.0.0.1',r));url=`http://127.0.0.1:${server.address().port}/cuzebox.html`;
 }
 try{
  browser=await chromium.launch({headless:true,channel:process.env.WEB_BROWSER_CHANNEL||'msedge'});
  for(const width of [1100,460]){
   const page=await browser.newPage({viewport:{width,height:1000}});
   await page.goto(url);await page.waitForFunction(()=>Module.ready);
   async function point(x,y){const b=await page.locator('canvas').boundingBox();const [w,h]=await page.locator('canvas').evaluate(e=>[e.width,e.height]);return [b.x+x*b.width/w,b.y+y*b.height/h];}
   async function click(x,y){const [a,b]=await point(x,y);await page.mouse.move(a,b);await page.waitForTimeout(120);await page.mouse.click(a,b,{delay:80});await page.waitForTimeout(180);}
   async function setting(){return page.evaluate(()=>{Module._main_web_save();return Module.FS.readFile('/user/config.cfg',{encoding:'utf8'}).match(/^AudioS16=(\d)$/m)[1];});}
   await click(170,12);await click(140,145);
   assert.equal(await setting(),'1');
   await page.mouse.move(...await point(22,90));await page.waitForTimeout(120);
   // Shift onto the window's noninteractive status row, avoiding a title drag.
   await page.evaluate(()=>document.querySelector('header').style.paddingBottom='32px');
   await page.waitForTimeout(120);await page.mouse.down();await page.waitForTimeout(80);await page.mouse.up();await page.waitForTimeout(180);
   assert.equal(await setting(),'1',`width ${width}: layout shift must not click the old checkbox position`);
   await click(22,90);assert.equal(await setting(),'0',`width ${width}: the visible checkbox must still work`);
   await page.close();
  }
  console.log('PASS: pointer clicks follow canvas layout changes at full and CSS-scaled sizes');
 }finally{if(browser)await browser.close();if(server)await new Promise(r=>server.close(r));}
})().catch(e=>{console.error(e);process.exitCode=1;});
