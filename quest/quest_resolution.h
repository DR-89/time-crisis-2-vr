#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Same per-eye presets as the NFSU VR ports. Zero preserves this port's
 * existing runtime-recommended resolution cap. Changes apply at startup. */
enum { QRESOLUTION_COUNT=6 };
static const uint32_t qresolution_sizes[QRESOLUTION_COUNT][2]={
    {0,0},{1280,1344},{1680,1760},{2550,2640},{2940,3080},{3360,3520}
};
static const char *const qresolution_labels[QRESOLUTION_COUNT]={
    "DEFAULT","1280 X 1344","1680 X 1760","2550 X 2640","2940 X 3080","3360 X 3520"
};
static inline bool qresolution_choose(int preset,uint32_t default_w,uint32_t default_h,
                                      uint32_t max_w,uint32_t max_h,uint32_t *w,uint32_t *h){
    *w=default_w<max_w?default_w:max_w;
    *h=default_h<max_h?default_h:max_h;
    if(preset==0)return true;
    if(preset<0||preset>=QRESOLUTION_COUNT)return false;
    uint32_t pw=qresolution_sizes[preset][0],ph=qresolution_sizes[preset][1];
    if(pw>max_w||ph>max_h)return false;
    *w=pw;*h=ph;return true;
}
