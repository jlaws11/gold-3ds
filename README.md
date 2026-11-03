# Gold 3DS

Pokémon Gold on the Nintendo 3DS with a 3D overworld. The top screen rebuilds
the map you are standing in as a 3D world, live, from the game's own memory;
the bottom screen shows the original Game Boy picture, so text, menus and
battles stay readable.

**This repository contains no game content.** You supply your own ROM; the
3DS app reads it from the SD card at runtime.

## Install

1. A 3DS / 2DS with custom firmware and the Homebrew Launcher.
2. Download `Gold3DS.3dsx` from the latest Actions build (or build it, below).
3. Copy it to `/3ds/gold3ds/Gold3DS.3dsx` on the SD card.
4. Copy your Pokémon Gold (USA/Europe) ROM to `/3ds/gold3ds/gold.gbc`.
5. Launch **Gold 3DS** from the Homebrew Launcher. Saves go to `/3ds/gold3ds/gold.sav`.
6. Optional: copy your own Pokémon FireRed or LeafGreen ROM (English, v1.0 or
   v1.1) to `/3ds/gold3ds/firered.gba`. This turns on:
   - **a taller player**: Red's walking sprite, read from that ROM, replaces the
     player in 3D (biking and surfing keep the Gold sprite);
   - **3D battles**: see below;
   - **FireRed's menus** in single-screen mode: see below.
   Without it, the player uses the Gold sprite and battles show the original
   picture on both screens.

The app checks the ROM header and refuses other versions, since the memory
addresses it reads are specific to this release.

## Controls

| Button | Action |
| --- | --- |
| A, B, START, SELECT, D-pad / Circle Pad | Game Boy controls |
| X | Top screen: 3D world ↔ original picture |
| Y | Single screen on/off (remembered) |
| ZL / ZR (New 3DS) | Zoom out / in |
| 3D slider | Stereoscopic depth |

The camera is fixed behind the player, looking north like the original game.

## Single screen

For playing on one screen: Azahar's Single Screen layout, a Steam Deck or a
TV. Press **Y** to turn it on (it's remembered in `/3ds/gold3ds/settings.txt`).
Everything then happens on the top screen:

- Walking around: the 3D world, nothing in the way.
- Talking and signs: Gold's text box along the bottom at 2x, with YES/NO and
  other small boxes just above it.
- The START menu, party, bag, Pokédex and other full-screen menus: the whole
  screen in the middle, with the 3D world dimmed behind.
- 3D battles: the opponent's HP box top-left, yours on the right, and the
  text and move menus along the bottom, around the Pokémon in the world.
- Anywhere else (title screen, battles without FireRed): the Game Boy picture.

The layout is chosen each frame from Gold's own screen buffer, so it follows
the game without any changes to it. The bottom screen keeps showing the Game
Boy picture either way.

### FireRed's look

With FireRed/LeafGreen on the SD card, single-screen mode redraws Gold's
windows in FireRed's style, at the top screen's full resolution: FireRed's
font, its dialogue box with the "more text" arrow, its menu frame for the START
menu, YES/NO and other boxes, and in battle its message panel, FIGHT/PKMN/PACK/RUN
panel, HP boxes with HP and EXP bars, and a move list with PP and type. The
text, cursor and numbers come from Gold each frame, so Gold's menus work as
always.

Full screens are restyled too: the party, its menu, the summary pages, the bag,
the Pokégear, the trainer card, options, naming and save screens and Pokédex
entries are laid out at 1.5x in a FireRed window, with Gold's boxes redrawn as
FireRed frames, its text in FireRed's font, FireRed's HP bars, FireRed's picture
of the Pokémon on summary and Pokédex pages, and FireRed's hopping party icons.
Gold's other artwork on those screens is drawn over the window with its lightest
shade see-through. A screen that scrolls or only partly covers the picture (the
Pokédex list, for one) keeps Gold's own picture.

## Day and night

Lighting follows the game's own clock (the one you set at the start of Gold):

- The sun rises in the east, crosses the sky and sets in the west, so shadows
  from houses and trees are long in the morning, short at noon and swing
  round in the evening. They update every 20 game minutes.
- Warm light at dawn and sunset, a sky colour that changes through the day,
  and a blue night on top of Gold's own morning/day/night palettes.
- House windows and doorways light up after dark.
- The player and NPCs have soft shadows under their feet.
- Indoors and in caves the light comes from above instead.

## Battles in 3D

With FireRed/LeafGreen on the SD card, battles happen in the world. When a
battle starts the camera swings down beside the player, and both Pokémon
appear standing on open ground nearby, drawn with FireRed's pictures (front for
the opponent, back for yours, shiny colours when Gold's rules say so). The
bottom screen keeps Gold's own battle screen for moves, HP and text.

- A wild Pokémon is there from the start; a trainer's appears when sent out.
- Red stands in your Pokémon's place until you send one out.
- Pokémon grow in when sent out, shake when hit and sink away when they faint.
- Lighting and shadows follow the time of day, as in the overworld.

## How it works

- `source/core.c` runs the game with the [Peanut-GB](https://github.com/deltabeard/Peanut-GB) emulator core (MIT).
- `source/world.c` reads the current map's block layout from WRAM
  (`wOverworldMapBlocks`), the tileset's metatiles and collision from the ROM,
  and the tile graphics from VRAM, then builds a textured mesh: raised trees,
  fences and ledges, sunken water, rounded eight-sided tree crowns on trunks,
  tall grass as clumps of upright blades cut from the grass tile,
  and houses whose wall art becomes the front
  facade under a gable roof made from the roof tiles. Colours are Gold's own
  Game Boy Color palettes for the current time of day, picked per environment
  the way Gold does, with each town's own roof colours. Caves get tall rock
  walls; rooms stand their back wall up and float in darkness like the
  original. The mesh is built on a background thread from a snapshot of
  Gold's memory, so walking between maps doesn't pause the game.
- `source/light.c` turns the game clock into sun direction, shadow strength,
  sky and tint; shadows are baked into the mesh by tracing toward the sun.
- `source/render.c` draws it with citro3d, culled to the squares around the
  player, plus the player and NPC sprites as upright cut-outs.
- `source/overlay.c` picks the single-screen layout from Gold's screen buffer
  (`wTilemap`): text box frames, menus and the battle HP boxes.
- `source/frui.c` builds the FireRed-style interface: it decodes FireRed's font,
  frames, battle panels and HP boxes into one texture at startup, then lays out
  Gold's windows and battle screens with them every frame.
- `source/battle.c` stages battles in the world from Gold's battle memory;
  `source/frpics.c` decompresses FireRed's pictures (LZ77, 4bpp) at runtime.
- Memory addresses come from [pret/pokegold](https://github.com/pret/pokegold)'s symbol file.

`tools/pctest.c` builds the same mesh on a PC from a RAM dump, for testing
without a console, and can fake a battle with FireRed's pictures.

## Build

With [devkitPro](https://devkitpro.org/wiki/Getting_Started) (`3ds-dev` installed):

```
make
```

Every push also builds `Gold3DS.3dsx` with GitHub Actions (download it from the run's Artifacts).

## Status: first prototype

- Runs in Game Boy (not Color) mode, so the bottom screen is monochrome; the
  3D world already uses Gold's colour palettes. A Game Boy Color core is next.
- No sound yet.
- Indoor maps use the same rules as outdoors and look rougher.
- Not yet tested on hardware or an emulator.

Pokémon and Pokémon Gold are trademarks of Nintendo, Game Freak, Creatures and
The Pokémon Company. This is an unofficial fan project, not affiliated with or
endorsed by them.
