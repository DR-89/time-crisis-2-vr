/* quest_canvas.c -- see quest_canvas.h. Shapes use a signed distance (rounded boxes, circles) or 4x4 supersampling (polygons)
 * for their edges; text blends the baked 4-bit glyph coverage. Everything blends as straight alpha ("over"). */
#include "quest_canvas.h"
#include "ui_font_data.h"
#include <math.h>
#include <string.h>

void qc_init(QCanvas *c,uint8_t *px,int w,int h){c->px=px;c->w=w;c->h=h;qc_limit(c,0,0,w,h);}
void qc_limit(QCanvas *c,int x,int y,int w,int h){
    c->lx0=x>0?x:0;c->ly0=y>0?y:0;c->lx1=x+w<c->w?x+w:c->w;c->ly1=y+h<c->h?y+h:c->h;
    if(c->lx1<c->lx0)c->lx1=c->lx0;if(c->ly1<c->ly0)c->ly1=c->ly0;
    qc_unclip(c);
}
void qc_unclip(QCanvas *c){c->cx0=c->lx0;c->cy0=c->ly0;c->cx1=c->lx1;c->cy1=c->ly1;}
void qc_clip(QCanvas *c,int x,int y,int w,int h){
    qc_unclip(c);
    if(x>c->cx0)c->cx0=x;if(y>c->cy0)c->cy0=y;if(x+w<c->cx1)c->cx1=x+w;if(y+h<c->cy1)c->cy1=y+h;
    if(c->cx1<c->cx0)c->cx1=c->cx0;if(c->cy1<c->cy0)c->cy1=c->cy0;
}
void qc_fill(QCanvas *c,QColor k){
    for(int y=c->cy0;y<c->cy1;y++)for(int x=c->cx0;x<c->cx1;x++){uint8_t *p=c->px+((size_t)y*c->w+x)*4;p[0]=k.r;p[1]=k.g;p[2]=k.b;p[3]=k.a;}
}
static inline void blend(QCanvas *c,int x,int y,QColor k,float cover){
    if(x<c->cx0||x>=c->cx1||y<c->cy0||y>=c->cy1||cover<=0)return;
    uint8_t *p=c->px+((size_t)y*c->w+x)*4;
    if(cover>=1&&k.a==255){p[0]=k.r;p[1]=k.g;p[2]=k.b;p[3]=255;return;}   /* the common case: a solid pixel */
    if(cover>1)cover=1;
    /* straight alpha "over", in 8-bit fixed point */
    const int a=(int)(k.a*cover+.5f);if(!a)return;
    const int da=p[3],ia=255-a,oa=a*255+da*ia;   /* x 255 */
    if(!oa)return;
    p[0]=(uint8_t)((k.r*a*255+p[0]*da*ia+oa/2)/oa);p[1]=(uint8_t)((k.g*a*255+p[1]*da*ia+oa/2)/oa);p[2]=(uint8_t)((k.b*a*255+p[2]*da*ia+oa/2)/oa);
    p[3]=(uint8_t)((oa+127)/255);
}
static void bounds(QCanvas *c,float x0,float y0,float x1,float y1,int *ix0,int *iy0,int *ix1,int *iy1){
    *ix0=(int)floorf(x0)-1;*iy0=(int)floorf(y0)-1;*ix1=(int)ceilf(x1)+1;*iy1=(int)ceilf(y1)+1;
    if(*ix0<c->cx0)*ix0=c->cx0;if(*iy0<c->cy0)*iy0=c->cy0;if(*ix1>c->cx1)*ix1=c->cx1;if(*iy1>c->cy1)*iy1=c->cy1;
}
/* distance from a pixel centre to a rounded box (negative inside) */
static inline float rbox(float px,float py,float cx,float cy,float hw,float hh,float r){
    float qx=fabsf(px-cx)-(hw-r),qy=fabsf(py-cy)-(hh-r);
    float ox=qx>0?qx:0,oy=qy>0?qy:0,in=qx>qy?qx:qy;if(in>0)in=0;
    return sqrtf(ox*ox+oy*oy)+in-r;
}
/* Rows clear of the corners need no distance field: coverage is the nearer straight edge (and is 1 in between). */
void qc_rrect(QCanvas *c,float x,float y,float w,float h,float r,QColor k){
    if(w<=0||h<=0)return;
    if(r>w/2)r=w/2;if(r>h/2)r=h/2;if(r<0)r=0;
    int x0,y0,x1,y1;bounds(c,x,y,x+w,y+h,&x0,&y0,&x1,&y1);
    const float cx=x+w/2,cy=y+h/2;
    for(int py=y0;py<y1;py++){
        const float fy=py+.5f;
        if(fy>=y+r&&fy<=y+h-r){
            const float vy=fminf(fy-y,y+h-fy);
            for(int px=x0;px<x1;px++){const float fx=px+.5f;blend(c,px,py,k,.5f+fminf(vy,fminf(fx-x,x+w-fx)));}
            continue;
        }
        for(int px=x0;px<x1;px++){
            const float fx=px+.5f;
            if(fx>=x+r&&fx<=x+w-r){blend(c,px,py,k,.5f+fminf(fy-y,y+h-fy));continue;}
            float d=rbox(fx,fy,cx,cy,w/2,h/2,r);
            if(d<.5f)blend(c,px,py,k,.5f-d);
        }
    }
}
/* A border: only the pixels within reach of the edge. */
void qc_rrect_line(QCanvas *c,float x,float y,float w,float h,float r,float t,QColor k){
    if(w<=0||h<=0)return;
    if(r>w/2)r=w/2;if(r>h/2)r=h/2;
    float ri=r-t;if(ri<0)ri=0;
    int x0,y0,x1,y1;bounds(c,x,y,x+w,y+h,&x0,&y0,&x1,&y1);
    const float cx=x+w/2,cy=y+h/2,reach=(r>t?r:t)+2;
    for(int py=y0;py<y1;py++){
        const float fy=py+.5f;const bool band=fy<y+reach||fy>y+h-reach;
        for(int px=x0;px<x1;px++){
            if(!band&&px>=x+reach&&px+1<x+w-reach){const int skip=(int)(x+w-reach)-1;if(skip>px)px=skip;continue;}   /* the hollow middle */
            const float fx=px+.5f;
            float o=.5f-rbox(fx,fy,cx,cy,w/2,h/2,r);if(o<=0)continue;if(o>1)o=1;
            float i=.5f-rbox(fx,fy,cx,cy,w/2-t,h/2-t,ri);if(i<0)i=0;if(i>1)i=1;
            blend(c,px,py,k,o-i);
        }
    }
}
void qc_circle(QCanvas *c,float cx,float cy,float r,QColor k){
    int x0,y0,x1,y1;bounds(c,cx-r,cy-r,cx+r,cy+r,&x0,&y0,&x1,&y1);
    for(int py=y0;py<y1;py++)for(int px=x0;px<x1;px++){
        float dx=px+.5f-cx,dy=py+.5f-cy,d=sqrtf(dx*dx+dy*dy)-r;
        if(d<.5f)blend(c,px,py,k,.5f-d);
    }
}
static bool inside(const float *xy,int n,float px,float py){
    bool in=false;
    for(int i=0,j=n-1;i<n;j=i++){
        float xi=xy[i*2],yi=xy[i*2+1],xj=xy[j*2],yj=xy[j*2+1];
        if((yi>py)!=(yj>py)&&px<(xj-xi)*(py-yi)/(yj-yi)+xi)in=!in;
    }
    return in;
}
void qc_poly(QCanvas *c,const float *xy,int n,QColor k){
    if(n<3)return;
    float mx=xy[0],my=xy[1],Mx=xy[0],My=xy[1];
    for(int i=1;i<n;i++){if(xy[i*2]<mx)mx=xy[i*2];if(xy[i*2]>Mx)Mx=xy[i*2];if(xy[i*2+1]<my)my=xy[i*2+1];if(xy[i*2+1]>My)My=xy[i*2+1];}
    int x0,y0,x1,y1;bounds(c,mx,my,Mx,My,&x0,&y0,&x1,&y1);
    for(int py=y0;py<y1;py++)for(int px=x0;px<x1;px++){
        int hits=0;
        for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++)hits+=inside(xy,n,px+(sx+.5f)/4,py+(sy+.5f)/4);
        if(hits)blend(c,px,py,k,hits/16.f);
    }
}
void qc_line(QCanvas *c,float x0,float y0,float x1,float y1,float width,QColor k){
    /* distance to the segment: one pass, so overlapping caps never double-blend */
    const float r=width/2,dx=x1-x0,dy=y1-y0,len2=dx*dx+dy*dy;
    int ix0,iy0,ix1,iy1;bounds(c,fminf(x0,x1)-r,fminf(y0,y1)-r,fmaxf(x0,x1)+r,fmaxf(y0,y1)+r,&ix0,&iy0,&ix1,&iy1);
    for(int py=iy0;py<iy1;py++)for(int px=ix0;px<ix1;px++){
        float qx=px+.5f-x0,qy=py+.5f-y0,t=len2>0?(qx*dx+qy*dy)/len2:0;if(t<0)t=0;if(t>1)t=1;
        float ex=qx-t*dx,ey=qy-t*dy,d=sqrtf(ex*ex+ey*ey)-r;
        if(d<.5f)blend(c,px,py,k,.5f-d);
    }
}
void qc_arc(QCanvas *c,float cx,float cy,float r,float a0,float a1,float width,QColor k){
    /* distance to the arc (or to its nearer end): one pass */
    const float hw=width/2;
    int x0,y0,x1,y1;bounds(c,cx-r-hw,cy-r-hw,cx+r+hw,cy+r+hw,&x0,&y0,&x1,&y1);
    if(a1<a0){float t=a0;a0=a1;a1=t;}
    const float ex0=cx+cosf(a0)*r,ey0=cy+sinf(a0)*r,ex1=cx+cosf(a1)*r,ey1=cy+sinf(a1)*r;
    for(int py=y0;py<y1;py++)for(int px=x0;px<x1;px++){
        float dx=px+.5f-cx,dy=py+.5f-cy,a=atan2f(dy,dx);
        while(a<a0)a+=6.2831853f;
        float d;
        if(a<=a1)d=fabsf(sqrtf(dx*dx+dy*dy)-r);
        else{float d0=hypotf(px+.5f-ex0,py+.5f-ey0),d1=hypotf(px+.5f-ex1,py+.5f-ey1);d=d0<d1?d0:d1;}
        d-=hw;
        if(d<.5f)blend(c,px,py,k,.5f-d);
    }
}

