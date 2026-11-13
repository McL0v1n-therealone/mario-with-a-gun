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
#include "camera.h"
#include "level_update.h"
#include "game_init.h"

#define WEAPON_FIRE_BUTTON  L_TRIG
#define WEAPON_NEXT_BUTTON  R_JPAD
#define WEAPON_PREV_BUTTON  L_JPAD
#define WEAPON_JUMP_UP      U_JPAD   // +4 weapons
#define WEAPON_JUMP_DOWN    D_JPAD   // -4 weapons

#define PROJECTILE_BUDGET   200      // don't spawn projectiles above this many active objects (pool holds 240)
#define STREAK_WINDOW       75       // frames between kills to keep a multi-kill going

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

enum WeaponPartAnim { PART_NONE, PART_SLIDE, PART_SPIN, PART_PUMP, PART_SPINUP, PART_BREAK };
enum WeaponProjectile { PROJ_BOLT, PROJ_BULLET, PROJ_PLASMA, PROJ_ROCKET, PROJ_GRENADE, PROJ_FLAME, PROJ_RAIL };

struct WeaponInfo {
    const char *name;     // HUD label (US HUD font has no J, Q, V, X, Z)
    u8 automatic;         // hold to fire
    u8 pellets;           // projectiles per shot
    s16 spread;           // multi-pellet: angle between pellets, others: random +/- spread
    s16 cooldown;         // frames between shots
    f32 speed;            // projectile units per frame
    s16 lifetime;         // projectile frames
    u8 pierce;            // enemies a projectile can pass through
    u8 proj;              // enum WeaponProjectile
    s16 power;            // damage per hit (bosses count hits)
    f32 splash;           // explosion radius, 0 = none
    s32 sound;
    s16 recoilFrames;
    f32 recoilKick;       // how far the gun moves back
    s16 recoilClimb;      // muzzle climb angle
    u8 partAnim;
    s16 partDelay;        // frames after the shot before the moving part starts
    s16 partFrames;
    f32 partDist;         // slide/pump travel, break-open angle
    f32 flashScale;
    f32 muzzleX;          // muzzle distance from the wrist (fist units, see gen_weapons.py)
};

