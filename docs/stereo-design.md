# Stereoscopic 3D: design (P3.1-P3.4)

Status: proposal, 2026-10-03. Nothing here is implemented yet.

The SNES has no depth. The port invents it by drawing the frame twice, once per eye,
with each layer shifted sideways by its own amount. Everything below is about choosing
those amounts so the picture stays readable, and about drawing it twice cheaply.

## What we take from mzm, and what we do not

`../mzm/platform/3ds/source/port_stereo_depth.{h,c}` and its host test
(`tests/stereo_depth_test.c`) settled four things that carry over unchanged:

1. **Depth is a pure function of the PPU state of the frame**, in its own file with no
   3DS headers, so a host test can enumerate every input. Rules that infer depth from
   level data or collision were tried in mzm and each broke something else.
2. **The two failures to test for**: one object split across two planes, and a layer
   placed nearer than something that visibly draws over it (parallax says "in front",
   occlusion says "behind").
3. **Whole-pixel offsets, rounded once per plane per eye.** With nearest sampling every
   quad rounds a fractional offset on its own, so part of a layer moves by a pixel and
   part does not: glyphs tear, and which ones tear changes as the slider moves (mzm's
   "ghost pixels" on text). mzm rounds `slider * plane_px` per plane per eye; the same
   here. The HUD and every text plane additionally use offsets that are whole pixels
   at every slider step they are drawn at (below), so text never shimmers.
4. **World sprites on one plane, just behind the platform layer**, so the ground Samus
   walks on reads as having thickness in front of her (she walks along its middle).
   This is the one accepted contradiction with the 2D order (BG1 low-priority tiles are
   drawn under her, yet sit slightly nearer), and the test pins it as deliberate.

Not carried over: mzm's per-cutscene, per-menu and per-block override lists
(`port_cutscene_depth`, `port_layer_fixes`, `port_sprite_depth`). They patch how
Zero Mission builds its menus, maps and cutscenes; SM builds them differently, so P3.4
looks at SM's own screens first and only then decides whether any list is needed.

## The SNES side: what a frame gives us

Gameplay is mode 1 with BG3 priority on (`bg3priority=1`). The GPU renderer already
gives every quad a compositor level (`GpuQuad.level`, gpu_ppu.c), higher drawn in front:

| Level | Layer | In gameplay |
|---|---|---|
| 15 | BG3, tile priority 1 | HUD (rows 0-31), message boxes, FX drawn over everything |
| 14 | OBJ priority 3 | |
| 12 | BG1, tile priority 1 | foreground tiles the room draws over Samus (pillars, grass) |
| 11 | BG2, tile priority 1 | |
| 10 | OBJ priority 2 | Samus, most enemies |
| 8 | BG1, tile priority 0 | the level: floors, walls, platforms |
| 7 | BG2, tile priority 0 | the room's background |
| 6 | OBJ priority 1 | |
| 2 | OBJ priority 0 | |
| 1 | BG3, tile priority 0 | FX behind everything (fog, haze) |
| 0 | backdrop | |

Mode 7 (Ceres elevator, Ridley's getaway) has one BG (the plane) plus sprites.

So the depth input is small and explicit: the layer, its tile priority, the OBJ
priority, the BG mode, and a few frame facts the renderer already knows (whether this
quad is in the HUD list, whether the frame is gameplay, WIDE margins).

## Planes

Eight planes; offsets are pixels of shift per eye at full slider, positive = nearer.
Values are a starting point to tune on the console, not a decision.

| Plane | What goes there | Offset (full slider) |
|---|---|---|
| `HUD` | the HUD list (BG3 HUD rows, the escape timer), message boxes, the port's text (FPS overlay, toasts) | +2 |
| `FRONT` | BG3 priority 1 that is not the HUD: water surface, lava, fog over the room | +1 |
| `PLAY` | BG1 and BG2 tile priority 1 (levels 12, 11): SM's walls and floors; sprites of OAM priority 3 (drawn over them); the mode 7 plane | 0 |
| `OBJ` | Samus and enemies (OAM priority 0-2) | -1 |
| `BACK` | BG1 tile priority 0 (level 8): the level's parts Samus passes in front of (the save station's glass, background pipes) | -2 |
| `MID` | BG2 tile priority 0 (level 7) | -3 |
| `FAR` | BG3 priority 0 FX, backdrop | -4 |
| `SCREEN` | non-gameplay screens until P3.4 treats them: everything flat | 0 |

Why these (revised 2026-10-03 after the first console test):

- SM draws its walls and floors with BG1 tile priority 1 and Samus at OAM priority 2,
  under them (she passes behind wall edges). Following that order puts the walls one step
  in front of her: mzm's platform thickness comes from the compositor itself, with no
  contradiction. The first cut had BG1 priority 1 as a "foreground" plane and priority 0
  as the level, which put the save station's glass (priority 0, drawn under Samus) in
  front of her and sank the wall faces of 9A44 (sprites of priority 3 drawn over the wall)
  behind it.
