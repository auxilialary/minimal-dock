const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require('playwright');
(async () => {
    const browser = await chromium.launch({channel: 'msedge', headless: true});
    try {
        for (const initial of [true, false]) for (const dpi of [1, 1.25, 1.5, 2]) {
            const page = await browser.newPage({viewport: {width: 220, height: 56}, deviceScaleFactor: dpi});
            // Model native latest-snapshot replay; this does not exercise GSMTC or WebView2.
            await page.addInitScript(({initial}) => {
                const listeners = [];
                window.messages = [];
                window.current = {type: 'media', playing: initial, title: 'Before boot', artwork: '', elapsed: 12, duration: 200};
                window.deliver = data => listeners.forEach(fn => fn({data}));
                window.chrome = {webview: {
                    addEventListener: (_, fn) => listeners.push(fn),
                    postMessage: message => {
                        window.messages.push(message);
                        if (message === 'ui:boot') {
                            window.deliver({type: 'media:visibility', visible: false});
                            window.deliver(window.current);
                        }
                    }
                }};
            }, {initial});
            await page.route('https://dock.local/**', route => {
                const name = new URL(route.request().url()).pathname.slice(1);
                return route.fulfill({contentType: name.endsWith('.js') ? 'text/javascript' : name.endsWith('.css') ? 'text/css' : 'text/html', body: fs.readFileSync(path.join('ui', name))});
            });
            await page.goto('https://dock.local/index.html?role=media');
            await page.waitForFunction(() => window.messages.includes('ui:painted'));
            assert.equal(await page.evaluate(() => window.dockBoot.failed), false);
            if (initial) assert.equal(await page.locator('#media-title').textContent(), 'Before boot');
            for (const playing of [true, false, true, false, true]) {
                await page.evaluate(playing => window.deliver({...window.current, playing, title: 'After boot'}), playing);
                await page.evaluate(() => window.deliver({type: 'media:visibility', visible: true}));
            }
            assert.equal(await page.locator('#media-title').textContent(), 'After boot');
            await page.evaluate(() => window.deliver({...window.current, playing: true, title: 'Other player'}));
            assert.equal(await page.locator('#media-title').textContent(), 'Other player');
            const artwork = 'data:image/svg+xml,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10"><rect width="10" height="10" fill="red"/></svg>');
            await page.evaluate(artwork => window.deliver({...window.current, playing: true, artwork}), artwork);
            await page.waitForFunction(() => document.getElementById('media-art').naturalWidth > 0);
            assert.equal(await page.locator('#media-art').isVisible(), true);
            await page.evaluate(() => window.deliver({...window.current, playing: true, artwork: ''}));
            assert.equal(await page.locator('#media-art').isHidden(), true);
            await page.close();
        }
        console.log('PASS: 8 media boot/paint cases, cached playing/inactive snapshot replay, start after boot, pause/resume/stop, player/title/artwork replacement. Browser bridge simulation only.');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exit(1); });
