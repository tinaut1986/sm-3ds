#!/usr/bin/env python3
"""Compares the console's VRAM dump of the Crocomire garbage with a clean host VRAM of the same room.

Usage: compare_vram.py CONSOLE_VRAM.bin HOST_VRAM.bin
The host one: SAMUS_AT=890,155 SHOTS=100-100 tools/gpu-ppu-test/run.sh ROM rooms 120 A98D
(writes /tmp/sm-gpu-ppu-test/vram-0100.bin).
"""
import struct, sys

host = struct.unpack('<32768H', open(sys.argv[2], 'rb').read())
cons = struct.unpack('<32768H', open(sys.argv[1], 'rb').read())
regions = (('BG2 tilemap page 0, rows 0-15 (what Crocomire transfers)', 0x4800, 512),
           ('BG2 tilemap page 0, rows 16-31', 0x4A00, 512),
           ('BG2 tilemap page 1', 0x4C00, 1024),
           ('BG1 tilemap', 0x5000, 2048),
           ('BG3 chars 0x4000-0x47FF', 0x4000, 0x800),
           ('BG1/BG2 chars 0x0000-0x3FFF', 0, 0x4000))
for name, a, n in regions:
    eq = sum(1 for x, y in zip(host[a:a + n], cons[a:a + n]) if x == y)
    print('%-58s equal %4d / %5d  non-zero: host %4d console %4d' % (
        name, eq, n, sum(1 for w in host[a:a + n] if w), sum(1 for w in cons[a:a + n] if w)))
