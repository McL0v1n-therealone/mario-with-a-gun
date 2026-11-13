#include <PR/ultratypes.h>

#include "sm64.h"
#include "types.h"
#include "weapons.h"
#include "audio/external.h"
#include "behavior_actions.h"
#include "behavior_data.h"
#include "engine/behavior_script.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "interaction.h"
#include "model_ids.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "print.h"
#include "gore.h"
#include "memory.h"
#include "object_constants.h"
#include "obj_behaviors.h"
#include "save_file.h"

#define WEAPON_FIRE_BUTTON  L_TRIG
#define WEAPON_NEXT_BUTTON  R_JPAD
#define WEAPON_PREV_BUTTON  L_JPAD

#define SHOT_SUBSTEPS 4      // collision checks per frame so fast shots don't skip enemies
#define SHOT_RADIUS   25.0f

// Interactions that are never shot: pickups, doors, warps, talking NPCs and other level machinery.
#define SHOT_IGNORED (INTERACT_TEXT | INTERACT_HOOT | INTERACT_DOOR | INTERACT_WARP | INTERACT_WARP_DOOR \
                      | INTERACT_COIN | INTERACT_CAP | INTERACT_POLE | INTERACT_STAR_OR_KEY | INTERACT_CANNON_BASE \
                      | INTERACT_WATER_RING | INTERACT_STRONG_WIND | INTERACT_TORNADO | INTERACT_WHIRLPOOL \
                      | INTERACT_BBH_ENTRANCE | INTERACT_IGLOO_BARRIER)

#define BOSS_HITS        25  // King Bob-omb, Whomp King, Wiggler
#define BIG_BULLY_HITS   15
#define EYEROK_HAND_HITS 12
#define BOWSER_HITS      15  // per "bomb hit" phase

enum WeaponPartAnim { PART_NONE, PART_SLIDE, PART_SPIN, PART_PUMP };

struct WeaponInfo {
    const char *name;     // HUD label (US HUD font has no J, Q, V, X, Z)
    u8 automatic;         // hold to fire
    u8 pellets;           // projectiles per shot
    s16 spread;           // shotgun: angle between pellets, others: random +/- spread
    s16 cooldown;         // frames between shots
    f32 speed;            // projectile units per frame
    s16 lifetime;         // projectile frames
    u8 pierce;            // enemies a projectile can pass through
    u8 tracer;            // bullet tracer model instead of light gun bolt
    s32 sound;
    s16 recoilFrames;
    f32 recoilKick;       // how far the gun moves back
    s16 recoilClimb;      // muzzle climb angle
    u8 partAnim;
    s16 partDelay;        // frames after the shot before the moving part starts
    s16 partFrames;
    f32 partDist;         // slide/pump travel
    f32 flashScale;
};

static const struct WeaponInfo sWeapons[WEAPON_COUNT] = {
    //  name         auto pel spread cd  speed  life pierce tracer sound                          rFr kick climb  part        delay frames dist  flash
    { "LIGHT GUN",   0,   1,  0,     8,  100.0f, 30, 0,     0,     SOUND_OBJ_SNUFIT_SHOOT,        5,  10.0f, 0x200,  PART_NONE,  0,    0,     0.0f, 0.0f },
    { "PISTOL",      0,   1,  0x80,  7,  150.0f, 24, 0,     1,     SOUND_GENERAL_RACE_GUN_SHOT,   6,  14.0f, 0x700,  PART_SLIDE, 0,    5,    34.0f, 1.0f },
    { "MAGNUM",      0,   1,  0,     18, 170.0f, 30, 2,     1,     SOUND_OBJ_CANNON1,             12, 26.0f, 0x1400, PART_SPIN,  4,    6,     0.0f, 1.4f },
    { "SHOTGUN",     0,   6,  0x380, 28, 120.0f, 10, 0,     1,     SOUND_OBJ_CANNON2,             14, 34.0f, 0x1000, PART_PUMP,  7,    14,   58.0f, 1.8f },
    { "SMG",         1,   1,  0x300, 3,  140.0f, 18, 0,     1,     SOUND_GENERAL_RACE_GUN_SHOT,   3,  9.0f,  0x300,  PART_SLIDE, 0,    3,    24.0f, 0.8f },
    { "RIFLE",       1,   1,  0xA0,  5,  180.0f, 36, 1,     1,     SOUND_GENERAL_RACE_GUN_SHOT,   4,  12.0f, 0x400,  PART_SLIDE, 0,    4,    14.0f, 1.0f },
};

