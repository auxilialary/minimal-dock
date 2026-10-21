(() => {
    if (new URLSearchParams(location.search).get('role') !== 'media') return;
    const media = document.getElementById('media'), art = document.getElementById('media-art');
    const title = document.getElementById('media-title'), time = document.getElementById('media-time');
    const progress = document.getElementById('media-progress'), fill = document.getElementById('media-progress-fill');
    media.hidden = false;
    media.setAttribute('role', 'button'); media.tabIndex = 0;
    media.setAttribute('aria-haspopup', 'dialog');
    media.setAttribute('aria-label', 'Show lyrics');
    const open = () => window.chrome?.webview?.postMessage('lyrics');
    media.addEventListener('click', open);
    media.addEventListener('keydown', e => {
        if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); open(); }
    });
    let timer = 0, base = 0, elapsed = 0, duration = 0, playing = false, visible = true;
    const format = value => `${Math.floor(value / 60)}:${String(Math.floor(value % 60)).padStart(2, '0')}`;
    function tick() {
        cancelAnimationFrame(timer); timer = 0;
        const position = elapsed + (playing ? (performance.now() - base) / 1000 : 0);
        progress.hidden = duration <= 0;
        const current = Math.min(position, duration);
        fill.style.transform = `scaleX(${duration > 0 ? current / duration : 0})`;
        if (duration > 0) {
            progress.setAttribute('aria-valuemin', '0'); progress.setAttribute('aria-valuemax', String(duration));
            const seconds = String(Math.floor(current));
            if (progress.getAttribute('aria-valuenow') !== seconds) progress.setAttribute('aria-valuenow', seconds);
        } else progress.removeAttribute('aria-valuenow');
        const label = duration > 0 ? `${format(Math.min(position, duration))} / ${format(duration)}` : '';
        if (time.textContent !== label) time.textContent = label;
        if (visible && !document.hidden && playing && duration > 0 && position < duration) timer = requestAnimationFrame(tick);
    }
    art.addEventListener('error', () => { art.hidden = true; });
    document.addEventListener('visibilitychange', tick);
    window.chrome?.webview?.addEventListener('message', ({data}) => {
        if (data?.type === 'media:visibility') { visible = data.visible; tick(); return; }
        if (data?.type !== 'media') return;
        if (!data.playing) {
            elapsed = duration > 0 ? Math.min(duration, elapsed + (playing ? (performance.now() - base) / 1000 : 0)) : 0;
            playing = false; tick(); return;
        }
        playing = true;
        // Preserve title/artwork throughout native fade-out.
        elapsed = Number.isFinite(data.elapsed) ? Math.max(0, data.elapsed) : 0; duration = Number.isFinite(data.duration) ? Math.max(0, data.duration) : 0; base = performance.now();
        title.textContent = data.title || 'Now Playing';
        if (art.getAttribute('src') !== data.artwork) {
            art.hidden = !data.artwork;
            if (data.artwork) art.src = data.artwork; else art.removeAttribute('src');
        }
        tick();
    });
})();
