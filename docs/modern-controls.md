# Modern control scheme (design, #32)

A second control mode, closer to Zero Mission's and Fusion's: no item cycling, the weapons on
held combinations. The owner's decisions of 2026-10-10 are below (running always on: the owner's, the same day). They are a starting point:
the owner expects to adjust them while playing with it. The original controls (CLASSIC) stay
exactly as they are.

**Go.** Tracked as PLAN P4.15. Implemented on `feat/modern-controls` (`source/modern_controls.c`), except
the CONTROLLER SETTING note (below, "The option").

## Mapping (MODERN)

The scheme is complete on an Old 3DS / 2DS (A B X Y L R START SELECT, D-pad, circle pad as the
D-pad). ZL, ZR and the C-stick may add shortcuts on a New 3DS later, never something needed.

| Physical | Does |
|---|---|
| A | jump |
| (nothing) | Samus always runs, as in Zero Mission and Fusion: the speed booster starts by itself after a long enough run |
| B | walk while held (proposed, to try: the only way left to move slowly) |
| X | fire (beam; in morph ball, a bomb) |
| L | aim diagonally up; Down while L is held turns the aim down and it stays down after Down is let go, until L is let go or Up is pressed (Up and Down do not reach the game while L is held: no crouching) |
| R + X | fire the missile kind chosen with SELECT (missile or super missile) |
| R + X in morph ball | power bomb (never stored as a choice: only R decides it) |
| R + B | X-ray scope while both are held (Up/Down move the beam, as in the original); the game itself decides where it can start |
| R + Y | grapple beam; once hooked, Y alone keeps it (proposed, to try) |
| SELECT | switch the chosen missile kind: missile / super missile |
| START | pause, as always |

| Y | into the morph ball, or out of it to standing (the owner's, 2026-10-10: the original's item cancel had nothing left to cancel) |

## What is shown

- **Marked**: the chosen missile kind, missile or super missile, always visible.
- **Active**: the item the game uses right now (none = beam or bombs, missile, super missile,
  power bomb, grapple, X-ray), the game's `hud_item_index`. The marked missile kind keeps its
  marked look while another item is active.
- Both on the bottom screen (STATUS: `StatusHudMarked` / `StatusHudActive` in
  `source/bottom_ui.c`, ready since tap-to-select) **and in the game's HUD**.
- STATUS taps in MODERN only mark missile / super missile; the other panels do not respond.

## Where it lives: the frontend

The achievements must keep working, and they read the game's WRAM, not the buttons. So the game
must only ever see what a player with a SNES pad could produce:

- SNES pad bits, translated from the physical buttons through the game's own `button_config_*`
  (`sm/src/variables.h`, `0x9B2`..`0x9BE`): MODERN's A becomes whatever bit the config calls
  jump, and so on, so the result is right whatever the config holds.
- Item changes through `Hud_RequestSelect()` / `g_rtl_hud_select` (`source/cheats.h`,
  `sm/src/sm_rtl.h`): the game takes them in `HandleSwitchingHudSelection` with SELECT's own
  routines and skip rules.

`joypad1_*`, `hud_item_index` and the rest then hold values the original can hold, and save
states, `.srm` saves, the test tools (they record SNES bits) and RetroAchievements are not
affected. RetroAchievements runs softcore only (`source/retro_ach.c`).

Nothing of MODERN goes into WRAM: the chosen missile kind is frontend state, kept in
`config.ini`.

### Frontend translation

One function between the physical state and `RtlRunFrame` (`source/main.c`, where `inputs` is
built from `g_input1_state | g_gamepad_buttons | CirclePadAsDpad()`), only when MODERN is on:

- L → the aim-up bit, or the aim-down bit once Down was pressed with L held (kept until L is let go or
  Up is pressed); Up and Down masked while L is held.
- The run bit (`button_config_run_b`) is sent held during gameplay only (`game_state` 8: in the
  game's menus B is "back"), except while B alone is held (walk) and while the X-ray is on: the
  X-ray stays on as long as run is held (`XrayRunHandler`, the `HdmaobjPreInstr_Xray*` functions
  in `sm_88.c`), so then run = R and B both held, and letting go ends it as in the original.
- R, SELECT, Y and B never reach the game as themselves.
- While R is held: request the item the context wants every frame it differs from
  `hud_item_index`: power bomb (3) in morph ball (`samus_movement_type` 4 or 8, to check the
  full list), X-ray (5) if B is held, grapple (4) if Y is pressed,
  else the marked missile kind (1 or 2). Re-requesting each frame also covers morphing with R
  held and coming back from the pause menu; a request the game skips (no ammo) leaves the
  selection as it was and is harmless to repeat. In the grapple case Y is sent as the fire bit.
- R released: request 0, except while the grapple is hooked (`grapple_beam_function` not
  inactive) and Y is still held.
- SELECT flips the marked missile kind (saved to `config.ini` once out of gameplay).
- Y alone (morph ball equipped): the frontend presses Down for the player (into the ball) or Up
  (out of it, until standing), one frame in two and not while Samus is in a crouch transition
  (movement type 0x0F, where a press is lost), until it is done or 40 frames have gone by. Left
  and Right are held back meanwhile: in the air Down with a direction is a diagonal aim, and a
  direction between the two Downs undoes the first. The game morphs as it would for a player: on
  the ground (crouch, then ball), running, in a jump straight up, forward or spinning.

The selection is handled before the projectile code in the same game frame
(`Samus_HandleHudSpecificBehaviorAndProjs`, `sm/src/sm_90.c`), so R and X pressed on the same
frame fire the missile, not a beam shot.

### Game side (`sm/src`), HUD only

The HUD's item icons are BG3 tiles in `hud_tilemap`; `ToggleHudItemHighlight` (`sm_80.c`,
`0x809CEA`) swaps their palette bits (`0x1000` highlighted, `0x1400` normal) and
`HandleHudTilemap` repaints them when `hud_item_index` changes. The marked look is a third
state: BG3 palette 0 (`0x0000`), amber in gameplay (checked in Crateria, Brinstar and Norfair
rooms; palette 7, red and yellow, looked like an alarm). The frontend gives the HUD code a mask
of marked items in a C global (`g_rtl_hud_marked`, beside `g_rtl_hud_select`), never in WRAM;
`HandleHudTilemap` repaints them every frame. CLASSIC leaves it 0 and the HUD is drawn as today.
MODERN also silences the HUD's click on a selection change (`g_rtl_hud_quiet`: R would click on
every press and release) and clicks once when SELECT or a STATUS tap changes the marked kind
(`g_rtl_hud_click`).