static s32 sWeapon = WEAPON_PISTOL;
static s16 sCooldown = 0;
static s16 sRecoilTimer = 0;
static s16 sFlashTimer = 0;
static s16 sPartTimer = 0;     // counts up after a shot, -1 = idle
static s16 sDrawTimer = 0;     // weapon-switch animation
static s16 sSpinBase = 0;      // revolver cylinder angle
static s16 sHudTimer = 0;      // weapon name highlight after switching
static s16 sAimTimer = 0;      // frames left in the aiming pose after the last shot
static f32 sAimWeight = 0.0f;  // 0 = normal animation, 1 = full aiming pose
static s32 sPendingBone = 0;   // arm bone the renderer is about to draw (set by geo_weapon_arm_override)

#define AIM_HOLD_FRAMES 40

// Aiming is only shown for normal on-foot / airborne movement (not swimming, hanging, climbing...)
static s32 weapon_can_aim(struct MarioState *m) {
    u32 group = m->action & ACT_GROUP_MASK;

    if (group != ACT_GROUP_STATIONARY && group != ACT_GROUP_MOVING && group != ACT_GROUP_AIRBORNE) {
        return FALSE;
    }
    if (m->action & (ACT_FLAG_SWIMMING | ACT_FLAG_HANGING | ACT_FLAG_ON_POLE | ACT_FLAG_METAL_WATER)) {
        return FALSE;
    }
    return m->heldObj == NULL;
}

static s32 weapon_can_fire(struct MarioState *m) {
    u32 group = m->action & ACT_GROUP_MASK;

    if (m->marioObj == NULL || m->heldObj != NULL || m->riddenObj != NULL) {
        return FALSE;
    }
    if (group == ACT_GROUP_CUTSCENE || group == ACT_GROUP_AUTOMATIC || group == ACT_GROUP_OBJECT) {
        return FALSE;
    }
    if (m->action & ACT_FLAG_INTANGIBLE) {
        return FALSE;
    }
    return TRUE;
}

static void weapon_spawn_shot(struct MarioState *m, const struct WeaponInfo *w, s16 yaw) {
    struct Object *shot = spawn_object(m->marioObj, w->tracer ? MODEL_BULLET : MODEL_ZAPPER_SHOT, bhvWeaponShot);

    f32 fwd = 80.0f, side = 0.0f;

    if (sAimWeight > 0.5f) {
        fwd = 105.0f;  // arm + gun length
        side = 20.0f;  // right shoulder
    }
    shot->oPosX = m->pos[0] + sins(m->faceAngle[1]) * fwd - coss(m->faceAngle[1]) * side;
    shot->oPosY = m->pos[1] + 80.0f;
    shot->oPosZ = m->pos[2] + coss(m->faceAngle[1]) * fwd + sins(m->faceAngle[1]) * side;
    shot->oMoveAngleYaw = yaw;
    shot->oFaceAngleYaw = yaw;
    shot->oMoveAnglePitch = 0;
    shot->oFaceAnglePitch = 0;
    shot->oBhvParams2ndByte = sWeapon;
    shot->oHealth = w->pierce;
}

