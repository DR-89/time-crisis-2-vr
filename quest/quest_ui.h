#pragma once
#include "vr_math.h"
#include "quest_menu.h"
/* The pause menu's panel in the world (the menu itself is quest_menu.c) and the online toast. */
#define QUI_PANEL_W 1.4f                                   /* metres; the height follows the menu's aspect */
#define QUI_PANEL_H (QUI_PANEL_W*QMENU_H/QMENU_W)
bool qui_init(void);
void qui_shutdown(void);
/* The panel: centre and orientation in the scene (the player stands on its +Z side). Uploads its texture only when the menu
 * redrew. */
void qui_draw_menu(const float view[16],const float projection[16],V3 center,Q4 rotation,const QMenuHost *host,double now);
/* Where a ray meets the panel, in menu pixels; false when it misses (or points away). The player is on the panel's +Z side. */
static inline bool qui_panel_hit(V3 origin,V3 direction,V3 center,Q4 rotation,float *px,float *py,V3 *point){
    const Q4 inv=conjugate(rotation);
    const V3 o=rotate(inv,sub(origin,center)),d=rotate(inv,direction);
    if(d.z>=-1e-4f||o.z<=0)return false;
    const float t=-o.z/d.z,x=o.x+d.x*t,y=o.y+d.y*t;
    *px=(x/QUI_PANEL_W+.5f)*QMENU_W;*py=(.5f-y/QUI_PANEL_H)*QMENU_H;
    if(point)*point=add(origin,mul(direction,t));
    return *px>=0&&*px<QMENU_W&&*py>=0&&*py<QMENU_H;
}
/* A one-line status banner below the view centre (online events); serial changes with the text. */
void qui_draw_toast(const float view[16],const float projection[16],V3 head,Q4 rotation,const char *text,unsigned serial);
