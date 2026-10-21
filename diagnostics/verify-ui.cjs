const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
(async () => {
 const browser = await chromium.launch({channel:'msedge', headless:true});
 for (const scale of [1, 1.25, 1.5, 2]) for (const [role, broken] of [['system',null],['apps',null],['system','theme.css'],['system','system.js']]) {
  if (scale !== 1 && broken) continue;
  const page = await browser.newPage({deviceScaleFactor:scale,reducedMotion:'reduce',viewport:{width:role==='apps'?400:120,height:56}});
  await page.addInitScript(() => {
   window.messages=[];
   const listeners=[];
   window.chrome={webview:{postMessage:m=>window.messages.push(m),addEventListener:(type,fn)=>listeners.push(fn)}};
   window.deliver = data => listeners.forEach(fn=>fn({data}));
  });
  await page.route('https://dock.local/**', route => {
   const asset = new URL(route.request().url()).pathname.slice(1);
   if(asset === broken) return route.fulfill({status:404,body:'missing'});
   const mime = asset.endsWith('.css')?'text/css':asset.endsWith('.js')?'text/javascript':'text/html';
   return route.fulfill({status:200,contentType:mime,body:fs.readFileSync(path.join('x64/Release/ui',asset))});
  });
  await page.goto('https://dock.local/index.html?role='+role);
  if(role==='apps') await page.evaluate(()=>window.deliver({type:'apps',items:[{id:'1',title:'Readiness test',active:true,icon:''}]}));
  await page.waitForFunction(() => window.messages.some(m=>m==='ui:painted'||m.startsWith('ui:error:')));
  if (role === 'system' && !broken) {
   if (await page.locator('#network').count()) throw new Error('Wi-Fi still exists');
   for (const selector of ['#sound','#clock']) {
    const r=await page.locator(selector).boundingBox();
    if(!r || r.x<0 || r.x+r.width>120) throw new Error('System content clipped');
    if(Math.abs(r.y+r.height/2-28)>.01) throw new Error('System control not vertically centered');
   }
  }
  if (role === 'apps' && !broken) {
   await page.evaluate(() => {
    window.originalApp = document.querySelector('.app');
    window.deliver({type:'apps',items:[{id:'1',title:'Readiness test',active:false,icon:''},{id:'2',title:'Foreground',active:true,icon:''}]});
   });
   const focus = await page.evaluate(() => ({active:[...document.querySelectorAll('.app.active')].map(n=>n.getAttribute('aria-label')),stable:document.querySelector('.app') === window.originalApp}));
   if (focus.active.join() !== 'Foreground' || !focus.stable) throw new Error('Focus update or DOM stability failed');
   await page.setViewportSize({width:100,height:56});
   const overflow = await page.evaluate(() => {const a=document.getElementById('apps');return a.scrollWidth>a.clientWidth && getComputedStyle(a).overflowX==='auto'});
   if (!overflow) throw new Error('Narrow apps island must scroll');
   await page.setViewportSize({width:290,height:56});
   const icon = color => 'data:image/svg+xml,'+encodeURIComponent(`<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><rect width="64" height="64" fill="${color}"/></svg>`);
   const first = icon('red'), updated = icon('blue');
   await page.evaluate(src => window.deliver({type:'apps',items:[1,2,3].map(n=>({id:String(n),title:'Icon '+n,icon:src}))}), first);
   await page.evaluate(src => window.deliver({type:'apps',items:[1,2,3].map(n=>({id:String(n),title:'Icon '+n,icon:src}))}), updated);
   await page.waitForFunction(src => [...document.querySelectorAll('.app-icon')].every(i=>i.complete&&i.naturalWidth&&i.getAttribute('src')===src), updated);
   const geometry = await page.evaluate(() => [...document.querySelectorAll('.app-icon')].map(i=>{
    const r=i.getBoundingClientRect(), d=devicePixelRatio;
    return {x:r.x*d,y:r.y*d,width:r.width*d,height:r.height*d};
   }));
   for (const r of geometry) for (const [key,value] of Object.entries(r)) {
    if (Math.abs(value-Math.round(value))>0.04) throw new Error(`Fractional physical icon ${scale}: ${key}=${value}`);
    if ((key==='width'||key==='height')&&Math.abs(value-Math.round(24*scale))>0.04) throw new Error('Wrong physical icon size');
   }
   const stable = await page.evaluate(()=>document.querySelector('.app')===window.originalApp);
   if (!stable) throw new Error('Icon/DPI source refresh lost button identity');
   console.log(JSON.stringify({scale,geometry,iconSourceUpdated:true,stable}));
  }
  const messages=await page.evaluate(()=>window.messages);
  const passed = broken ? messages.some(m=>m.startsWith('ui:error:'))&&!messages.includes('ui:painted') : messages.includes('ui:painted');
  if (!passed) throw new Error(JSON.stringify({role,broken,messages}));
  if (!broken) {
   const glass = await page.evaluate(() => {
    const island = getComputedStyle(document.getElementById('island'));
    return {
     backgrounds: [document.documentElement, document.body, document.getElementById('island')].map(n=>getComputedStyle(n).backgroundColor),
     border: island.borderTopColor, width: island.borderTopWidth, shadow: island.boxShadow,
     radius: island.borderTopLeftRadius
    };
   });
   if (glass.backgrounds.some(c=>c!=='rgba(0, 0, 0, 0)') ||
       glass.width!=='0px' || glass.shadow!=='none' || glass.radius!=='0px') throw new Error('Glass styles: '+JSON.stringify(glass));
   console.log(JSON.stringify({role,scale,glass}));
  }
  if (!broken && scale===1) if (process.env.DOCK_TEST_SCREENSHOTS) await page.screenshot({path:'diagnostics/quality-'+role+'.png',omitBackground:true});
  console.log(JSON.stringify({role,scale,broken,messages,passed}));
  await page.close();
 }
 await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});

