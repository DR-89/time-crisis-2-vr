#pragma once
/* The pause menu: tabs (GUN, PLAYER, GRAPHICS, ONLINE) over a footer (RESUME, RESTART GAME, RECENTER, RESTART APP), driven by
 * the gun's laser and trigger or by the sticks, A and B. It draws on the CPU into a QMENU_W x QMENU_H RGBA texture
 * (quest_ui.c shows it), only when something on it changes. Nothing on any tab scrolls. */
#include <stdbool.h>
#include <stdint.h>

enum { QMENU_W=1120,QMENU_H=728 };
/* The settings it shows; quest_host.c owns and saves them. */
enum { QOPT_HAND,QOPT_AIM,QOPT_PITCH,QOPT_GUN_Y,QOPT_GUN_Z,QOPT_PLAYER,QOPT_COVER,QOPT_WORLD,QOPT_DISTANCE,QOPT_RESOLUTION,QOPT_FILTER,QOPT_COUNT };
enum { QACT_RESUME,QACT_RESTART_GAME,QACT_RECENTER,QACT_RESTART_APP };
typedef struct QMenuHost {
    int  (*get)(int option);
    void (*set)(int option,int value);
    void (*action)(int action);
    int  boot_player,boot_resolution;   /* what the app started with: Player and Resolution apply after RESTART APP */
    unsigned active_w,active_h;         /* the running per-eye size (0: a desktop window) */
    bool preset_unsupported;            /* the saved preset was too big for this headset: running the default */
    bool saved;                         /* the settings file was written */
    const char *talk_key;               /* push to talk's key, as the ONLINE tab names it ("STICK CLICK", "V"); NULL: a stick click */
} QMenuHost;

typedef struct QMenuInput {
    bool pointer;                       /* the laser (or mouse) is on the panel, at panel pixel px, py */
    float px,py;
    bool press,release,held;            /* the pointing hand's trigger / the mouse button */
    float stick_x,stick_y;              /* either thumbstick, the larger deflection (up is +) */
    int nav_x,nav_y;                    /* keyboard arrows: one step each (up is +) */
    bool select,back;                   /* A or X / Enter; B or Y / Backspace */
    int tab;                            /* -1 left grip / Shift-Tab, +1 right grip / Tab */
    double now;                         /* seconds */
} QMenuInput;

enum { QMENU_TAB_GUN,QMENU_TAB_PLAYER,QMENU_TAB_GRAPHICS,QMENU_TAB_ONLINE,QMENU_TAB_COUNT };
void qmenu_open(int tab);               /* the menu button: focus on RESUME; -1 keeps the last tab */
void qmenu_update(const QMenuHost *host,const QMenuInput *in);
/* Draws into pixels (QMENU_W*QMENU_H*4) if anything changed since the last call; returns true when it did, with the box it
 * changed in dirty (x, y, w, h; may be NULL). A pointer or highlight moving redraws only the few boxes it touches. */
bool qmenu_render(const QMenuHost *host,uint8_t *pixels,double now,int dirty[4]);
int  qmenu_tab(void);
bool qmenu_pointer(float *px,float *py);   /* the steadied laser point on the panel (menu pixels); false: none */
bool qmenu_modal(void);                 /* a confirm dialog or keyboard is open */
