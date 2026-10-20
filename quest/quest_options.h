#pragma once
#include <stdbool.h>
#include "quest_resolution.h"

enum { QOPTIONS_PAGE_GRAPHICS=2,QOPTIONS_PAGE_COUNT=QOPTIONS_PAGE_GRAPHICS+1 };
enum { QOPTIONS_GUN_PITCH_MIN=-60,QOPTIONS_GUN_PITCH_MAX=60 };
enum { QOPTIONS_WORLD_SCALE_MIN=25,QOPTIONS_WORLD_SCALE_MAX=400,
       QOPTIONS_WORLD_SCALE_DEFAULT=100,QOPTIONS_WORLD_SCALE_STEP=5 };
enum { QOPTIONS_VIEW_DISTANCE_MIN=0,QOPTIONS_VIEW_DISTANCE_MAX=100,
       QOPTIONS_VIEW_DISTANCE_DEFAULT=0,QOPTIONS_VIEW_DISTANCE_STEP=5 };
enum { QOPTIONS_GUN_OFFSET_MIN=-200,QOPTIONS_GUN_OFFSET_MAX=200,QOPTIONS_GUN_OFFSET_STEP=5 };
typedef struct QOptions { bool laser_enabled,physical_crouch,left_handed; int gun_pitch,world_scale,view_distance;
    bool tracer_enabled; int gun_offset_y_mm,gun_offset_z_mm,player_side; bool texture_filtering; int resolution; } QOptions;
/* Positive Y raises the gun; positive Z pulls it back towards the hand. */
static inline const char *qoptions_aim_label(const QOptions *o){return o->laser_enabled?"LASER":o->tracer_enabled?"TRACER":"OFF";}
void qoptions_load(const char *path,QOptions *options);
bool qoptions_save(const char *path,const QOptions *options);
