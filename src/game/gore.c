#include <PR/ultratypes.h>

#include "sm64.h"
#include "types.h"
#include "gore.h"
#include "audio/external.h"
#include "behavior_data.h"
#include "engine/behavior_script.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "model_ids.h"
#include "object_helpers.h"
#include "object_list_processor.h"

#define GORE_OBJECT_BUDGET 170  // stop spawning gore above this many active objects (pool holds 240)
#define GORE_GRAVITY       2.6f
#define POOL_LIFETIME      600  // frames a blood pool stays (20 s)
#define GIB_LIFETIME       300

// gib / pool bookkeeping lives in otherwise unused object fields
#define oGoreBaseScale     oHomeX
#define oGoreResting       oAction

static s32 gore_has_room(void) {
    return gPrevFrameObjectCount < GORE_OBJECT_BUDGET;
}

static struct Object *gore_spawn(struct Object *spawner, s32 model, const BehaviorScript *bhv, f32 x, f32 y, f32 z) {
    struct Object *g;

    if (!gore_has_room()) {
        return NULL;
    }
    g = spawn_object(spawner, model, bhv);
    g->oPosX = x;
    g->oPosY = y;
    g->oPosZ = z;
    g->oVelX = g->oVelY = g->oVelZ = 0.0f;
    return g;
}

static void gore_spawn_pool(struct Object *spawner, f32 x, f32 y, f32 z, f32 scale) {
    struct Surface *floor;
    f32 floorY = find_floor(x, y + 50.0f, z, &floor);
    struct Object *pool;

    if (floor == NULL || y - floorY > 400.0f) {
        return; // nothing to land on nearby
    }
    pool = gore_spawn(spawner, MODEL_GORE_BLOOD_POOL, bhvGoreBloodPool, x, floorY + 1.0f, z);
    if (pool != NULL) {
        pool->oGoreBaseScale = scale;
        pool->oFaceAngleYaw = random_u16();
        obj_scale(pool, 0.01f);
    }
}

void gore_spray(struct Object *spawner, f32 x, f32 y, f32 z, s16 yaw, s32 count, f32 power) {
    s32 i;

    for (i = 0; i < count; i++) {
        struct Object *d = gore_spawn(spawner, MODEL_GORE_BLOOD_DROP, bhvGoreBloodDrop, x, y, z);
        s16 a = yaw + (s16)((random_u16() % 0x7000) - 0x3800);
        f32 speed = power * (0.35f + random_float() * 0.9f);

        if (d == NULL) {
            return;
        }
        d->oVelX = sins(a) * speed;
        d->oVelZ = coss(a) * speed;
        d->oVelY = 4.0f + random_float() * 16.0f;
    }
}

void gore_explode(struct Object *spawner, f32 x, f32 y, f32 z, f32 size) {
    s32 i;
    s32 gibs = 4 + (s32)(size * 3.0f);

    play_sound(SOUND_GENERAL_SPLATTERING, spawner->header.gfx.cameraToObject);

    gore_spawn_pool(spawner, x, y, z, 0.6f * size + 0.4f);
    gore_spray(spawner, x, y, z, random_u16(), 10, 10.0f);
    gore_spray(spawner, x, y, z, random_u16(), 10, 14.0f);

    for (i = 0; i < gibs; i++) {
        s32 bone = (i % 4) == 3;
        struct Object *g = gore_spawn(spawner, bone ? MODEL_GORE_GIB_BONE : MODEL_GORE_GIB_FLESH, bhvGoreGib,
                                      x + (random_float() - 0.5f) * 30.0f * size, y, z + (random_float() - 0.5f) * 30.0f * size);
        s16 a = random_u16();
        f32 speed = 6.0f + random_float() * 14.0f;
        f32 scale;

        if (g == NULL) {
            return;
        }
        g->oVelX = sins(a) * speed;
        g->oVelZ = coss(a) * speed;
        g->oVelY = 18.0f + random_float() * 26.0f;
        g->oAngleVelPitch = (s16)(random_sign() * (0x800 + random_u16() % 0x1800));
        g->oAngleVelYaw = (s16)(random_sign() * (0x800 + random_u16() % 0x1800));
        g->oFaceAngleYaw = random_u16();
        scale = (0.6f + random_float() * 0.7f) * (0.7f + size * 0.3f);
        g->oGoreBaseScale = scale;
        obj_scale(g, scale);
    }
}