/* ---- text ---- */
static unsigned next_cp(const char **s){
    const unsigned char *p=(const unsigned char*)*s;unsigned cp=*p++;
    if(cp>=0xF0&&p[0]&&p[1]&&p[2]){cp=(cp&7)<<18|(p[0]&63)<<12|(p[1]&63)<<6|(p[2]&63);p+=3;}
    else if(cp>=0xE0&&p[0]&&p[1]){cp=(cp&15)<<12|(p[0]&63)<<6|(p[1]&63);p+=2;}
    else if(cp>=0xC0&&p[0]){cp=(cp&31)<<6|(p[0]&63);p+=1;}
    *s=(const char*)p;return cp;
}
static const UiGlyph *glyph(int font,unsigned cp){
    if(font<0||font>=UI_FONT_COUNT)return NULL;
    const UiFontData *f=&ui_fonts[font];
    if(cp>=32&&cp<127&&f->count>=95&&ui_glyphs[f->first+cp-32].cp==cp)return &ui_glyphs[f->first+cp-32];
    for(int i=0;i<f->count;i++)if(ui_glyphs[f->first+i].cp==cp)return &ui_glyphs[f->first+i];
    if(cp>='a'&&cp<='z')return glyph(font,cp-32);   /* the big faces carry capitals only */
    return NULL;
}
int qc_font_size(int font){return font>=0&&font<UI_FONT_COUNT?ui_fonts[font].size:0;}
int qc_font_ascent(int font){return font>=0&&font<UI_FONT_COUNT?ui_fonts[font].ascent:0;}
int qc_text_width(int font,const char *s,int tracking){
    int w16=0,n=0;
    while(s&&*s){const UiGlyph *g=glyph(font,next_cp(&s));if(g){w16+=g->advance;n++;}}
    return (w16+8)/16+(n>0?(n-1)*tracking:0);
}
int qc_text(QCanvas *c,int font,float x,float y,const char *s,QColor k,int tracking){
    float pen=x;const float start=x;
    while(s&&*s){
        const UiGlyph *g=glyph(font,next_cp(&s));if(!g)continue;
        const int gx=(int)floorf(pen+.5f)+g->x0,gy=(int)floorf(y+.5f)+g->y0,stride=(g->w+1)/2;
        for(int row=0;row<g->h;row++){
            const uint8_t *src=ui_font_bits+g->offset+(size_t)row*stride;
            for(int col=0;col<g->w;col++){
                const int a=col&1?src[col/2]&15:src[col/2]>>4;
                if(a)blend(c,gx+col,gy+row,k,a/15.f);
            }
        }
        pen+=g->advance/16.f+tracking;
    }
    return (int)(pen-start+.5f);
}
int qc_text_fit(QCanvas *c,int font,float x,float y,float max_w,const char *s,QColor k,int align){
    char buf[160];
    size_t n=strlen(s);if(n>=sizeof buf)n=sizeof buf-1;memcpy(buf,s,n);buf[n]=0;
    int w=qc_text_width(font,buf,0);
    if(w>max_w){
        const char *dots="\xE2\x80\xA6";
        while(n>0){
            do n--;while(n>0&&((unsigned char)buf[n]&0xC0)==0x80);   /* whole UTF-8 characters */
            buf[n]=0;
            while(n>0&&buf[n-1]==' ')buf[--n]=0;
            char t[164];memcpy(t,buf,n);memcpy(t+n,dots,4);
            if(qc_text_width(font,t,0)<=max_w){memcpy(buf+n,dots,4);break;}
        }
        w=qc_text_width(font,buf,0);
    }
    float at=align==1?x+(max_w-w)/2:align==2?x+max_w-w:x;
    return qc_text(c,font,at,y,buf,k,0);
}
