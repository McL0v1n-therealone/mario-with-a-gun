#include <PR/ultratypes.h>

#include "sm64.h"
#include "types.h"
#include "trooper.h"
#include "weapons.h"
#include "audio/external.h"
#include "behavior_data.h"
#include "engine/behavior_script.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "interaction.h"
#include "level_update.h"
#include "behavior_actions.h"
#include "model_ids.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "rendering_graph_node.h"
#include "game_init.h"
#include "spawn_sound.h"
#include "libc/math.h"

// trooper-specific object fields (object-specific slots start at 0x1B)
#define oTrooperWalkPhase   OBJECT_FIELD_F32(0x1B)
#define oTrooperBurstLeft   OBJECT_FIELD_S32(0x1C)
#define oTrooperFireTimer   OBJECT_FIELD_S32(0x1D)
#define oTrooperSeeTimer    OBJECT_FIELD_S32(0x1E)  // frames Mario has been visible (reaction delay)
#define oTrooperLostTimer   OBJECT_FIELD_S32(0x1F)  // frames since Mario was last seen
#define oTrooperCanSee      OBJECT_FIELD_S32(0x20)
#define oTrooperStrafe      OBJECT_FIELD_S32(0x21)
#define oTrooperFlash       OBJECT_FIELD_S32(0x22)

#define TROOPER_ACT_PATROL  0
#define TROOPER_ACT_COMBAT  1

#define SIGHT_RANGE      3200.0f
#define REACTION_FRAMES  20
#define BURST_SHOTS      3
#define BURST_GAP        5
#define BULLET_SPEED     75.0f
#define BULLET_LIFETIME  60

static struct ObjectHitbox sTrooperHitbox = {
    /* interactType:      */ INTERACT_BOUNCE_TOP,
    /* downOffset:        */ 0,
    /* damageOrCoinValue: */ 2,
    /* health:            */ 1,
    /* numLootCoins:      */ 2,
    /* radius:            */ 45,
    /* height:            */ 175,
    /* hurtboxRadius:     */ 35,
    /* hurtboxHeight:     */ 165,
};

static struct ObjectHitbox sBulletHitbox = {
    /* interactType:      */ INTERACT_SNUFIT_BULLET,
    /* downOffset:        */ 15,
    /* damageOrCoinValue: */ 1,
    /* health:            */ 0,
    /* numLootCoins:      */ 0,
    /* radius:            */ 30,
    /* height:            */ 30,
    /* hurtboxRadius:     */ 30,
    /* hurtboxHeight:     */ 30,
};

