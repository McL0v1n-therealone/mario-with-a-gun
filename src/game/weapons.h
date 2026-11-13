#ifndef WEAPONS_H
#define WEAPONS_H

#include "types.h"

// Mario's weapons. Fire with L, switch with D-pad left/right.
// Order must match the weapon list in actors/mario/gen_weapons.py.
enum WeaponId {
    WEAPON_LIGHT_GUN,
    WEAPON_PISTOL,
    WEAPON_MAGNUM,
    WEAPON_SHOTGUN,
    WEAPON_SMG,
    WEAPON_RIFLE,
    WEAPON_COUNT
};

void weapon_update(struct MarioState *m);
void weapon_render_hud(void);
void bhv_weapon_shot_loop(void);

// geo layout callbacks (actors/mario/weapons_geo.inc.c)
Gfx *geo_switch_weapon(s32 callContext, struct GraphNode *node, UNUSED void *context);
Gfx *geo_weapon_recoil(s32 callContext, struct GraphNode *node, UNUSED void *context);
Gfx *geo_weapon_draw_scale(s32 callContext, struct GraphNode *node, UNUSED void *context);
Gfx *geo_weapon_moving_part(s32 callContext, struct GraphNode *node, UNUSED void *context);
Gfx *geo_weapon_muzzle_flash(s32 callContext, struct GraphNode *node, UNUSED void *context);
Gfx *geo_weapon_arm_override(s32 callContext, struct GraphNode *node, UNUSED void *context);

// called by the renderer for every animated bone; adjusts Mario's right arm into the aiming pose
void weapon_apply_bone_override(Vec3s rotation);

#endif // WEAPONS_H
