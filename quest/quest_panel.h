#pragma once
#include <stdint.h>
#include "font_data.h"
enum { QP_WIDTH=640, QP_HEIGHT=480 };
static inline void qpanel_rect(uint8_t *pixels,int x,int y,int w,int h,uint8_t r,uint8_t g,uint8_t b) {
    for(int j=y;j<y+h && j<QP_HEIGHT;j++)for(int i=x;i<x+w && i<QP_WIDTH;i++) {
        if(i<0||j<0)continue;
        uint8_t *p=pixels+(j*QP_WIDTH+i)*4;p[0]=r;p[1]=g;p[2]=b;p[3]=255;
    }
}
static inline void qpanel_text(uint8_t *pixels,int x,int y,const char *s,uint8_t r,uint8_t g,uint8_t b) {
    for(;*s;s++,x+=16){
        unsigned c=(unsigned char)*s;if(c>='a'&&c<='z')c-='a'-'A';if(c<32||c>127)continue;
        const uint8_t *glyph=builtin_font+(c-32)*128;
        for(int row=0;row<16;row++)for(int col=0;col<16;col++){
            int px=x+col,py=y+row;if(px<0||px>=QP_WIDTH||py<0||py>=QP_HEIGHT)continue;
            uint8_t packed=glyph[row*8+col/2];int shade=col&1?packed&15:packed>>4;
            if(shade==15)continue;
            uint8_t *p=pixels+(py*QP_WIDTH+px)*4;int a=15-shade;
            p[0]=(r*a+p[0]*shade)/15;p[1]=(g*a+p[1]*shade)/15;p[2]=(b*a+p[2]*shade)/15;p[3]=255;
        }
    }
}
