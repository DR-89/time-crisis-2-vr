#pragma once
#include <stdint.h>
#include "vr_math.h"
/* A shot keeps its sampled ray for both eyes. A held trigger cannot turn the
 * tracer into a laser; only another accepted arcade shot restarts its life. */
typedef struct QTracer { V3 origin,end; uint64_t started; bool active; } QTracer;
static inline void qtracer_start(QTracer *t,V3 origin,V3 end,uint64_t now){
    *t=(QTracer){origin,end,now,true};
}
static inline float qtracer_opacity(const QTracer *t,uint64_t now){
    if(!t->active||now<t->started||now-t->started>=65)return 0;
    return 1.f-(float)(now-t->started)/65.f;
}
