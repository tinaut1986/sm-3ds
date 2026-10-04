# Layer workbench

A desktop tool (one HTML page and a small Python server) to look at every room layer by layer and decide by
hand which blocks, or which whole layers of a room, go to another plane of the stereoscopic 3D. It is the
counterpart of `../mzm/tools/layer-workbench`, and the depth rules it cannot express (`StereoDepth_Plane`:
by layer and priority) are what it is for: the platform Samus stands on (#28), the spikes (#24), the ash
(#35), the Ceres rooms whose level is BG2 (#34). Tracking issue: #33.

The decisions are saved in `source/sm_plane_fixes.inc`, which the renderer will read (not wired yet: see
"What exists").

## Use

```sh
tools/layer-workbench/export.sh /path/to/Super\ Metroid\ \(Japan,\ USA\).sfc     # once: ~/sm-3ds-rooms/*.room
tools/layer-workbench/serve.py                                                   # opens http://127.0.0.1:8765/
```

The exporter needs the ROM (never committed, nor is its output) and a 64-bit gcc; it boots the game headless,
warps into every room with `source/sm_warp.c` (as `tools/warp-test` does) and writes one `.room` file per
room. The viewer only reads those files: it does not emulate anything, so it can be left open while the
`.inc` is edited. `serve.py ROOMS_DIR --port N --no-browser` change where it reads from and how it starts.

- **Left:** the rooms by area (search by hex or area). A number shows how many fixes a room already has.
- **Centre:** the room. `RESULT` draws it as the game does (BG2 prio 0, BG1 prio 0, BG2 prio 1, BG1 prio 1);
  `PLANES` tints every tile with the colour of the plane it goes to (the same colours as PLANE TINT on the
  console); `BG1` and `BG2` show one layer. The BG1 / BG2 boxes hide a layer: they start from what the game
  really shows in that room (the top bar says which layers are on the main and the sub screen; Ceres' `E0B5`
  has level data in BG1 that is never drawn). Hovering a block prints its words, type and the plane each
  priority gets.
- **Select** by clicking or dragging a rectangle; `Ctrl` adds (or removes a block that is already selected),
  the arrows move the selection, `Esc` clears it.
- **Right:** pick a plane and `APPLY`: every selected block of the ticked layers that has something to draw
  gets a `SM_PLANE_FIX`. `CLEAR FIXES` removes those of the selected blocks. **Whole layer of this room**
  adds a `SM_LAYER_PLANE`: all the tiles of a layer with a given tile priority go to a plane. `COPY INFO`
  puts the selection's data on the clipboard, to reason about a case without the tool. `SAVE` writes the
  `.inc` (the whole file, sorted).

## The `.inc`

```
SM_PLANE_FIX(0xE0B5, 2, 3, 12, 0x001F, kStereoPlay)    /* room, layer, bx, by, block word, plane */
SM_LAYER_PLANE(0xE0B5, 2, 0, kStereoPlay)              /* room, layer, tile priority, plane */
```

`room` is the room header pointer (as `SmRoom.header`); `layer` 1 is BG1, 2 is BG2; `(bx, by)` is the block in
the room's level data (not a screen or tilemap position: rooms are larger than the tilemap, which wraps); `block`
is that block's 16-bit word in the level data, a checksum: if the data changes, the fix no longer applies and
says so (it never moves another block). A block fix beats a layer rule, which beats the default of
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

- Done: the exporter, the viewer with selection and fixes, the server, the `.inc` (empty), this note.
- Not yet: the renderer reading the `.inc` (blocks to another plane need BG1/BG2 split into one texture per
  plane, as `1fa4320` did by block type, and that did not work on the console, so the cause is to be found first);
  sprites (`SM_SPRITE_PLANE` by enemy) and Mode 7; showing the room's enemies and PLMs in the viewer; scene
  recordings and dumps as extra views (mzm has them).
