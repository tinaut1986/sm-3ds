/* Per-room depth fixes for the stereo 3D. Written by tools/layer-workbench (serve.py): edit them there.
 *
 * SM_PLANE_FIX(room, layer, bx, by, block, plane)
 *     One 16x16 block of the room's layer 1 (BG1) or 2 (BG2) goes to `plane` (StereoPlane, stereo_depth.h).
 *     (bx, by) is the block in the room (the level data's own position, not a screen position);
 *     `block` is that block's word in the level data (tile table index + flips + type), a checksum:
 *     a fix whose block no longer matches is ignored and logged.
 * SM_LAYER_PLANE(room, layer, priority, plane)
 *     Every tile of the layer with that tile priority (0 or 1) in the room goes to `plane`.
 */

/* First rules, from the owner's captures of 2026-10-04: tune them in the workbench. */

/* #34: in Ridley's room (Ceres) the floor and walls are BG2, which the depth function takes for the background
 * and puts behind Samus: the scenery goes to the level's plane, in front of her. */
SM_LAYER_PLANE(0xE0B5, 2, 0, kStereoPlay)  /* -> PLAY */
SM_LAYER_PLANE(0xE0B5, 2, 1, kStereoPlay)  /* -> PLAY */

/* #34: in the Ceres elevator shaft (Mode 7) Samus is drawn in front of the room but sat behind it. */
SM_LAYER_PLANE(0xDF45, 4, 2, kStereoFront)  /* -> FRONT */

/* #35: the ash (BG3, behind BG2 on paper, seen over it through colour math) in 9D19. */
SM_LAYER_PLANE(0x9D19, 3, 0, kStereoFront)  /* -> FRONT */
