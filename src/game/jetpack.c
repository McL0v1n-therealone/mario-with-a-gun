#include <PR/ultratypes.h>

#include "sm64.h"
#include "types.h"
#include "jetpack.h"
#include "audio/external.h"
#include "behavior_data.h"
#include "engine/behavior_script.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "mario.h"
#include "model_ids.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "print.h"
#include "game_init.h"

#define JETPACK_BUTTON     A_BUTTON
#define FUEL_MAX           150     // frames of thrust (5 seconds)
#define FUEL_REFILL        3       // per frame on the ground
#define THRUST             8.5f    // added to vertical speed each frame (gravity takes 4)
#define MAX_CLIMB          34.0f
#define BOOST_SPEED        34.0f   // forward speed when steering while flying

static s16 sFuel = FUEL_MAX;
static u8 sThrusting = FALSE;
static f32 sFlame = 0.0f;          // 0..1 flame size
static u8 sFlewThisJump = FALSE;   // used the jetpack since last touching the ground

// Jumps and falls the jetpack can take over from.
static s32 jetpack_can_ignite(struct MarioState *m) {
    switch (m->action) {
        case ACT_JUMP:
        case ACT_DOUBLE_JUMP:
        case ACT_TRIPLE_JUMP:
        case ACT_BACKFLIP:
        case ACT_SIDE_FLIP:
        case ACT_LONG_JUMP:
        case ACT_WALL_KICK_AIR:
        case ACT_FREEFALL:
        case ACT_JUMP_KICK:
        case ACT_STEEP_JUMP:
        case ACT_TOP_OF_POLE_JUMP:
        case ACT_SPECIAL_TRIPLE_JUMP:
        case ACT_WATER_JUMP:
        case ACT_HOLD_JUMP:
        case ACT_HOLD_FREEFALL:
            return TRUE;
    }
    return FALSE;
}

void jetpack_update(struct MarioState *m) {
    u32 group = m->action & ACT_GROUP_MASK;
    s32 wantThrust = FALSE;

    if (m->marioObj == NULL) {
        return;
    }

    // refuel on solid ground
    if (group == ACT_GROUP_STATIONARY || group == ACT_GROUP_MOVING) {
        sFuel += FUEL_REFILL;
        if (sFuel > FUEL_MAX) sFuel = FUEL_MAX;
        sFlewThisJump = FALSE;
    }

    if (sFuel > 0 && (m->controller->buttonDown & JETPACK_BUTTON)) {
        if (sThrusting) {
            wantThrust = (m->action == ACT_FREEFALL || m->action == ACT_HOLD_FREEFALL);
        } else if ((m->controller->buttonPressed & JETPACK_BUTTON) && jetpack_can_ignite(m)) {
            // second press in the air lights the jetpack
            set_mario_action(m, m->heldObj != NULL ? ACT_HOLD_FREEFALL : ACT_FREEFALL, 0);
            play_sound(SOUND_AIR_BOWSER_SPIT_FIRE, m->marioObj->header.gfx.cameraToObject);
            wantThrust = TRUE;
        }
    }
    sThrusting = wantThrust;

    // no fall damage for the whole flight: the drop is measured from the current height until landing
    if (sThrusting || (sFlewThisJump && group == ACT_GROUP_AIRBORNE)) {
        m->peakHeight = m->pos[1];
    }

    if (sThrusting) {
        sFuel--;
        m->vel[1] += THRUST;
        if (m->vel[1] > MAX_CLIMB) m->vel[1] = MAX_CLIMB;
        sFlewThisJump = TRUE;

        // steering boost toward the stick direction
        if (m->intendedMag > 8.0f) {
            m->faceAngle[1] = approach_s16_symmetric(m->faceAngle[1], m->intendedYaw, 0x600);
            m->forwardVel = approach_f32(m->forwardVel, BOOST_SPEED * (m->intendedMag / 32.0f), 2.0f, 2.0f);
            m->vel[0] = sins(m->faceAngle[1]) * m->forwardVel;
            m->vel[2] = coss(m->faceAngle[1]) * m->forwardVel;
            m->slideVelX = m->vel[0];
            m->slideVelZ = m->vel[2];
        }

        play_sound(SOUND_MOVING_FLYING, m->marioObj->header.gfx.cameraToObject);
        if ((gGlobalTimer % 3) == 0 && gPrevFrameObjectCount < 180) {
            struct Object *puff = spawn_object(m->marioObj, MODEL_SMOKE, bhvWhitePuffSmoke2);
            puff->oPosY -= 20.0f;
        }
        sFlame = approach_f32(sFlame, 1.0f, 0.3f, 0.3f);
    } else {
        sFlame = approach_f32(sFlame, 0.0f, 0.2f, 0.2f);
    }
}

void jetpack_render_hud(void) {
    if (sFuel < FUEL_MAX || sThrusting) {
        print_text_fmt_int(22, 51, "FUEL %d", sFuel * 100 / FUEL_MAX);
    }
}

// exhaust flames flicker while thrusting
Gfx *geo_jetpack_flame(s32 callContext, struct GraphNode *node, UNUSED void *context) {
    if (callContext == GEO_CONTEXT_RENDER) {
        f32 flicker = 0.75f + (random_u16() % 100) / 300.0f;
        ((struct GraphNodeScale *) node->next)->scale = sFlame * flicker;
    }
    return NULL;
}
