#ifndef JETPACK_H
#define JETPACK_H

#include "types.h"

// Mario's jetpack: press and hold A again in mid-air to fly.
void jetpack_update(struct MarioState *m);
void jetpack_render_hud(void);
Gfx *geo_jetpack_flame(s32 callContext, struct GraphNode *node, UNUSED void *context);

#endif // JETPACK_H
