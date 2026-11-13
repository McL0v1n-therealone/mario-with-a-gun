# Super Mario 64 weapons mod

Built on the n64decomp/sm64 decompilation (US). Only source is tracked here;
ROMs (`*.z64`, including `baserom.us.z64`) and `build/` are ignored.

## Build & play
```
make VERSION=us COMPARE=0 -j$(nproc)          # run `make clean` first after changing Makefile DEFINES
cp build/us/sm64.us.z64 "../Super Mario 64 (Zapper).z64"
../play.sh                                     # copy of tools/play.sh (RMG, compositing off while playing)
```
After editing weapon models: `cd actors/mario && python3 gen_weapons.py` (GUN_SCALE=2 for test-size guns).

## Controls
X / N64 L = fire (hold for SMG & rifle), A / D = D-pad left/right = switch weapon.

## Features
- 6 weapons (light gun, pistol, magnum, shotgun, SMG, rifle) with projectiles that hit enemies like a punch
- Recoil, muzzle flash, slide/pump/cylinder animations, weapon draw animation, HUD weapon name
- Aiming pose: right arm raises the gun when firing (renderer bone override)
- Opening cutscene skipped on new save files
- 8MB Expansion Pak memory layout (USE_EXT_RAM) - vanilla 4MB pool ran out in Castle Grounds

## Where things live
| What | File |
|---|---|
| Weapon stats, firing, projectiles, animations, aiming | `src/game/weapons.c` |
| Weapon models (generator + generated output) | `actors/mario/gen_weapons.py`, `weapons*.inc.c` |
| Gun + aim markers in Mario's skeleton | `actors/mario/geo.inc.c` |
| Bone override hook | `src/game/rendering_graph_node.c` |
| Intro skip | `src/game/level_update.c` |
| Weapon HUD label | `src/game/hud.c` |
| Model IDs / loading / projectile behavior | `include/model_ids.h`, `levels/scripts.c`, `data/behavior_data.c` |