- `PLAY` at 0 keeps the walls at the screen's own depth, where the eyes focus anyway.
- `OBJ` = `PLAY` when the thickness option is off; the test checks both settings.
- `MID` is BG2 priority 0, the room's background. BG2 priority 1 (level 11) draws over
  Samus, so it is a wall and goes on `PLAY` like BG1 priority 1. Some rooms keep their
  whole level on it: the Fireflea room (9C5E) has BG1 empty, and with BG2 all on `MID` the
  floors sat 3 px behind Samus and level with the background (found with the scene recorder).
- `HUD` and every other plane move in whole pixels (point 3).
- The mode 7 plane goes on `PLAY`: in the Ceres elevator it is the room Samus stands
  in, in Ridley's room it is Ridley flying at her.
- Without WIDE the HUD is not taken out into the HUD list: BG3 quads within the top 32
  rows count as HUD there (gpu_ppu_3ds.c `QuadDx`).

## The 3D slider and whole pixels

The slider gives a float 0..1 (`osGet3DSliderState`). Per eye and plane:
`offset = round(eye_sign * slider * plane_px)`, computed once per frame and plane, then
added to every quad of that plane. That alone makes each plane move rigidly.

For the text planes that is not enough: as the slider moves, the HUD's offset changes
at different slider positions than the world's, so for an instant the HUD can sit at
the same depth as the level behind it. Fine for depth, but the HUD must never land on a
fractional position, and it does not: offsets are integers by construction. What
remains is a matter of taste (the HUD stepping from 0 to 1 to 2 px), to judge on the
console.

SCALED mode stretches 256 to 274 columns (x1.07, nearest). Both eyes go through the
same stretch, so a 1-pixel shift in SNES pixels is still a clean shift in each eye's
image. Whether that reads well at 1.07 is a check for the console; PIXEL PERFECT is 1:1
and has no such question.

## Drawing it twice

Today (gpu_ppu_3ds.c): the frame's quads go into a 512x256 main target (plus a sub
target for colour math), and that texture is drawn scaled onto the top screen.

With 3D on and the slider above 0:

1. The frame is built once on the CPU, as now (`GpuPpu_BuildFrame`). Every quad gets
   its plane from the depth function (a byte next to `level`).
2. Per eye: draw the bands into the main/sub targets with each quad moved by its
   plane's offset, then present into that eye's top target (`GFX_LEFT`, `GFX_RIGHT`).
   The second eye re-issues the same vertex data with a per-plane offset uniform, as
   mzm's batch replay does, so the CPU cost of the second eye is small. The GPU cost
   roughly doubles; on the New 3DS the log shows "wait for GPU 0.0", and the 2DS has
   no 3D screen (the slider reads 0, one eye, nothing changes there).
3. Masks, colour windows and the HUD list move with their own planes: a mask belongs to
   the margins (no shift), the HUD list to `HUD`.

Edges: shifting a layer by N pixels uncovers N columns at the edge of the view. With
WIDE those are the margins' columns, already built. Without WIDE the frame builder adds
`max offset` columns per side (as it does for the margins, `GpuPpu_SetMargins`) and the
present step crops them, so the edge never shows empty columns.

Colour math: the sub screen is drawn per eye with the same planes, so translucent water
over the level lines up in each eye.

The CPU renderer (refused frames, rare in gameplay) draws one flat image for both eyes.

## Tests (host)

`tools/stereo-test/`, built like the other host tools, compiling the depth file alone:

- Exhaustive over mode 1 levels x OBJ priorities x the frame facts: no BG is nearer than
  a layer that draws over it, except the pinned `PLAY`/`OBJ` thickness; the HUD plane is
  the nearest of all; every plane's offset is an integer for every slider step.
- Mode 7 frames: one plane for the plane, sprites as in gameplay.
- From real frames: a few save states (Landing Site, a heat room, Maridia water, Ceres
  elevator, Ridley) run through gpu-ppu-test with a `STEREO=1` dump of each eye, to look
  at, plus a check that no quad of one tile is split across two planes.

## Tasks this splits into

- **P3.1**: the depth file and the host test above. No rendering change.
- **P3.2**: per-eye drawing with the offsets, edges, slider, an option to turn 3D off.
  Done when the New 3DS shows the planes with no torn text at any slider position and
  holds 60 fps in Landing Site, Ceres and a heat room.
- **P3.3**: a debug tint per plane (like mzm's), and the scene checks: Spore Spawn's BG2,
  water and lava surfaces, the escape timer, message boxes.
- **P3.4**: title, file select, map and pause, cutscenes, Ceres mode 7: flat first, depth
  only where it reads well.
