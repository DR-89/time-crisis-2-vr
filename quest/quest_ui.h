#pragma once
#include "vr_math.h"
#include "quest_options.h"
bool qui_init(void);
/* Actual startup size is distinct from the saved selection pending a restart. */
void qui_set_resolution(int preset,unsigned width,unsigned height);
void qui_draw(const float view[16],const float projection[16],V3 head,Q4 rotation,const QOptions *options,int page,int boot_side,bool saved);
void qui_shutdown(void);
/* The ONLINE page's content, supplied by the host each frame (tc2_online_menu_lines); styles as TC2_LINE_*. */
enum { QUI_ONLINE_LINES=15,QUI_ONLINE_COLUMNS=40 };
void qui_set_online_page(char text[QUI_ONLINE_LINES][QUI_ONLINE_COLUMNS],const int style[QUI_ONLINE_LINES],unsigned serial);
/* A one-line status banner below the view centre (online events); serial changes with the text. */
void qui_draw_toast(const float view[16],const float projection[16],V3 head,Q4 rotation,const char *text,unsigned serial);
