# bg.py FRAME LAYER(1-3) [OUT.ppm]: renders one BG layer (32x32 map from regs) to a PPM,
# and prints the tilemap char numbers in rows that are not blank.
import sys, struct, re
fr, L = sys.argv[1], int(sys.argv[2])
vram = struct.unpack('<32768H', open(f'vram-{fr}.bin','rb').read())
cg = struct.unpack('<256H', open(f'cgram-{fr}.bin','rb').read())
regs = open(f'regs-{fr}.txt').read()
m = re.search(rf'bg{L} map ([0-9a-f]+)[^\n]*chars ([0-9a-f]+)', regs)
mp, ch = int(m.group(1),16), int(m.group(2),16)
bpp = 2 if L == 3 else 4
W, H = 256, 256
img = bytearray(W*H*3)
def col(c):
    return ((c&31)<<3, ((c>>5)&31)<<3, ((c>>10)&31)<<3)
rows = []
for ty in range(32):
    line = []
    for tx in range(32):
        e = vram[(mp + ty*32 + tx) & 0x7fff]
        c, pal, hf, vf = e & 0x3ff, (e>>10)&7, (e>>14)&1, (e>>15)&1
        line.append(e)
        base = (ch + c*(8*bpp//2)) & 0x7fff
        for y in range(8):
            yy = 7-y if vf else y
            planes = [vram[(base+yy) & 0x7fff]] + ([vram[(base+8+yy)&0x7fff]] if bpp==4 else [])
            for x in range(8):
                b = 7-x
                v = ((planes[0]>>b)&1) | (((planes[0]>>(b+8))&1)<<1)
                if bpp == 4: v |= (((planes[1]>>b)&1)<<2) | (((planes[1]>>(b+8))&1)<<3)
                px = 7-x if hf else x
                o = ((ty*8+y)*W + tx*8+px)*3
                if v:
                    img[o:o+3] = bytes(col(cg[(pal*(1<<bpp) + v) if L != 3 else pal*4+v]))
    rows.append(line)
out = sys.argv[3] if len(sys.argv) > 3 else f'bg{L}-{fr}.ppm'
open(out,'wb').write(b'P6\n256 256\n255\n'+bytes(img))
if '-v' in sys.argv:
    for ty,line in enumerate(rows):
        print('%2d '%ty + ' '.join('%04x'%e for e in line))
