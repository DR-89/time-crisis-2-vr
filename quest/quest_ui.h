#pragma once
#include "vr_math.h"
#include "quest_options.h"
bool qui_init(void);
void qui_draw(const float view[16],const float projection[16],V3 head,Q4 rotation,const QOptions *options,int page,int boot_side,bool saved);
void qui_shutdown(void);
