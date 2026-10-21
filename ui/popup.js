(() => {
    if (new URLSearchParams(location.search).get('role') !== 'popup') return;
    let revision;
    const send = value => window.chrome?.webview?.postMessage(value.startsWith('popup:size:') && revision !== undefined ? value + ' ' + revision : value);
    const tooltip = document.getElementById('tooltip'), mixer = document.getElementById('mixer');
    const rows = document.getElementById('audio-rows'), error = document.getElementById('audio-error');
    const nodes = new Map();
    document.addEventListener('pointerdown', () => { mixer.dataset.input = 'pointer'; }, true);
    document.addEventListener('keydown', () => { mixer.dataset.input = 'keyboard'; }, true);
    function clearMixer() {
        cancelAnimationFrame(frame); frame = 0; pending.clear(); dragging = null;
        nodes.clear(); rows.replaceChildren(); lastSize = '';
    }
    // Acknowledge the new surface only after DOM replacement and a paint opportunity.
    let presentation = 0;
    window.chrome?.webview?.addEventListener('message', ({data}) => {
        if (!['lyrics', 'calendar', 'mixer', 'tooltip', 'popup:hidden'].includes(data?.type)) return;
        const ticket = ++presentation;
        if (data.type === 'popup:hidden' || data.revision === undefined) return;
        requestAnimationFrame(() => requestAnimationFrame(() => {
            if (ticket === presentation) send('popup:present:' + data.revision);
        }));
    });
    const fallbackIcon = 'data:image/svg+xml,' + encodeURIComponent(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="none" stroke="${getComputedStyle(document.documentElement).getPropertyValue('--dock-text').trim()}" stroke-width="1.5" stroke-linejoin="round" d="M4 9v6h4l5 4V5L8 9H4zm13 0a5 5 0 0 1 0 6"/></svg>`);
    let mode = '', dragging = null, frame = 0, pending = new Map(), lastSize = '';
    const resize = () => {
        if (!mode) return;
        const style = getComputedStyle(mixer);
        const limit = parseFloat(style.getPropertyValue('--mixer-max-height'));
        const height = mode === 'tooltip' ? 36 : Math.ceil(Math.min(limit,
            mixer.getBoundingClientRect().height + rows.scrollHeight - rows.clientHeight));
        const canvas = document.createElement('canvas').getContext('2d');
        canvas.font = getComputedStyle(tooltip).font;
        const width = mode === 'tooltip' ? Math.min(360, Math.ceil(canvas.measureText(tooltip.textContent).width) + 28) : 320;
        const size = `${width} ${height}`;
        if (size !== lastSize) { lastSize = size; send('popup:size:' + size); }
    };
    function flush() {
        cancelAnimationFrame(frame); frame = 0;
        for (const [id, value] of pending) send(`audio:volume ${id} ${value}`);
        pending.clear();
    }
    function row(item) {
        const node = document.createElement('div'); node.className = 'audio-row';
        const icon = document.createElement('img'); icon.className = 'audio-icon'; icon.alt = '';
        const name = document.createElement('span'); name.className = 'audio-name';
        const percent = document.createElement('output'); percent.className = 'audio-percent';
        const slider = document.createElement('input'); slider.type = 'range'; slider.min = 0; slider.max = 100; slider.step = 1; slider.className = 'audio-slider';
        const mute = document.createElement('button'); mute.className = 'audio-mute'; mute.type = 'button';
        slider.addEventListener('pointerdown', () => { dragging = item.id; });
        slider.addEventListener('input', () => {
            slider.style.setProperty('--volume', slider.value + '%');
            percent.textContent = slider.value + '%'; pending.set(item.id, Number(slider.value));
            if (!frame) frame = requestAnimationFrame(flush);
        });
        slider.addEventListener('change', flush);
        slider.addEventListener('pointerup', () => { flush(); dragging = null; });
        slider.addEventListener('pointercancel', () => { flush(); dragging = null; });
        mute.addEventListener('click', () => { flush(); send(`audio:mute ${item.id} ${mute.getAttribute('aria-pressed') === 'true' ? 0 : 1}`); });
        node.append(icon, name, percent, slider, mute);
        return {node, icon, name, percent, slider, mute};
    }
    window.chrome?.webview?.addEventListener('message', event => {
        const data = event.data;
        if (['lyrics','calendar','mixer','tooltip','popup:hidden'].includes(data?.type)) revision = data.revision;
        if (data?.type === 'popup:hidden') {
            mode = ''; tooltip.hidden = mixer.hidden = true; cancelAnimationFrame(frame);
            clearMixer(); return;
        }
        if (data?.type === 'calendar' || data?.type === 'lyrics') { mode = ''; tooltip.hidden = mixer.hidden = true; clearMixer(); return; }
        if (!['tooltip', 'mixer'].includes(data?.type)) return;
        const changed = mode !== data.type; mode = data.type;
        if (changed) clearMixer();
        tooltip.hidden = mode !== 'tooltip'; mixer.hidden = mode !== 'mixer';
        if (mode === 'tooltip') tooltip.textContent = data.title;
        else {
            error.textContent = data.error || ''; error.hidden = !data.error;
            const ids = new Set(data.rows.filter(r => r.id !== 0).map(r => r.id));
            for (const [id, r] of nodes) if (!ids.has(id)) {
                r.node.remove(); nodes.delete(id); pending.delete(id); if (dragging === id) dragging = null;
            }
            for (const item of data.rows.filter(r => r.id !== 0)) {
                let r = nodes.get(item.id);
                if (!r) { r = row(item); nodes.set(item.id, r); rows.append(r.node); }
                r.name.textContent = item.name; r.name.title = item.identity || item.name;
                const artwork = item.icon || fallbackIcon;
                if (r.icon.getAttribute('src') !== artwork) r.icon.src = artwork;
                r.slider.setAttribute('aria-label', item.name + ' volume');
                if (dragging !== item.id && !pending.has(item.id)) { r.slider.value = item.volume; r.percent.textContent = item.volume + '%'; r.slider.style.setProperty('--volume', item.volume + '%'); }
                r.mute.innerHTML = `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 9v6h4l5 4V5L8 9H4Z"/><path d="${item.muted ? 'm17 9 5 6m0-6-5 6' : 'M17 9a5 5 0 0 1 0 6'}"/></svg>`;
                r.mute.setAttribute('aria-label', (item.muted ? 'Unmute ' : 'Mute ') + item.name);
                r.mute.setAttribute('aria-pressed', String(item.muted));
            }
        }
        if (changed) {
            const content = mode === 'tooltip' ? tooltip : mixer;
            content.classList.remove('popup-enter'); void content.offsetWidth; content.classList.add('popup-enter');
            if (mode === 'mixer') requestAnimationFrame(() => {
                if (mode === 'mixer') mixer.querySelector('input, button')?.focus();
            });
        }
        resize();
    });
    document.addEventListener('keydown', event => { if (event.key === 'Escape') { event.preventDefault(); send('popup:close'); } });
    // A slider owns its wheel; all other mixer areas scroll only the list.
    mixer.addEventListener('wheel', event => {
        event.preventDefault(); event.stopPropagation();
        if (event.target.matches('.audio-slider')) {
            const entry = [...nodes].find(([, row]) => row.slider === event.target);
            if (entry) { flush(); send(`audio:wheel ${entry[0]} ${window.volumeWheel(event)}`); }
            return;
        }
        const unit = event.deltaMode === 1 ? parseFloat(getComputedStyle(mixer).fontSize) :
            event.deltaMode === 2 ? rows.clientHeight : 1;
        rows.scrollTop += (event.deltaY || event.deltaX) * unit;
    }, {passive: false});
    rows.addEventListener('keydown', event => {
        if (event.target !== rows) return; // Ranges retain native precise keyboard controls.
        const step = Number(getComputedStyle(document.documentElement).getPropertyValue('--mixer-scroll-step'));
        const delta = {ArrowDown: step, ArrowUp: -step, PageDown: rows.clientHeight, PageUp: -rows.clientHeight};
        if (event.key === 'Home') rows.scrollTop = 0;
        else if (event.key === 'End') rows.scrollTop = rows.scrollHeight;
        else if (event.key in delta) rows.scrollTop += delta[event.key];
        else return;
        event.preventDefault(); event.stopPropagation();
    });
    window.addEventListener('resize', resize);
})();

