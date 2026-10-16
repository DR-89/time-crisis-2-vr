#include <assert.h>
#include <stdint.h>
#include "quest_frame.h"

int main(void) {
    uint32_t width, height;
    qframe_eye_dimensions(2880, 2880, 3072, 3072, 2160, &width, &height);
    assert(width == 2160 && height == 2160);
    qframe_eye_dimensions(1600, 1200, 2048, 1024, 2160, &width, &height);
    assert(width == 1365 && height == 1024);
    qframe_eye_dimensions(2000, 1000, 4096, 4096, 100, &width, &height);
    assert(width == 512 && height == 256);
    qframe_eye_dimensions(2000, 1000, 4096, 4096, 4096, &width, &height);
    assert(width == 2160 && height == 1080);
    qframe_eye_dimensions(2000, 1000, 100, 4096, 2160, &width, &height);
    assert(width == 100 && height == 50);
    qframe_eye_dimensions(0, 1000, 4096, 4096, 2160, &width, &height);
    assert(width == 0 && height == 0);
    return 0;
}
