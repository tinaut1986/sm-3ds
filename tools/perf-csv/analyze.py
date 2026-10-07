#!/usr/bin/env python3
"""Summarises the PERF RECORDER's CSVs (debug/sm-perf-NN.csv, columns in docs/debug-tools.md).

Usage: analyze.py FILE.csv [FILE2.csv ...]

Per file: the fps shown, the cost of a *steady* frame (no tile decode, no bottom-screen redraw: logic,
draw, GPU build/submit, the GPU's own time), the frames with tile decodes grouped by how many (and why),
and what the frames over the 16.7 ms budget were doing. No ROM needed. An older CSV without the newest
columns still works for the first part.
"""
import csv, sys, statistics as st, collections

BUDGET = 16.7


def load(fn):
    with open(fn) as f:
        return list(csv.DictReader(l for l in f if not l.startswith('#')))


def fl(r, k, d=0.0):
    try:
        return float(r[k])
    except (KeyError, ValueError):
        return d


def mean(it):
    it = list(it)
    return st.mean(it) if it else 0.0


def report(fn):
    rows = load(fn)
    n = len(rows)
    shown = sum(int(r['shown']) for r in rows)
    W = sorted(fl(r, 'work_ms') for r in rows)
    drawn = [r for r in rows if fl(r, 'draw_ms') > 0]
    steady = [r for r in drawn if int(fl(r, 'tiles')) <= 1 and fl(r, 'ui_ms') <= 1]
    print('== %s: %d frames (%.1f s), %.1f fps shown, %d skipped; work mean %.1f p95 %.1f max %.0f ms' % (
        fn, n, n / 60, shown / (n / 60), n - shown, mean(W), W[int(n * .95)], W[-1]))
    if steady:
        print('   steady frames (%d): logic %.1f draw %.1f (build %.1f submit %.1f) | GPU own draw %.1f wait %.2f | quads %.0f | work %.1f' % (
            len(steady), mean(fl(r, 'logic_ms') for r in steady), mean(fl(r, 'draw_ms') for r in steady),
            mean(fl(r, 'gpu_build_ms') for r in steady), mean(fl(r, 'gpu_submit_ms') for r in steady),
            mean(fl(r, 'gpu_draw_ms') for r in steady), mean(fl(r, 'gpu_wait_ms') for r in steady),
            mean(fl(r, 'quads') for r in steady), mean(fl(r, 'work_ms') for r in steady)))
        print('   steady frames over %.1f ms: %d' % (BUDGET, sum(fl(r, 'work_ms') > BUDGET for r in steady)))
    if 'tiles' in rows[0]:
        print('   tile decodes by size (tiles | reused | deferred | why: map pal char | draw build bg submit | upload copy+flush KB):')
        for lo, hi in ((2, 100), (100, 300), (300, 700), (700, 1700), (1700, 99999)):
            g = [r for r in drawn if lo <= int(fl(r, 'tiles')) < hi]
            if g:
                print('     %5d-%5d n=%3d | %5.0f | %5.0f | %5.0f | %5.0f %5.0f %5.0f | %5.1f %5.1f %5.1f %5.1f | %4.1f+%.1f %4.0f' % (
                    lo, hi, len(g), mean(fl(r, 'tiles') for r in g), mean(fl(r, 'tiles_reused') for r in g),
                    mean(fl(r, 'tiles_deferred') for r in g), mean(fl(r, 'why_map') for r in g), mean(fl(r, 'why_pal') for r in g),
                    mean(fl(r, 'why_char') for r in g), mean(fl(r, 'draw_ms') for r in g), mean(fl(r, 'gpu_build_ms') for r in g),
                    mean(fl(r, 'bg_ms') for r in g), mean(fl(r, 'gpu_submit_ms') for r in g), mean(fl(r, 'tex_copy_ms') for r in g),
                    mean(fl(r, 'tex_flush_ms') for r in g), mean(fl(r, 'tex_kb') for r in g)))
        print('   tiles still waiting (SPREAD TILES): max %d' % max(int(fl(r, 'tiles_pending')) for r in rows))
    over = [r for r in rows if fl(r, 'work_ms') > BUDGET and fl(r, 'draw_ms') > 0]
    c = collections.Counter()
    for r in over:
        t = int(fl(r, 'tiles'))
        c['bottom screen / battery / taps' if fl(r, 'ui_ms') > 1 else
          'palette burst (pal > 200)' if fl(r, 'why_pal') > 200 else
          'tile update' if t > 1 else 'steady (no event)'] += 1
    print('   frames drawn over the budget: %d %s' % (len(over), dict(c)))
    ui = [i for i, r in enumerate(rows) if fl(r, 'ui_ms') > 1]
    if ui:
        print('   bottom-screen frames: %d, gaps %s, ui %.1f present %.1f wait %.1f ms' % (
            len(ui), collections.Counter(b - a for a, b in zip(ui, ui[1:])).most_common(3), mean(fl(rows[i], 'ui_ms') for i in ui),
            mean(fl(rows[i], 'present_ms') for i in ui), mean(fl(rows[i], 'present_wait_ms') for i in ui)))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for fn in sys.argv[1:]:
        report(fn)
