(() => {
    if (new URLSearchParams(location.search).get("role") !== "apps") return;
    const container = document.getElementById("apps");
    const reduced = matchMedia("(prefers-reduced-motion: reduce)");
    let buttons = [], states = [], centers = [], pointer = null, frame = 0, previous = 0;
    let settings;
    const read = () => {
        const css = getComputedStyle(document.documentElement);
        const value = name => parseFloat(css.getPropertyValue(name));
        settings = {size: value("--dock-icon-size"), max: value("--magnify-max-scale"),
            radius: value("--magnify-radius"), rate: value("--magnify-response"),
            spacing: value("--magnify-spacing"), height: value("--dock-height"), buttonSize: value("--app-button-size")};
    };
    function measure() {
        // Rest centers ignore transforms, preventing pointer feedback loops.
        const bottom = buttons[0]?.getBoundingClientRect().bottom || 0;
        const dpr = devicePixelRatio || 1;
        settings.iconBottom = bottom - Math.round((bottom - (settings.buttonSize - settings.size) / 2) * dpr) / dpr;
        centers = buttons.map(b => b.offsetLeft + b.offsetWidth / 2 - container.scrollLeft - innerWidth / 2);
    }
    function wake() { if (!frame) { previous = performance.now(); frame = requestAnimationFrame(tick); } }
    function tick(now) {
        frame = 0;
        const amount = reduced.matches ? 1 : DockWave.blend(settings.rate, Math.max(0, (now - previous) / 1000));
        previous = now;
        // Permanent padding bounds the total cosine-wave displacement. An
        // overflowing row keeps normal-sized, scrollable icons on small screens.
        const maximum = reduced.matches || container.scrollWidth > container.clientWidth ? 1 : settings.max;
        const targets = DockWave.wave(centers, pointer, settings.size, maximum, settings.radius, settings.spacing);
        let moving = false;
        buttons.forEach((button, i) => {
            const state = states[i], target = targets[i];
            for (const key of ["scale", "x"]) {
                state[key] += (target[key] - state[key]) * amount;
                if (Math.abs(target[key] - state[key]) < .001) state[key] = target[key];
                else moving = true;
            }
            button.style.transform = `translateX(${state.x}px)`;
            // Enlarge the actual hit box upward together with the artwork.
            button.style.height = `${settings.buttonSize + settings.size * (state.scale - 1)}px`;
            const icon = button.firstElementChild;
            icon.style.transform = `scale(${state.scale})`;
            icon.style.top = "auto";
            icon.style.bottom = `${settings.iconBottom}px`;
        });
        window.dockTooltip?.position();
        if (moving) frame = requestAnimationFrame(tick);
    }
    window.dockMagnification = {refresh() {
        read();
        const old = new Map(buttons.map((b, i) => [b, states[i]]));
        buttons = [...container.children];
        states = buttons.map(b => old.get(b) || {scale: 1, x: 0});
        measure(); wake();
    }};
    container.addEventListener("pointermove", event => {
        if (reduced.matches) return;
        pointer = event.clientX - innerWidth / 2;
        wake();
    });
    const leave = () => { pointer = null; wake(); };
    container.addEventListener("pointerleave", leave);
    window.addEventListener("blur", leave);
    reduced.addEventListener("change", leave);
    window.addEventListener("resize", () => { measure(); wake(); });
    container.addEventListener("scroll", () => { measure(); wake(); }, {passive:true});
    read();
})();
