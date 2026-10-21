(() => {
    const role = new URLSearchParams(location.search).get("role");
    const apps = document.getElementById("apps");
    const system = document.getElementById("system");

    if (!["apps", "system", "popup", "media"].includes(role)) throw new Error("Invalid island role");
    document.body.dataset.role = role || "system";
    apps.hidden = role !== "apps";
    system.hidden = role !== "system";

    const iconDip = parseFloat(getComputedStyle(document.documentElement).getPropertyValue("--dock-icon-size"));
    const alignIconSize = () => {
        const scale = window.devicePixelRatio || 1;
        if (Number.isFinite(iconDip)) document.documentElement.style.setProperty(
            "--dock-icon-size", `${Math.round(iconDip * scale) / scale}px`);
        window.alignDockIcons?.();
    };
    alignIconSize();
    window.addEventListener("resize", alignIconSize);

    const send = value => window.chrome?.webview?.postMessage(value);
    const profiling = new URLSearchParams(location.search).get('perf') === '1';
    if (profiling && window.PerformanceObserver?.supportedEntryTypes.includes('longtask')) {
        new PerformanceObserver(list => {
            for (const task of list.getEntries()) send(`perf:longtask:${task.startTime.toFixed(3)}:${task.duration.toFixed(3)}`);
        }).observe({type: 'longtask', buffered: true});
    }
    document.addEventListener("contextmenu", event => {
        event.preventDefault();
        window.dockReorder?.cancel();
        if (role !== "popup") send("menu");
    });
    document.addEventListener("dragstart", event => event.preventDefault());
    window.dockCheck = async () => {
        if (window.dockPainted || window.dockBoot.failed) return;
        await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
        const island = document.getElementById("island");
        const style = getComputedStyle(island);
        const visible = element => element && element.getBoundingClientRect().width > 0 &&
            element.getBoundingClientRect().height > 0 && getComputedStyle(element).display !== "none";
        const stylesLoaded = [...document.querySelectorAll('link[rel="stylesheet"]')].every(link => link.sheet);
        const modulesLoaded = ["dock", "system"].every(name => window.dockBoot.modules.has(name));
        const contentReady = ["popup", "media"].includes(role) ? true : role === "system"
            ? visible(document.getElementById("sound")) &&
                /^\d{2}:\d{2}$/.test(document.getElementById("clock").textContent)
            : apps.children.length > 0 && [...apps.querySelectorAll("img")].every(img => img.complete && img.naturalWidth > 0);
        if (!stylesLoaded || !modulesLoaded || !visible(island) ||
            !style.getPropertyValue("--dock-surface").trim() ||
            style.backgroundColor !== "rgba(0, 0, 0, 0)" ||
            parseFloat(style.borderTopWidth) !== 0) {
            window.dockReport("Missing design styles, modules, or layout");
            return;
        }
        if (contentReady && !window.dockBoot.failed && !window.dockPainted) {
            window.dockPainted = true;
            send("ui:painted");
        }
    };
    window.addEventListener("load", () => {
        if (profiling) {
            const navigation = performance.getEntriesByType('navigation')[0];
            if (navigation) send(`perf:document:responseEnd=${navigation.responseEnd.toFixed(3)};interactive=${navigation.domInteractive.toFixed(3)};load=${performance.now().toFixed(3)}`);
        }
        send("ui:boot");
        window.dockCheck();
    });
    document.getElementById("apps").addEventListener("load", () => window.dockCheck(), true);
})();
