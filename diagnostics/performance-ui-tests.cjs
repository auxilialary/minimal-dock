const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require('playwright');

(async () => {
    const browser = await chromium.launch({channel: 'msedge', headless: true});
    async function pageFor(role, baseline = false) {
        const page = await browser.newPage({viewport: {width: 400, height: 56}});
        await page.addInitScript(() => {
            const listeners = [];
            window.chrome = {webview: {postMessage() {}, addEventListener: (_, fn) => listeners.push(fn)}};
            window.deliver = data => listeners.forEach(fn => fn({data}));
        });
        await page.route('https://dock.local/**', route => {
            const asset = new URL(route.request().url()).pathname.slice(1);
            let body = fs.readFileSync(path.join('ui', asset), 'utf8');
            // Replay the pre-audit media behavior against identical markup/styles.
            if (baseline && asset === 'media.js') body = body
                .replace("if (time.textContent !== label) time.textContent = label;", 'time.textContent = label;')
                .replace('visible && !document.hidden && playing', 'playing')
                .replace("if (data?.type === 'media:visibility') { visible = data.visible; tick(); return; }", '');
            route.fulfill({contentType: asset.endsWith('.css') ? 'text/css' : asset.endsWith('.js') ? 'text/javascript' : 'text/html', body});
        });
        await page.clock.install();
        await page.goto(`https://dock.local/index.html?role=${role}`);
        await page.evaluate(() => {
            const raf = window.requestAnimationFrame.bind(window);
            window.framesRun = 0;
            window.requestAnimationFrame = fn => raf(t => { ++window.framesRun; fn(t); });
        });
        return page;
    }
    const measured = [];
    for (const baseline of [true, false]) {
        const page = await pageFor('media', baseline);
        await page.evaluate(() => {
            window.labelWrites = 0;
            new MutationObserver(records => window.labelWrites += records.length)
                .observe(document.getElementById('media-time'), {childList: true});
            window.deliver({type: 'media', playing: true, title: 'Test', elapsed: 10, duration: 100});
        });
        await page.clock.runFor(1000);
        const writes = await page.evaluate(() => window.labelWrites);
        await page.evaluate(() => window.deliver({type: 'media:visibility', visible: false}));
        const before = await page.evaluate(() => window.framesRun);
        await page.clock.runFor(1000);
        const hiddenFrames = await page.evaluate(n => window.framesRun - n, before);
        measured.push({variant: baseline ? 'pre-audit behavior replay' : 'optimized', labelWrites: writes, hiddenFrames});
        if (!baseline) {
            assert(writes <= 2, 'time label changes only when the displayed second changes');
            assert.equal(hiddenFrames, 0, 'hidden media schedules no RAF');
            await page.evaluate(() => window.deliver({type: 'media:visibility', visible: true}));
            await page.clock.runFor(100);
            assert(await page.evaluate(n => window.framesRun > n, before), 'visible media resumes smoothly');
            await page.evaluate(() => window.deliver({type: 'media', playing: false}));
            const stopped = await page.evaluate(() => window.framesRun);
            await page.clock.runFor(1000);
            assert.equal(await page.evaluate(() => window.framesRun), stopped, 'paused media schedules no RAF');
        }
        await page.close();
    }
    const page = await pageFor('apps');
    await page.evaluate(() => window.deliver({type: 'apps', items: [1, 2, 3].map(id => ({id: String(id), title: 'Test'}))}));
    await page.clock.runFor(2000);
    await page.mouse.move(200, 30);
    await page.clock.runFor(3000);
    let settled = await page.evaluate(() => window.framesRun);
    await page.clock.runFor(1000);
    assert.equal(await page.evaluate(() => window.framesRun), settled, 'stationary magnification settles to zero RAF');
    await page.mouse.move(0, 0);
    await page.clock.runFor(3000);
    settled = await page.evaluate(() => window.framesRun);
    await page.clock.runFor(1000);
    assert.equal(await page.evaluate(() => window.framesRun), settled, 'pointer leave settles to zero RAF');
    console.log(JSON.stringify(measured, null, 2));
    console.log('PASS: media label mutations, hidden/pause RAF cancellation, resume, magnification settles and stays idle');
    await browser.close();
})().catch(error => { console.error(error); process.exit(1); });