static void weapon_fire(struct MarioState *m) {
    const struct WeaponInfo *w = &sWeapons[sWeapon];
    s16 yaw = m->faceAngle[1];
    s32 i;

    if (w->pellets > 1) {
        for (i = 0; i < w->pellets; i++) {
            s16 offset = (s16)((i * 2 - (w->pellets - 1)) * w->spread / 2);
            weapon_spawn_shot(m, w, yaw + offset + (s16)(random_sign() * (random_u16() % 0x100)));
        }
    } else {
        s16 jitter = w->spread ? (s16)(random_sign() * (random_u16() % w->spread)) : 0;
        weapon_spawn_shot(m, w, yaw + jitter);
    }

    sCooldown = w->cooldown;
    sRecoilTimer = w->recoilFrames;
    sAimTimer = AIM_HOLD_FRAMES;
    sFlashTimer = 2;
    if (w->partAnim != PART_NONE) {
        if (w->partAnim == PART_SPIN && sPartTimer >= 0) {
            sSpinBase += 0x2AAB; // finish the previous turn
        }
        sPartTimer = 0;
    }
    play_sound(w->sound, m->marioObj->header.gfx.cameraToObject);
}

static void weapon_switch(struct MarioState *m, s32 dir) {
    sWeapon = (sWeapon + dir + WEAPON_COUNT) % WEAPON_COUNT;
    sDrawTimer = 8;
    sHudTimer = 45;
    sRecoilTimer = 0;
    sFlashTimer = 0;
    sPartTimer = -1;
    if (sCooldown < 6) {
        sCooldown = 6; // can't fire mid-draw
    }
    play_sound(SOUND_MENU_CLICK_CHANGE_VIEW, m->marioObj->header.gfx.cameraToObject);
}

void weapon_update(struct MarioState *m) {
    const struct WeaponInfo *w;
    u16 pressed = m->controller->buttonPressed;
    u16 held = m->controller->buttonDown;

    if (sCooldown > 0) sCooldown--;
    if (sRecoilTimer > 0) sRecoilTimer--;
    if (sFlashTimer > 0) sFlashTimer--;
    if (sDrawTimer > 0) sDrawTimer--;
    if (sHudTimer > 0) sHudTimer--;

    w = &sWeapons[sWeapon];
    if (sPartTimer >= 0) {
        sPartTimer++;
        if (w->partAnim == PART_PUMP && sPartTimer == w->partDelay) {
            play_sound(SOUND_GENERAL2_ROTATING_BLOCK_CLICK, m->marioObj->header.gfx.cameraToObject);
        }
        if (sPartTimer >= w->partDelay + w->partFrames) {
            if (w->partAnim == PART_SPIN) {
                sSpinBase += 0x2AAB;
            }
            sPartTimer = -1;
        }
    }

    if (m->marioObj == NULL) {
        return;
    }

    // aiming pose: snap up when firing (handled in weapon_fire), ease back down afterwards
    if (sAimTimer > 0) sAimTimer--;
    if (sAimTimer > 0 && weapon_can_aim(m)) {
        sAimWeight += 0.34f;
        if (sAimWeight > 1.0f) sAimWeight = 1.0f;
    } else {
        sAimWeight -= 0.12f;
        if (sAimWeight < 0.0f) sAimWeight = 0.0f;
    }

    if (pressed & WEAPON_NEXT_BUTTON) {
        weapon_switch(m, 1);
    } else if (pressed & WEAPON_PREV_BUTTON) {
        weapon_switch(m, -1);
    }

    w = &sWeapons[sWeapon];
    if (sCooldown == 0 && ((w->automatic ? held : pressed) & WEAPON_FIRE_BUTTON) && weapon_can_fire(m)) {
        if (weapon_can_aim(m)) {
            sAimWeight = 1.0f; // snap the gun up so the shot leaves the muzzle
        }
        weapon_fire(m);
    }
}

void weapon_render_hud(void) {
    // blink briefly after switching
    if (sHudTimer == 0 || (sHudTimer & 4)) {
        print_text(22, 15, sWeapons[sWeapon].name);
    }
}

// ------------------------------------------------------------------ projectile

