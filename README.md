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

The app checks the ROM header and refuses other versions, since the memory
addresses it reads are specific to this release.

## Controls

| Button | Action |
| --- | --- |
| A, B, START, SELECT, D-pad / Circle Pad | Game Boy controls |
| X | Top screen: 3D world ↔ original picture |
| Y | Cycle camera angle (3DS angle, close, top-down) |
| L / R | Rotate camera |
| ZL / ZR (New 3DS) | Zoom |
| C-stick (New 3DS) | Orbit camera |
| 3D slider | Stereoscopic depth |

## How it works

- `source/core.c` runs the game with the [Peanut-GB](https://github.com/deltabeard/Peanut-GB) emulator core (MIT).
- `source/world.c` reads the current map's block layout from WRAM
  (`wOverworldMapBlocks`), the tileset's metatiles and collision from the ROM,
  and the tile graphics from VRAM, then builds a textured mesh: raised trees,
  fences and ledges, sunken water, and houses whose wall art becomes the front
  facade under a gable roof made from the roof tiles. Colours are Gold's own
  Game Boy Color palettes for the current time of day.
- `source/render.c` draws it with citro3d, culled to the squares around the
  player, plus the player and NPC sprites as upright cut-outs.
- Memory addresses come from [pret/pokegold](https://github.com/pret/pokegold)'s symbol file.

`tools/pctest.c` builds the same mesh on a PC from a RAM dump, for testing
without a console.

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
