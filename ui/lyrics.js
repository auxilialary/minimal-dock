(() => {
    if (new URLSearchParams(location.search).get('role') !== 'popup') return;
    const host = document.getElementById('lyrics'), {parse, active, position} = window.lyricsCore;
    const title = document.createElement('header'), artist = document.createElement('div');
    artist.className = 'lyrics-artist';
    const viewport = document.createElement('div'); viewport.className = 'lyrics-viewport'; viewport.tabIndex = 0;
    viewport.setAttribute('aria-label', 'Lyrics');
    const credit = document.createElement('footer'); credit.textContent = 'Lyrics by LRCLIB';
    const genius = document.createElement('button'); genius.textContent = 'Search on Genius'; genius.className = 'lyrics-genius'; genius.hidden = true;
    genius.addEventListener('click', () => window.chrome?.webview?.postMessage('lyrics:genius'));
    host.append(title, artist, viewport, genius, credit);
    const retry = document.createElement('button'); retry.textContent = 'Retry'; retry.hidden = true;
    retry.addEventListener('click', () => { retry.hidden = true; window.chrome?.webview?.postMessage('lyrics:retry'); });
    host.append(retry);
    let countdown = 0;
    let lines = [], nodes = [], frame = 0, current = -2, key = '', snapshot = {}, base = 0, idle = 0, resume = 0;
    let reduced = false;
    const stop = () => { cancelAnimationFrame(frame); frame = 0; clearTimeout(resume); clearTimeout(countdown); };
    const manual = () => {
        idle = performance.now() + 4500; clearTimeout(resume);
        resume = setTimeout(() => { current = -2; tick(); }, 4550);
    };
    viewport.addEventListener('wheel', manual, {passive: true});
    viewport.addEventListener('touchstart', manual, {passive: true});
    viewport.addEventListener('keydown', manual);
    function tick() {
        cancelAnimationFrame(frame); frame = 0;
        if (host.hidden || document.hidden) return;
        const now = performance.now();
        const p = position(snapshot.elapsed || 0, base, now, snapshot.playing, snapshot.duration);
        const index = active(lines, p);
        if (index !== current || (idle && now >= idle)) {
            current = index;
            nodes.forEach((node, i) => {
                node.classList.toggle('lyric-active', i === index);
                if (i === index) node.setAttribute('aria-current', 'true'); else node.removeAttribute('aria-current');
            });
            if (now >= idle && nodes[index]) {
                idle = 0;
                viewport.scrollTo({top: nodes[index].offsetTop - viewport.offsetTop - viewport.clientHeight / 2 + nodes[index].clientHeight / 2,
                    behavior: reduced ? 'instant' : 'smooth'});
            }
        }
        if (snapshot.playing && lines.length && p < lines[lines.length-1].time && (!snapshot.duration || p < snapshot.duration)) frame = requestAnimationFrame(tick);
    }
    document.addEventListener('visibilitychange', () => { if (document.hidden) stop(); else tick(); });
    window.chrome?.webview?.addEventListener('message', ({data}) => {
        if (data?.type !== 'lyrics') {
            if (['menu','mixer','calendar','tooltip','popup:hidden'].includes(data?.type)) { host.hidden = true; stop(); key = ''; }
            return;
        }
        host.hidden = false;
        title.textContent = data.title || 'Now Playing'; artist.textContent = data.artist || '';
        reduced = data.reduced || matchMedia('(prefers-reduced-motion: reduce)').matches;
        host.classList.toggle('lyrics-reduced', reduced);
        // Small corrections retain the current visual clock; seeks replace it immediately.
        const now = performance.now(), predicted = position(snapshot.elapsed || 0, base, now, snapshot.playing, snapshot.duration);
        const contentKey = JSON.stringify([data.title, data.artist, data.loading, data.lyrics]);
        const changed = contentKey !== key;
        snapshot = {...data, elapsed: !changed && data.playing && Math.abs(predicted-data.elapsed) < .15 ? predicted : data.elapsed}; base = now;
        if (changed) {
            key = contentKey; stop(); viewport.replaceChildren(); viewport.scrollTop = 0; idle = 0; current = -2;
            const result = data.lyrics || {}; data.loading ||= result.state === 'loading'; retry.hidden = !result.exhausted;
            genius.hidden = data.loading || result.instrumental || !!result.synced || !!result.plain || !data.title; lines = parse(result.synced); nodes = [];
            if (lines.length && !result.instrumental) {
                for (const line of lines) { const node = document.createElement('p'); node.textContent = line.text || '\u00a0'; viewport.append(node); nodes.push(node); }
                viewport.classList.add('lyrics-synced');
            } else {
                lines = []; viewport.classList.remove('lyrics-synced');
                const text = result.instrumental ? 'Instrumental' : result.plain || (data.loading ? 'Searching…' : ({network:'Network unavailable',temporary:'Service temporarily unavailable',malformed:'Service returned an invalid response','rate-limited':'Rate limited',metadata:'Metadata insufficient',configuration:'Lyrics provider not configured'}[result.state] || 'Lyrics not found'));
                const node = document.createElement('div'); node.className = 'lyrics-plain';
                const estimate = !data.loading && !result.exhausted && result.retrySeconds > 0 ? ` · Retry in ${Math.ceil(result.retrySeconds)}s` : '';
                node.textContent = typeof text === 'string' && text.length <= 262144 ? text + estimate : 'Lyrics not found'; viewport.append(node);
                if (result.retrySeconds > 0 && !result.exhausted) {
                    const deadline = performance.now() + result.retrySeconds * 1000;
                    const update = () => {
                        const seconds = Math.max(0, Math.ceil((deadline - performance.now()) / 1000));
                        node.textContent = text + (seconds ? ` · Retry in ${seconds}s` : ' · Retrying…');
                        if (seconds && !host.hidden) countdown = setTimeout(update, 200);
                    }; update();
                }
            }
        }
        tick();
    });
})();
