(() => {
    if (new URLSearchParams(location.search).get('role') !== 'apps') return;
    const container = document.getElementById('apps');
    const send = value => window.chrome?.webview?.postMessage(value);
    let target = null, timer = 0, visible = false, last = '';
    function hide() {
        clearTimeout(timer); timer = 0; target = null; visible = false; last = '';
        send('tooltip:hide');
    }
    function position() {
        if (!visible || !target?.isConnected) {
            if (visible) hide();
            return;
        }
        const rect = target.getBoundingClientRect();
        const message = `tooltip:${target.dataset.id} ${(rect.left + rect.width / 2).toFixed(2)}`;
        if (message !== last) { send(message); last = message; }
    }
    container.addEventListener('pointerover', event => {
        const next = event.target.closest('.app');
        if (!next || next === target) return;
        hide(); target = next;
        timer = setTimeout(() => { visible = true; position(); }, 150);
    });
    container.addEventListener('pointerleave', hide);
    container.addEventListener('pointerout', event => {
        if (target && !target.contains(event.relatedTarget)) hide();
    });
    container.addEventListener('pointerdown', hide);
    container.addEventListener('scroll', hide, {passive: true});
    new MutationObserver(() => { if (target && !target.isConnected) hide(); })
        .observe(container, {childList: true});
    window.addEventListener('blur', hide);
    window.chrome?.webview?.addEventListener('message', event => {
        if (event.data?.type === 'apps') queueMicrotask(position);
    });
    window.dockTooltip = {position};
})();
