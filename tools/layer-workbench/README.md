# Layer workbench

A desktop tool (one HTML page and a small Python server) to look at every room layer by layer and decide by
hand which blocks, or which whole layers of a room, go to another plane of the stereoscopic 3D. It is the
counterpart of `../mzm/tools/layer-workbench`, and the depth rules it cannot express (`StereoDepth_Plane`:
by layer and priority) are what it is for: the platform Samus stands on (#28), the spikes (#24), the ash
(#35), the Ceres rooms whose level is BG2 (#34). Tracking issue: #33.

The decisions are saved in `source/sm_plane_fixes.inc`, which the renderer will read (not wired yet: see
"What exists").

## Use

`./run_workbench.sh [ROM]` (repo root) does both steps below: it exports the rooms the first time (ROM as the
argument or in `SM_ROM`; `EXPORT=1` exports again) and starts the server; further options go to `serve.py`.
By hand:

```sh
tools/layer-workbench/export.sh /path/to/Super\ Metroid\ \(Japan,\ USA\).sfc     # once: ~/sm-3ds-rooms/*.room
tools/layer-workbench/serve.py                                                   # opens http://127.0.0.1:8765/
```

The exporter needs the ROM (never committed, nor is its output) and a 64-bit gcc; it boots the game headless,
warps into every room with `source/sm_warp.c` (as `tools/warp-test` does) and writes one `.room` file per
room. The viewer only reads those files: it does not emulate anything, so it can be left open while the
`.inc` is edited. `serve.py ROOMS_DIR --rom ROM --port N --no-browser` change where it reads from, where the area map gets its room positions (optional) and how it starts.

The page is a desk of floating windows (drag by the title, resize from the corner, minimise with the dash; the
layout is kept in the browser, `RELAYOUT` resets it):

- **Rooms:** the rooms in one folder per area (click to fold; the open ones are remembered). Search by hex or area.
  A number shows how many fixes a room or an area has.
- **Map:** the area map, one rectangle per room (positions read from the ROM's room headers: `run_workbench.sh` passes
  `--rom`). Click a room to open it; where several rooms share a cell, each further click takes the next. The open room is
  outlined in yellow, rooms with fixes carry a green dot.
- **Result:** the room as the game draws it (BG2 prio 0, BG1 prio 0, BG2 prio 1, BG1 prio 1). The BG1 / BG2 boxes
  in its title hide a layer; they start from what the game really shows in that room (the top bar says which
  layers are on the main and the sub screen; Ceres' `E0B5` has level data in BG1 that is never drawn).
- **BG1 and BG2:** one window per layer, each with a selector: `REAL` (the picture), `BEFORE` (every tile tinted
  by the plane the depth function gives it, the colours of PLANE TINT), `AFTER` (the same with the fixes and
  rules applied) or `BEFORE|AFTER`, which stacks two panes, each with its own scroll bars. All panes scroll and zoom
  together, and the selection is drawn in all of them. A layer that is not level data (BG2 in most rooms) says so;
  whole-layer rules still apply to it. The `TINT` button in the top bar turns the tint off in BEFORE and AFTER to
  see the real colours (a bar in the plane's colour still marks the tiles with a fix). In AFTER a tile belongs to
  the window of the layer its plane puts it on (BACK and PLAY are BG1's, MID is BG2's): a BG2 tile sent to PLAY leaves
  BG2's AFTER and shows in BG1's, on top of what is there.
- **BG3 (effects):** the effects layer as the game left it in VRAM (2 bpp, whole tilemap, without its scroll), by tile
  priority: tinted by its plane (BEFORE) and with the whole-layer rule applied (AFTER). It has no blocks to select:
  only whole-layer rules apply (Tools -> Whole layer: BG3, priority 0 or 1). Needs room files exported after this was
  added (`EXPORT=1 ./run_workbench.sh`); older ones say so in the window. In rooms where BG3 is on the subscreen the
  game adds it over the scene, and the renderer gives it the depth of what it covers unless a rule here sets its plane.
- **Tools:** the plane buttons, `same as` (the plane a tile on that layer would get: BG1 prio 0 BACK, BG1 prio 1 PLAY, BG2
  MID, BG3 prio 0 FAR, BG3 prio 1 FRONT, sprites OBJ), `APPLY`, `CLEAR FIXES`, `COPY INFO`, the whole-layer rules,
  the room's fixes (click to select, `x` to remove) and `SAVE`.
- **Mouse:** a plain drag selects (a click is a one-block drag), `Ctrl` adds to the selection (or removes what is already
  selected), `Shift` + drag or the middle button pans, `Ctrl` + wheel zooms around the pointer. A selection is bound to the
  pane it started in. Select with the top bar's `BLOCKS (16x16)` or `TILES (8x8)`; the arrows move the selection, `Esc`
  clears it. What a selection covers depends on where it
  was made: in the Result it covers every layer, in a layer window only that layer (the Tools window says which).
  Moving a tile "to another layer" means sending it to that layer's plane with `same as`: only its 3D depth changes,
  the picture still draws it where the game does. Hovering prints the block's words, type, the tile's own data and the
  plane it has before and after.

## The `.inc`

```
SM_PLANE_FIX(0xE0B5, 2, 3, 12, 0x001F, 0x3, kStereoPlay)    /* room, layer, bx, by, block word, corners, plane */
SM_LAYER_PLANE(0xE0B5, 2, 0, kStereoPlay)              /* room, layer, tile priority, plane */
SM_TILE_PRIO(0xA6A1, 1, 34, 3, 0x8194, 0xF, 1)          /* room, layer, bx, by, block word, corners, priority */
```

`corners` is a mask of the block's four 8x8 tiles (1 top left, 2 top right, 4 bottom left, 8 bottom right, `0xF` all of it,
corners of the block as drawn, flips applied), so a single wrong tile can be moved. `room` is the room header pointer (as `SmRoom.header`); `layer` 1 is BG1, 2 is BG2; `(bx, by)` is the block in
the room's level data (not a screen or tilemap position: rooms are larger than the tilemap, which wraps); `block`
is that block's 16-bit word in the level data, a checksum: if the data changes, the fix no longer applies and
says so (it never moves another block). `SM_TILE_PRIO` changes the priority a block's tiles are *drawn* with, not only their depth: priority 1 draws them over the
sprites of priority 0-2 (Samus's weapon), 0 under them, and a tile drawn with priority 1 goes to PLAY by default. Use it when a
wall marked PLAY is still crossed by a sprite: the game has the tile at priority 0. It is the GPU renderer's (the picture changes
there only), and the Result and the BG windows show it (AFTER; BEFORE keeps the tile's own priority).
A block fix beats a layer rule, which beats the default of
`StereoDepth_Plane` (BG1 prio 0 BACK, BG1 prio 1 PLAY, BG2 MID). Planes are the `StereoPlane` names of
`source/stereo_depth.h`.

## The `.room` file

Written by `export_rooms.c` (its comment has the layout): the header (room, area, size in blocks, which layers
the PPU shows on the main and the sub screen, mode and BG3 priority), bank `$7F` of WRAM (the level data: layer 1
words from `$7F:0002`, their BTS after them, BG2's words from `$7F:9602` when it scrolls with the level), the tile
table, CGRAM and VRAM. That is enough to draw the blocks without the emulator: `smroom.py` does it in Python
(`Room.render(layer, priority)`) and `index.html` in the browser. A block word is: bits 0-9 index in the tile
table, 10 hflip, 11 vflip, 12-15 type (0 air, 1 slope, 2 spike air, 3 special air, 4 shootable air, 5 and 13
extensions, 8 solid, 9 door, A spike, B crumble, C shot, E grapple, F bomb). A tile's own bit 13 is its priority.

## What exists, what does not

- Done: the exporter, the viewer with selection and fixes, the server, the `.inc`, and the renderer reading it:
  - the **layer rules** `SM_LAYER_PLANE` for BG1, BG2, BG3 (an effects layer on the subscreen only with a rule: it follows what it covers otherwise), the sprites of a priority (`layer` 4) and Mode 7 (`layer` 5)
    (`source/sm_planes.c`, `GpuPpu_SetPlaneRule`: the quads carry `GpuQuad.plane`, and `QuadPlane` in `gpu_ppu_3ds.c`
    uses it, never over the HUD or outside gameplay);
  - the **block fixes** `SM_PLANE_FIX` for BG1 and BG2 (`GpuPpu_SetSlotPlanes`): a tile of a fixed block is decoded into a
    texture of its plane (per priority, made when first needed) instead of the layer's own, and drawn with the same
    quads at the same level, so only its plane changes. Where a block of the level sits in the tilemap is
    `(2*bx + i, 2*by + j)` modulo the tilemap's size, and of the blocks that share a slot the one nearest the camera
    is on screen (`SmPlanes_SlotPlanes`; checked against WRAM and VRAM of several rooms, `tools/gpu-ppu-test`). A fix
    is ignored when its block word no longer matches the level data (another state of the room).
  The debug log says when a room with rules or fixes is entered; `tools/gpu-ppu-test` with `STEREO_QUADS=1` marks the
  quads "(set by hand)" and `STEREO_PLANES` splits a frame by plane.
- Not yet: sprites by enemy (`SM_SPRITE_PLANE`); the room's enemies and PLMs in the viewer; scene recordings and dumps
  as extra views (mzm has them). Rooms whose layer scrolls on so many lines that the GPU renderer composes its rows on
  the CPU (it never has, on the console) fall back to the CPU renderer while they have tile fixes.
