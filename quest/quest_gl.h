#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "vr_math.h"
bool qgl_init(void);
bool qgl_stereo_init(void *framebuffer_multiview_proc);
bool qgl_stereo_begin(const float views[32],const float projections[32],int w,int h);
void qgl_stereo_end(void);
void qgl_stereo_blit(int eye);
void qgl_eye(const float view[16],const float projection[16]);
void qgl_target(int eye,unsigned framebuffer);
void qgl_scene_begin(const uint8_t lut[3][256],int w,int h);
void qgl_atlas_texture(unsigned texture,int page,int dimension,int pages);
void qgl_flush(void);
void qgl_flat_camera(float cx,float cy,float focal);
void qgl_pointer(V3 origin,V3 end);
void qgl_tracer(V3 origin,V3 end,float opacity);
void qgl_shutdown(void);