// Friendly / progression objects that must never be destroyed.
static s32 weapon_is_protected(struct Object *obj) {
    static const BehaviorScript *const protectedBhvs[] = {
        bhvMips, bhvBowserTailAnchor, bhvSmallPenguin, bhvUkiki, bhvUnagi, bhvBobombBuddy,
        bhvBobombBuddyOpensCannon, bhvToadMessage, bhvYoshi, bhvTuxiesMother, bhvHoot, bhvChainChompGate,
    };
    s32 i;

    for (i = 0; i < (s32) ARRAY_COUNT(protectedBhvs); i++) {
        if (obj_has_behavior(obj, protectedBhvs[i])) {
            return TRUE;
        }
    }
    // Koopa the Quick (race NPC)
    if (obj_has_behavior(obj, bhvKoopa)
        && (obj->oBhvParams2ndByte == KOOPA_BP_KOOPA_THE_QUICK_BOB || obj->oBhvParams2ndByte == KOOPA_BP_KOOPA_THE_QUICK_THI)) {
        return TRUE;
    }
    return FALSE;
}

// Solid enemies without a hitbox (they hurt by crushing / being touched as a platform).
static s32 weapon_is_solid_enemy(struct Object *obj) {
    return obj_has_behavior(obj, bhvThwomp) || obj_has_behavior(obj, bhvThwomp2) || obj_has_behavior(obj, bhvSmallWhomp)
        || obj_has_behavior(obj, bhvWhompKingBoss) || obj_has_behavior(obj, bhvGrindel) || obj_has_behavior(obj, bhvSpindel);
}

static s32 weapon_is_boo(struct Object *obj) {
    return obj_has_behavior(obj, bhvBoo) || obj_has_behavior(obj, bhvGhostHuntBoo) || obj_has_behavior(obj, bhvGhostHuntBigBoo)
        || obj_has_behavior(obj, bhvBooInCastle) || obj_has_behavior(obj, bhvMerryGoRoundBoo)
        || obj_has_behavior(obj, bhvMerryGoRoundBigBoo) || obj_has_behavior(obj, bhvBalconyBigBoo) || obj_has_behavior(obj, bhvBooWithCage);
}

static void weapon_target_bounds(struct Object *obj, f32 *radius, f32 *bottom, f32 *height) {
    if (obj->hitboxRadius > 0.0f) {
        *radius = obj->hitboxRadius;
        *bottom = obj->oPosY - obj->hitboxDownOffset;
        *height = obj->hitboxHeight;
    } else {
        *radius = 150.0f; // solid enemies (Thwomp, Whomp, ...)
        *bottom = obj->oPosY;
        *height = 250.0f;
    }
}

static struct Object *weapon_find_target(struct Object *shot) {
    static const s32 lists[] = { OBJ_LIST_GENACTOR, OBJ_LIST_PUSHABLE, OBJ_LIST_DESTRUCTIVE, OBJ_LIST_SURFACE };
    s32 i;

    for (i = 0; i < (s32) ARRAY_COUNT(lists); i++) {
        struct ObjectNode *head = &gObjectLists[lists[i]];
        struct Object *obj = (struct Object *) head->next;

        while (obj != (struct Object *) head) {
            u32 type = obj->oInteractType;

            if ((obj->activeFlags & ACTIVE_FLAG_ACTIVE) && (s32) obj != shot->oAction
                && !(type & SHOT_IGNORED) && !(obj->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE)
                && (type != 0 || weapon_is_solid_enemy(obj)) && !weapon_is_protected(obj)) {
                f32 radius, bottom, height;
                f32 dx = obj->oPosX - shot->oPosX;
                f32 dz = obj->oPosZ - shot->oPosZ;
                f32 reach;

                weapon_target_bounds(obj, &radius, &bottom, &height);
                reach = radius + SHOT_RADIUS;
                if (dx * dx + dz * dz < reach * reach
                    && shot->oPosY > bottom - SHOT_RADIUS && shot->oPosY < bottom + height + SHOT_RADIUS) {
                    return obj;
                }
            }
            obj = (struct Object *) obj->header.next;
        }
    }
    return NULL;
}

// ------------------------------------------------------------------ damage

struct BossHits {
    struct Object *obj;
    const BehaviorScript *bhv;
    s16 hits;
};
static struct BossHits sBossHits[8];

