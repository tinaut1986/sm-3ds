# Release notes

One file per tag: `vX.Y.Z.md`. The CI copies it into the GitHub release body between
`<!-- sm-notes -->` markers, and the console shows it in the updater (OPTIONS -> WHAT'S NEW,
and on the "new version" prompt) before the player installs.

Rules (details in `CLAUDE.md`, "Release notes"):

- 3 to 6 short lines, each starting with `- `. Plain text for the player, in English, under
  ~2.5 KB. The console's font is 5x7: no tables, links or images.
- A **stable** release lists everything since the previous *stable* tag, betas included
  (stable players never see beta pages). A **beta** lists only what is new since the
  previous tag.
- Written when the tag is made, as one summary of the range, and merged before the tag exists.

Example (`v0.3.0.md`):

```
- A charging bolt on the bottom screen's battery.
- WIDE shows projectiles and enemy pieces in the rows above the picture.
- HOME shows the game again and no longer freezes the console after closing it.
- Any number of save states, with a detail window.
- The game can now update itself from OPTIONS.
```
