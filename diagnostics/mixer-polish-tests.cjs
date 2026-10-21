const assert=require('node:assert/strict');
const {chromium}=require('playwright');
const fs=require('node:fs'),path=require('node:path');
(async()=>{
 const browser=await chromium.launch({channel:'msedge',headless:true});let cases=0;
 for(const dpi of [1,1.25,1.5,2]) for(const count of [0,1,4,10,25]) {
  const page=await browser.newPage({viewport:{width:320,height:130},deviceScaleFactor:dpi,reducedMotion:'reduce'});
  await page.addInitScript(()=>{window.messages=[];const receive=[];window.chrome={webview:{postMessage:m=>window.messages.push(m),addEventListener:(_,fn)=>receive.push(fn)}};window.deliver=data=>receive.forEach(fn=>fn({data}));});
  await page.route('https://dock.local/**',route=>{const file=new URL(route.request().url()).pathname.slice(1);return route.fulfill({contentType:file.endsWith('.css')?'text/css':file.endsWith('.js')?'text/javascript':'text/html',body:fs.readFileSync(path.join('ui',file))});});
  await page.goto('https://dock.local/index.html?role=popup');
  const rows=[{id:0,name:'Master volume',volume:70},...Array.from({length:count},(_,i)=>({id:i+1,name:'Long application name '.repeat(5),identity:'session-'+i,volume:42}))];
  await page.evaluate(rows=>window.deliver({type:'mixer',rows}),rows);
  const height=await page.evaluate(()=>Number(window.messages.filter(m=>m.startsWith('popup:size:')).at(-1).split(' ')[1]));
  assert(height<=400);await page.setViewportSize({width:320,height});
  assert.equal(await page.locator('.audio-slider').count(),count);
  assert.equal(await page.locator('#popup-close,#audio-master').count(),0);
  const header=await page.locator('#mixer header').boundingBox();assert(Math.abs(header.x+header.width/2-160)<1);
  const overflow=await page.locator('#audio-rows').evaluate(e=>e.scrollHeight>e.clientHeight);assert.equal(overflow,count>=10);
  if(count) {
   const slider=page.locator('.audio-slider').first();const before=await slider.inputValue();
   await slider.dispatchEvent('wheel',{deltaY:-12.5});
   assert.equal((await page.evaluate(()=>window.messages)).at(-1),'audio:wheel 1 0.25');
   assert.equal(await slider.inputValue(),before); // Only native notification changes the rendered volume.
   assert.equal(await slider.evaluate(e=>getComputedStyle(e).appearance),'none');
   assert.equal(await page.locator('.audio-name').first().evaluate(e=>getComputedStyle(e).textOverflow),'ellipsis');
  }
  if(overflow) {await page.locator('#audio-rows').dispatchEvent('wheel',{deltaY:100});assert(await page.locator('#audio-rows').evaluate(e=>e.scrollTop)>0);}
  if(dpi===1&&count===10) { await page.locator('#audio-rows').evaluate(e=>e.scrollTop=0); if (process.env.DOCK_TEST_SCREENSHOTS) await page.screenshot({path:'diagnostics/mixer-final.png'}); }
  await page.keyboard.press('Escape');assert.equal((await page.evaluate(()=>window.messages)).at(-1),'popup:close');
  await page.close();cases++;
 }
 await browser.close();console.log(`PASS: ${cases} mixer layouts at four DPI scales: no master/close space, centered header, overflow, slider wheel isolation, native appearance, Escape.`);
})().catch(e=>{console.error(e);process.exit(1)});
