const assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path');
const {chromium} = require('playwright');
const core = require('../ui/lyrics-core');
assert.deepEqual(core.parse('[ar:Artist]\r\n[00:01.2][01:02.345]Hello\r\n[00:03]'), [
    {time:1.2,text:'Hello'}, {time:3,text:''}, {time:62.345,text:'Hello'}]);
assert.equal(core.parse('[00:99]bad\n[x]\n[00:02.123456789]Fine').length, 1);
assert.equal(core.parse('x'.repeat(262145)).length, 0);
assert.equal(core.parse('[00:01]x\n'.repeat(4001)).length, 0);
assert.equal(core.parse('[00:02.123456]Fine')[0].time, 2.123456);
assert.equal(core.parse('[offset:-500]\n[00:01.0]Hi')[0].time, .5);
const lines = core.parse('[00:00]A\n[00:02]B\n[00:04]C');
assert.equal(core.active(lines, -1), -1); assert.equal(core.active(lines, 2), 1);
assert.equal(core.position(2, 1000, 3000, true, 10), 4);
assert.equal(core.position(2, 1000, 3000, false, 10), 2);
assert.equal(core.position(8, 1000, 5000, true, 10), 10);
(async () => {
    const browser = await chromium.launch({channel:'msedge', headless:true});
    try {
        for (const dpi of [1,1.25,1.5,2]) for (const reduced of [false,true]) {
            const page = await browser.newPage({viewport:{width:380,height:420},deviceScaleFactor:dpi,reducedMotion:reduced?'reduce':'no-preference'});
            await page.clock.install();
            await page.addInitScript(() => {
                const listeners = []; window.messages = []; window.frames = new Set();
                const raf = window.requestAnimationFrame.bind(window), caf = window.cancelAnimationFrame.bind(window);
                window.requestAnimationFrame = callback => { let id = raf(time => { window.frames.delete(id); callback(time); }); window.frames.add(id); return id; };
                window.cancelAnimationFrame = id => { window.frames.delete(id); caf(id); };
                window.chrome = {webview:{postMessage:m=>window.messages.push(m),addEventListener:(_,fn)=>listeners.push(fn)}};
                window.deliver = data => listeners.forEach(fn=>fn({data}));
            });
            await page.route('https://dock.local/**', route => {
                const name = new URL(route.request().url()).pathname.slice(1);
                return route.fulfill({contentType:name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html',body:fs.readFileSync(path.join('ui',name))});
            });
            await page.goto('https://dock.local/index.html?role=popup');
            await page.waitForFunction(()=>window.messages.includes('ui:painted'));
            await page.clock.pauseAt(await page.evaluate(()=>Date.now()+100));
            const send = data => page.evaluate(data=>window.deliver(data),data);
            const data = {type:'lyrics',revision:4,title:'Glass Split',artist:'Artist',elapsed:0,duration:200,playing:true,lyrics:{synced:'[00:00]First\n[00:01]Second\n[00:02]Third\n[00:10]<img src=x onerror=alert(1)>'}};
            await send(data);
            await page.clock.runFor(50);
            await page.waitForFunction(()=>window.messages.includes('popup:present:4'));
            assert.equal(await page.locator('#lyrics').isVisible(),true);
            assert.equal(await page.locator('#mixer').isHidden(),true);
            assert.equal(await page.locator('.lyric-active').textContent(),'First');
            await page.clock.runFor(1100);
            assert.equal(await page.locator('.lyric-active').textContent(),'Second');
            await send({...data,elapsed:2,playing:false});
            assert.equal(await page.locator('.lyric-active').textContent(),'Third');
            await page.clock.runFor(80); assert.equal(await page.evaluate(()=>window.frames.size),0);
            await send({...data,elapsed:1,playing:false});
            assert.equal(await page.locator('.lyric-active').textContent(),'Second');
            await send({...data,elapsed:10,playing:false});
            assert.equal(await page.locator('#lyrics img').count(),0);
            assert.equal(await page.locator('.lyric-active').textContent(),'<img src=x onerror=alert(1)>');
            if (reduced) assert.equal(await page.locator('.lyric-active').evaluate(n=>getComputedStyle(n).transitionDuration),'0s');
            await send({...data,elapsed:0});
            await page.evaluate(() => { Object.defineProperty(document,'hidden',{value:true,configurable:true}); document.dispatchEvent(new Event('visibilitychange')); });
            await page.clock.runFor(50); assert.equal(await page.evaluate(()=>window.frames.size),0);
            await page.evaluate(() => { Object.defineProperty(document,'hidden',{value:false,configurable:true}); document.dispatchEvent(new Event('visibilitychange')); });
            await page.locator('.lyrics-viewport').dispatchEvent('wheel',{deltaY:30});
            await send({...data,elapsed:2});
            await send({type:'popup:hidden'});
            await page.clock.runFor(100);
            assert.equal(await page.evaluate(()=>window.frames.size),0);
            for (const result of [{instrumental:true},{plain:'Plain\nSecond line'},{}]) {
                await send({...data,playing:false,lyrics:result});
                assert.equal(await page.locator('#lyrics p').count(),0);
                assert.equal(await page.locator('.lyrics-plain').textContent(),result.instrumental?'Instrumental':result.plain||'Lyrics not found');
            }
            await send({...data,lyrics:{plain:'x'.repeat(262145)}});
            assert.equal(await page.locator('.lyrics-plain').textContent(),'Lyrics not found');
            await send({...data,loading:true,lyrics:{}});
            assert.equal(await page.locator('.lyrics-plain').textContent(),'Searching…');
            for (const [state, text] of Object.entries({network:'Network unavailable',temporary:'Service temporarily unavailable',malformed:'Service returned an invalid response','rate-limited':'Rate limited',metadata:'Metadata insufficient',configuration:'Lyrics provider not configured','not-found':'Lyrics not found'})) {
                await send({...data, loading:false, lyrics:{state}});
                assert.equal(await page.locator('.lyrics-plain').textContent(),text);
                assert.equal(await page.locator('.lyrics-genius').isVisible(),true);
            }
            await send({...data,loading:false,lyrics:{state:'rate-limited',retrySeconds:30}});
            assert.equal(await page.locator('.lyrics-plain').textContent(),'Rate limited · Retry in 30s');
            await page.locator('.lyrics-genius').click();
            assert.equal(await page.evaluate(()=>window.messages.includes('lyrics:genius')),true);
            await send({type:'calendar',revision:5,locale:'en-US',firstDay:1});
            assert.equal(await page.locator('#lyrics').isHidden(),true);
            await send(data); assert.equal(await page.locator('#calendar').isHidden(),true);
            await send({type:'mixer',revision:6,rows:[]});
            assert.equal(await page.locator('#lyrics').isHidden(),true);
            await send({...data,revision:77}); await send({type:'popup:hidden'});
            assert.equal(await page.evaluate(()=>window.messages.includes('popup:present:77')),false);
            await page.clock.runFor(100);
            assert.equal(await page.evaluate(()=>window.frames.size),0);
            await send({...data,playing:false,lyrics:{synced:'[00:00]Quietly, the evening falls\n[00:04]And the city turns to glass\n[00:08]Every light becomes a memory\n[00:12]Every moment drifting past'},elapsed:8});
            if(dpi===1 && !reduced) if (process.env.DOCK_TEST_SCREENSHOTS) await page.screenshot({path:'diagnostics/lyrics-preview.png'});
            await page.keyboard.press('Escape');
            assert.equal(await page.evaluate(()=>window.messages.includes('popup:close')),true);
            assert.equal(await page.evaluate(()=>window.dockBoot.failed),false);
            await page.close();
        }
        console.log('PASS lyrics browser: parser, interpolation, seek/pause/resume snapshots, 8 DPI/motion cases, popup exclusivity/readiness, plain/instrumental/missing/loading, injection, oversize, hidden RAF cancellation, Escape');
    } finally { await browser.close(); }
})().catch(error=>{console.error(error);process.exitCode=1;});
