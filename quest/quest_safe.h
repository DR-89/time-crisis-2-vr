#pragma once
#include <stdbool.h>
#include "vr_math.h"
/* The physical-cover SAFE signpost: a red plate with blinking yellow SAFE and
 * a yellow arrow pointing along the cover slide. Drawn in the host's VR frame. */
bool qsafe_init(void);
/* centre: plate centre; arrow_radians: arrow direction in the sign plane
 * (0 right, pi/2 up); approach 0..1 tints the arrow toward green. */
void qsafe_draw(const float view[16],const float projection[16],V3 centre,float arrow_radians,float approach,bool text_on);
void qsafe_shutdown(void);
/* The 256x192 RGBA atlas (plate, text, arrow rows of 64), exposed for tests. */
void qsafe_paint(unsigned char *rgba);
