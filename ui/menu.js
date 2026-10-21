(() => {
    if (new URLSearchParams(location.search).get('role') !== 'popup') return;
    const host = document.getElementById('dock-menu');
    const send = value => window.chrome?.webview?.postMessage(value);
    let revision, state = {}, expanded = false;
    function render(focus = 0) {
        host.replaceChildren();
        const add = (label, action, checked, submenu = false, parent = host) => {
            const button = document.createElement('button'); button.textContent = label;
            button.setAttribute('role', checked === undefined ? 'menuitem' : 'menuitemradio');
            if (checked !== undefined) button.setAttribute('aria-checked', String(checked));
            if (action === 4) button.setAttribute('role','menuitemcheckbox');
            if (submenu) { button.setAttribute('aria-haspopup','menu'); button.setAttribute('aria-expanded',String(expanded)); }
            button.onclick = () => {
                if (submenu) { expanded = !expanded; render(expanded ? 3 : 2); }
                else send('menu:action:' + action);
            };
            parent.append(button); return button;
        };
        add('Sound Settings',1); add('Network Settings',2);
        add('Visibility',0,undefined,true);
        if (expanded) {
            const submenu = document.createElement('div'); submenu.className = 'menu-submenu';
            submenu.setAttribute('role','menu'); submenu.setAttribute('aria-label','Visibility'); host.append(submenu);
            ['Always Visible','Auto Hide','Smart Hide'].forEach((label,i) => {
                add(label,10+i,state.visibility === i,false,submenu).className = 'menu-child';
            });
        }
        const startup = add('Start with Windows',4,!!state.startup);
        if (state.startupUnavailable) startup.disabled = true;
        add('Reset App Order',5); add('Exit Dock',3);
        host.querySelectorAll('button')[focus]?.focus();
        send(`popup:size:240 ${Math.ceil(host.scrollHeight + 2)} ${revision}`);
    }
    host.addEventListener('keydown', event => {
        if (event.key === 'Escape' && expanded) { event.preventDefault(); event.stopPropagation(); expanded = false; render(2); return; }
        const buttons = [...host.querySelectorAll('button:not(:disabled)')];
        const index = buttons.indexOf(document.activeElement);
        if (['ArrowDown','ArrowUp','Home','End'].includes(event.key)) {
            event.preventDefault();
            const next = event.key === 'Home' ? 0 : event.key === 'End' ? buttons.length-1 :
                (index + (event.key === 'ArrowDown' ? 1 : -1) + buttons.length) % buttons.length;
            buttons[next]?.focus();
        }
        if (event.key === 'ArrowRight' && document.activeElement?.hasAttribute('aria-haspopup')) { event.preventDefault(); expanded = true; render(3); }
        if (event.key === 'ArrowLeft' && expanded) { event.preventDefault(); expanded = false; render(2); }
        if (event.key === 'Tab') event.preventDefault();
    });
    window.chrome?.webview?.addEventListener('message', ({data}) => {
        if (data?.type !== 'menu') {
            if (['lyrics','calendar','mixer','tooltip','popup:hidden'].includes(data?.type)) host.hidden = true;
            return;
        }
        state = data; revision = data.revision; expanded = false; host.hidden = false;
        host.classList.remove('popup-enter'); void host.offsetWidth; host.classList.add('popup-enter'); render();
    });
})();
