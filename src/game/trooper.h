#ifndef TROOPER_H
#define TROOPER_H

#include "types.h"

// Desert troopers: original rifle-armed enemies for the Outpost map.
void bhv_trooper_init(void);
void bhv_trooper_loop(void);
void bhv_enemy_bullet_loop(void);

Gfx *geo_trooper_body(s32 callContext, struct GraphNode *node, UNUSED void *context);
Gfx *geo_trooper_leg(s32 callContext, struct GraphNode *node, UNUSED void *context);
Gfx *geo_trooper_flash(s32 callContext, struct GraphNode *node, UNUSED void *context);

#endif // TROOPER_H
