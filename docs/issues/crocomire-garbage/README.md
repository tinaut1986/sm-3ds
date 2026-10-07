# Garbage in Crocomire's room (A98D)

Evidence for the GitHub issue of the same name (the issue has the analysis). Taken on the owner's 2DS on 2026-10-08, build
`v0.3.3-dev.10.18+3b67c41`, UI language Spanish, WIDE + PIXEL PERFECT, GLITCH OR GARBAGE report.

- `sm-dump-0010-*`: the SCREEN DUMP set (top screen as drawn by the CPU renderer, VRAM, CGRAM, OAM, WRAM, PPU registers, game state).
- `console-top.png`, `console-vs-host.png`, `scene-rec-first-and-last-frame.png`: what the screen showed; `vram-4a00-4fff-as-2bpp.png`:
  the VRAM words 0x4A00-0x4FFF drawn as 2bpp chars.
- `compare_vram.py`: compares this VRAM with a clean host VRAM of the same room.
- The scene recording (`sm-rec-0006.bin`, 41 frames, 5 MB) is not kept here: it stays on the owner's SD card.

Delete this folder when the issue is closed.
