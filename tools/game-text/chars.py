# chars.py FRAME CHARBASE(hex) BPP PAL [N] OUT.ppm: char sheet, 16 per row, scaled x3 with grid
import sys, struct
fr, base, bpp, pal = sys.argv[1], int(sys.argv[2],16), int(sys.argv[3]), int(sys.argv[4])
n = int(sys.argv[5]); out = sys.argv[6]
vram = struct.unpack('<32768H', open(f'vram-{fr}.bin','rb').read())
cg = struct.unpack('<256H', open(f'cgram-{fr}.bin','rb').read())
S=3; cw=8*S+2; W=16*cw; H=((n+15)//16)*cw
img=bytearray([40]*(W*H*3))
for c in range(n):
    b=(base+c*4*bpp)&0x7fff
    for y in range(8):
        p0=vram[(b+y)&0x7fff]; p1=vram[(b+8+y)&0x7fff] if bpp==4 else 0
        for x in range(8):
            k=7-x; v=((p0>>k)&1)|(((p0>>(k+8))&1)<<1)
            if bpp==4: v|=(((p1>>k)&1)<<2)|(((p1>>(k+8))&1)<<3)
            col=cg[pal*(1<<bpp)+v] if v else 0
            rgb=((col&31)<<3,((col>>5)&31)<<3,((col>>10)&31)<<3) if v else (0,0,0)
            for sy in range(S):
                for sx in range(S):
                    o=(((c//16)*cw+1+y*S+sy)*W+(c%16)*cw+1+x*S+sx)*3
                    img[o:o+3]=bytes(rgb)
open(out,'wb').write(b'P6\n%d %d\n255\n'%(W,H)+bytes(img))
