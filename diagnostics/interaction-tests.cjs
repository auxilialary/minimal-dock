const assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path');
const {chromium} = require('playwright');
(async () => {
 const browser = await chromium.launch({channel:'msedge',headless:true});
 try {
  async function pageFor(role,dpi,width,height) {
   const page = await browser.newPage({viewport:{width,height},deviceScaleFactor:dpi});
   await page.addInitScript(() => {
    const listeners=[]; window.messages=[];
    window.chrome={webview:{postMessage:m=>window.messages.push(m),addEventListener:(_,fn)=>listeners.push(fn)}};
    window.deliver=data=>listeners.forEach(fn=>fn({data}));
   });
   await page.route('https://dock.local/**',route=>{
    const name=new URL(route.request().url()).pathname.slice(1);
    route.fulfill({contentType:name.endsWith('.css')?'text/css':name.endsWith('.js')?'text/javascript':'text/html',body:fs.readFileSync(path.join('ui',name))});
   });
   await page.goto('https://dock.local/index.html?role='+role);
   return page;
  }
  for (const dpi of [1,1.25,1.5,2]) {
   const page=await pageFor('popup',dpi,200,36);
   await page.clock.setFixedTime(new Date(2026,1,15,12));
   await page.evaluate(()=>document.fonts.ready);
   const deliver=data=>page.evaluate(data=>window.deliver(data),data);
   await deliver({type:'calendar',revision:1,firstDay:0,locale:'ru-RU'});
   async function size() {
    for(let i=0;i<5;i++) {
     await page.waitForTimeout(100);
     const command=await page.evaluate(()=>window.messages.filter(m=>m.startsWith('popup:size:')).at(-1));
     const [width,height]=command.slice(11).split(' ').map(Number);
     await page.setViewportSize({width,height});
    }
    await page.waitForTimeout(200);
    const fit=await page.locator('#calendar').evaluate(host=>{
     const last=host.querySelector('[role="row"]:last-child').getBoundingClientRect();
     return {fits:last.bottom<=innerHeight && host.scrollHeight<=innerHeight,
      lastRowBottom:last.bottom,viewport:innerHeight,content:host.scrollHeight,
      transform:getComputedStyle(host).transform,messages:window.messages.filter(m=>m.startsWith('popup:size:'))};
    }); assert(fit.fits,JSON.stringify({dpi,...fit}));
   }
   await size(); assert.equal(await page.getByRole('gridcell').count(),35);
   assert.equal(await page.locator('#calendar-month').textContent(),'February 2026');
   await page.screenshot({path:`diagnostics/calendar-five-${dpi}.png`});
   for(let i=0;i<3;i++) await page.locator('#calendar-next').click();
   await size(); assert.equal(await page.getByRole('gridcell').count(),42);
   assert((await page.evaluate(()=>window.messages)).some(m=>m.startsWith('popup:present:1 320 ')));
   await page.screenshot({path:`diagnostics/calendar-six-${dpi}.png`});
   await page.setViewportSize({width:320,height:180});
   assert.equal(await page.locator('#calendar').evaluate(e=>getComputedStyle(e).overflowY),'auto');
   await deliver({type:'popup:hidden'}); await deliver({type:'calendar',revision:2,firstDay:1}); await size();
   await deliver({type:'menu',revision:3,startup:true,visibility:2});
   assert(await page.locator('#calendar').isHidden());
   await page.setViewportSize({width:240,height:340});
   assert.equal(await page.getByRole('menuitem').first().textContent(),'Sound Settings');
   await page.keyboard.press('ArrowDown'); await page.keyboard.press('ArrowDown'); await page.keyboard.press('ArrowRight');
   assert.equal(await page.getByRole('menuitemradio').count(),3);
   assert.equal(await page.getByRole('menuitemradio',{name:'Smart Hide'}).getAttribute('aria-checked'),'true');
   await page.keyboard.press('ArrowLeft'); assert.equal(await page.getByRole('menuitemradio').count(),0);
   await page.getByRole('menuitemcheckbox').click();
   assert((await page.evaluate(()=>window.messages)).includes('menu:action:4'));
   await deliver({type:'lyrics',revision:4,title:'Recovery',playing:false,lyrics:{state:'temporary',retrySeconds:2}});
   assert(await page.locator('#dock-menu').isHidden());
   await page.waitForTimeout(2100); assert.match(await page.locator('.lyrics-plain').textContent(),/Retrying/);
   await deliver({type:'lyrics',revision:4,title:'Recovery',playing:false,lyrics:{state:'loading'}});
   assert.equal(await page.locator('.lyrics-plain').textContent(),'Searching…');
   await deliver({type:'lyrics',revision:4,title:'Recovery',playing:false,lyrics:{state:'network',exhausted:true}});
   await page.getByRole('button',{name:'Retry',exact:true}).click();
   assert((await page.evaluate(()=>window.messages)).includes('lyrics:retry'));
   await page.close();
   const apps=await pageFor('apps',dpi,290,56);
   await apps.evaluate(()=>window.deliver({type:'apps',items:[1,2,3,4].map(id=>({id:String(id),title:'App '+id,icon:''}))}));
   const first=apps.locator('.app').first();
   await first.click(); assert((await apps.evaluate(()=>window.messages)).includes('activate:1'));
   await apps.evaluate(()=>window.messages=[]);
   const box=await first.boundingBox();
   await apps.mouse.move(box.x+24,box.y+24); await apps.mouse.down();
   await apps.mouse.move(250,30,{steps:8}); await apps.mouse.up();
   const messages=await apps.evaluate(()=>window.messages);
   assert(messages.includes('reorder:2 3 4 1'),JSON.stringify(messages)); assert(!messages.some(m=>m.startsWith('activate:')));
   assert.equal(await apps.evaluate(()=>window.dockDragging),false);
   assert.deepEqual(await apps.evaluate(()=>[innerWidth,innerHeight]),[290,56]);
   await apps.locator('.app').last().focus(); await apps.keyboard.press('Alt+Shift+ArrowLeft');
   assert.equal(await apps.locator('.app').nth(2).getAttribute('data-id'),'1');
   const original=await apps.locator('.app').evaluateAll(nodes=>nodes.map(n=>n.dataset.id));
   const start=await apps.locator('.app').first().boundingBox();
   await apps.mouse.move(start.x+24,start.y+24); await apps.mouse.down(); await apps.mouse.move(250,30);
   await apps.keyboard.press('Escape'); await apps.mouse.up();
   assert.deepEqual(await apps.locator('.app').evaluateAll(nodes=>nodes.map(n=>n.dataset.id)),original);
   const capture=await apps.locator('.app').first().boundingBox();
   await apps.mouse.move(capture.x+24,capture.y+24); await apps.mouse.down(); await apps.mouse.move(250,30);
   await apps.evaluate(()=>window.deliver({type:'native:capture-lost'})); await apps.mouse.up();
   assert.deepEqual(await apps.locator('.app').evaluateAll(nodes=>nodes.map(n=>n.dataset.id)),original);
   await apps.close();
  }
  console.log('PASS interaction: calendar first bounds/5+6 rows/English/reopen/small viewport/screenshots, menu keyboard/checks/exclusivity, Lyrics countdown/loading/retry, drag/click/cancel/keyboard at four DPIs');
 } finally { await browser.close(); }
})().catch(error=>{console.error(error);process.exitCode=1;});