## The item tutorials (message boxes)

The texts shown when an item is picked up ("select it and press X") are message boxes the port
already redraws for the translations (`source/game_text.c`). Each item's instruction line is a
`(line)` key in `romfs/lang/*.txt`, lowercase, with `{}` where each picture of the English line
goes (the item's icon, a button letter). For MODERN:

- A second key per box, `(modern line)`, used when MODERN is on. Six boxes change: MISSILE,
  SUPER MISSILE, POWER BOMB, GRAPPLING BEAM, X-RAY SCOPE and SPEED BOOSTER (BOMB reads the same
  in both).
- Button letters by name in the line (`{R}`, `{X}`, ...), drawn from the game's own button
  tiles (`kTileNumbersForButtonLetters`, `sm_85.c`), since the English line has no R or L to
  copy. `{}` keeps meaning "the next picture of the English line".
- English goes through the same drawing in MODERN (the ROM's text is CLASSIC's); its lines live
  in the code as the fallback, like every English key.
- The English is `kModernLines` in `source/game_text.c`; a line wider than the box widens it. New keys follow `docs/translations.md` (every `romfs/lang/*.txt` and
  `docs/lang-template.txt`).

The title screen's demos replay recorded SNES input and show no buttons: unaffected.

## The option

- OPTIONS on the bottom screen: the first row is FRAMES | AUDIO (the loudspeaker drawn in AUDIO's
  cell) and CONTROLS: CLASSIC / MODERN with a "?" beside it (where AUDIO's loudspeaker was) that
  opens a window with a tab per scheme and what each button does (`DrawControlsModal`): the
  original's buttons read from the game's configuration, the modern ones fixed. The cell:
  CONTROLS: CLASSIC / MODERN,
  `controls=` and `modern_missile=` in `config.ini`, switchable at any time (switching requests 0
  and clears the marked mask). Done.
- The game's CONTROLLER SETTING MODE becomes a list that scrolls a row at a time (the game's own
  scroll is 32 px, all its 32-row tilemap allows: no room for more rows). Under the title a fixed
  row shows the game's left-right D-pad (SPECIAL SETTING MODE's "change") and CLASSIC / MODERN, the
  chosen one bright; left and right switch it from any row, so the cursor never stops on it. With
  CLASSIC the game's 7 button rows, END and RESET TO
  DEFAULT follow, assigned, checked and reset by the game's own functions; with MODERN 11 rows show
  the modern buttons and combinations, dim and not assignable, then END. The game's code moves the
  cursor and the first row shown (`RtlControlsMenu`, `g_rtl_ctl_cursor` / `g_rtl_ctl_top` in
  `sm/src/sm_rtl.h`); `game_text_screens.c` draws the rows over the game's list every frame, with
  the button icons from the ROM's table (`RtlCtlButtonIcon`), and the font's two D-pads (arrows left
  and right, or up and down: AIM is L + that one) and "+".
  (Tried first as a row in SPECIAL SETTING MODE, then as a small-font line under the title; the
  owner preferred the scheme where the buttons are mapped, with the modern ones listed.) Changes
  away from the touch screen (this row, SELECT's missile kind) reach `config.ini` once out of
  gameplay.
- The game's CONTROLLER SETTING screen remaps CLASSIC only: done as above. In MODERN it is shown as not
  applying (or hidden); its config is still what the translation writes into, so it never
  breaks MODERN.

## Host check

`tools/gpu-ppu-test/run.sh ROM rooms N ROOM` with `MODERN=1` plays `ROOM_SEQ`'s buttons through
the translation as physical buttons; `MODERN_TRACE=1` prints the HUD item, the marked mask, the
ammo, the grapple and Samus's pose each frame; `AMMO=1` gives ammunition with its HUD icons and
`HUD_MARKED=mask` draws the marked look. Checked that way in the Landing Site: R + X fires a
missile (also with both pressed on the same frame), SELECT then R + X a super missile, L aims up, Down
with L held aims down and stays down after Down is let go until Up, without crouching, R + B starts the X-ray and letting go ends it, R + Y
fires the grapple, R + X in morph ball lays a power bomb.

## Open, to settle while trying it

- Whether Y alone should keep the grapple once hooked (proposed above) or R + Y must stay held.
- What R + X does with no missiles of the marked kind: fire the beam (as the original's skip
  rules give) or fall back to the other kind.
- Whether B alone as walk is needed, or running always is enough.
- Whether running always makes the speed booster start where it is not wanted (long corridors).
- The marked look in the HUD: a palette or a blink.
- New 3DS shortcuts (ZL, ZR, C-stick).
