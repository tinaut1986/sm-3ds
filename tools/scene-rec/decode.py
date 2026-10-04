#!/usr/bin/env python3
"""Decodes a scene recording (debug/sm-rec-NNNN.bin, debug tools -> SCENE RECORDER).

Usage: decode.py REC.bin [OUTDIR] [--crop] [--mp4] [--scale N]
  OUTDIR   default: REC name without .bin. Gets frame-NNNN.png and frames.csv.
  --crop   drop the black side bars a 4:3 frame has (keeps all 400 px of WIDE frames).
  --mp4    also write OUTDIR.mp4 with ffmpeg, at the rate the frames were recorded.
  --scale  integer upscale of the PNGs (nearest), default 1.
Needs python3 + Pillow (+ ffmpeg for --mp4). Format: docs/debug-tools.md.
"""
import csv, os, struct, subprocess, sys
from PIL import Image

W, H = 400, 240
FRAME_HEAD = struct.Struct('<I4H2HB3x')   # game_frame, state, room, x, y, logic, draw, flags


def unpack(words, n):
    out = []
    i = 0
    while len(out) < n:
        t = words[i]
        if t & 0x8000:
            out.extend([words[i + 1]] * (t & 0x7FFF))
            i += 2
        else:
            out.extend(words[i + 1:i + 1 + t])
            i += 1 + t
    return out


def to_image(px):
    # Framebuffer order: column x from the left, each column from the bottom row up.
    img = Image.new('RGB', (W, H))
    data = [(0, 0, 0)] * (W * H)
    for x in range(W):
        col = px[x * H:(x + 1) * H]
        for j, p in enumerate(col):
            r, g, b = (p >> 11) & 31, (p >> 5) & 63, p & 31
            data[(H - 1 - j) * W + x] = ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))
    img.putdata(data)
    return img


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    flags = [a for a in sys.argv[1:] if a.startswith('--')]
    if not args:
        sys.exit(__doc__)
    scale = 1
    for i, a in enumerate(sys.argv):
        if a == '--scale':
            scale = int(sys.argv[i + 1])
            args = [x for x in args if x != sys.argv[i + 1]]
    src = args[0]
    out = args[1] if len(args) > 1 else os.path.splitext(src)[0]
    os.makedirs(out, exist_ok=True)
    data = open(src, 'rb').read()
    magic, w, h, frames, every, version = struct.unpack_from('<8s2H2I44s', data, 0)
    if not magic.startswith(b'SMREC1') or (w, h) != (W, H):
        sys.exit('not a scene recording: %r %dx%d' % (magic, w, h))
    version = version.split(b'\0')[0].decode()
    print('%s: %d frames, 1 in %d (%d Hz), build %s' % (src, frames, every, 60 // every, version))
    off = 64
    rows = []
    for k in range(frames):
        gf, state, room, x, y, logic, draw, fl = FRAME_HEAD.unpack_from(data, off)
        off += FRAME_HEAD.size
        (nwords,) = struct.unpack_from('<I', data, off)
        off += 4
        words = struct.unpack_from('<%dH' % nwords, data, off)
        off += nwords * 2
        img = to_image(unpack(words, W * H))
        if '--crop' in flags and not fl & 2:
            bbox = img.getbbox()
            if bbox:
                img = img.crop((bbox[0], 0, bbox[2], H))
        if scale > 1:
            img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
        img.save(os.path.join(out, 'frame-%04d.png' % k))
        rows.append([k, gf, '%02X' % state, '%04X' % room, x, y, logic / 100, draw / 100,
                     'GPU' if fl & 1 else 'CPU', int(bool(fl & 2))])
    with open(os.path.join(out, 'frames.csv'), 'w', newline='') as f:
        wr = csv.writer(f)
        wr.writerow(['index', 'game_frame', 'game_state', 'room', 'samus_x', 'samus_y', 'logic_ms', 'draw_ms',
                     'renderer', 'wide'])
        wr.writerows(rows)
    print('-> %s/frame-*.png, frames.csv' % out)
    if '--mp4' in flags:
        # Cropped frames can differ in width: pad them to the widest.
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-framerate', str(60 // every), '-i',
                        os.path.join(out, 'frame-%04d.png'), '-vf', 'pad=ceil(iw/2)*2:ceil(ih/2)*2',
                        '-pix_fmt', 'yuv420p', out + '.mp4'], check=True)
        print('-> %s.mp4' % out)


if __name__ == '__main__':
    main()
