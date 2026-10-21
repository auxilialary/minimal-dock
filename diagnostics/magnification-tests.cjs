const assert = require('node:assert/strict');
const {wave, blend} = require('../ui/magnification-math.js');
const centers = [-108,-54,0,54,108];
const w = wave(centers,0,24,1.5,110,1);
assert.equal(w[2].scale,1.5);
assert(Math.abs(w[2].x)<1e-9);
assert(w[1].scale > w[0].scale && w[1].scale < w[2].scale);
for(let i=1;i<w.length;i++) assert(54+w[i].x-w[i-1].x >= 24*(w[i].scale+w[i-1].scale)/2);
for(let i=0;i<w.length;i++) assert(Math.abs(w[i].x+w[4-i].x)<1e-9);
assert(wave(centers,null,24,1.5,110,1).every(x=>x.scale===1&&x.x===0));
assert(Math.abs((1-blend(18,1/60))**60-(1-blend(18,1/144))**144)<1e-9);
// The browser cannot request native hover layout, including through legacy commands.
assert(!require('node:fs').readFileSync('src/app/Application.cpp','utf8').includes('magnify:'));
assert(!require('node:fs').readFileSync('ui/magnification.js','utf8').includes('postMessage'));
const {chromium} = require('playwright');
const fs=require('node:fs'), path=require('node:path');
(async()=>{
 const browser=await chromium.launch({channel:'msedge',headless:true});
 for(const dpi of [1,1.25,1.5,2]) {
  const page=await browser.newPage({deviceScaleFactor:dpi,viewport:{width:290,height:56}});
  await page.addInitScript(()=>{
   window.messages=[]; const listeners=[]; window.frameCount=0; window.violations=[];
   const raf=requestAnimationFrame.bind(window);
   window.requestAnimationFrame=fn=>raf(t=>{
    window.frameCount++; fn(t);
    const icons=[...document.querySelectorAll('.app')].map(b=>b.firstElementChild.getBoundingClientRect());
    if(innerWidth===290) {
     for(let i=0;i<icons.length;i++) {
      const r=icons[i];
      if(r.left<0 || r.right>290 || r.top<0 || r.bottom>56) violations.push('clip');
      if(i && r.left<icons[i-1].right) violations.push('overlap');
     }
    }
   });
   window.chrome={webview:{postMessage:m=>{messages.push(m)},addEventListener:(t,f)=>listeners.push(f)}};
   window.deliver=data=>listeners.forEach(f=>f({data}));
  });
  await page.route('https://dock.local/**' ,route=>{
   const file=new URL(route.request().url()).pathname.slice(1);
   route.fulfill({contentType:file.endsWith('.js')?'text/javascript':file.endsWith('.css')?'text/css':'text/html',body:fs.readFileSync(path.join('ui',file))});
  });
  await page.goto('https://dock.local/index.html?role=apps');
  await page.evaluate(()=>deliver({type:'apps',items:[1,2,3,4,5].map(id=>({id:String(id),title:'test',icon:'data:image/svg+xml,'+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256"><rect x="24" y="64" width="208" height="128" fill="blue"/></svg>'),active:id===3}))}));
  await page.mouse.move(143,30); await page.waitForFunction(()=>document.querySelectorAll('.app')[2].firstElementChild.getBoundingClientRect().width>35.9);
  const r=await page.evaluate(()=>[...document.querySelectorAll('.app')].map(b=>({button:b.getBoundingClientRect().toJSON(),icon:b.firstElementChild.getBoundingClientRect().toJSON()})));
  assert(r[2].icon.width>35.9); assert(r[1].icon.width>r[0].icon.width);
  for(let i=1;i<r.length;i++) assert(r[i].icon.left>=r[i-1].icon.right);
  for(const x of [9,20,266,277,100,186,143]) {
   await page.mouse.move(x,50);
   await page.waitForTimeout(80);
   const samples=await page.evaluate(()=>[...document.querySelectorAll('.app')].map(b=>({
    background:getComputedStyle(b).backgroundColor, r:b.firstElementChild.getBoundingClientRect().toJSON()})));
   for(const sample of samples) {
    assert.equal(sample.background,'rgba(0, 0, 0, 0)');
    assert(sample.r.left>=0 && sample.r.right<=290 && sample.r.top>=0 && sample.r.bottom<=56);
   }
   for(let i=1;i<samples.length;i++) assert(samples[i].r.left>=samples[i-1].r.right);
   assert.deepEqual(page.viewportSize(),{width:290,height:56});
  }
  await page.waitForFunction(()=>document.querySelectorAll('.app')[2].firstElementChild.getBoundingClientRect().width>35.9);
  const b=await page.locator('.app').nth(2).boundingBox();
  await page.mouse.click(b.x+b.width/2,b.y+b.height-10);
  assert(await page.evaluate(()=>messages.includes('activate:3')));
  await page.mouse.move(285,0);
  await page.waitForFunction(()=>[...document.querySelectorAll('.app')].every(b=>b.firstElementChild.style.transform==='scale(1)'));
  assert(!(await page.evaluate(()=>messages.some(m=>m.startsWith('magnify:')))));
  await page.waitForTimeout(150);
  const frames=await page.evaluate(()=>frameCount); await page.waitForTimeout(150);
  assert.equal(await page.evaluate(()=>frameCount),frames);
  assert(await page.evaluate(()=>[...document.querySelectorAll('.app')].every(b=>b.style.transform==='translateX(0px)')));
  assert.deepEqual(await page.evaluate(()=>violations),[]);
  for (const count of [1,2,8,12]) {
   const width=count*54+20;
   await page.setViewportSize({width,height:56});
   await page.evaluate(count=>deliver({type:'apps',items:Array.from({length:count},(_,i)=>({id:String(i),title:'test',active:true}))}),count);
   for (const x of [10,width/2,width-10]) {
    await page.mouse.move(x,30); await page.waitForTimeout(200);
    const boxes=await page.evaluate(()=>[...document.querySelectorAll('.app')].map(b=>({
     icon:b.firstElementChild.getBoundingClientRect().toJSON(),dot:b.lastElementChild.getBoundingClientRect().toJSON(),
     hit:document.elementFromPoint(b.getBoundingClientRect().x+24,30)===b
    })));
    for(const {icon:r,dot,hit} of boxes) {
     const inset=y=>y<17?17-Math.sqrt(17**2-(17-y)**2):1;
     assert(r.top>=1 && r.bottom<=55);
     assert(r.left>=inset(r.top) && r.right<=width-inset(r.top),'rounded contour clearance');
     assert(Math.abs((r.left+r.right-dot.left-dot.right)/2)<=.5/dpi+.001,'dot alignment');
     assert(hit,'transformed hit target');
    }
   }
  }
  await page.setViewportSize({width:240,height:56});
  await page.mouse.move(120,50);
  await page.waitForFunction(()=>[...document.querySelectorAll('.app')].every(b=>b.firstElementChild.style.transform==='scale(1)'));
  assert(await page.evaluate(()=>[...document.querySelectorAll('.app')].every(b=>b.firstElementChild.style.transform==='scale(1)')));
  await page.emulateMedia({reducedMotion:'reduce'});
  await page.mouse.move(143,30); await page.waitForTimeout(150);
  assert(await page.evaluate(()=>[...document.querySelectorAll('.app')].every(b=>b.firstElementChild.style.transform==='scale(1)')));
  console.log('PASS: cosine/spacing/click/rapid movement/leave/idle/reduced motion DPI '+dpi);
  await page.close();
 }
 await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});