// Walk a straight line from the trooper's eyes to Mario's chest, looking for walls in the way.
static s32 trooper_has_line_of_sight(struct Object *o) {
    f32 sx = o->oPosX, sy = o->oPosY + 150.0f, sz = o->oPosZ;
    f32 dx = gMarioState->pos[0] - sx;
    f32 dy = gMarioState->pos[1] + 80.0f - sy;
    f32 dz = gMarioState->pos[2] - sz;
    f32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
    s32 steps = (s32)(dist / 120.0f);
    s32 i;

    for (i = 1; i < steps; i++) {
        struct WallCollisionData wall;
        f32 t = (f32) i / steps;

        wall.x = sx + dx * t;
        wall.y = sy + dy * t;
        wall.z = sz + dz * t;
        wall.offsetY = 0.0f;
        wall.radius = 25.0f;
        if (find_wall_collisions(&wall) != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

static void trooper_fire(struct Object *o) {
    struct Object *b;
    f32 fx = sins(o->oFaceAngleYaw), fz = coss(o->oFaceAngleYaw);
    f32 dx, dy, dz, horiz;
    s16 yaw, pitch;

    if (gPrevFrameObjectCount >= 200) {
        return;
    }
    b = spawn_object(o, MODEL_BULLET, bhvEnemyBullet);
    // muzzle: in front of the trooper at rifle height, slightly to its right
    b->oPosX = o->oPosX + fx * 150.0f - fz * 14.0f;
    b->oPosY = o->oPosY + 120.0f;
    b->oPosZ = o->oPosZ + fz * 150.0f + fx * 14.0f;

    dx = gMarioState->pos[0] - b->oPosX;
    dy = gMarioState->pos[1] + 70.0f - b->oPosY;
    dz = gMarioState->pos[2] - b->oPosZ;
    horiz = sqrtf(dx * dx + dz * dz);
    yaw = atan2s(dz, dx) + (s16)(random_sign() * (random_u16() % 0x280)); // not perfectly accurate
    pitch = atan2s(horiz, dy);

    b->oMoveAngleYaw = b->oFaceAngleYaw = yaw;
    b->oVelX = sins(yaw) * coss(pitch) * BULLET_SPEED;
    b->oVelZ = coss(yaw) * coss(pitch) * BULLET_SPEED;
    b->oVelY = sins(pitch) * BULLET_SPEED;

    o->oTrooperFlash = 2;
    cur_obj_play_sound_2(SOUND_GENERAL_RACE_GUN_SHOT);
}

void bhv_trooper_init(void) {
    struct Object *o = gCurrentObject;

    obj_set_hitbox(o, &sTrooperHitbox);
    o->oGravity = -4.0f;
    o->oFriction = 0.8f;
    o->oBuoyancy = 1.3f;
    o->oWallHitboxRadius = 50.0f;
    o->oTrooperStrafe = (random_u16() & 1) ? 1 : -1;
    o->oTrooperFireTimer = 30 + random_u16() % 30;
    o->oAction = TROOPER_ACT_PATROL;
}

static void trooper_patrol(struct Object *o) {
    // wander between random points near its post
    if (o->oTimer == 0 || o->oTimer % 90 == 0) {
        o->oAngleVelYaw = o->oMoveAngleYaw + (s16)(random_sign() * (0x2000 + random_u16() % 0x4000));
    }
    if (cur_obj_lateral_dist_to_home() > 700.0f) {
        o->oAngleVelYaw = cur_obj_angle_to_home();
    }
    cur_obj_rotate_yaw_toward(o->oAngleVelYaw, 0x300);
    o->oFaceAngleYaw = o->oMoveAngleYaw;
    o->oForwardVel = 6.0f;

    if (o->oTrooperCanSee) {
        o->oAction = TROOPER_ACT_COMBAT;
        o->oTrooperSeeTimer = 0;
        cur_obj_play_sound_2(SOUND_OBJ_GOOMBA_ALERT);
    }
}

static void trooper_combat(struct Object *o) {
    f32 dist = o->oDistanceToMario;
    s16 toMario = o->oAngleToMario;

    // keep facing Mario
    o->oFaceAngleYaw = approach_s16_symmetric(o->oFaceAngleYaw, toMario, 0x500);

    // movement: close in, back off, or strafe
    if (dist > 1500.0f) {
        o->oMoveAngleYaw = toMario;
        o->oForwardVel = 9.0f;
    } else if (dist < 500.0f) {
        o->oMoveAngleYaw = toMario + 0x8000;
        o->oForwardVel = 7.0f;
    } else {
        if (o->oTimer % 70 == 0) {
            o->oTrooperStrafe = -o->oTrooperStrafe;
        }
        o->oMoveAngleYaw = toMario + o->oTrooperStrafe * 0x4000;
        o->oForwardVel = 5.0f;
    }
    if (cur_obj_lateral_dist_to_home() > 1600.0f) {
        o->oMoveAngleYaw = cur_obj_angle_to_home(); // don't wander off across the map
    }

    if (o->oTrooperCanSee) {
        o->oTrooperLostTimer = 0;
        o->oTrooperSeeTimer++;
    } else {
        o->oTrooperLostTimer++;
        o->oTrooperBurstLeft = 0;
        if (o->oTrooperLostTimer > 120) {
            o->oAction = TROOPER_ACT_PATROL;
            return;
        }
    }

    // shooting: reaction delay, then 3-round bursts with a pause in between
    if (o->oTrooperFireTimer > 0) {
        o->oTrooperFireTimer--;
    }
    if (o->oTrooperCanSee && o->oTrooperSeeTimer > REACTION_FRAMES && dist < SIGHT_RANGE
        && abs_angle_diff(o->oFaceAngleYaw, toMario) < 0x900 && o->oTrooperFireTimer == 0) {
        if (o->oTrooperBurstLeft == 0) {
            o->oTrooperBurstLeft = BURST_SHOTS;
        }
        trooper_fire(o);
        o->oTrooperBurstLeft--;
        o->oTrooperFireTimer = o->oTrooperBurstLeft > 0 ? BURST_GAP : 40 + random_u16() % 40;
    }
}

void bhv_trooper_loop(void) {
    struct Object *o = gCurrentObject;

    // stomped by Mario
    if (o->oInteractStatus & INT_STATUS_INTERACTED) {
        if (o->oInteractStatus & INT_STATUS_WAS_ATTACKED) {
            o->oInteractStatus = 0;
            weapon_kill_object(o);
            return;
        }
        o->oInteractStatus = 0;
    }

    // line-of-sight is expensive: refresh every 6 frames, staggered per trooper
    if (((gGlobalTimer + (s32) o) % 6) == 0) {
        o->oTrooperCanSee = o->oDistanceToMario < SIGHT_RANGE && gMarioState->action != ACT_UNINITIALIZED
                            && trooper_has_line_of_sight(o);
    }

    cur_obj_update_floor_and_walls();
    switch (o->oAction) {
        case TROOPER_ACT_PATROL: trooper_patrol(o); break;
        case TROOPER_ACT_COMBAT: trooper_combat(o); break;
    }
    cur_obj_move_standard(-78);

    o->oTrooperWalkPhase += o->oForwardVel * 0.11f;
    if (o->oTrooperFlash > 0) {
        o->oTrooperFlash--;
    }
}

void bhv_enemy_bullet_loop(void) {
    struct Object *o = gCurrentObject;
    s32 i;

    if (o->oTimer == 0) {
        obj_set_hitbox(o, &sBulletHitbox);
    }
    if ((o->oInteractStatus & INT_STATUS_INTERACTED) || o->oTimer > BULLET_LIFETIME) {
        obj_mark_for_deletion(o); // hit Mario, or flew out of range
        return;
    }
    for (i = 0; i < 2; i++) {
        struct WallCollisionData wall;
        struct Surface *floor;
        f32 floorY;

        o->oPosX += o->oVelX / 2;
        o->oPosY += o->oVelY / 2;
        o->oPosZ += o->oVelZ / 2;

        wall.x = o->oPosX; wall.y = o->oPosY; wall.z = o->oPosZ;
        wall.offsetY = 0.0f; wall.radius = 15.0f;
        floorY = find_floor(o->oPosX, o->oPosY + 20.0f, o->oPosZ, &floor);
        if (find_wall_collisions(&wall) != 0 || floor == NULL || o->oPosY < floorY) {
            spawn_mist_particles_variable(2, 0, 6.0f);
            obj_mark_for_deletion(o);
            return;
        }
    }
}

// ------------------------------------------------------------------ geo: procedural animation

static struct Object *trooper_current(void) {
    return (struct Object *) gCurGraphNodeObject;
}

// body bob while walking + kick back when firing
Gfx *geo_trooper_body(s32 callContext, struct GraphNode *node, UNUSED void *context) {
    if (callContext == GEO_CONTEXT_RENDER && gCurGraphNodeObject != NULL) {
        struct Object *o = trooper_current();
        struct GraphNodeTranslationRotation *tr = (struct GraphNodeTranslationRotation *) node->next;
        f32 speed = o->oForwardVel > 10.0f ? 1.0f : o->oForwardVel / 10.0f;

        tr->translation[0] = 0;
        tr->translation[1] = (s16)(sins((s16)(o->oTrooperWalkPhase * 2.0f * 0x2000)) * 4.0f * speed);
        tr->translation[2] = 0;
        tr->rotation[0] = (s16)(-0x200 * o->oTrooperFlash); // lean back on recoil
        tr->rotation[1] = 0;
        tr->rotation[2] = 0;
    }
    return NULL;
}

// legs swing in opposite phase
Gfx *geo_trooper_leg(s32 callContext, struct GraphNode *node, UNUSED void *context) {
    if (callContext == GEO_CONTEXT_RENDER && gCurGraphNodeObject != NULL) {
        struct Object *o = trooper_current();
        struct GraphNodeTranslationRotation *tr = (struct GraphNodeTranslationRotation *) node->next;
        s32 side = ((struct GraphNodeGenerated *) node)->parameter == 1 ? 1 : -1;
        f32 speed = o->oForwardVel > 10.0f ? 1.0f : o->oForwardVel / 10.0f;

        tr->translation[0] = tr->translation[1] = tr->translation[2] = 0;
        tr->rotation[0] = (s16)(side * sins((s16)(o->oTrooperWalkPhase * 0x2000)) * 0x1C00 * speed);
        tr->rotation[1] = tr->rotation[2] = 0;
    }
    return NULL;
}

Gfx *geo_trooper_flash(s32 callContext, struct GraphNode *node, UNUSED void *context) {
    if (callContext == GEO_CONTEXT_RENDER && gCurGraphNodeObject != NULL) {
        struct Object *o = trooper_current();
        ((struct GraphNodeScale *) node->next)->scale = o->oTrooperFlash > 0 ? 0.35f : 0.0f;
    }
    return NULL;
}
