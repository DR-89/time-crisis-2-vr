#pragma once
#include "vr_math.h"
#include "tc2_build.h"
extern int qvr_flat_view;
extern float qvr_units_per_meter;
void qvr_scene_prepare(const tc2_built *frame);
const tc2_built *qvr_current_frame(void);
V3 qvr_tc2_vertex(const tc2_built *b,const tc2_poly *p,int index);
bool qvr_flat_camera(float *cx,float *cy,float *focal);
bool qvr_aim(V3 origin,V3 direction,float *nx,float *ny,V3 *hit);
