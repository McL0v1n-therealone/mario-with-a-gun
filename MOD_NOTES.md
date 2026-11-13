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
X / N64 L = fire (hold for automatic weapons), A / D = D-pad left/right = switch weapon, W / S = D-pad up/down = jump 4 weapons.

## Features
- 14 weapons: light gun, pistol, magnum, shotgun, SMG, rifle, minigun (spin-up), rocket launcher, grenade launcher,
  flamethrower, sniper, railgun, double barrel, plasma rifle; explosives do splash damage with screen shake
- Kill counter, multi-kill announcer (DOUBLE KILL ... GODLIKE), kills heal a quarter wedge
- Recoil, muzzle flash, slide/pump/cylinder animations, weapon draw animation, HUD weapon name
- Aiming pose: right arm raises the gun when firing (renderer bone override)
- Guns kill everything hostile: ordinary enemies explode into gibs + coins; bosses take several hits and
  die through their own defeat logic (stars, music, progress intact); Bowser counts as a bomb hit every 15 shots
- Gore: blood spray on every hit, flesh/bone gibs and spreading blood pools on kills (`src/game/gore.c`,
  models from `actors/mario/gen_gore.py`); gore is capped so it never exhausts the object pool
- Objects don't bleed: crates/boxes break normally, bombs explode, stone/metal/fire enemies crumble into debris
- Toad can be shot (he respawns when the area reloads)
- Protected (never shot): other talking NPCs, Bob-omb Buddies, Yoshi, Hoot, Koopa the Quick, Mips,
  baby penguin, Tuxie's mom, Ukiki, Unagi, Bowser's tail
- Opening cutscene skipped on new save files
- 8MB Expansion Pak memory layout (USE_EXT_RAM) - vanilla 4MB pool ran out in Castle Grounds

## Where things live
| What | File |
|---|---|
| Weapon stats, firing, projectiles, animations, aiming | `src/game/weapons.c` |
| Weapon models (generator + generated output) | `actors/mario/gen_weapons.py`, `weapons*.inc.c` |
| Gun + aim markers in Mario's skeleton | `actors/mario/geo.inc.c` |
| Bone override hook | `src/game/rendering_graph_node.c` |
| Damage rules, bosses, kills | `src/game/weapons.c` (`weapon_damage`, `weapon_kill`) |
| Blood & gibs | `src/game/gore.c`, `actors/mario/gen_gore.py` |
| Intro skip | `src/game/level_update.c` |
| Weapon HUD label | `src/game/hud.c` |
| Model IDs / loading / projectile behavior | `include/model_ids.h`, `levels/scripts.c`, `data/behavior_data.c` |
