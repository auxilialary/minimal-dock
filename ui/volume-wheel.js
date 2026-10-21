// WebView converts native wheel input to DOM units; keep fractional percentage points.
window.volumeWheel = event => {
    const pixels = event.deltaY * (event.deltaMode === 1 ? 40 : event.deltaMode === 2 ? 120 : 1);
    return Number.isFinite(pixels) ? -pixels / 100 * 2 : 0;
};

window.chrome?.webview?.addEventListener('message', ({data}) => {
    if (data?.type !== 'native:wheel') return;
    const target = document.elementFromPoint(data.x, data.y);
    target?.dispatchEvent(new WheelEvent('wheel', {bubbles:true, cancelable:true, deltaMode:0, deltaY:-data.delta / 120 * 100}));
});
