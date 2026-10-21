(() => {
    if (new URLSearchParams(location.search).get('role') !== 'apps') return;
    const host = document.getElementById('apps');
    const send = value => window.chrome?.webview?.postMessage(value);
    let gesture = null, suppress = null;
    const commit = () => send('reorder:' + [...host.children].map(node => node.dataset.id).join(' '));
    const animate = mutate => {
        const before = new Map([...host.children].map(node => [node, node.offsetLeft]));
        mutate();
        for (const node of host.children) {
            const delta = before.get(node) - node.offsetLeft;
            if (delta && !matchMedia('(prefers-reduced-motion: reduce)').matches)
                node.animate([{translate: `${delta}px 0`}, {translate:'0 0'}], {duration:160, easing:'ease-out'});
        }
        window.alignDockIcons?.(); window.dockMagnification?.refresh();
    };
    host.addEventListener('dragstart', event => event.preventDefault());
    host.addEventListener('pointerdown', event => {
        if (event.button !== 0) return;
        const node = event.target.closest('.app'); if (!node) return;
        suppress = null;
        gesture = {node, id:event.pointerId, x:event.clientX, active:false, original:[...host.children], order:[...host.children]};
        node.setPointerCapture(event.pointerId);
    });
    host.addEventListener('pointermove', event => {
        const g = gesture; if (!g || g.id !== event.pointerId) return;
        if (!g.active && Math.abs(event.clientX - g.x) < 6) return;
        if (!g.active) {
            g.active = true; window.dockDragging = true;
            host.dispatchEvent(new Event('pointerleave')); send('tooltip:hide');
            host.classList.add('reordering'); g.node.classList.add('app-dragging');
        }
        const siblings = g.order.filter(node => node !== g.node);
        const next = siblings.find(node => event.clientX < (node.offsetParent?.getBoundingClientRect().left || 0) +
            node.offsetLeft - host.scrollLeft + node.offsetWidth / 2);
        const position = next ? siblings.indexOf(next) : siblings.length;
        siblings.splice(position,0,g.node);
        if (siblings.some((node,i)=>node!==g.order[i])) animate(() => {
            g.order = siblings;
            g.order.forEach((node,i)=>node.style.order=String(i));
        });
        event.preventDefault();
    });
    function finish(cancel = false) {
        const g = gesture; if (!g) return;
        gesture = null;
        if (g.node.hasPointerCapture(g.id)) g.node.releasePointerCapture(g.id);
        if (g.active || cancel) {
            suppress = g.node;
            animate(() => {
                (cancel ? g.original : g.order).filter(node=>node.isConnected).forEach(node=>host.append(node));
                [...host.children].forEach(node=>node.style.removeProperty('order'));
            });
            if (!cancel) commit();
        }
        g.node.classList.remove('app-dragging'); host.classList.remove('reordering'); window.dockDragging = false;
        window.dockMagnification?.refresh();
    }
    host.addEventListener('pointerup', event => finish(event.clientX < 0 || event.clientX > innerWidth || event.clientY < 0 || event.clientY > innerHeight));
    host.addEventListener('pointercancel', () => finish(true));
    host.addEventListener('lostpointercapture', () => finish(true));
    window.addEventListener('blur', () => finish(true));
    host.addEventListener('click', event => {
        if (event.target.closest('.app') === suppress) { event.preventDefault(); event.stopImmediatePropagation(); suppress = null; }
    }, true);
    host.addEventListener('keydown', event => {
        if (event.key === 'Escape' && gesture) { event.preventDefault(); finish(true); return; }
        if (!event.altKey || !event.shiftKey || !['ArrowLeft','ArrowRight'].includes(event.key)) return;
        const node = event.target.closest('.app'); if (!node) return;
        const next = event.key === 'ArrowLeft' ? node.previousElementSibling : node.nextElementSibling;
        if (!next) return;
        event.preventDefault(); animate(() => host.insertBefore(node, event.key === 'ArrowLeft' ? next : next.nextElementSibling));
        node.focus(); commit();
    });
    window.dockReorder = {cancel: () => finish(true)};
    window.chrome?.webview?.addEventListener('message', ({data}) => {
        if (data?.type === 'native:capture-lost') finish(true);
    });
})();
