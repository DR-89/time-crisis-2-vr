#pragma once

#include <math.h>
#include <stdint.h>

/* Scale the runtime's recommended eye image to a bounded Frame target while
 * preserving its aspect ratio and staying within its reported image limits. */
static inline void qframe_eye_dimensions(uint32_t recommended_width,
                                         uint32_t recommended_height,
                                         uint32_t maximum_width,
                                         uint32_t maximum_height,
                                         int target_max,
                                         uint32_t *width,
                                         uint32_t *height) {
    if (target_max < 512) target_max = 512;
    if (target_max > 2160) target_max = 2160;
    if (!recommended_width || !recommended_height || !maximum_width ||
        !maximum_height) {
        *width = *height = 0;
        return;
    }
    float scale = (float)target_max / fmaxf(recommended_width, recommended_height);
    float supported = fminf((float)maximum_width / recommended_width,
                            (float)maximum_height / recommended_height);
    scale = fminf(scale, supported);
    *width = (uint32_t)floorf(recommended_width * scale);
    *height = (uint32_t)floorf(recommended_height * scale);
    if (*width > maximum_width) *width = maximum_width;
    if (*height > maximum_height) *height = maximum_height;
}
