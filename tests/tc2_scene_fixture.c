#include "tc2_scene.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static void near(float actual,float expected){if(fabsf(actual-expected)>=0.0001f){fprintf(stderr,"actual %.8f expected %.8f\n",actual,expected);abort();}}
int main(void){
    tc2_built *b=calloc(1,sizeof *b);assert(b);
    b->fs.list.count=1;
    tc2_render_entry *e=&b->fs.list.e[0];
    e->type=TC2_RE_MODEL;e->vp_fov=800;e->vp_size_x=320;e->vp_size_y=240;e->vp_offset_x=20;e->vp_offset_y=-10;
    b->npoly=1;tc2_poly *p=&b->polys[0];p->n=3;p->entry=0;p->vp_size_x=320;p->vp_size_y=240;p->vp_offset_x=20;p->vp_offset_y=-10;
    float xy[3][2]={{240,160},{400,160},{320,320}};
    for(int i=0;i<3;i++){p->pv[i].x=xy[i][0];p->pv[i].y=xy[i][1];p->pv[i].p[0]=.1f;}
    qvr_scene_prepare(b);float cx,cy,f;assert(qvr_flat_camera(&cx,&cy,&f));near(cx,340);near(cy,250);near(f,800);
    for(int i=0;i<3;i++){
        V3 v=qvr_tc2_vertex(b,p,i);near(-v.z,10);
        near(cx+f*v.x/-v.z,xy[i][0]+20);near(cy-f*v.y/-v.z,xy[i][1]+10);
    }
    float nx,ny;V3 hit;
    assert(qvr_aim(v3(0,0,0),v3(0,0,-1),&nx,&ny,&hit));near(-hit.z,10);near(nx,340.f/640);near(ny,250.f/480);
    // A gun offset from the eye still maps to the actual intersection, not to
    // a point on the 2.5-m HUD plane.
    assert(qvr_aim(v3(.2f,0,0),v3(0,0,-1),&nx,&ny,&hit));near(-hit.z,10);near(nx,356.f/640);
    // No polygons: preserve the optical camera for menus and the fallback ray.
    b->npoly=0;qvr_scene_prepare(b);assert(qvr_aim(v3(.2f,0,0),v3(0,0,-1),&nx,&ny,&hit));near(-hit.z,2.5f);near(nx,404.f/640);
    assert(!qvr_aim(v3(0,0,0),v3(0,0,1),&nx,&ny,&hit));
    qvr_flat_view=1;assert(!qvr_flat_camera(&cx,&cy,&f));near(f,500);
    assert(qvr_aim(v3(0,0,0),v3(0,0,-1),&nx,&ny,&hit));near(nx,.5f);near(ny,.5f);
    qvr_flat_view=0;e->type=TC2_RE_DIRECT;V3 flat=qvr_tc2_vertex(b,p,0);near(-flat.z,2.5f);
    // HUD model depths must not detach health/ammo from the text-layer frames,
    // nor intercept aiming rays. A neighbouring world model retains its depth.
    e->type=TC2_RE_MODEL;b->npoly=1;
    const int hud_models[]={3474,3475,3476,6083};
    for(unsigned i=0;i<sizeof hud_models/sizeof *hud_models;i++){
        int model=hud_models[i];
        p->model=model;qvr_scene_prepare(b);flat=qvr_tc2_vertex(b,p,0);near(-flat.z,2.5f);
        assert(qvr_flat_camera(&cx,&cy,&f));near(cx+f*flat.x/-flat.z,xy[0][0]+20);
        assert(qvr_aim(v3(0,0,0),v3(0,0,-1),&nx,&ny,&hit));near(-hit.z,2.5f);
    }
    p->model=3473;qvr_scene_prepare(b);near(-qvr_tc2_vertex(b,p,0).z,10);
    free(b);puts("TC2 camera, stereo geometry, surface aim, direct layer and empty-space projection passed");return 0;
}
