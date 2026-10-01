# GPU PPU renderer: design notes (draft, P2.3)

Status 2026-10-01: analysis only, no code yet. Next session continues from here.

## What SM uses in gameplay (FRAME DUMP captures, docs/debug-tools.md)

Captures: Crateria 93D5 (host, console state), Norfair A75D, Maridia CFC9,
Tourian DE4D (console).

- Always Mode 1 (BG1, BG2 4bpp, BG3 2bpp, OBJ), bg3priority set, no mosaic (the
  CPU PPU asserts on mosaic in mode 1), no windows in these rooms, 8x8 tiles.
- HUD split: the IRQ rewrites BG3SC, TM, CGWSEL, CGADSUB at line 32 (logged as
  lines 1 and 32). HDMA ch7 sets BG3 H/V scroll on lines 0-31.
- Norfair heat: HDMA on BG2VOFS and BG3VOFS every line (225 lines).
- Maridia water: HDMA on BG2HOFS and BG3HOFS every line.
- Colour math with subscreen layers (add, and subtract in Maridia).
- No data-port writes (VRAM/CGRAM/OAM) during the visible lines.

## How the CPU PPU composes (sm/src/snes/ppu.c, new renderer, mode 1)

Per line a 16-bit z-buffer: high byte = priority level, low byte = CGRAM index.
Levels, front to back: BG3 hi F2 (always, regardless of bg3priority), OBJ3 E4/E6,
BG1 hi C0, BG2 hi B1, OBJ2 A4/A6, BG1 lo 80, BG2 lo 71, OBJ1 64/66, OBJ0 24/26,
BG3 lo 12, backdrop 05. Bits 8-11 give the layer for colour math (0-2 BG, 4 OBJ
palettes 4-7, 6 OBJ palettes 0-3 = no math, 5 backdrop).
Sprites are written first, lower OAM index wins (first written), 32 sprites /
34 slivers per line limits apply; BGs then write where their level is higher.
Output row r = line r+1: BG samples y = r+1+vScroll, a sprite's top row is y.
Math: main +/- sub (sub backdrop -> fixed colour, no halving), 5-bit, clamp,
then brightness. Line N is drawn at hPos 512 of line N; HDMA at hPos 1024 of
line N applies from N+1; Vector_IRQ runs between lines.

## Plan

1. Capture: in GPU mode ppu_runLine stores a compact per-line register state
   (scroll, addresses, TM/TS, windows, math, brightness, OBSEL, oamAdr) instead
   of drawing. Unsupported frames (mode 7, windows, mosaic, mid-frame VRAM
   writes) are re-rendered by the CPU PPU from those states (replay), i.e. the
   current path is the fallback.
2. Bands: runs of lines with the same structural state (typically HUD 1-31 and
   gameplay 32-224), scissored. Within a band, per layer, runs of lines with the
   same scroll.
3. BG layers as whole-tilemap textures (RGBA5551, 256/512 px, GPU_REPEAT), one
   for priority-0 and one for priority-1 tiles, decoded on the CPU and kept in
   sync by diffing VRAM/CGRAM against a shadow copy each frame (re-decode tiles
   whose entry, char data or palette changed). An SNES 8x8 tile is exactly one
   PICA 8x8 Morton block (mzm's kSwizzleLUT; mzm's sLmTex are the same idea).
4. Constant-scroll runs: one quad per layer/priority. Per-line scroll (Norfair,
   Maridia): mzm measured per-strip blits as expensive, so when a band has many
   runs, compose that layer on the CPU into a screen-space texture from the
   decoded surface (row copies), then draw it as one quad.
5. Priority = depth test GREATER with per-level depth; sprites drawn first with a
   stencil "first wins" bit in OAM order (reproduces lower-index-wins); stencil
   bit 2 records "math enabled" for the winning pixel.
6. Subscreen into its own target (cleared to the fixed colour, alpha 0 =
   backdrop); math pass = full-band quad of the sub target with stencil test on
   the math bit, two passes split by alpha test (halved / not halved), blend
   add or reverse-subtract. RGBA5551 targets keep the maths 5-bit.
7. Present: draw the 256x224 result scaled to the top screen with brightness as
   a constant multiply (replaces the 2.8 ms CPU copy, P1.4).
8. Host verification before hardware: a software backend implementing the same
   primitives, compared pixel by pixel (5-bit) against the CPU PPU on frames
   from every room (warp harness) and console save states (frame-capture tool).