static s32 weapon_boss_hit(struct Object *obj) {
    s32 i, slot = 0;

    for (i = 0; i < (s32) ARRAY_COUNT(sBossHits); i++) {
        if (sBossHits[i].obj == obj && sBossHits[i].bhv == obj->behavior && (obj->activeFlags & ACTIVE_FLAG_ACTIVE)) {
            return ++sBossHits[i].hits;
        }
        if (sBossHits[i].obj == NULL || !(sBossHits[i].obj->activeFlags & ACTIVE_FLAG_ACTIVE)) {
            slot = i;
        }
    }
    sBossHits[slot].obj = obj;
    sBossHits[slot].bhv = obj->behavior;
    sBossHits[slot].hits = 1;
    return 1;
}

static void weapon_boss_reset(struct Object *obj) {
    s32 i;
    for (i = 0; i < (s32) ARRAY_COUNT(sBossHits); i++) {
        if (sBossHits[i].obj == obj) {
            sBossHits[i].hits = 0;
        }
    }
}

// spawn_default_star() takes the star ID from the current object, so run it "as" the enemy
static void weapon_spawn_star_from(struct Object *obj, f32 x, f32 y, f32 z) {
    struct Object *saved = gCurrentObject;
    gCurrentObject = obj;
    spawn_default_star(x, y, z);
    gCurrentObject = saved;
}

static f32 weapon_target_center_y(struct Object *obj) {
    f32 radius, bottom, height;
    weapon_target_bounds(obj, &radius, &bottom, &height);
    return bottom + height * 0.5f;
}

static f32 weapon_target_size(struct Object *obj) {
    f32 radius, bottom, height, size;
    weapon_target_bounds(obj, &radius, &bottom, &height);
    size = radius / 60.0f;
    return size < 0.6f ? 0.6f : (size > 3.0f ? 3.0f : size);
}

// Blow an ordinary enemy apart: gore, coins, any item it carried, and its child objects.
static void weapon_kill(struct Object *t, struct Object *shot) {
    static const s32 lists[] = { OBJ_LIST_GENACTOR, OBJ_LIST_PUSHABLE, OBJ_LIST_DESTRUCTIVE, OBJ_LIST_DEFAULT,
                                 OBJ_LIST_UNIMPORTANT, OBJ_LIST_SURFACE, OBJ_LIST_POLELIKE };
    s32 i;

    gore_explode(shot, t->oPosX, weapon_target_center_y(t), t->oPosZ, weapon_target_size(t));
    play_sound(SOUND_OBJ_DEFAULT_DEATH, shot->header.gfx.cameraToObject);
    if (t->oNumLootCoins > 0) {
        obj_spawn_loot_yellow_coins(t, t->oNumLootCoins, 20.0f);
    }

    // items some enemies carry
    if (obj_has_behavior(t, bhvMrI) && t->oBhvParams2ndByte != 0) {
        weapon_spawn_star_from(t, 1370.0f, 2000.0f, -320.0f); // Big Mr. I
    } else if (obj_has_behavior(t, bhvKlepto)) {
        if (t->oAnimState == KLEPTO_ANIM_STATE_HOLDING_CAP) {
            save_file_clear_flags(SAVE_FLAG_CAP_ON_KLEPTO);
            spawn_object(t, MODEL_MARIOS_CAP, bhvNormalCap);
        } else if (t->oAnimState == KLEPTO_ANIM_STATE_HOLDING_STAR) {
            weapon_spawn_star_from(t, -5550.0f, 300.0f, -930.0f);
        }
    } else if (obj_has_behavior(t, bhvChainChomp)) {
        // the chomp normally breaks its gate when freed; do it now so that star stays reachable
        struct ObjectNode *head = &gObjectLists[OBJ_LIST_SURFACE];
        struct Object *g = (struct Object *) head->next;
        while (g != (struct Object *) head) {
            if (obj_has_behavior(g, bhvChainChompGate)) {
                spawn_mist_particles_variable(0, 0, 100.0f);
                obj_mark_for_deletion(g);
            }
            g = (struct Object *) g->header.next;
        }
    }

    // remove parts that belong to it (chain links, body segments, ...)
    for (i = 0; i < (s32) ARRAY_COUNT(lists); i++) {
        struct ObjectNode *head = &gObjectLists[lists[i]];
        struct Object *c = (struct Object *) head->next;
        while (c != (struct Object *) head) {
            if (c != t && c->parentObj == t && (c->activeFlags & ACTIVE_FLAG_ACTIVE)
                && !(c->oInteractType & (INTERACT_STAR_OR_KEY | INTERACT_COIN | INTERACT_CAP))
                && c->behavior != segmented_to_virtual(bhvGoreGib) && c->behavior != segmented_to_virtual(bhvGoreBloodDrop)
                && c->behavior != segmented_to_virtual(bhvGoreBloodPool)) {
                obj_mark_for_deletion(c);
            }
            c = (struct Object *) c->header.next;
        }
    }
    obj_mark_for_deletion(t);
}

