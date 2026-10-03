# maps.py FRAME CHARBASE BPP OUT: every 0x400-word VRAM page as a 32x32 tilemap (chars at CHARBASE)
import sys, struct
fr, ch, bpp, out = sys.argv[1], int(sys.argv[2],16), int(sys.argv[3]), sys.argv[4]
vram = struct.unpack('<32768H', open(f'vram-{fr}.bin','rb').read())
cg = struct.unpack('<256H', open(f'cgram-{fr}.bin','rb').read())
pages=list(range(0,0x8000,0x400)); cols=8
W=cols*258; H=((len(pages)+cols-1)//cols)*258
img=bytearray(W*H*3)
for pi,mp in enumerate(pages):
    ox=(pi%cols)*258; oy=(pi//cols)*258
    for ty in range(32):
        for tx in range(32):
            e=vram[mp+ty*32+tx]; c=e&0x3ff; pal=(e>>10)&7; hf=(e>>14)&1; vf=(e>>15)&1
            b=(ch+c*4*bpp)&0x7fff
            for y in range(8):
                yy=7-y if vf else y
                p0=vram[(b+yy)&0x7fff]; p1=vram[(b+8+yy)&0x7fff] if bpp==4 else 0
                for x in range(8):
                    k=7-x; v=((p0>>k)&1)|(((p0>>(k+8))&1)<<1)
                    if bpp==4: v|=(((p1>>k)&1)<<2)|(((p1>>(k+8))&1)<<3)
                    if not v: continue
                    col=cg[(pal*(1<<bpp)+v)&255]
                    px=7-x if hf else x
                    o=((oy+ty*8+y)*W+ox+tx*8+px)*3
                    img[o:o+3]=bytes(((col&31)<<3,((col>>5)&31)<<3,((col>>10)&31)<<3))
open(out,'wb').write(b'P6\n%d %d\n255\n'%(W,H)+bytes(img))
