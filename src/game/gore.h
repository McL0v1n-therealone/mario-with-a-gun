#ifndef GORE_H
#define GORE_H

#include "types.h"

// Blood and gibs for Mario's weapons. All gore objects are "unimportant" and are only
// spawned while the object pool has room, so they can never starve real game objects.
void gore_spray(struct Object *spawner, f32 x, f32 y, f32 z, s16 yaw, s32 count, f32 power);
void gore_explode(struct Object *spawner, f32 x, f32 y, f32 z, f32 size);

void bhv_gore_blood_drop_loop(void);
void bhv_gore_gib_loop(void);
void bhv_gore_blood_pool_loop(void);

#endif // GORE_H
