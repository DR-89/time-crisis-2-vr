/* Recover the System 23 camera-space geometry before applying either eye pose.
 * OpenXR adds local head movement to the original game's camera. Text and
 * direct screen quads use the same optical camera at a distance of 2.5 m. */
#include "tc2_scene.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#ifdef TCVR_PC
#include "../tests/tc2_capture.h"
#endif
int qvr_flat_view;
float qvr_units_per_meter=1.f;
typedef struct {float cx,cy,f;} Camera;
typedef struct {V3 a,b,c;Camera cam;} Triangle;
static Triangle triangles[TC2_MAX_POLYS*6];
static int triangle_count;
static const tc2_built *frame;
static int letterbox_top,letterbox_bottom=480;
/* Time Crisis II's camera; retained through title screens with no polygons. */
static Camera main_camera={320,240,772.53125f};
static bool camera_valid(Camera c){return isfinite(c.f)&&c.f>1&&isfinite(c.cx)&&isfinite(c.cy);}
static Camera camera(const tc2_built *b,const tc2_poly *p){
    if(p->entry<0||p->entry>=b->fs.list.count)return main_camera;
    const tc2_render_entry *e=&b->fs.list.e[p->entry];
    Camera c={320.f+e->vp_offset_x,240.f-e->vp_offset_y,e->vp_fov};
    return camera_valid(c)?c:main_camera;
}
QTC2Layer qvr_tc2_layer(const tc2_built *b,const tc2_poly *p){
    if(p->entry<0||p->entry>=b->fs.list.count)return QTC2_OVERLAY;
    const tc2_render_entry *e=&b->fs.list.e[p->entry];
    if(e->type==TC2_RE_DIRECT)return QTC2_OVERLAY;
    if(e->type!=TC2_RE_MODEL)return QTC2_WORLD;
    /* US TSS3 HUD assets, checked against captured frames and the game code:
     * 3474 health, 3475/76 ammo, 3477 the animated ejected HUD cartridge,
     * 5077..82 player/COM markers, 6083 machine-gun ammunition icon.
     * Their tiny synthetic depths are screen layout, not world positions. */
    if((p->model>=3474&&p->model<=3477)||(p->model>=5077&&p->model<=5082)||p->model==6083)return QTC2_OVERLAY;
    if(p->model==3478){
        /* The same black mask asset also draws full-screen transitions.
         * Remove only wide rectangles entering from the top/bottom, never a
         * central/full-screen mask. Flatten any other use of the screen mask. */
        float x0=FLT_MAX,x1=-FLT_MAX,y0=FLT_MAX,y1=-FLT_MAX;
        for(int i=0;i<p->n;i++){
            float x=p->pv[i].x+320-p->vp_size_x+p->vp_offset_x,y=p->pv[i].y+240-p->vp_size_y-p->vp_offset_y;
            x0=fminf(x0,x);x1=fmaxf(x1,x);y0=fminf(y0,y);y1=fmaxf(y1,y);
        }
        if(x0<=1&&x1>=639&&((y0<=0&&y1>0&&y1<240)||(y0>240&&y0<480&&y1>=480)))return QTC2_LETTERBOX;
        return QTC2_OVERLAY;
    }
    return QTC2_WORLD;
}
static bool world(const tc2_built *b,const tc2_poly *p){return qvr_tc2_layer(b,p)==QTC2_WORLD;}
void qvr_tc2_letterbox(int *top,int *bottom){*top=letterbox_top;*bottom=letterbox_bottom;}
static V3 unproject(Camera c,float sx,float sy,float z){return v3((sx-c.cx)*z/c.f,(c.cy-sy)*z/c.f,-z);}
V3 qvr_tc2_vertex(const tc2_built *b,const tc2_poly *p,int index){
    const tc2_pvert *v=&p->pv[index];
    float sx=v->x+(320-p->vp_size_x)+p->vp_offset_x;
    float sy=v->y+(240-p->vp_size_y)-p->vp_offset_y;
    if(world(b,p)&&isfinite(v->p[0])&&v->p[0]>0)
        return unproject(camera(b,p),sx,sy,1.f/v->p[0]/qvr_units_per_meter);
    return unproject(main_camera,sx,sy,2.5f);
}
bool qvr_flat_camera(float *cx,float *cy,float *focal){
    Camera c=qvr_flat_view?(Camera){320,240,500}:main_camera;
    *cx=c.cx;*cy=c.cy;*focal=c.f;return !qvr_flat_view;
}
const tc2_built *qvr_current_frame(void){return frame;}
void qvr_scene_prepare(const tc2_built *b){
    frame=b;triangle_count=0;letterbox_top=0;letterbox_bottom=480;if(!b)return;
#ifdef TCVR_PC
    static unsigned capture_frame;tc2_capture_frame(b,++capture_frame);
#endif
    static bool init;
    if(!init){init=true;const char *s=getenv("TC2_UNITS_PER_METER");if(s){float v=strtof(s,NULL);if(isfinite(v)&&v>.0001f)qvr_units_per_meter=v;}}
    struct {Camera cam;unsigned count;} candidates[64];int nc=0;
    for(int i=0;i<b->npoly;i++){
        const tc2_poly *p=&b->polys[i];if(p->n<3||p->n>8)continue;
        QTC2Layer layer=qvr_tc2_layer(b,p);
        if(layer==QTC2_LETTERBOX){
            float y0=FLT_MAX,y1=-FLT_MAX;
            for(int j=0;j<p->n;j++){float y=p->pv[j].y+240-p->vp_size_y-p->vp_offset_y;y0=fminf(y0,y);y1=fmaxf(y1,y);}
            if(y1<240)letterbox_top=(int)fmaxf(letterbox_top,ceilf(y1));
            if(y0>240)letterbox_bottom=(int)fminf(letterbox_bottom,floorf(y0));
        }
        if(layer!=QTC2_WORLD)continue;
        Camera c=camera(b,p);int k;
        for(k=0;k<nc;k++)if(!memcmp(&c,&candidates[k].cam,sizeof c))break;
        if(k==nc&&nc<64){candidates[nc].cam=c;candidates[nc++].count=0;}
        if(k<nc)candidates[k].count+=(unsigned)(p->n-2);
    }
    unsigned best=0;for(int i=0;i<nc;i++)if(candidates[i].count>best){main_camera=candidates[i].cam;best=candidates[i].count;}
    float low=FLT_MAX,high=0;
    for(int i=0;i<b->npoly;i++){
        const tc2_poly *p=&b->polys[i];if(!world(b,p)||p->n<3||p->n>8)continue;
        V3 v[8];bool good=true;
        for(int j=0;j<p->n;j++){
            v[j]=qvr_tc2_vertex(b,p,j);
            if(!isfinite(v[j].x)||!isfinite(v[j].y)||!isfinite(v[j].z)||v[j].z>=0)good=false;
            low=fminf(low,-v[j].z);high=fmaxf(high,-v[j].z);
        }
        if(!good)continue;
        for(int j=1;j+1<p->n&&triangle_count<(int)(sizeof triangles/sizeof *triangles);j++)
            triangles[triangle_count++]=(Triangle){v[0],v[j],v[j+1],camera(b,p)};
    }
    static unsigned frames;
    if(++frames%300==0)fprintf(stderr,"[TC2VR] geometry polygons=%d triangles=%d camera=%.3f,%.3f,%.3f depth=%.3f..%.3f m units/m=%.3f\n",b->npoly,triangle_count,main_camera.cx,main_camera.cy,main_camera.f,triangle_count?low:0,high,qvr_units_per_meter);
}
bool qvr_aim(V3 o,V3 d,float *nx,float *ny,V3 *hit){
    float nearest=FLT_MAX;Camera c=main_camera;bool found=false;
    if(!qvr_flat_view)for(int i=0;i<triangle_count;i++){
        Triangle *t=&triangles[i];float distance=nearest;
        if(ray_triangle(o,d,t->a,t->b,t->c,&distance)&&distance<nearest){nearest=distance;c=t->cam;found=true;}
    }
    if(found)*hit=add(o,mul(d,nearest));
    else {
        if(d.z>=-1e-5f)return false;
        float t=(-2.5f-o.z)/d.z;if(t<=0)return false;
        *hit=add(o,mul(d,t));
        if(qvr_flat_view)c=(Camera){320,240,500};
    }
    if(hit->z>=-1e-6f)return false;
    *nx=(c.cx+c.f*hit->x/-hit->z)/640.f;*ny=(c.cy-c.f*hit->y/-hit->z)/480.f;
    return *nx>=0&&*nx<=1&&*ny>=0&&*ny<=1;
}
