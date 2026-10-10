#include "tc2_scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *rom,*attr;static uint32_t *map;
static tc2_built *frames[2],*b;static int slot;
void tc2_render_assets(const uint8_t **r,const uint32_t **m,const uint8_t **a){*r=rom;*m=map;*a=attr;}
int tc2_depth_on(void){return 1;}
float tc2_depth_keep(void){return .99f;}
__declspec(dllexport) int tc2_fixture_init(const char *assets){
    FILE *f=fopen(assets,"rb");if(!f)return 0;
    rom=malloc(0x2000000);map=malloc(0x800000);attr=malloc(0x200000);
    int ok=fread(rom,1,0x2000000,f)==0x2000000&&fread(map,1,0x800000,f)==0x800000&&fread(attr,1,0x200000,f)==0x200000;
    fclose(f);frames[0]=malloc(sizeof *b);frames[1]=malloc(sizeof *b);
    return ok&&tc2_gl_init();
}
__declspec(dllexport) int tc2_fixture_load(const char *path,int flat){
    b=frames[slot^=1];FILE *f=fopen(path,"rb");if(!f)return 0;
    int ok=fread(b,1,sizeof *b,f)==sizeof *b&&fgetc(f)==EOF;fclose(f);
    if(!ok||b->npoly<0||b->npoly>TC2_MAX_POLYS||b->fs.list.count<0||b->fs.list.count>TC2_RENDER_MAX)return 0;
    for(int i=0;i<b->npoly;i++)if(b->order[i]>=(unsigned)b->npoly||b->polys[i].n<3||b->polys[i].n>8)return 0;
    qvr_flat_view=flat;qvr_scene_prepare(b);return 1;
}
__declspec(dllexport) int tc2_fixture_draw(int w,int h,float x,float y,float yaw,unsigned char *pixels){
    float v[16],p[16];view_matrix(v,v3(x,y,0),(Q4){0,sinf(yaw*.5f),0,cosf(yaw*.5f)});
    projection(p,-atanf(.64f),atanf(.64f),-atanf(.48f),atanf(.48f));
    void qtc2_draw(const float*,const float*,int,int,unsigned);qtc2_draw(v,p,w,h,0);
    return tc2_gl_read_full(pixels,w*h*3)==w;
}
__declspec(dllexport) void tc2_fixture_only_model(int model){
    int n=0;for(int i=0;i<b->npoly;i++)if(b->polys[i].model==model)b->polys[n++]=b->polys[i];
    b->npoly=n;for(int i=0;i<n;i++)b->order[i]=i;
    b->fx.layers=1;b->fx.bg=0x222733;qvr_scene_prepare(b);
}
__declspec(dllexport) int tc2_fixture_report(const char *path){
    FILE *f=fopen(path,"w");if(!f)return 0;
    fprintf(f,"{\"polygons\":%d,\"layers\":%d,\"fade\":%d,\"bands\":[",b->npoly,b->fx.layers,b->fx.ffactor);
    for(int y=0;y<480;y++)fprintf(f,"%s%u",y?",":"",(b->band[y]&0x8000)?b->fs.pens[b->band[y]&0x7fff]:0xffffffffu);
    fprintf(f,"],\"entries\":[");int written=0;
    for(int i=0;i<b->fs.list.count;i++){
        const tc2_render_entry *e=&b->fs.list.e[i];int count=0;float x0=1e30f,y0=x0,x1=-x0,y1=x1,z0=x0,z1=x1;const tc2_poly *first=NULL;
        for(int j=0;j<b->npoly;j++){
            const tc2_poly *p=&b->polys[j];if(p->entry!=i)continue;if(!first)first=p;count++;
            for(int k=0;k<p->n;k++){float x=p->pv[k].x+320-p->vp_size_x+p->vp_offset_x,y=p->pv[k].y+240-p->vp_size_y-p->vp_offset_y,z=1/p->pv[k].p[0];
                x0=fminf(x0,x);x1=fmaxf(x1,x);y0=fminf(y0,y);y1=fmaxf(y1,y);z0=fminf(z0,z);z1=fmaxf(z1,z);}
        }
        if(!count)continue;
        fprintf(f,"%s{\"entry\":%d,\"type\":%d,\"model\":%d,\"count\":%d,\"bounds\":[%.3f,%.3f,%.3f,%.3f],\"depth\":[%.5f,%.5f],\"priority\":%u,\"zkey\":%u,\"vp\":[%d,%d,%d,%d,%.3f],\"v\":[%d,%d,%d],\"matrix\":[",written++?",":"",i,e->type,first->model,count,x0,y0,x1,y1,z0,z1,e->absolute_priority,first->zkey,e->vp_size_x,e->vp_size_y,e->vp_offset_x,e->vp_offset_y,e->vp_fov,e->v[0],e->v[1],e->v[2]);
        for(int k=0;k<9;k++)fprintf(f,"%s%d",k?",":"",e->m[k]);fprintf(f,"]}");
    }
    fprintf(f,"]}");fclose(f);return 1;
}
