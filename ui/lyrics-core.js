(function(root) {
    function parse(text) {
        if (typeof text !== 'string' || text.length > 262144) return [];
        const lines = [];
        let offset = 0;
        for (const line of text.split(/\r?\n/)) {
            const meta = line.match(/^\[offset:([+-]?\d+)\]$/i);
            if (meta) { offset = Math.max(-60000, Math.min(60000, Number(meta[1]))) / 1000; continue; }
            const times = []; let rest = line;
            for (;;) {
                const m = rest.match(/^\[(\d{1,4}):([0-5]\d)(?:\.(\d{1,9}))?\]/);
                if (!m) break;
                times.push(Number(m[1]) * 60 + Number(m[2]) + Number('0.' + (m[3] || '0')));
                rest = rest.slice(m[0].length);
            }
            for (const time of times) {
                if (lines.length >= 4000) return [];
                lines.push({time, text: rest.trim()});
            }
        }
        return lines.map(l => ({...l, time: Math.max(0, l.time + offset)})).sort((a,b) => a.time-b.time);
    }
    function active(lines, position) {
        let lo = 0, hi = lines.length;
        while (lo < hi) { const mid = (lo + hi) >>> 1; if (lines[mid].time <= position) lo = mid + 1; else hi = mid; }
        return lo - 1;
    }
    function position(elapsed, base, now, playing, duration) {
        const value = Math.max(0, elapsed + (playing ? Math.max(0, now-base)/1000 : 0));
        return duration > 0 ? Math.min(value, duration) : value;
    }
    root.lyricsCore = {parse, active, position};
    if (typeof module !== 'undefined') module.exports = root.lyricsCore;
})(globalThis);
