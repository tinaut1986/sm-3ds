// Software execution of a GpuFrame with the CPU renderer's exact rules (see GpuBand
// in gpu_ppu.h). It is the reference the citro3d backend is meant to match, and it is
// what tools/gpu-ppu-test compares against the CPU renderer on the PC.
#include "gpu_ppu_ref.h"

#include <string.h>

typedef struct {
  uint8_t r, g, b, level;
  bool set, obj, math;
} Px;

static void DrawRow(const GpuFrame *f, int first, int count, int row, Px *px) {
  for (int q = first; q < first + count; q++) {
    const GpuQuad *qd = &f->quads[q];
    if (row < qd->y || row >= qd->y + qd->h) continue;
    const GpuTex *t = f->tex[qd->tex];
    const int dy = row - qd->y;
    const int ty = (qd->sy + ((qd->flags & kGpuQuadFlipY) ? qd->h - 1 - dy : dy)) & (t->h - 1);
    int x0 = qd->x < 0 ? 0 : qd->x, x1 = qd->x + qd->w > 256 ? 256 : qd->x + qd->w;
    for (int x = x0; x < x1; x++) {
      Px *p = &px[x];
      const bool obj = qd->flags & kGpuQuadObj;
      if (obj ? p->obj : qd->level <= p->level) continue;
      const int dx = x - qd->x;
      uint16_t v;
      if (qd->flags & kGpuQuadAffine) {
        // Mode 7, the CPU renderer's arithmetic: wraps at 32 bits, and with the large
        // field anything outside 0..1023 (either way) is transparent.
        const uint32_t xpos = (uint32_t)qd->ax + (uint32_t)qd->adx * (uint32_t)dx;
        const uint32_t ypos = (uint32_t)qd->ay + (uint32_t)qd->ady * (uint32_t)dx;
        if ((qd->flags & kGpuQuadBorder) && (xpos | ypos) > 0x3ffff) continue;
        v = t->px[GpuTexelIndex((xpos >> 8) & 1023, (ypos >> 8) & 1023, t->w)];
      } else {
        const int tx = (qd->sx + ((qd->flags & kGpuQuadFlipX) ? qd->w - 1 - dx : dx)) & (t->w - 1);
        v = t->px[GpuTexelIndex(tx, ty, t->w)];
      }
      if (!(v & 1)) continue;
      p->r = v >> 11;
      p->g = (v >> 6) & 31;
      p->b = (v >> 1) & 31;
      p->level = qd->level;
      p->set = true;
      p->obj |= obj;
      p->math = (qd->flags & kGpuQuadMath) != 0;
    }
  }
}

void GpuRef_DrawFrame(const GpuFrame *f, uint8_t *out, int pitch) {
  for (int bi = 0; bi < f->band_count; bi++) {
    const GpuBand *b = &f->bands[bi];
    uint8_t map[63], half[63];
    for (int i = 0; i < 63; i++) {
      const int c = i > 31 ? 31 : i;
      map[i] = (uint8_t)(((c << 3) | (c >> 2)) * b->brightness / 15);
    }
    for (int i = 0; i < 63; i++) half[i] = map[i >> 1];
    for (int row = b->y0; row < b->y1; row++) {
      uint32_t *dst = (uint32_t *)(out + row * pitch);
      if (b->black) {
        memset(dst, 0, 256 * 4);
        continue;
      }
      Px main[256], sub[256];
      const Px back = { b->backdrop & 31, (b->backdrop >> 5) & 31, (b->backdrop >> 10) & 31, 0, false, false,
                        b->backdrop_math };
      for (int x = 0; x < 256; x++) main[x] = back;
      memset(sub, 0, sizeof(sub));
      DrawRow(f, b->main_first, b->main_count, row, main);
      DrawRow(f, b->sub_first, b->sub_count, row, sub);
      const int fr = b->fixed & 31, fg = (b->fixed >> 5) & 31, fb = (b->fixed >> 10) & 31;
      for (int x = 0; x < 256; x++) {
        const Px *p = &main[x];
        int r = b->clip ? 0 : p->r, g = b->clip ? 0 : p->g, bl = b->clip ? 0 : p->b;
        const uint8_t *m = map;
        if (b->math && p->math) {
          int r2 = fr, g2 = fg, b2 = fb;
          if (b->add_subscreen && sub[x].set) {
            r2 = sub[x].r, g2 = sub[x].g, b2 = sub[x].b;
            if (b->half) m = half;
          } else if (!b->add_subscreen && b->half) {
            m = half;
          }
          if (b->subtract) {
            r = r >= r2 ? r - r2 : 0;
            g = g >= g2 ? g - g2 : 0;
            bl = bl >= b2 ? bl - b2 : 0;
          } else {
            r += r2, g += g2, bl += b2;
          }
        }
        dst[x] = m[bl] | m[g] << 8 | m[r] << 16;
      }
    }
  }
}
