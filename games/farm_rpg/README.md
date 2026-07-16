# Farm RPG

A Stardew-shaped farming sim built on Vortex, using the
[FarmRPG asset pack](https://emanuelledev.itch.io) by **EmanuelleDev** (credit required by
the pack's licence — see `assets/2d/FarmRPG/Documentation.txt`).

```
cmake --build build/relwithdebinfo --target farm_rpg
./build/relwithdebinfo/games/farm_rpg/farm_rpg
```

## The loop

Hoe a tile, sow a seed, water it, sleep, repeat until it is ripe, pick it, drop it in the
shipping bin by the house, and wake up richer. Seeds come from the General Store on the
far side of the map.

The calendar is the pressure. A season is 28 days, and a crop caught by the turn of the
season **dies where it stands** — so a cauliflower (12 days) sown on day 20 is 80 G
thrown away. The store only stocks seeds that can still grow, which is the cheapest way
to teach that rule. Nothing grows in winter: the pack ships no winter crops, and that
happens to be the real game's answer too.

Energy limits the day and 2am ends it whether you are ready or not.

## Controls

| | |
|---|---|
| `WASD` / arrows | walk (`Shift` to run) |
| `1`..`0` | select hotbar item |
| `Space` / left click | use held item on the tile you face |
| `E` | interact — shop, shipping bin, bed |
| `F5` / `F9` | save / load |
| `Esc` | quit (autosaves) |

## Layout

| file | what lives there |
|---|---|
| `farm.hpp/.cpp` | calendar, item ids, the crop table, `GameState` |
| `assets.hpp/.cpp` | art loading: paper-doll compositing, crop-sheet probing |
| `world.hpp/.cpp` | tile layers, collision, the farm grid, `advanceDay` |
| `player.hpp/.cpp` | movement, facing, tool swings |
| `hud.hpp/.cpp` | hotbar, clock, energy, shop, sleep card |
| `save.hpp/.cpp` | the plain-text save |

Two decisions in `assets.cpp` are worth knowing about, because both are the art pack
pushing back:

* **The player is a paper doll.** Skin, clothes, eyes, hair and the tool being swung all
  ship as separate sheets, composited once at load. `CharacterLook` is therefore already
  a character creator — every field is a directory name in the pack.
* **Crop sheets are read, not described.** The pack lays a crop out as growth stages, a
  gap, then the harvested item's icon, and the stage count varies per crop. The loader
  scans the alpha to find the split. It has to walk *backwards* from the icon: Pumpkin's
  second stage is blank, so counting forward stops at frame 1 and the crop never grows.

The watered-soil tiles in the pack are periwinkle blue, which no farm has ever been, so
`world.cpp` redraws the dry art under a dark tint instead.

## Checking it

```bash
VORTEX_FARM_CHECK=1 ./build/relwithdebinfo/games/farm_rpg/farm_rpg
```

Plays the core loop with no hands — till, sow, water, sleep until ripe, harvest, ship,
sleep — and exits non-zero unless money actually moved. It needs a GPU, like every
App-based example in this repo.

`VORTEX_SCREENSHOT=<path>` writes one frame to a PNG (`VORTEX_SHOT_FRAME=<n>` picks
which), which is the only way to see the game on a machine that cannot grab its window.

## Not here yet

Fishing, mining and combat, NPCs and relationships, animals, cooking and crafting,
tool upgrades, and multi-tile buildings you can walk inside. The tilled soil also uses
one flat centre tile rather than the pack's 47-blob autotile, so a field is a hard-edged
rectangle instead of a rounded patch.
