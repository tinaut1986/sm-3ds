"""Reads a .room file written by export_rooms.c and draws its layers (format: export_rooms.c)."""
import struct

HEAD = struct.Struct('<8sHBBHHHHHHHHHHH')   # 34 bytes, see export_rooms.c
BANK_7F = 0x10000
TILE_TABLE_BYTES = 0x2000


class Room:
    def __init__(self, path):
        raw = open(path, 'rb').read()
        (magic, self.header, self.area, _res, self.width, self.height, self.l2_scroll_x,
         self.bg1_sc, self.bg2_sc, self.bg1_map, self.bg2_map, self.main, self.sub, mode, _spare) = HEAD.unpack_from(raw, 0)
        self.mode, self.bg3_priority = mode & 15, mode >> 4 & 1
        if not magic.startswith(b'SMRM1'):
            raise ValueError(path + ': not a .room file')
        o = HEAD.size
        self.bank7f = raw[o:o + BANK_7F]
        o += BANK_7F
        self.tile_table = struct.unpack_from('<4096H', raw, o)   # [index * 4 + corner]
        o += TILE_TABLE_BYTES
        self.cgram = struct.unpack_from('<256H', raw, o)
        o += 512
        self.vram = raw[o:o + 0x10000]
        o += 0x10000
        # BG3 (the effects layer): optional, after the VRAM; (tilemap, tiles, size, _) in VRAM words
        self.bg3 = struct.unpack_from('<4H', raw, o) if len(raw) >= o + 8 else None
        w, h = self.width, self.height
        self.layer1 = struct.unpack_from('<%dH' % (w * h), self.bank7f, 2)
        # BG2's own block map exists only when it scrolls with the level data (bit 0 clear).
        self.has_bg2 = (self.l2_scroll_x & 1) == 0
        self.layer2 = struct.unpack_from('<%dH' % (w * h), self.bank7f, 0x9602) if self.has_bg2 else None
        self._chars = {}

    # --- graphics -------------------------------------------------------------------------
    def char(self, n):
        """8x8 4bpp char n (0..1023) as 64 palette indices."""
        c = self._chars.get(n)
        if c is None:
            b = self.vram[n * 32:n * 32 + 32]
            c = []
            for y in range(8):
                p0, p1, p2, p3 = b[y * 2], b[y * 2 + 1], b[16 + y * 2], b[16 + y * 2 + 1]
                for x in range(8):
                    s = 7 - x
                    c.append((p0 >> s & 1) | (p1 >> s & 1) << 1 | (p2 >> s & 1) << 2 | (p3 >> s & 1) << 3)
            self._chars[n] = c
        return c

    def colour(self, i):
        v = self.cgram[i]
        r, g, b = v & 31, v >> 5 & 31, v >> 10 & 31
        return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)

    def block_words(self, word):
        """The four tilemap words of a block (TL, TR, BL, BR), flips applied as the game does."""
        t = self.tile_table
        i = (word & 0x3FF) * 4
        tl, tr, bl, br = t[i], t[i + 1], t[i + 2], t[i + 3]
        flip = word & 0xC00
        if flip == 0x400:
            return tr ^ 0x4000, tl ^ 0x4000, br ^ 0x4000, bl ^ 0x4000
        if flip == 0x800:
            return bl ^ 0x8000, br ^ 0x8000, tl ^ 0x8000, tr ^ 0x8000
        if flip == 0xC00:
            return br ^ 0xC000, bl ^ 0xC000, tr ^ 0xC000, tl ^ 0xC000
        return tl, tr, bl, br

    def draw_block(self, img, bx, by, word, priority=None):
        """Draws one 16x16 block into PIL image `img` at block (bx, by); priority 0/1 keeps only those tiles.
        Returns whether anything was drawn."""
        drew = False
        for k, e in enumerate(self.block_words(word)):
            pri = e >> 13 & 1
            if priority is not None and pri != priority:
                continue
            ch, pal, hf, vf = e & 0x3FF, e >> 10 & 7, e & 0x4000, e & 0x8000
            px = self.char(ch)
            ox, oy = bx * 16 + (k & 1) * 8, by * 16 + (k >> 1) * 8
            for y in range(8):
                sy = 7 - y if vf else y
                for x in range(8):
                    sx = 7 - x if hf else x
                    c = px[sy * 8 + sx]
                    if c:
                        img.putpixel((ox + x, oy + y), self.colour(pal * 16 + c))
                        drew = True
        return drew

    def render(self, layer, priority=None):
        """The whole layer (1 or 2) as an RGBA PIL image, transparent where no tile draws."""
        from PIL import Image
        words = self.layer1 if layer == 1 else self.layer2
        img = Image.new('RGBA', (self.width * 16, self.height * 16), (0, 0, 0, 0))
        if words is None:
            return img
        for by in range(self.height):
            for bx in range(self.width):
                self.draw_block(img, bx, by, words[by * self.width + bx], priority)
        return img
