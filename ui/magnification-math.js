/* Pure math shared by the controller and deterministic tests. */
((root) => {
    function wave(centers, pointer, size, maximum, radius, spacing) {
        const scales = centers.map(x => pointer === null ? 1 :
            1 + (maximum - 1) * (1 + Math.cos(Math.PI * Math.min(1, Math.abs(x - pointer) / radius))) / 2);
        const extras = scales.map(s => size * (s - 1) * spacing);
        const total = extras.reduce((a, b) => a + b, 0);
        let accumulated = -total / 2;
        return scales.map((scale, i) => {
            const x = accumulated + extras[i] / 2;
            accumulated += extras[i];
            return {scale, x};
        });
    }
    const blend = (rate, seconds) => 1 - Math.exp(-rate * seconds);
    root.DockWave = {wave, blend};
    if (typeof module !== "undefined") module.exports = root.DockWave;
})(globalThis);
