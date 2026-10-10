#pragma once
#include "vr_math.h"

/* Asset coordinates are metres, +Y up, muzzle facing -Z; origin is the aim pose. */
typedef enum { QGUN_PISTOL, QGUN_MACHINE_GUN, QGUN_COUNT } QGunKind;
bool qgun_init(const char *path);
bool qgun_load_weapon(QGunKind kind,const char *path);
void qgun_select(QGunKind kind);
QGunKind qgun_selected(void);
void qgun_draw(const float view[16],const float projection[16],V3 position,Q4 rotation,float recoil,float trigger_pull,float flash);
V3 qgun_muzzle(V3 position,Q4 rotation);
void qgun_shutdown(void);

/* Fast slide kick, then a smooth spring return; independent of display rate. */
static inline float qgun_recoil(float age_ms){
    if(age_ms<0||age_ms>=150)return 0;
    if(age_ms<18)return sinf(age_ms*(1.570796327f/18.f));
    float t=(age_ms-18.f)/132.f;return 1-t*t*(3-2*t);
}

/* Short local muzzle flash, driven by accepted arcade shots, never by the
 * trigger alone. A shared age is used for both eyes in each headset frame. */
static inline float qgun_flash(float age_ms){
    if(age_ms<0||age_ms>=42)return 0;
    float t=1-age_ms/42.f;return t*t;
}