// Apply one bullet hit. Bosses and enemies with special defeat logic use their own death
// so stars, music and level progress still work; everything else is blown apart.
static void weapon_damage(struct Object *t, struct Object *shot) {
    f32 cy = weapon_target_center_y(t);
    s32 hits;

    gore_spray(shot, shot->oPosX, shot->oPosY, shot->oPosZ, shot->oMoveAngleYaw, 6, 9.0f);

    if (obj_has_behavior(t, bhvBowser)) {
        if (t->oAction != BOWSER_ACT_HIT_MINE && t->oAction != BOWSER_ACT_DEAD && t->oAction != BOWSER_ACT_THROWN
            && t->oAction != BOWSER_ACT_JUMP_ONTO_STAGE && t->oAction != BOWSER_ACT_INTRO_WALK
            && weapon_boss_hit(t) >= BOWSER_HITS) {
            weapon_boss_reset(t);
            gore_explode(shot, t->oPosX, cy, t->oPosZ, 1.5f);
            if (--t->oHealth <= 0) {
                t->oAction = BOWSER_ACT_DEAD;
            } else {
                t->oAction = BOWSER_ACT_HIT_MINE;
            }
            t->oTimer = 0;
            t->oSubAction = 0;
        }
    } else if (obj_has_behavior(t, bhvKingBobomb)) {
        if (t->oAction != 7 && t->oAction != 8 && weapon_boss_hit(t) >= BOSS_HITS) {
            gore_explode(shot, t->oPosX, cy, t->oPosZ, 2.5f);
            t->oHealth = 0;
            t->oAction = 7; // defeated: dialog, then his star
        }
    } else if (obj_has_behavior(t, bhvWhompKingBoss) || obj_has_behavior(t, bhvSmallWhomp)) {
        s32 needed = obj_has_behavior(t, bhvWhompKingBoss) ? BOSS_HITS : 1;
        if (t->oAction != 8 && weapon_boss_hit(t) >= needed) {
            gore_explode(shot, t->oPosX, cy, t->oPosZ, 2.5f);
            t->oHealth = 0;
            t->oAction = 8; // whomp_die: coins, or the star for the king
        }
    } else if (obj_has_behavior(t, bhvSmallBully) || obj_has_behavior(t, bhvSmallChillBully)
               || obj_has_behavior(t, bhvBigBully) || obj_has_behavior(t, bhvBigBullyWithMinions)
               || obj_has_behavior(t, bhvBigChillBully)) {
        s32 needed = t->oBhvParams2ndByte == BULLY_BP_SIZE_SMALL ? 1 : BIG_BULLY_HITS;
        if (t->oAction != BULLY_ACT_LAVA_DEATH && t->oAction != BULLY_ACT_DEATH_PLANE_DEATH
            && weapon_boss_hit(t) >= needed) {
            gore_explode(shot, t->oPosX, cy, t->oPosZ, needed > 1 ? 2.5f : 1.2f);
            t->oForwardVel = 0.0f;
            t->oAction = BULLY_ACT_LAVA_DEATH; // sinks; counts minions / spawns the big bully's star
        }
    } else if (obj_has_behavior(t, bhvWigglerHead)) {
        if (t->oAction != WIGGLER_ACT_SHRINK && t->oAction != WIGGLER_ACT_FALL_THROUGH_FLOOR
            && weapon_boss_hit(t) >= BOSS_HITS) {
            gore_explode(shot, t->oPosX, cy, t->oPosZ, 2.0f);
            t->oHealth = 1;
            t->oAction = WIGGLER_ACT_SHRINK; // shrinks, drops his star
        }
    } else if (obj_has_behavior(t, bhvEyerokHand)) {
        if (t->oAction != EYEROK_HAND_ACT_DIE && t->oHealth > 0 && weapon_boss_hit(t) >= EYEROK_HAND_HITS) {
            gore_explode(shot, t->oPosX, cy, t->oPosZ, 2.0f);
            t->oHealth = 0;
            t->parentObj->oEyerokBossNumHands--;
            t->oAction = EYEROK_HAND_ACT_DIE;
            t->oVelY = 50.0f;
        }
    } else if (weapon_is_boo(t)) {
        // Boos count kills for their ghost hunts, so let them take a normal hit
        t->oInteractStatus = ATTACK_PUNCH | INT_STATUS_INTERACTED | INT_STATUS_WAS_ATTACKED;
    } else {
        weapon_kill(t, shot);
    }
}

