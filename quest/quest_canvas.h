#pragma once
/* A small CPU canvas for the pause menu (quest_menu.c): anti-aliased shapes and Chakra Petch text (ui_font_data.h, baked by
 * tools/bake_ui_font.py) into straight-alpha RGBA8. Drawn only when the menu changes, then uploaded as one texture. */
#include <stdbool.h>
#include <stdint.h>

typedef struct QColor { uint8_t r,g,b,a; } QColor;
typedef struct QCanvas { uint8_t *px; int w,h; int cx0,cy0,cx1,cy1; int lx0,ly0,lx1,ly1; } QCanvas;   /* clip [cx0,cx1) x [cy0,cy1), within the limit l */

static inline QColor qrgb(int r,int g,int b){return (QColor){(uint8_t)r,(uint8_t)g,(uint8_t)b,255};}
static inline QColor qrgba(int r,int g,int b,int a){return (QColor){(uint8_t)r,(uint8_t)g,(uint8_t)b,(uint8_t)a};}

void qc_init(QCanvas *c,uint8_t *px,int w,int h);
void qc_clip(QCanvas *c,int x,int y,int w,int h);    /* intersected with the limit; qc_unclip restores the limit */
void qc_unclip(QCanvas *c);
void qc_limit(QCanvas *c,int x,int y,int w,int h);   /* nothing outside this box is touched (a partial redraw); qc_init: all */
void qc_fill(QCanvas *c,QColor color);               /* replaces every pixel, alpha included */
void qc_rrect(QCanvas *c,float x,float y,float w,float h,float radius,QColor color);
void qc_rrect_line(QCanvas *c,float x,float y,float w,float h,float radius,float width,QColor color);   /* a border inside the box */
void qc_circle(QCanvas *c,float cx,float cy,float r,QColor color);
void qc_poly(QCanvas *c,const float *xy,int n,QColor color);              /* even-odd fill of n points (x,y pairs) */
void qc_line(QCanvas *c,float x0,float y0,float x1,float y1,float width,QColor color);   /* round caps */
void qc_arc(QCanvas *c,float cx,float cy,float r,float a0,float a1,float width,QColor color);   /* radians, y down */

/* Text. font: a UI_* face from ui_font_data.h. y is the baseline. tracking adds pixels after each character. */
int  qc_text_width(int font,const char *utf8,int tracking);
int  qc_text(QCanvas *c,int font,float x,float y,const char *utf8,QColor color,int tracking);
/* Fits text into max_w, ending it with an ellipsis if it does not fit; align 0 left, 1 centre, 2 right of [x, x+max_w). */
int  qc_text_fit(QCanvas *c,int font,float x,float y,float max_w,const char *utf8,QColor color,int align);
int  qc_font_size(int font);
int  qc_font_ascent(int font);
