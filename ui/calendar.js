// Local-noon dates avoid midnight DST transitions; no event storage or network state.
(function(root) {
    const day = (year, month, date) => new Date(year, month, date, 12);
    const cells = (year, month, firstDay) => {
        const start = day(year, month, 1);
        start.setDate(1 - (start.getDay() - firstDay + 7) % 7);
        const offset = (day(year, month, 1).getDay() - firstDay + 7) % 7;
        const length = Math.max(5, Math.ceil((offset + day(year, month + 1, 0).getDate()) / 7)) * 7;
        return Array.from({length}, (_, i) => day(start.getFullYear(), start.getMonth(), start.getDate() + i));
    };
    const moveMonth = (date, delta) => {
        const last = day(date.getFullYear(), date.getMonth() + delta + 1, 0).getDate();
        return day(date.getFullYear(), date.getMonth() + delta, Math.min(date.getDate(), last));
    };
    const formatTime = (date, pattern, am = 'AM', pm = 'PM') => pattern.replace(/'([^']|'')*'|HH?|hh?|mm?|ss?|tt?/g, token => {
        if (token[0] === "'") return token.slice(1,-1).replace(/''/g,"'");
        if (token[0] === 't') { const marker = date.getHours()<12 ? am : pm; return token.length===1 ? marker.slice(0,1) : marker; }
        const n = token[0] === 'H' ? date.getHours() : token[0] === 'h' ? date.getHours()%12||12 : token[0] === 'm' ? date.getMinutes() : date.getSeconds();
        return String(n).padStart(token.length,'0');
    });
    root.calendarDates = {day, cells, moveMonth, formatTime};
    if (typeof module !== 'undefined') module.exports = root.calendarDates;
})(globalThis);
(() => {
    if (typeof document === 'undefined' || new URLSearchParams(location.search).get('role') !== 'popup') return;
    const host = document.getElementById('calendar'), {day, cells, moveMonth, formatTime} = window.calendarDates;
    let revision;
    const send = value => window.chrome?.webview?.postMessage(value.startsWith('popup:size:') && revision !== undefined ? value + ' ' + revision : value);
    host.innerHTML = `<div class="calendar-now"><div id="calendar-date"></div><time id="calendar-time"></time></div>
        <nav aria-label="Calendar navigation"><button id="calendar-prev" aria-label="Previous month">‹</button><button id="calendar-month" aria-label="Return to today"></button><button id="calendar-next" aria-label="Next month">›</button></nav>
        <div id="calendar-weekdays" aria-hidden="true"></div><div id="calendar-days" role="grid" aria-label="Month"></div>`;
    const grid = host.querySelector('#calendar-days'), month = host.querySelector('#calendar-month');
    let preferences = {}, openedDate = '';
    let locale, firstDay = 1, hour12, selected = new Date(), shown = new Date(), timer = 0;
    const key = d => `${d.getFullYear()}-${d.getMonth()}-${d.getDate()}`;
    function render(focus = false) {
        const today = new Date(), fmt = new Intl.DateTimeFormat(locale, {dateStyle:'full',calendar:'gregory'});
        month.textContent = new Intl.DateTimeFormat(locale, {month:'long',year:'numeric',calendar:'gregory'}).format(shown);
        grid.setAttribute('aria-label', month.textContent);
        const week = host.querySelector('#calendar-weekdays'); week.replaceChildren();
        for (let i = 0; i < 7; ++i) {
            const span = document.createElement('span');
            span.textContent = new Intl.DateTimeFormat(locale, {weekday:'short'}).format(day(2024,0,7 + (firstDay+i)%7)); week.append(span);
        }
        grid.replaceChildren();
        const dates = cells(shown.getFullYear(), shown.getMonth(), firstDay);
        for (let rowIndex = 0; rowIndex < dates.length / 7; ++rowIndex) {
            const row = document.createElement('div'); row.setAttribute('role','row');
            for (const date of dates.slice(rowIndex*7,rowIndex*7+7)) {
                const button = document.createElement('button'); button.setAttribute('role','gridcell');
                button.textContent = date.getDate(); button.setAttribute('aria-label',fmt.format(date));
                button.dataset.date = key(date); button.tabIndex = key(date) === key(selected) ? 0 : -1;
                button.setAttribute('aria-selected',String(key(date) === key(selected)));
                if (key(date) === key(today)) button.setAttribute('aria-current','date');
                button.classList.toggle('other-month',date.getMonth() !== shown.getMonth());
                button.addEventListener('click', () => { selected = date; shown = date; render(true); });
                button.addEventListener('keydown', e => {
                    let next;
                    if (e.key === 'ArrowLeft') next = day(date.getFullYear(),date.getMonth(),date.getDate()-1);
                    if (e.key === 'ArrowRight') next = day(date.getFullYear(),date.getMonth(),date.getDate()+1);
                    if (e.key === 'ArrowUp') next = day(date.getFullYear(),date.getMonth(),date.getDate()-7);
                    if (e.key === 'ArrowDown') next = day(date.getFullYear(),date.getMonth(),date.getDate()+7);
                    if (e.key === 'PageUp' || e.key === 'PageDown') next = moveMonth(date,e.key === 'PageUp' ? -1 : 1);
                    if (e.key === 'Home') next = day(date.getFullYear(),date.getMonth(),date.getDate()-(date.getDay()-firstDay+7)%7);
                    if (e.key === 'End') next = day(date.getFullYear(),date.getMonth(),date.getDate()+6-(date.getDay()-firstDay+7)%7);
                    if (next) { e.preventDefault(); selected = shown = next; render(true); }
                });
                row.append(button);
            }
            grid.append(row);
        }
        if (focus) grid.querySelector('[tabindex="0"]')?.focus();
        send('popup:size:320 ' + Math.ceil(host.getBoundingClientRect().height));
    }
    function tick() {
        clearTimeout(timer); if (host.hidden) return;
        const now = new Date();
        host.querySelector('#calendar-time').textContent = preferences.timePattern ? formatTime(now,preferences.timePattern,preferences.am,preferences.pm) : new Intl.DateTimeFormat(locale,{hour:'numeric',minute:'2-digit',second:'2-digit',hour12}).format(now);
        host.querySelector('#calendar-date').textContent = key(now) === openedDate && preferences.date ? preferences.date : new Intl.DateTimeFormat(locale,{dateStyle:'full',calendar:'gregory'}).format(now);
        timer = setTimeout(tick,1000 - Date.now()%1000);
    }
    host.querySelector('#calendar-prev').onclick = () => { selected = shown = moveMonth(shown,-1); render(); };
    host.querySelector('#calendar-next').onclick = () => { selected = shown = moveMonth(shown,1); render(); };
    month.onclick = () => { selected = shown = new Date(); render(true); };
    host.addEventListener('keydown', e => {
        if (e.key !== 'Tab') return;
        const controls = [...host.querySelectorAll('button')].filter(b=>b.tabIndex>=0);
        const edge = e.shiftKey ? controls[0] : controls.at(-1);
        if (document.activeElement === edge) { e.preventDefault(); (e.shiftKey ? controls.at(-1) : controls[0]).focus(); }
    });
    window.chrome?.webview?.addEventListener('message', ({data}) => {
        if (data?.type !== 'calendar') { if (['lyrics','mixer','tooltip','popup:hidden'].includes(data?.type)) { host.hidden = true; clearTimeout(timer); } return; }
        revision = data.revision;
        const opening = host.hidden;
        preferences = data; openedDate = key(new Date());
        locale = data.locale || undefined; firstDay = data.firstDay ?? 1; hour12 = data.hour12;
        host.hidden = false;
        if (opening) { selected = shown = new Date(); host.classList.remove('popup-enter'); void host.offsetWidth; host.classList.add('popup-enter'); }
        render(opening); tick();
    });
})();
