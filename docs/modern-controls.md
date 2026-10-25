# Modern control scheme (design, #32)

A second control mode, closer to Zero Mission's and Fusion's: no item cycling, the weapons on
held combinations. The owner's decisions of 2026-10-10 are below (running always on: the owner's, the same day; replaced on 2026-10-11 by B held to run, below). They are a starting point:
the owner expects to adjust them while playing with it. The original controls (CLASSIC) stay
exactly as they are.

**Go.** Tracked as PLAN P4.15. Implemented on `feat/modern-controls` (`source/modern_controls.c`), except
the CONTROLLER SETTING note (below, "The option").

## Mapping (MODERN)

The scheme is complete on an Old 3DS / 2DS (A B X Y L R START SELECT, D-pad, circle pad as the
D-pad). On a New 3DS a weapon can sit on ZL or ZR instead (Remapping, P4.21), never something needed; the
C-stick is unused.

| Physical | Does |
|---|---|
| A | jump |
| B | run while held, as in the original (2026-10-11, the owner's: running always on was dropped; without B Samus walks) |
| X | fire (beam; in morph ball, a bomb) |
| L | aim diagonally up; Down while L is held turns the aim down and it stays down after Down is let go, until L is let go or Up is pressed (Up and Down do not reach the game while L is held: no crouching) |
| R + X | fire the missile kind chosen with SELECT (missile or super missile) |
| R + X in morph ball | power bomb (never stored as a choice: only R decides it) |
| R + B | X-ray scope while both are held (Up/Down move the beam, as in the original); the game itself decides where it can start |
| R + Y | grapple beam; once hooked, Y alone keeps it (proposed, to try) |
| SELECT | switch the chosen missile kind: missile / super missile |
| START | pause, as always |
| ZL, ZR (New 3DS) | nothing by default; a weapon put on one works with it alone, in place of R + its button (Remapping) |

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
- The run bit (`button_config_run_b`) is sent while B is held during gameplay (`game_state` 8: in the
  game's menus B is "back" and passes as it is); while the X-ray is selected only with R and B both
  held: the X-ray stays on as long as run is held (`XrayRunHandler`, the `HdmaobjPreInstr_Xray*` functions
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
  DEFAULT follow, assigned, checked and reset by the game's own functions; with MODERN 10 rows show
  the modern buttons and combinations (seven of them assignable, see Remapping), then END. The game's code moves the
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
`HUD_MARKED=mask` draws the marked look; `MODERN_WITH` also takes `ZL` / `ZR`, which are `1000` / `2000` in
`ROOM_SEQ`. Checked that way in the Landing Site: R + X fires a
missile (also with both pressed on the same frame), SELECT then R + X a super missile, L aims up, Down
with L held aims down and stays down after Down is let go until Up, without crouching, R + B starts the X-ray and letting go ends it, R + Y
fires the grapple, R + X in morph ball lays a power bomb.

## Remapping (P4.18)

Two small tables, both frontend state in `config.ini`:

- **Actions**: jump, run, shoot, morph ball, each on one of A / B / X / Y. A permutation: giving an action a
  taken button swaps the two actions. Default A, B, X, Y.
- **Weapons**: missile kind, X-ray, grapple, each riding on one action's button, one weapon per action, held
  with R, or (New 3DS) alone on ZL or ZR. Default shoot's, run's and morph's (R + X, R + B, R + Y). Rules: no
  weapon rides on jump (R held must still jump, so R + jump + shoot jumps and fires a missile; 2026-10-26), and
  the missile is never on the run action's button (R + run is another weapon's shortcut, so there are no missiles
  while running: R + run + shoot is not practical to press). A button with a weapon does that weapon while R is held
  instead of its action; with no weapon on it, it does its action (R + the jump button is a jump with the missile
  selected, while the missile is held with R). Because weapons ride on actions, moving an action moves its weapon
  with it and the rules cannot break that way.
- **ZL / ZR** (P4.21, New 3DS only): R + ZR cannot be pressed, so a trigger *replaces* a weapon's R + button, it is
  not a combination with it. The weapon then works with the trigger alone, as it did with R + its button: the missile
  fires on the press (the power bomb in morph ball), the X-ray lasts while the trigger is held (the run bit the game
  needs is sent while it is), the hooked grapple stays while it is held. The button it rode on is free and does its
  action with R too. With the missile on a trigger, R alone selects nothing (otherwise R + a free button would still
  fire missiles); in morph ball R still selects the power bomb. Two weapons can be on triggers; the third stays on R.
  Putting a weapon on a trigger another one has swaps them (that one takes the first one's old place, if the rules
  allow it). ZL and ZR never reach the game: they are pad bits 12 and 13 of the frontend's input, which
  `ModernControls_Translate` strips in both schemes.

The translation (`ModernControls_Translate`) stops naming physical buttons: it resolves each action and weapon
through the tables. Two things already done by it now have to follow the table, not B and Y: the run bit the X-ray
needs (sent while R and the X-ray's button are held, wherever that button is) and the fire bit the grapple needs.
R + the jump button, with no weapon there, is a jump with the missile selected, as today.

Saved as `modern_jump=B`, `modern_run=`, `modern_shoot=`, `modern_morph=` (the button) and
`modern_with_missile=shoot`, `modern_with_xray=run`, `modern_with_grapple=morph` (the action, or `ZL` / `ZR`) in
`config.ini`; what does not read as a valid name, a weapon on jump, and `ZL` / `ZR` on an Old 3DS / 2DS are ignored.

UI, both writing the same table: the rows of CONTROLLER SETTING MODE (JUMP, DASH, SHOT / BOMB, MORPH BALL, MISSILE,
GRAPPLING BEAM and X-RAY SCOPE take the face button pressed on them, the three weapons ZL / ZR too on a New 3DS, which
the game learns of through `RtlControlsMenu.trigger` since they are no SNES buttons, and written as letters: the ROM has
no icon for them; AIM, POWER BOMB (it is the missile's) and MISSILE / SUPER stay dim), and OPTIONS -> CONTROLS -> "?" on the bottom screen (tap a row, press the button: the main
loop gives that press to the window, `BottomUi_CapturingButton`, and the game does not see it; RESET puts the
scheme's defaults back). The same window remaps CLASSIC's seven buttons by writing the game's `button_config_*`
(A B X Y L R SELECT, the one that had the button takes this one's old one). A weapon refused (on jump, the missile on
run) shows a toast saying why, from either UI. Button names in the item boxes (`{X}`, `{B}`...) mean the action
that button has by default and resolve to the button in use; with the weapon on a trigger the box shows its
`(trigger line)` key instead ("{} press {ZR}.", `kTriggerLines` in `source/game_text.c`), where `{ZR}` is the
trigger in use, drawn as a Z in the L / R letter's colour beside that letter. The "?" window and STATUS's help read the
tables ("ZR" for a weapon on a trigger, "R+X" otherwise).

## Open, to settle while trying it

- Whether Y alone should keep the grapple once hooked (proposed above) or R + Y must stay held.
- What R + X does with no missiles of the marked kind: fire the beam (as the original's skip
  rules give) or fall back to the other kind.
- The marked look in the HUD: a palette or a blink.
- The C-stick (New 3DS): unused.
