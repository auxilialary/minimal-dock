const assert = require('node:assert/strict');
const {chromium} = require('playwright');
const fs = require('node:fs'), path = require('node:path');
(async () => {
 const browser = await chromium.launch({channel:'msedge',headless:true});
 for (const dpi of [1,1.25,1.5,2]) {
  const page = await browser.newPage({viewport:{width:320,height:400},deviceScaleFactor:dpi});
  await page.addInitScript(() => {
   const listeners=[]; window.messages=[];
   window.chrome={webview:{postMessage:m=>window.messages.push(m),addEventListener:(_,fn)=>listeners.push(fn)}};
   window.deliver=data=>listeners.forEach(fn=>fn({data}));
  });
  await page.route('https://dock.local/**',route=>{
   const name=new URL(route.request().url()).pathname.slice(1);
   route.fulfill({contentType:name.endsWith('.css')?'text/css':name.endsWith('.js')?'text/javascript':'text/html',body:fs.readFileSync(path.join('ui',name))});
  });
  await page.clock.install({time:new Date(2026,1,15,12)});
  await page.goto('https://dock.local/index.html?role=popup');
  await page.evaluate(()=>window.deliver({type:'calendar',revision:1,firstDay:0,locale:'en-US'}));
  assert.equal(await page.getByRole('gridcell').count(),35);
  const five = await page.locator('#calendar').evaluate(e=>e.getBoundingClientRect().height);
  // February 2026 fits five rows; May needs six with a Sunday first day.
  for(let i=0;i<3;i++) await page.locator('#calendar-next').click();
  assert.equal(await page.getByRole('gridcell').count(),42);
  const six = await page.locator('#calendar').evaluate(e=>e.getBoundingClientRect().height);
  assert.equal(six-five,30);
  await page.clock.runFor(200);
  assert((await page.evaluate(()=>window.messages)).includes(`popup:size:320 ${six} 1`));
  const rows=[{id:1,name:'Test session',volume:40,muted:false}];
  await page.evaluate(rows=>{window.messages=[];window.deliver({type:'mixer',rows,revision:2});},rows);
  await page.clock.runFor(200);
  assert(await page.locator('#calendar').isHidden());
  assert((await page.evaluate(()=>window.messages)).includes('popup:present:2'));
  const slider=page.getByRole('slider');
  await slider.click();
  assert.equal(await slider.evaluate(e=>getComputedStyle(e).outlineStyle),'none');
  assert.equal(await slider.evaluate(e=>getComputedStyle(e,'::-webkit-slider-thumb').outlineStyle),'none');
  await page.keyboard.press('Tab'); await page.keyboard.press('Shift+Tab');
  assert.equal(await slider.evaluate(e=>e===document.activeElement),true);
  assert.equal(await slider.evaluate(e=>e.matches(':focus-visible')),true);
  assert.equal(await slider.evaluate(e=>getComputedStyle(e).filter.startsWith('drop-shadow')),true);
  await slider.press('ArrowRight');
  // Cancel a queued slider write and obsolete presentation before the next frame.
  await page.evaluate(rows=>{
   window.messages=[];
   const input=document.querySelector('input'); input.value=75; input.dispatchEvent(new Event('input'));
   window.deliver({type:'calendar',revision:3,firstDay:1});
   window.deliver({type:'mixer',revision:4,rows});
   window.deliver({type:'calendar',revision:5,firstDay:1});
  },rows);
  await page.clock.runFor(200);
  assert.equal(await page.locator('input').count(),0);
  assert(await page.locator('#mixer').isHidden());
  assert(await page.locator('#tooltip').isHidden());
  let messages=await page.evaluate(()=>window.messages);
  assert(!messages.some(m=>m.startsWith('audio:')));
  assert.deepEqual(messages.filter(m=>m.startsWith('popup:present:')),['popup:present:5']);
  await page.evaluate(()=>{window.messages=[];window.deliver({type:'mixer',revision:6,rows:[]});window.deliver({type:'popup:hidden'});});
  await page.clock.runFor(1200);
  assert(!(await page.evaluate(()=>window.messages)).includes('popup:present:6'));
  assert(await page.locator('#calendar').isHidden());
  assert(await page.locator('#mixer').isHidden());
  // Title tooltip remains exclusive and functional after interactive modes.
  await page.evaluate(()=>window.deliver({type:'tooltip',revision:7,title:'App title'}));
  await page.clock.runFor(200);
  assert.equal(await page.getByRole('tooltip').textContent(),'App title');
  assert((await page.evaluate(()=>window.messages)).includes('popup:present:7'));
  await page.setViewportSize({width:220,height:56});
  await page.goto('https://dock.local/index.html?role=media');
  const deliver = data=>page.evaluate(data=>window.deliver({type:'media',artwork:'',title:'Track',...data}),data);
  await deliver({playing:true,elapsed:20,duration:100});
  const scale=()=>page.locator('#media-progress-fill').evaluate(e=>new DOMMatrixReadOnly(getComputedStyle(e).transform).a);
  assert(Math.abs(await scale()-.2)<.001);
  await page.clock.runFor(2000); assert(Math.abs(await scale()-.22)<.002);
  await deliver({playing:true,elapsed:70,duration:100});
  assert(Math.abs(await scale()-.7)<.001);
  await deliver({playing:false,elapsed:0,duration:0});
  const paused=await scale(); await page.clock.runFor(2000); assert.equal(await scale(),paused);
  await deliver({playing:true,elapsed:5,duration:100});
  await page.clock.runFor(1000); assert(Math.abs(await scale()-.06)<.002);
  await deliver({playing:true,elapsed:300,duration:100}); assert.equal(await scale(),1);
  await deliver({playing:true,elapsed:5,duration:0});
  assert(await page.getByRole('progressbar',{includeHidden:true}).isHidden());
  assert.equal(await page.locator('#media-time').textContent(),'');
  await deliver({playing:true,elapsed:20,duration:100});
  assert.equal(await page.getByRole('progressbar').getAttribute('aria-valuenow'),'20');
  assert.equal(await page.locator('#media-time').textContent(),'0:20 / 1:40');
  assert(await page.locator('#media').evaluate(e=>e.scrollHeight<=56));
  if (process.env.DOCK_TEST_SCREENSHOTS) await page.screenshot({path:`diagnostics/refinement-media-${dpi}.png`});
  await page.close();
 }
 await browser.close();
 console.log('PASS: refinement checks at 100/125/150/200% DPI: 5/6 calendar rows and heights, popup exclusivity, stale frame/write cancellation, tooltip recovery, pointer/keyboard focus, progress interpolation/seek/pause/resume/clamping/missing duration.');
})().catch(e=>{console.error(e);process.exit(1)});