static const struct WeaponInfo sWeapons[WEAPON_COUNT] = {
    //  name            auto pel spread cd  speed   life pierce proj          pow splash  sound                           rFr kick   climb   part         dly frm dist     flash muzzle
    { "LIGHT GUN",      0,   1,  0,     8,  100.0f, 30,  0,     PROJ_BOLT,    1,  0.0f,   SOUND_OBJ_SNUFIT_SHOOT,         5,  10.0f, 0x200,  PART_NONE,   0,  0,  0.0f,    0.0f, 226.0f },
    { "PISTOL",         0,   1,  0x80,  7,  150.0f, 24,  0,     PROJ_BULLET,  1,  0.0f,   SOUND_GENERAL_RACE_GUN_SHOT,    6,  14.0f, 0x700,  PART_SLIDE,  0,  5,  34.0f,   1.0f, 208.0f },
    { "MAGNUM",         0,   1,  0,     18, 170.0f, 30,  2,     PROJ_BULLET,  3,  0.0f,   SOUND_OBJ_CANNON1,              12, 26.0f, 0x1400, PART_SPIN,   4,  6,  0.0f,    1.4f, 264.0f },
    { "SHOTGUN",        0,   6,  0x380, 28, 120.0f, 10,  0,     PROJ_BULLET,  1,  0.0f,   SOUND_OBJ_CANNON2,              14, 34.0f, 0x1000, PART_PUMP,   7,  14, 58.0f,   1.8f, 432.0f },
    { "SMG",            1,   1,  0x300, 3,  140.0f, 18,  0,     PROJ_BULLET,  1,  0.0f,   SOUND_GENERAL_RACE_GUN_SHOT,    3,  9.0f,  0x300,  PART_SLIDE,  0,  3,  24.0f,   0.8f, 238.0f },
    { "RIFLE",          1,   1,  0xA0,  5,  180.0f, 36,  1,     PROJ_BULLET,  1,  0.0f,   SOUND_GENERAL_RACE_GUN_SHOT,    4,  12.0f, 0x400,  PART_SLIDE,  0,  4,  14.0f,   1.0f, 406.0f },
    { "MINIGUN",        1,   1,  0x400, 2,  160.0f, 24,  0,     PROJ_BULLET,  1,  0.0f,   SOUND_GENERAL_RACE_GUN_SHOT,    2,  8.0f,  0x180,  PART_SPINUP, 0,  0,  0.0f,    1.1f, 352.0f },
    { "ROCKET",         0,   1,  0,     40, 60.0f,  60,  0,     PROJ_ROCKET,  10, 380.0f, SOUND_OBJ_CANNON3,              16, 40.0f, 0x1200, PART_NONE,   0,  0,  0.0f,    1.6f, 274.0f },
    { "GRENADE",        0,   1,  0,     22, 55.0f,  50,  0,     PROJ_GRENADE, 8,  320.0f, SOUND_OBJ_CANNON1,              10, 24.0f, 0x1000, PART_SPIN,   2,  6,  0.0f,    1.2f, 266.0f },
    { "FLAMETHROWER",   1,   1,  0x500, 1,  42.0f,  16,  2,     PROJ_FLAME,   1,  0.0f,   SOUND_AIR_BOWSER_SPIT_FIRE,     1,  3.0f,  0x80,   PART_NONE,   0,  0,  0.0f,    0.0f, 324.0f },
    { "SNIPER",         0,   1,  0,     32, 260.0f, 30,  5,     PROJ_BULLET,  6,  0.0f,   SOUND_OBJ_CANNON1,              18, 44.0f, 0x1A00, PART_SLIDE,  8,  14, 40.0f,   1.6f, 548.0f },
    { "RAILGUN",        0,   1,  0,     45, 420.0f, 12,  99,    PROJ_RAIL,    12, 0.0f,   SOUND_OBJ_MRI_SHOOT,            16, 36.0f, 0x1400, PART_NONE,   0,  0,  0.0f,    1.4f, 362.0f },
    { "DOUBLE BARREL",  0,   10, 0x330, 34, 120.0f, 9,   0,     PROJ_BULLET,  1,  0.0f,   SOUND_OBJ_CANNON2,              16, 40.0f, 0x1800, PART_BREAK,  10, 18, 0x1800,  2.2f, 402.0f },
    { "PLASMA",         1,   1,  0x100, 6,  120.0f, 30,  0,     PROJ_PLASMA,  2,  140.0f, SOUND_OBJ_SNUFIT_SHOOT,         4,  8.0f,  0x300,  PART_NONE,   0,  0,  0.0f,    1.0f, 246.0f },
    { "M4A1",           1,   1,  0x90,  4,  190.0f, 34,  1,     PROJ_BULLET,  2,  0.0f,   SOUND_GENERAL_RACE_GUN_SHOT,    4,  12.0f, 0x380,  PART_SLIDE,  0,  4,  10.0f,   1.0f, 376.0f },
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
static f32 sMinigunSpin = 0.0f; // 0..1 spin-up
static s16 sBarrelAngle = 0;    // minigun barrel rotation
static u16 sKills = 0;
static s16 sStreak = 0;
static s16 sStreakTimer = 0;
static s16 sAnnounceTimer = 0;
static const char *sAnnounceText = NULL;

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
    struct Object *shot;
    f32 fwd = 80.0f, side = 0.0f, speed = w->speed;
    s32 model;
    const BehaviorScript *bhv = bhvWeaponShot;

    if (gPrevFrameObjectCount >= PROJECTILE_BUDGET) {
        return; // too busy: skip this projectile rather than risk running out of objects
    }
    switch (w->proj) {
        case PROJ_BOLT:    model = MODEL_ZAPPER_SHOT; break;
        case PROJ_PLASMA:  model = MODEL_PLASMA_BOLT; break;
        case PROJ_ROCKET:  model = MODEL_ROCKET_PROJECTILE; break;
        case PROJ_GRENADE: model = MODEL_GRENADE_PROJECTILE; break;
        case PROJ_FLAME:   model = MODEL_RED_FLAME; bhv = bhvWeaponFlame; break;
        case PROJ_RAIL:    model = MODEL_NONE; break;
        default:           model = MODEL_BULLET; break;
    }
    shot = spawn_object(m->marioObj, model, bhv);

    if (sAimWeight > 0.5f) {
        fwd = 40.0f + w->muzzleX * 0.25f; // arm + gun length (Mario's model is drawn at 1/4 scale)
        side = 20.0f;                     // right shoulder
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
    shot->oAction = 0;

    if (w->proj == PROJ_FLAME) {
        speed *= 0.8f + random_float() * 0.4f;
        shot->oVelY = random_float() * 3.0f;
        obj_scale(shot, 0.8f);
    } else if (w->proj == PROJ_GRENADE) {
        shot->oVelY = 26.0f; // lob
    } else {
        shot->oVelY = 0.0f;
    }
    shot->oVelX = sins(yaw) * speed;
    shot->oVelZ = coss(yaw) * speed;
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
    if (w->partAnim != PART_NONE && w->partAnim != PART_SPINUP) {
        if (w->partAnim == PART_SPIN && sPartTimer >= 0) {
            sSpinBase += 0x2AAB; // finish the previous turn
        }
        sPartTimer = 0;
    }
    // heavy hitters shake the camera
    if (w->recoilKick >= 36.0f) {
        set_camera_shake_from_point(SHAKE_POS_SMALL, m->pos[0], m->pos[1], m->pos[2]);
    }
    // the flamethrower would spam its sound every frame
    if (w->proj != PROJ_FLAME || (sCooldown == 1 && (gGlobalTimer & 7) == 0)) {
        play_sound(w->sound, m->marioObj->header.gfx.cameraToObject);
    }
}

static void weapon_switch(struct MarioState *m, s32 dir) {
    sWeapon = ((sWeapon + dir) % WEAPON_COUNT + WEAPON_COUNT) % WEAPON_COUNT;
    sDrawTimer = 8;
    sHudTimer = 45;
    sRecoilTimer = 0;
    sFlashTimer = 0;
    sPartTimer = -1;
    sMinigunSpin = 0.0f;
    if (sCooldown < 6) {
        sCooldown = 6; // can't fire mid-draw
    }
    play_sound(SOUND_MENU_CLICK_CHANGE_VIEW, m->marioObj->header.gfx.cameraToObject);
}

// Kill feed: count, multi-kill announcer, and a little health back per kill.
static void weapon_register_kill(void) {
    static const char *const sStreakNames[] = { "DOUBLE KILL", "TRIPLE KILL", "RAMPAGE", "RAMPAGE",
                                                "UNSTOPPABLE", "UNSTOPPABLE", "UNSTOPPABLE", "GODLIKE" };

    if (sKills < 9999) {
        sKills++;
    }
    sStreak = sStreakTimer > 0 ? sStreak + 1 : 1;
    sStreakTimer = STREAK_WINDOW;
    if (sStreak >= 2) {
        s32 idx = sStreak - 2;
        if (idx >= (s32) ARRAY_COUNT(sStreakNames)) {
            idx = ARRAY_COUNT(sStreakNames) - 1;
        }
        sAnnounceText = sStreakNames[idx];
        sAnnounceTimer = 60;
    }
    if (gMarioState->healCounter < 200) {
        gMarioState->healCounter += 1; // a quarter wedge of health per kill
    }
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
    if (sStreakTimer > 0) sStreakTimer--;
    if (sAnnounceTimer > 0) sAnnounceTimer--;

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
    } else if (pressed & WEAPON_JUMP_UP) {
        weapon_switch(m, 4);
    } else if (pressed & WEAPON_JUMP_DOWN) {
        weapon_switch(m, -4);
    }

    w = &sWeapons[sWeapon];

    // minigun: barrels spin up while the trigger is held, and only fire at full speed
    if (w->partAnim == PART_SPINUP) {
        if ((held & WEAPON_FIRE_BUTTON) && weapon_can_fire(m)) {
            sMinigunSpin += 0.07f;
            if (sMinigunSpin > 1.0f) sMinigunSpin = 1.0f;
            sAimTimer = AIM_HOLD_FRAMES;
        } else {
            sMinigunSpin -= 0.03f;
            if (sMinigunSpin < 0.0f) sMinigunSpin = 0.0f;
        }
        sBarrelAngle += (s16)(0x1C00 * sMinigunSpin);
    }

    if (sCooldown == 0 && ((w->automatic ? held : pressed) & WEAPON_FIRE_BUTTON) && weapon_can_fire(m)
        && (w->partAnim != PART_SPINUP || sMinigunSpin >= 1.0f)) {
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
    print_text_fmt_int(22, 33, "KILLS %d", sKills);
    if (sAnnounceTimer > 0 && sAnnounceText != NULL) {
        s32 len = 0;
        while (sAnnounceText[len] != '\0') {
            len++;
        }
        // centred, pops in slightly bigger feel by blinking for the first few frames
        if (sAnnounceTimer < 54 || (sAnnounceTimer & 2)) {
            print_text(160 - len * 6, 150, sAnnounceText);
        }
    }
}

// ------------------------------------------------------------------ projectile

// Friendly / progression objects that must never be destroyed.
static s32 weapon_is_protected(struct Object *obj) {
    static const BehaviorScript *const protectedBhvs[] = {
        bhvMips, bhvBowserTailAnchor, bhvSmallPenguin, bhvUkiki, bhvUnagi, bhvBobombBuddy,
        bhvBobombBuddyOpensCannon, bhvYoshi, bhvTuxiesMother, bhvHoot, bhvChainChompGate, bhvEnemyBullet,
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

// Things that aren't alive: they break, crumble or explode instead of bleeding.
static s32 weapon_is_object(struct Object *obj) {
    static const BehaviorScript *const objectBhvs[] = {
        bhvBreakableBox, bhvBreakableBoxSmall, bhvJumpingBox, bhvExclamationBox, bhvHiddenObject,
        bhvBigBoulder, bhvBowlingBall, bhvPitBowlingBall, bhvFreeBowlingBall, bhvBulletBill,
        bhvCirclingAmp, bhvHomingAmp, bhvMadPiano, bhvFlyingBookend, bhvHeaveHo, bhvMrBlizzard,
        bhvSnufitBalls, bhvThwomp, bhvThwomp2, bhvSmallWhomp, bhvWhompKingBoss, bhvGrindel, bhvSpindel,
        bhvChainChomp, bhvEyerokHand, bhvFlame, bhvFlameBouncing, bhvFlameBowser, bhvFlameFloatingLanding,
        bhvFlameLargeBurningOut, bhvFlameMovingForwardGrowing, bhvFlamethrowerFlame,
        bhvBobomb, bhvBowserBomb,
    };
    s32 i;

    if (obj->oInteractType & (INTERACT_BREAKABLE | INTERACT_FLAME | INTERACT_SNUFIT_BULLET | INTERACT_SHOCK)) {
        return TRUE;
    }
    for (i = 0; i < (s32) ARRAY_COUNT(objectBhvs); i++) {
        if (obj_has_behavior(obj, objectBhvs[i])) {
            return TRUE;
        }
    }
    return FALSE;
}

static s32 weapon_is_explosive(struct Object *obj) {
    return obj_has_behavior(obj, bhvBobomb) || obj_has_behavior(obj, bhvBowserBomb);
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

static void weapon_damage(struct Object *t, struct Object *shot, s32 power);

static s32 weapon_is_valid_target(struct Object *obj, struct Object *shot) {
    u32 type = obj->oInteractType;

    return (obj->activeFlags & ACTIVE_FLAG_ACTIVE) && (s32) obj != shot->oAction
        && (!(type & SHOT_IGNORED) || obj_has_behavior(obj, bhvToadMessage))
        && !(obj->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE)
        && (type != 0 || weapon_is_solid_enemy(obj)) && !weapon_is_protected(obj);
}

// Is the sphere (x, y, z, r) touching obj's hit cylinder?
static s32 weapon_overlaps(struct Object *obj, f32 x, f32 y, f32 z, f32 r) {
    f32 radius, bottom, height;
    f32 dx = obj->oPosX - x;
    f32 dz = obj->oPosZ - z;
    f32 reach;

    weapon_target_bounds(obj, &radius, &bottom, &height);
    reach = radius + r;
    return dx * dx + dz * dz < reach * reach && y > bottom - r && y < bottom + height + r;
}

static const s32 sTargetLists[] = { OBJ_LIST_GENACTOR, OBJ_LIST_PUSHABLE, OBJ_LIST_DESTRUCTIVE, OBJ_LIST_SURFACE };

static struct Object *weapon_find_target(struct Object *shot) {
    s32 i;

    for (i = 0; i < (s32) ARRAY_COUNT(sTargetLists); i++) {
        struct ObjectNode *head = &gObjectLists[sTargetLists[i]];
        struct Object *obj = (struct Object *) head->next;

        while (obj != (struct Object *) head) {
            if (weapon_is_valid_target(obj, shot) && weapon_overlaps(obj, shot->oPosX, shot->oPosY, shot->oPosZ, SHOT_RADIUS)) {
                return obj;
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

static s32 weapon_boss_hit(struct Object *obj, s32 power) {
    s32 i, slot = 0;

    for (i = 0; i < (s32) ARRAY_COUNT(sBossHits); i++) {
        if (sBossHits[i].obj == obj && sBossHits[i].bhv == obj->behavior && (obj->activeFlags & ACTIVE_FLAG_ACTIVE)) {
            sBossHits[i].hits += power;
            return sBossHits[i].hits;
        }
        if (sBossHits[i].obj == NULL || !(sBossHits[i].obj->activeFlags & ACTIVE_FLAG_ACTIVE)) {
            slot = i;
        }
    }
    sBossHits[slot].obj = obj;
    sBossHits[slot].bhv = obj->behavior;
    sBossHits[slot].hits = power;
    return power;
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

// Death effect: gore for living things, rubble/dust for objects.
static void weapon_death_fx(struct Object *t, struct Object *shot, f32 size) {
    if (!weapon_is_object(t)) {
        gore_explode(shot, t->oPosX, weapon_target_center_y(t), t->oPosZ, size);
    } else {
        struct Object *saved = gCurrentObject;

        gCurrentObject = t; // the particle helpers spawn around the current object
        spawn_mist_particles_variable(0, 0, 40.0f * size);
        spawn_triangle_break_particles((s16)(8 + 4 * size), MODEL_DIRT_ANIMATION, 2.0f * size, 4);
        gCurrentObject = saved;
        play_sound(SOUND_GENERAL_WALL_EXPLOSION, shot->header.gfx.cameraToObject);
    }
}

// Blow an ordinary enemy apart: gore, coins, any item it carried, and its child objects.
static void weapon_kill(struct Object *t, struct Object *shot) {
    static const s32 lists[] = { OBJ_LIST_GENACTOR, OBJ_LIST_PUSHABLE, OBJ_LIST_DESTRUCTIVE, OBJ_LIST_DEFAULT,
                                 OBJ_LIST_UNIMPORTANT, OBJ_LIST_SURFACE, OBJ_LIST_POLELIKE };
    s32 i;

    if (weapon_is_explosive(t)) {
        struct Object *boom = spawn_object(t, MODEL_EXPLOSION, bhvExplosion);
        boom->oGraphYOffset += 100.0f;
    } else {
        weapon_death_fx(t, shot, weapon_target_size(t));
        if (!weapon_is_object(t)) {
            play_sound(SOUND_OBJ_DEFAULT_DEATH, shot->header.gfx.cameraToObject);
        }
    }
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
    weapon_register_kill();
}

void weapon_kill_object(struct Object *obj) {
    if (obj->activeFlags & ACTIVE_FLAG_ACTIVE) {
        weapon_kill(obj, obj);
    }
}

// Apply one bullet hit. Bosses and enemies with special defeat logic use their own death
// so stars, music and level progress still work; everything else is blown apart.
static void weapon_damage(struct Object *t, struct Object *shot, s32 power) {
    if (!weapon_is_object(t)) {
        gore_spray(shot, shot->oPosX, shot->oPosY, shot->oPosZ, shot->oMoveAngleYaw, 6, 9.0f);
    }

    if (t->oInteractType & INTERACT_BREAKABLE) {
        // crates, cork boxes, ! boxes: break exactly like a punch would (coins, items, debris)
        t->oInteractStatus = ATTACK_PUNCH | INT_STATUS_INTERACTED | INT_STATUS_WAS_ATTACKED;
        play_sound(SOUND_GENERAL_BREAK_BOX, shot->header.gfx.cameraToObject);
    } else if (obj_has_behavior(t, bhvBowser)) {
        if (t->oAction != BOWSER_ACT_HIT_MINE && t->oAction != BOWSER_ACT_DEAD && t->oAction != BOWSER_ACT_THROWN
            && t->oAction != BOWSER_ACT_JUMP_ONTO_STAGE && t->oAction != BOWSER_ACT_INTRO_WALK
            && weapon_boss_hit(t, power) >= BOWSER_HITS) {
            weapon_boss_reset(t);
            weapon_death_fx(t, shot, 1.5f);
            if (--t->oHealth <= 0) {
                t->oAction = BOWSER_ACT_DEAD;
                weapon_register_kill();
            } else {
                t->oAction = BOWSER_ACT_HIT_MINE;
            }
            t->oTimer = 0;
            t->oSubAction = 0;
        }
    } else if (obj_has_behavior(t, bhvKingBobomb)) {
        if (t->oAction != 7 && t->oAction != 8 && weapon_boss_hit(t, power) >= BOSS_HITS) {
            weapon_death_fx(t, shot, 2.5f);
            t->oHealth = 0;
            t->oAction = 7; // defeated: dialog, then his star
            weapon_register_kill();
        }
    } else if (obj_has_behavior(t, bhvWhompKingBoss) || obj_has_behavior(t, bhvSmallWhomp)) {
        s32 needed = obj_has_behavior(t, bhvWhompKingBoss) ? BOSS_HITS : 1;
        if (t->oAction != 8 && weapon_boss_hit(t, power) >= needed) {
            weapon_death_fx(t, shot, 2.5f);
            t->oHealth = 0;
            t->oAction = 8; // whomp_die: coins, or the star for the king
            weapon_register_kill();
        }
    } else if (obj_has_behavior(t, bhvSmallBully) || obj_has_behavior(t, bhvSmallChillBully)
               || obj_has_behavior(t, bhvBigBully) || obj_has_behavior(t, bhvBigBullyWithMinions)
               || obj_has_behavior(t, bhvBigChillBully)) {
        s32 needed = t->oBhvParams2ndByte == BULLY_BP_SIZE_SMALL ? 1 : BIG_BULLY_HITS;
        if (t->oAction != BULLY_ACT_LAVA_DEATH && t->oAction != BULLY_ACT_DEATH_PLANE_DEATH
            && weapon_boss_hit(t, power) >= needed) {
            weapon_death_fx(t, shot, needed > 1 ? 2.5f : 1.2f);
            t->oForwardVel = 0.0f;
            t->oAction = BULLY_ACT_LAVA_DEATH; // sinks; counts minions / spawns the big bully's star
            weapon_register_kill();
        }
    } else if (obj_has_behavior(t, bhvWigglerHead)) {
        if (t->oAction != WIGGLER_ACT_SHRINK && t->oAction != WIGGLER_ACT_FALL_THROUGH_FLOOR
            && weapon_boss_hit(t, power) >= BOSS_HITS) {
            weapon_death_fx(t, shot, 2.0f);
            t->oHealth = 1;
            t->oAction = WIGGLER_ACT_SHRINK; // shrinks, drops his star
            weapon_register_kill();
        }
    } else if (obj_has_behavior(t, bhvEyerokHand)) {
        if (t->oAction != EYEROK_HAND_ACT_DIE && t->oHealth > 0 && weapon_boss_hit(t, power) >= EYEROK_HAND_HITS) {
            weapon_death_fx(t, shot, 2.0f);
            t->oHealth = 0;
            t->parentObj->oEyerokBossNumHands--;
            t->oAction = EYEROK_HAND_ACT_DIE;
            weapon_register_kill();
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

// Explosion: big fireball, screen shake, damages everything in the radius (never Mario).
static void weapon_explode(struct Object *shot, f32 radius, s32 power) {
    struct Object *boom;
    s32 i;

    if (gPrevFrameObjectCount < 225) {
        boom = spawn_object(shot, MODEL_EXPLOSION, bhvWeaponExplosion);
        boom->oPosX = shot->oPosX;
        boom->oPosY = shot->oPosY;
        boom->oPosZ = shot->oPosZ;
        boom->oHomeX = radius / 200.0f; // base scale
    }
    play_sound(SOUND_GENERAL2_BOBOMB_EXPLOSION, shot->header.gfx.cameraToObject);
    set_camera_shake_from_point(radius > 250.0f ? SHAKE_POS_LARGE : SHAKE_POS_SMALL, shot->oPosX, shot->oPosY, shot->oPosZ);

    shot->oAction = 0; // splash may hit the direct-hit target too
    for (i = 0; i < (s32) ARRAY_COUNT(sTargetLists); i++) {
        struct ObjectNode *head = &gObjectLists[sTargetLists[i]];
        struct Object *obj = (struct Object *) head->next;

        while (obj != (struct Object *) head) {
            struct Object *next = (struct Object *) obj->header.next;
            if (weapon_is_valid_target(obj, shot) && weapon_overlaps(obj, shot->oPosX, shot->oPosY, shot->oPosZ, radius)) {
                weapon_damage(obj, shot, power);
            }
            obj = next;
        }
    }
    obj_mark_for_deletion(shot);
}

static void weapon_shot_end(struct Object *o, const struct WeaponInfo *w) {
    if (w->splash > 0.0f) {
        weapon_explode(o, w->splash, w->power);
    } else if (w->proj == PROJ_FLAME) {
        obj_mark_for_deletion(o);
    } else {
        weapon_shot_burst();
    }
}

static void weapon_spawn_beam(struct Object *o) {
    struct Object *beam;

    if (gPrevFrameObjectCount >= 225) {
        return;
    }
    beam = spawn_object(o, MODEL_RAIL_BEAM, bhvWeaponBeam);
    beam->oPosX = o->oPosX;
    beam->oPosY = o->oPosY;
    beam->oPosZ = o->oPosZ;
    beam->oFaceAngleYaw = beam->oMoveAngleYaw = o->oMoveAngleYaw;
}

void bhv_weapon_shot_loop(void) {
    struct Object *o = gCurrentObject;
    const struct WeaponInfo *w = &sWeapons[o->oBhvParams2ndByte % WEAPON_COUNT];
    s32 substeps = w->proj == PROJ_RAIL ? 16 : SHOT_SUBSTEPS;
    s32 i;

    // per-frame motion changes
    switch (w->proj) {
        case PROJ_ROCKET: // accelerates, smoke trail
            if (o->oTimer < 20) {
                o->oVelX *= 1.06f;
                o->oVelZ *= 1.06f;
            }
            if ((o->oTimer & 1) && gPrevFrameObjectCount < 190) {
                spawn_object(o, MODEL_SMOKE, bhvWhitePuffSmoke2);
            }
            break;
        case PROJ_GRENADE: // arcs, tumbles
            o->oVelY -= 2.4f;
            o->oFaceAnglePitch += 0x1400;
            break;
        case PROJ_FLAME: // slows down, rises, grows, flickers
            o->oVelX *= 0.9f;
            o->oVelZ *= 0.9f;
            o->oVelY += 0.4f;
            o->oAnimState++;
            obj_scale(o, 0.8f + o->oTimer * 0.18f);
            break;
    }

    for (i = 0; i < substeps; i++) {
        struct WallCollisionData wall;
        struct Surface *floor;
        struct Object *target;
        f32 floorY;

        o->oPosX += o->oVelX / substeps;
        o->oPosY += o->oVelY / substeps;
        o->oPosZ += o->oVelZ / substeps;

        if (w->proj == PROJ_RAIL && (i & 3) == 0) {
            weapon_spawn_beam(o);
        }

        // walls and the ground
        wall.x = o->oPosX;
        wall.y = o->oPosY;
        wall.z = o->oPosZ;
        wall.offsetY = 0.0f;
        wall.radius = 20.0f;
        floorY = find_floor(o->oPosX, o->oPosY + 30.0f, o->oPosZ, &floor);
        if (w->proj == PROJ_GRENADE && floor != NULL) {
            if (find_wall_collisions(&wall) != 0) {
                o->oPosX = wall.x;
                o->oPosZ = wall.z;
                o->oVelX *= -0.5f;
                o->oVelZ *= -0.5f;
            }
            if (o->oPosY < floorY) {
                o->oPosY = floorY;
                o->oVelY = o->oVelY < -6.0f ? -o->oVelY * 0.45f : 0.0f; // bounce, then roll
                o->oVelX *= 0.75f;
                o->oVelZ *= 0.75f;
                if (o->oVelY > 6.0f) {
                    play_sound(SOUND_GENERAL_BOX_LANDING, o->header.gfx.cameraToObject);
                }
            }
        } else if (find_wall_collisions(&wall) != 0 || floor == NULL || o->oPosY < floorY) {
            weapon_shot_end(o, w);
            return;
        }

        target = weapon_find_target(o);
        if (target != NULL) {
            if (w->splash > 0.0f) {
                weapon_explode(o, w->splash, w->power); // direct hit detonates
                return;
            }
            if (w->proj != PROJ_FLAME) {
                play_sound(SOUND_ACTION_HIT, o->header.gfx.cameraToObject);
            }
            o->oAction = (s32) target; // don't hit the same enemy twice while piercing
            weapon_damage(target, o, w->power);
            if (o->oHealth <= 0) {
                weapon_shot_end(o, w);
                return;
            }
            o->oHealth--;
        }
    }

    if (o->oTimer >= w->lifetime) {
        if (w->splash > 0.0f) {
            weapon_explode(o, w->splash, w->power); // rockets/grenades go off at the end of their fuse
        } else {
            obj_mark_for_deletion(o);
        }
    }
}

void bhv_weapon_explosion_loop(void) {
    struct Object *o = gCurrentObject;

    if (o->oTimer == 0) {
        o->oOpacity = 255;
    }
    if (o->oTimer == 9) {
        spawn_object(o, MODEL_SMOKE, bhvBobombBullyDeathSmoke);
        obj_mark_for_deletion(o);
        return;
    }
    o->oOpacity -= 14;
    obj_scale(o, ((f32) o->oTimer / 9.0f + 1.0f) * o->oHomeX);
}

void bhv_weapon_beam_loop(void) {
    struct Object *o = gCurrentObject;
    f32 s = 1.0f - o->oTimer / 8.0f;

    if (s <= 0.05f) {
        obj_mark_for_deletion(o);
        return;
    }
    o->header.gfx.scale[0] = s;
    o->header.gfx.scale[1] = s;
    o->header.gfx.scale[2] = 1.0f;
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
            case PART_SPINUP:
                tr->rotation[0] = sBarrelAngle;
                break;
            case PART_BREAK:
                // barrels drop open on the hinge and snap shut (muzzle down = +Z rotation in the fist frame)
                tr->rotation[2] = (s16)(w->partDist * (p < 0.5f ? p * 2.0f : (1.0f - p) * 2.0f));
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
