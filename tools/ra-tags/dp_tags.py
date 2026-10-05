#!/usr/bin/env python3
"""Lists the `$0032`/`$0034` tags the RetroAchievements set asks for and checks `kDpTags` in source/retro_ach.c against them.

Usage: tools/ra-tags/dp_tags.py ra-set.json [source/retro_ach.c]
  ra-set.json  the set as the port saved it on the console (debug/ra-set.json, written when the game is loaded)

Why (docs/PLAN.md, P4.4): some achievements ask, next to the item bit, boss bit or map byte that changes, for two 16-bit words
of direct-page scratch ($0032 and $0034) to hold a pair of numbers per event. The original code leaves them there; the C port
keeps its temporaries in locals, so retro_ach.c writes the pair for the frame the event happens. When the set changes, run
this: it prints the table the set wants and says what differs from the one in the source.
"""
import json, re, sys

BIT = {c: i for i, c in enumerate('MNOPQRST')}   # rcheevos' sizes: 0xM is bit 0 ... 0xT is bit 7


def wanted(path):
    out = []
    for st in json.load(open(path))['Sets']:
        for a in st['Achievements']:
            m = a['MemAddr']
            m32 = re.search(r'0x 000032=(\d+)', m)
            if not m32:
                continue
            m34 = re.search(r'0x 000034=(\d+)', m)
            bit = re.search(r'0x([MNOPQRST])([0-9a-f]{6})>d0x\1\2', m)       # a bit going 0 -> 1
            byte = re.search(r'0xH([0-9a-f]{6})>d0xH\1', m)                  # a byte going up (the maps: to 255)
            if bit:
                addr, b = int(bit.group(2), 16), BIT[bit.group(1)]
            elif byte:
                addr, b = int(byte.group(1), 16), 8
            else:
                print('no changing condition found for %d %s: %s' % (a['ID'], a['Title'], m[:80]), file=sys.stderr)
                continue
            out.append((addr, b, int(m32.group(1)), int(m34.group(1)) if m34 else 0, a['Title']))
    return sorted(out)


def in_source(path):
    src = open(path).read()
    block = src[src.index('kDpTags[]'):]
    block = block[:block.index('};')]
    return sorted((int(a, 16), int(b), int(v32), int(v34)) for a, b, v32, v34 in
                  re.findall(r'\{\s*(0x[0-9A-Fa-f]+),\s*(\d+),\s*(\d+),\s*(\d+),', block))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    w = wanted(sys.argv[1])
    for addr, b, v32, v34, title in w:
        print('  { 0x%X, %d, %d, %d, "%s" },' % (addr, b, v32, v34, title))
    if len(sys.argv) > 2:
        have, want = in_source(sys.argv[2]), [(a, b, x, y) for a, b, x, y, _ in w]
        missing, extra = [t for t in want if t not in have], [t for t in have if t not in want]
        print('\nretro_ach.c: %d rows, the set wants %d' % (len(have), len(want)))
        for t in missing:
            print('  MISSING or different in the source:', t)
        for t in extra:
            print('  in the source but not in the set:   ', t)
        sys.exit(1 if missing or extra else 0)