static void weapon_shot_burst(void) {
    spawn_mist_particles_variable(3, 0, 8.0f);
    obj_mark_for_deletion(gCurrentObject);
}

void bhv_weapon_shot_loop(void) {
    struct Object *o = gCurrentObject;
    const struct WeaponInfo *w = &sWeapons[o->oBhvParams2ndByte % WEAPON_COUNT];
    f32 step = w->speed / SHOT_SUBSTEPS;
    s32 i;

    for (i = 0; i < SHOT_SUBSTEPS; i++) {
        struct WallCollisionData wall;
        struct Surface *floor;
        struct Object *target;
        f32 floorY;

        o->oPosX += sins(o->oMoveAngleYaw) * step;
        o->oPosZ += coss(o->oMoveAngleYaw) * step;

        // stop at walls and the ground
        wall.x = o->oPosX;
        wall.y = o->oPosY;
        wall.z = o->oPosZ;
        wall.offsetY = 0.0f;
        wall.radius = 20.0f;
        floorY = find_floor(o->oPosX, o->oPosY, o->oPosZ, &floor);
        if (find_wall_collisions(&wall) != 0 || floor == NULL || o->oPosY < floorY) {
            weapon_shot_burst();
            return;
        }

        target = weapon_find_target(o);
        if (target != NULL) {
            play_sound(SOUND_ACTION_HIT, o->header.gfx.cameraToObject);
            o->oAction = (s32) target; // don't hit the same enemy twice while piercing
            weapon_damage(target, o);
            if (o->oHealth <= 0) {
                weapon_shot_burst();
                return;
            }
            o->oHealth--;
        }
    }

    if (o->oTimer >= w->lifetime) {
        obj_mark_for_deletion(o);
    }
}

// ------------------------------------------------------------------ geo callbacks

Gfx *geo_switch_weapon(s32 callContext, struct GraphNode *node, UNUSED void *context) {
    if (callContext == GEO_CONTEXT_RENDER) {
        ((struct GraphNodeSwitchCase *) node)->selectedCase = sWeapon;
    }
    return NULL;
}

// Kick back + muzzle climb after a shot, and the swing-in when switching weapons.
// In the fist frame "up" is -Y, so muzzle climb is a negative rotation about Z.
Gfx *geo_weapon_recoil(s32 callContext, struct GraphNode *node, UNUSED void *context) {
    if (callContext == GEO_CONTEXT_RENDER) {
        struct GraphNodeTranslationRotation *tr = (struct GraphNodeTranslationRotation *) node->next;
        const struct WeaponInfo *w = &sWeapons[sWeapon];
        f32 f = w->recoilFrames ? (f32) sRecoilTimer / w->recoilFrames : 0.0f;
        f32 d = sDrawTimer / 8.0f;

        tr->translation[0] = (s16)(-w->recoilKick * f);
        tr->translation[1] = 0;
        tr->translation[2] = 0;
        tr->rotation[0] = 0;
        tr->rotation[1] = 0;
        tr->rotation[2] = (s16)(-w->recoilClimb * f + 0x3000 * d * d);
    }
    return NULL;
}

