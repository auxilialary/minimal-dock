(() => {
    const container = document.getElementById("apps");
    const nodes = new Map();
    const send = value => window.chrome?.webview?.postMessage(value);

    // Use the button box (not the animated image box) to snap resting icon
    // placement to physical pixels without interfering with hover/entry motion.
    window.alignDockIcons = () => {
        const scale = window.devicePixelRatio || 1;
        const positions = [...container.querySelectorAll(".app-icon")].map(icon => {
            const box = icon.parentElement.getBoundingClientRect();
            const size = parseFloat(getComputedStyle(icon).width);
            return { icon, left: Math.round((box.left + (box.width - size) / 2) * scale) / scale - box.left,
                top: Math.round((box.top + (box.height - size) / 2) * scale) / scale - box.top };
        });
        for (const { icon, left, top } of positions) {
            icon.style.left = `${left}px`;
            icon.style.top = `${top}px`;
        }
    };
    container.addEventListener("scroll", window.alignDockIcons, { passive: true });

    function createApp(item) {
        const button = document.createElement("button");
        button.className = "app";
        button.type = "button";

        const icon = item.icon
            ? document.createElement("img")
            : document.createElement("span");

        icon.className = item.icon ? "app-icon" : "app-fallback";
        if (item.icon) {
            icon.src = item.icon;
            icon.alt = "";
            icon.draggable = false;
        } else {
            icon.textContent = (item.title || "?")[0].toUpperCase();
        }

        const dot = document.createElement("span");
        dot.className = "app-dot";
        button.append(icon, dot);
        button.addEventListener("click", () => send("activate:" + item.id));
        return button;
    }

    function renderApps(items) {
        const next = new Set(items.map(item => item.id));

        for (const [id, node] of nodes) {
            if (!next.has(id)) {
                node.remove();
                nodes.delete(id);
            }
        }

        for (const item of items) {
            let node = nodes.get(item.id);
            if (!node) {
                node = createApp(item);
                nodes.set(item.id, node);
            }
            // A DPI refresh changes the PNG even when the application ID stays
            // the same. Keep the button/focus but replace stale image content.
            const current = node.firstElementChild;
            if (item.icon && current.classList.contains("app-icon")) {
                if (current.getAttribute("src") !== item.icon) current.src = item.icon;
            } else if (Boolean(item.icon) !== current.classList.contains("app-icon")) {
                const replacement = createApp(item).firstElementChild;
                current.replaceWith(replacement);
            }
            node.dataset.id = item.id;
            node.setAttribute("aria-label", item.title || "Приложение");
            node.classList.toggle("active", Boolean(item.active));
            const position = items.indexOf(item);
            if (container.children[position] !== node)
                container.insertBefore(node, container.children[position] || null);
        }
        window.alignDockIcons();
        window.dockMagnification?.refresh();
    }

    window.chrome?.webview?.addEventListener("message", event => {
        if (event.data?.type === "apps" && Array.isArray(event.data.items))
            { renderApps(event.data.items); window.dockCheck?.(); }
    });
    window.dockBoot.modules.add("dock");
})();