void bhv_gore_blood_drop_loop(void) {
    struct Object *o = gCurrentObject;
    struct Surface *floor;
    f32 floorY;

    o->oVelY -= GORE_GRAVITY;
    o->oPosX += o->oVelX;
    o->oPosY += o->oVelY;
    o->oPosZ += o->oVelZ;

    floorY = find_floor(o->oPosX, o->oPosY + 20.0f, o->oPosZ, &floor);
    if (floor == NULL || o->oTimer > 60) {
        obj_mark_for_deletion(o);
        return;
    }
    if (o->oPosY <= floorY) {
        if ((random_u16() & 3) == 0) {
            gore_spawn_pool(o, o->oPosX, floorY, o->oPosZ, 0.12f + random_float() * 0.18f);
        }
        obj_mark_for_deletion(o);
    }
}

void bhv_gore_gib_loop(void) {
    struct Object *o = gCurrentObject;
    struct Surface *floor;
    f32 floorY;

    if (!o->oGoreResting) {
        struct WallCollisionData wall;

        o->oVelY -= GORE_GRAVITY;
        o->oPosX += o->oVelX;
        o->oPosY += o->oVelY;
        o->oPosZ += o->oVelZ;
        o->oFaceAnglePitch += o->oAngleVelPitch;
        o->oFaceAngleYaw += o->oAngleVelYaw;

        wall.x = o->oPosX; wall.y = o->oPosY; wall.z = o->oPosZ;
        wall.offsetY = 10.0f; wall.radius = 15.0f;
        if (find_wall_collisions(&wall) != 0) {
            o->oPosX = wall.x; o->oPosZ = wall.z;
            o->oVelX *= -0.3f; o->oVelZ *= -0.3f;
        }

        // drip a little blood while flying
        if ((o->oTimer & 3) == 0) {
            gore_spray(o, o->oPosX, o->oPosY, o->oPosZ, random_u16(), 1, 2.0f);
        }

        floorY = find_floor(o->oPosX, o->oPosY + 30.0f, o->oPosZ, &floor);
        if (floor == NULL) {
            obj_mark_for_deletion(o);
            return;
        }
        if (o->oPosY <= floorY) {
            o->oPosY = floorY;
            if (o->oVelY < -10.0f) {
                o->oVelY *= -0.3f; // bounce
                o->oVelX *= 0.5f;
                o->oVelZ *= 0.5f;
                o->oAngleVelPitch /= 2;
                o->oAngleVelYaw /= 2;
            } else {
                o->oGoreResting = TRUE;
                o->oVelX = o->oVelY = o->oVelZ = 0.0f;
                o->oFaceAnglePitch = 0;
                gore_spawn_pool(o, o->oPosX, floorY, o->oPosZ, 0.15f + random_float() * 0.15f);
            }
        }
    }

    // fade out by shrinking at the end of its life
    if (o->oTimer > GIB_LIFETIME) {
        f32 s = o->oGoreBaseScale * (1.0f - (o->oTimer - GIB_LIFETIME) / 30.0f);
        if (s <= 0.02f) {
            obj_mark_for_deletion(o);
            return;
        }
        obj_scale(o, s);
    }
}

void bhv_gore_blood_pool_loop(void) {
    struct Object *o = gCurrentObject;
    f32 s;

    if (o->oTimer < 20) {
        s = o->oGoreBaseScale * (0.2f + 0.8f * o->oTimer / 20.0f); // spread out
    } else if (o->oTimer < POOL_LIFETIME) {
        s = o->oGoreBaseScale;
    } else {
        s = o->oGoreBaseScale * (1.0f - (o->oTimer - POOL_LIFETIME) / 40.0f); // dry up
        if (s <= 0.01f) {
            obj_mark_for_deletion(o);
            return;
        }
    }
    obj_scale(o, s);
}