Gfx *geo_weapon_draw_scale(s32 callContext, struct GraphNode *node, UNUSED void *context) {
    if (callContext == GEO_CONTEXT_RENDER) {
        f32 d = sDrawTimer / 8.0f;
        ((struct GraphNodeScale *) node->next)->scale = 1.0f - 0.8f * d * d;
    }
    return NULL;
}

// Slide / bolt / pump travel and the revolver cylinder turn.
Gfx *geo_weapon_moving_part(s32 callContext, struct GraphNode *node, UNUSED void *context) {
    if (callContext == GEO_CONTEXT_RENDER) {
        s32 id = ((struct GraphNodeGenerated *) node)->parameter;
        struct GraphNodeTranslationRotation *tr = (struct GraphNodeTranslationRotation *) node->next;
        const struct WeaponInfo *w = &sWeapons[id];
        f32 p = 0.0f; // 0..1 progress through the part animation

        if (id == sWeapon && sPartTimer >= w->partDelay && w->partFrames > 0) {
            p = (f32)(sPartTimer - w->partDelay) / w->partFrames;
            if (p > 1.0f) p = 1.0f;
        }

        tr->translation[0] = tr->translation[1] = tr->translation[2] = 0;
        tr->rotation[0] = tr->rotation[1] = tr->rotation[2] = 0;

        switch (w->partAnim) {
            case PART_SLIDE:
            case PART_PUMP:
                // back then forward
                tr->translation[0] = (s16)(-w->partDist * (p < 0.5f ? p * 2.0f : (1.0f - p) * 2.0f));
                break;
            case PART_SPIN:
                tr->rotation[0] = (s16)(sSpinBase + (s16)(0x2AAB * p));
                break;
        }
    }
    return NULL;
}

Gfx *geo_weapon_muzzle_flash(s32 callContext, struct GraphNode *node, UNUSED void *context) {
    if (callContext == GEO_CONTEXT_RENDER) {
        s32 id = ((struct GraphNodeGenerated *) node)->parameter;
        struct GraphNodeScale *scale = (struct GraphNodeScale *) node->next;

        scale->scale = (id == sWeapon && sFlashTimer > 0) ? sWeapons[id].flashScale : 0.0f;
    }
    return NULL;
}

// ------------------------------------------------------------------ aiming pose

// Marks which right-arm bone the renderer draws next (1 shoulder, 2 upper arm, 3 forearm, 4 hand).
Gfx *geo_weapon_arm_override(s32 callContext, struct GraphNode *node, UNUSED void *context) {
    if (callContext == GEO_CONTEXT_RENDER) {
        sPendingBone = ((struct GraphNodeGenerated *) node)->parameter;
    }
    return NULL;
}

static s16 blend_angle(s16 from, s16 to, f32 w) {
    return from + (s16)((s16)(to - from) * w);
}

// Bone frames: +X runs along the bone; in the torso frame +X is up the spine and +Y is forward,
// so rotating the upper arm 90 degrees about Z points it straight ahead.
void weapon_apply_bone_override(Vec3s rotation) {
    s32 bone = sPendingBone;
    s16 target[3] = { 0, 0, 0 };
    f32 recoil;
    s32 i;

    if (bone == 0) {
        return;
    }
    sPendingBone = 0;
    if (sAimWeight <= 0.0f) {
        return;
    }

    recoil = sWeapons[sWeapon].recoilFrames ? (f32) sRecoilTimer / sWeapons[sWeapon].recoilFrames : 0.0f;
    switch (bone) {
        case 1: // shoulder joint: neutral
            break;
        case 2: // upper arm: straight forward, kicks up with recoil
            target[2] = 0x4000 - (s16)(sWeapons[sWeapon].recoilClimb * 1.5f * recoil);
            break;
        case 3: // forearm: straight
        case 4: // hand: straight, so the barrel lines up with the arm
            break;
    }
    for (i = 0; i < 3; i++) {
        rotation[i] = blend_angle(rotation[i], target[i], sAimWeight);
    }
}
