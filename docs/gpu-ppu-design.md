# GPU PPU renderer (P2.3 / P2.4)

Status 2026-10-01: implemented, verified on the PC against the CPU renderer, **not yet
run on hardware**. Off by default: Debug tab -> DEBUG TOOLS -> RENDERER switches it.

## What SM uses in gameplay (FRAME DUMP captures, docs/debug-tools.md)

Captures: Crateria 93D5 (host, console state), Norfair A75D, Maridia CFC9,
Tourian DE4D (console).

- Always Mode 1 (BG1, BG2 4bpp, BG3 2bpp, OBJ), bg3priority set, 8x8 tiles, no
  windows in these rooms. Mosaic never applies: `ppu->mosaicEnabled` is never
  written, and the CPU renderer asserts on mosaic in mode 1 anyway.
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

## Implementation

| Piece | File | Role |
|---|---|---|
| Capture / replay | `sm/src/snes/ppu.c` (`PpuLineState`, `g_ppu_line_capture`, `ppu_replayLines`) | In GPU mode `ppu_runLine` stores each line's registers instead of drawing. A refused frame is drawn by the CPU renderer from those states, so the fallback is exactly today's output. |
| Frame building | `source/gpu_ppu.c` | Portable. Bands, surfaces, sprites, draw list (`GpuFrame`). |
| Reference backend | `source/gpu_ppu_ref.c` | Executes a `GpuFrame` in software with the CPU renderer's rules. |
| citro3d backend | `source/gpu_ppu_3ds.c`, `gpu_ppu.v.pica` | Draws the list on the console and presents the top screen. |
| Host check | `tools/gpu-ppu-test/` | Compares the three (normal CPU, capture replay, GPU list via the reference) per frame. |

Frame building:

1. **Bands**: runs of lines whose registers differ only in the BG scrolls (in SM:
   HUD lines 1-31 and gameplay 32-224).
2. **BG surfaces**: each BG tilemap decoded whole into two RGBA5551 textures (priority
   0 tiles and priority 1 tiles), 256 or 512 px each way so GPU_REPEAT is the SNES
   wrap. An SNES 8x8 tile is exactly one PICA 8x8 Morton block; row 0 is at the start
   of memory and sampled at v = 1 (mzm's atlas convention, proven on hardware). Kept in
   sync by diffing VRAM and CGRAM against shadow copies every frame: a tile is
   re-decoded when its tilemap entry, its char data or its palette changed; a surface
   with nothing changed is skipped.
3. **Scroll runs**: per band and layer, lines with the same scroll become one quad
   (up to 8 runs). With more (Maridia, Norfair heat) the layer's rows are copied on the
   CPU into a 256x256 screen texture and drawn as one quad: mzm measured per-line
   strips as expensive on the GPU.
4. **Sprites**: decoded per frame into a 512x512 atlas (one entry per distinct look),
   one quad per sprite (two when it wraps past line 256), flips by texture coordinates.
5. **Refused** (CPU fallback): mode other than 1, OBJ interlace, a window that splits a
   line (a window covering a whole line just hides the layer), VRAM/CGRAM/OAM written
   during the lines, out of quads/textures.

citro3d backend: depth = priority level (BG quads pass where theirs is greater),
stencil bit 0 = a sprite owns the pixel (first in OAM order wins), bit 1 = colour math
applies to the pixel's owner. Subscreen drawn into its own target (backdrop = fixed
colour with alpha 0); math as blended passes on stencil bit 1 (two passes when the
subscreen is used: halved where it has a pixel, fixed colour unhalved where it has the
backdrop); clip-to-black as a colour-only quad. The 256x224 result is drawn scaled to
274x240 like `DrawPpuFrame`, brightness as a constant multiply per band. citro3d
presents the top screen; the main loop swaps only the bottom screen on those frames.

Calibrated at start-up because it cannot be checked off the console: the readback byte
order, which depth test means "higher level wins", how a display transfer orders rows,
and whether a rendered texture is sampled upside down. The result is on the Debug tab.

Not reproduced: the 32 sprites / 34 slivers per line limits; colour math runs in 8 bits
instead of 5 (differences of a few units); no FPS overlay on the top screen while the
GPU presents it.

## Results on the PC (2026-10-01)

`tools/gpu-ppu-test/run.sh ROM rooms 10`: warp into every room reachable by a door, 10
frames each: 2560 frames, capture replay identical to the normal render, GPU list
identical to the CPU renderer on every pixel, 20 frames refused (mode 7, Ceres).
Console state (Crateria), 120 frames: identical.

Host time per frame, steady state: frame build 46 us against 530 us for the CPU
renderer (about 1/11). On the console that would be about 0.8 ms instead of 8.4 ms,
plus the GPU's own time, which only hardware can tell.

## Checking it on hardware

1. Debug tab -> DEBUG TOOLS -> RENDERER: GPU. The Debug tab shows GPU/CPU frame counts,
   the last fallback reason and the calibration line.
2. GPU CHECK draws the next frame both ways and writes a dump set: `-top.rgb` the CPU
   renderer, `-gpu.rgb` the GPU read back, and the toast says how many pixels differ
   (and how many by more than 8, the 8-bit maths tolerance).
3. If the picture is upside down or garbled, the calibration line says what the GPU
   did; `g_rt_flip_v`, `g_readback_flipped`, `g_depth_greater` in gpu_ppu_3ds.c are
   the knobs.

## Next

- Hardware run; Old 3DS numbers (P0.3).
- Sprite per-line limits if a scene needs them.
- Stereo 3D (phase 3): every layer is already its own quad; per-eye offsets go in
  the vertex positions.
- Mode 7 (Ceres, ending) on the GPU, or keep it on the CPU.
