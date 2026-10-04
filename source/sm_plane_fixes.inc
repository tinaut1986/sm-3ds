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
