#pragma once
#include <stdbool.h>
#include "vr_math.h"
typedef struct QCover { float upright_y;bool calibrated,ducked; } QCover;
void qcover_calibrate(QCover *cover,float head_y);
/* True is the original arcade pedal: leave cover. Invalid tracking releases it. */
bool qcover_pedal(QCover *cover,float head_y,bool tracked);
/* Physical cover at a known slide: the head must travel `qcover_reach` metres
 * from the calibrated upright point along the slide's direction (the SAFE
 * mark). `displacement` is head minus upright point in the same VR frame as
 * `hide`. `approach` (optional) receives 0..1 progress toward the mark. */
float qcover_reach(V3 hide);
bool qcover_spot_pedal(QCover *cover,V3 hide,V3 displacement,bool tracked,float *approach);
