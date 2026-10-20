#pragma once
#include "vr_math.h"
#include "tc2_build.h"
extern int qvr_flat_view;
extern float qvr_units_per_meter;
/* Size relative to the human-scale calibration; geometry caches remain in base metres. */
extern float qvr_world_scale;
/* Extra camera-to-scene clearance in physical metres; overlays stay fixed. */
extern float qvr_view_distance;
typedef enum { QTC2_WORLD, QTC2_OVERLAY, QTC2_LETTERBOX } QTC2Layer;
QTC2Layer qvr_tc2_layer(const tc2_built *b,const tc2_poly *p);
void qvr_tc2_letterbox(int *top,int *bottom);
void qvr_scene_prepare(const tc2_built *frame);
const tc2_built *qvr_current_frame(void);
V3 qvr_tc2_vertex(const tc2_built *b,const tc2_poly *p,int index);
bool qvr_flat_camera(float *cx,float *cy,float *focal);
bool qvr_aim(V3 origin,V3 direction,float *nx,float *ny,V3 *hit);

/* World texture filtering; HUD/text and stencil masks retain point sampling. */
void tc2_gl_set_filter(int enabled);
