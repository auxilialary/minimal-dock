(() => {
    if (new URLSearchParams(location.search).get("role") !== "system") {
        window.dockBoot.modules.add("system"); return;
    }
    const clock = document.getElementById("clock");
    const send = value => window.chrome?.webview?.postMessage(value);

    const updateClock = () => {
        clock.textContent = new Intl.DateTimeFormat("ru-RU", {
            hour: "2-digit",
            minute: "2-digit",
            hour12: false
        }).format(new Date());

        const delay = 60000 - (Date.now() % 60000) + 25;
        window.setTimeout(updateClock, delay);
    };

    document.getElementById("sound")
        .addEventListener("click", () => send("mixer"));

    document.getElementById('sound').addEventListener('wheel', event => {
        event.preventDefault(); event.stopPropagation();
        const delta = window.volumeWheel(event);
        if (delta) send('volume:wheel:' + delta);
    }, {passive: false});

    clock.addEventListener("click", () => {
        const rect = clock.getBoundingClientRect(); send("calendar:" + (rect.left + rect.width / 2));
    });
    updateClock();
    window.dockBoot.modules.add("system");
})();
