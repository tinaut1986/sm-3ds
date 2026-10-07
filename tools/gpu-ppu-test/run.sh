#!/bin/bash
# Host check of the GPU renderer's frame building against the CPU renderer
# (see gpu_ppu_test.c for what is compared).
#
# Usage: tools/gpu-ppu-test/run.sh ROM state STATE.sav [FRAMES]
#        tools/gpu-ppu-test/run.sh ROM rooms [FRAMES] [ROOM_HEX]
#        tools/gpu-ppu-test/run.sh ROM pbomb STATE.sav [FRAMES]   (power bomb, every frame to pb-NNN.ppm)
#        tools/gpu-ppu-test/run.sh ROM boot SRAM.srm [FRAMES]     (continue the first file, test from the load)
# Mismatching frames are written to $WORK/diff-NN.ppm (CPU | GPU | difference).
# WIDE=M: also run the WIDE view (game side on, M px margins) and check each wide frame's
# middle equals the normal one; WIDE_DUMP=N writes wide-NNN.ppm, WIDE_BANDS=1 prints the
# bands, WIDE_INFO=1 the room's scroll colours, WIDE_DUMP_ROOM=hex dumps only that room's frames,
# WIDE_DUMP_FROM=n only from tested frame n on. SAMUS_HEALTH=n: her health in the rooms mode's
# tested frames (99 otherwise; below 30 Ceres Ridley gives up and flies off).
# HIDE_HUD=n (1 the HUD's status half, 2 its minimap, 3 both): they are not drawn (SmWide_HideHud); in gameplay frames the CPU
# renderer's picture must be black in the hidden half of lines 0-30 (prints HIDE_HUD ... lit pixels).
# WIDE_EDGE=1 (with WIDE=4): the margins of the 3D without WIDE, which show only the room's edge columns: the HUD stays in
# its band and the game does not draw the room under it (SmWide_SetView's hud_over_room off). Same checks.
# WIDE_Y=N: N extra rows above and below (PIXEL PERFECT), leaning off a room's top or bottom;
# WIDE_LEAN=1 prints each frame's lean; WIDE_HUD_INBAND=1 draws the reference's HUD in its band.
# FORCE_STATE=n sets the game state on frame 5 of the rooms mode (38: the ending).
# SRAM_SAVE=n saves the game to file n (0-2) on frame 2 of the rooms mode (saves/sm.srm).
# SAMUS_AT=x,y puts Samus there on frame 1 (SCROLLS_OPEN=1: all scroll screens blue;
# ITEMS=hex: items given; XRAY=1: the X-ray scope selected, hold Y with ROOM_SEQ=2@n). SHOTS=a-b writes tested frames a..b as shot-NNNN.ppm with vram-NNNN.bin, cgram-NNNN.bin (SHOTS_STEP=n: every n-th; MSGBOX=n queues
# message box n first; GAME_LANG=n in UI language n, ui_lang.h). WRAM_TRACE=1 writes wram-NNNN.bin per frame. CERES_ESCAPE=1: the Ceres escape is on (DF45 tilts).
# EPROJ_MARGIN=1 [EPROJ_ID=hex]: count the frames with an enemy projectile (of that id) outside the game's 256 px window
# (WIDE keeps them alive in the margins); EPROJ_LIST=n lists every projectile each n-th tested frame.
# ENEMY_LIST=n lists every enemy slot (pointer, place, screen x, properties) each n-th tested frame; ENEMY_MARGIN=ptr counts
# the frames with an enemy of that kind more than 40 px left of the normal view (WIDE lets it walk into the margin).
# STATE_SURVEY=n: every n-th tested frame and when the game state changes, what the frame holds per compositor level;
# LEVEL_SPLIT=f1,f2,...: those tested frames as one image per level (level-FFFF-L15.ppm; -o10 = sprites): what each
# non-gameplay screen is made of (P3.4, source/sm_planes.c SmPlanes_Screen).
# STEREO_PLANES=a-b: tested frames a..b split by stereo plane (planes-NNNN-P.ppm, magenta = none).
# STEREO_P3=n: every sprite quad of OAM priority 3 (level 14) in the tested frames must be on StereoPlane n (prints STEREO_P3 ... not on the plane N).
# STEREO_QUADS=1 (with STEREO_PLANES): also prints every quad of those frames with its level and plane.
# ONLY_LEVEL=n keeps only the quads of compositor level n in them; QUAD_LEVELS=n prints tested frame n's quads.
# ROOM_SEQ=hex@frame,... (buttons from each frame on) and AUTOFIRE=1 also work in the state mode;
# BOOT_SEQ=hex@frame,... is the same for the boot mode (instead of its START/A pattern).
# BOOT_SEQ_STATE=hex: BOOT_SEQ's frames count from the first frame in that game state.
# INTRO_CURSOR_CHECK=1 (boot mode, with GAME_LANG): in the intro's story text the typing cursor (sprite tile 0xFC) must be
# after the last letter on screen or at the start of the next line (prints INTRO CURSOR frames, bad). EDGE_CHECK=1: no OAM
# entry in the bottom band (raw Y 224-255) is untagged (see sm_rtl.h, g_rtl_oam_x) unless parked, and it counts the tagged
# ones outside the view (prints EDGE ...). SAMUS_PIN=dy[,pose]: Samus dy px below the camera's top every frame from frame 3
# (negative: above the view; over 224: below it), in that pose (hex) if given.
# Built 32-bit with -malign-double so console save states load (docs/debug-tools.md).
set -e
ROM=$(realpath "$1")
MODE=$2
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-gpu-ppu-test}
mkdir -p "$WORK/saves"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
# NO_BUILD=1: reuse $WORK/gpu_ppu_test (tools/test copies one build to every test)
if [ -z "$NO_BUILD" ]; then
  gcc ${HOST_CFLAGS:--m32 -malign-double} -O2 -g -fno-strict-aliasing -I"$S" -I"$ROOT/source" -I"$ROOT/third_party/sdl_keys" \
      -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE -w $SRCS \
      "$ROOT/source/gpu_ppu.c" "$ROOT/source/gpu_ppu_ref.c" "$ROOT/source/sm_map.c" "$ROOT/source/sm_warp.c" "$ROOT/source/sm_wide.c" "$ROOT/source/stereo_depth.c" "$ROOT/source/sm_planes.c" "$ROOT/source/game_text.c" "$ROOT/source/game_text_screens.c" "$ROOT/source/ui_lang.c" \
      "$ROOT/tools/gpu-ppu-test/gpu_ppu_test.c" -o "$WORK/gpu_ppu_test" -lm
fi
[ "$2" = build ] && exit 0   # tools/test: compile only
rm -f "$WORK"/diff-*.ppm
if [ "$MODE" = state ] || [ "$MODE" = pbomb ]; then
  cp "$(realpath "$3")" "$WORK/saves/save9.sav"
  rm -f "$WORK"/pb-*.ppm
  cd "$WORK" && ./gpu_ppu_test "$ROM" $MODE "$3" ${4:-60} 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* "
elif [ "$MODE" = boot ]; then
  # boot SRAM [FRAMES]: continue the first file of that SRAM and test from the load on
  rm -f "$WORK"/saves/*
  cp "$(realpath "$3")" "$WORK/saves/sm.srm"
  cd "$WORK" && ./gpu_ppu_test "$ROM" boot ${4:-1500} 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* \|RomPtr - Invalid"
else
  cd "$WORK" && ./gpu_ppu_test "$ROM" rooms ${3:-20} $4 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* "
fi
