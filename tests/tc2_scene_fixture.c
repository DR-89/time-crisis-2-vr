#include "tc2_scene.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static void near(float actual,float expected){if(fabsf(actual-expected)>=0.0001f){fprintf(stderr,"actual %.8f expected %.8f\n",actual,expected);abort();}}
static void calibrated_height(tc2_built *b,float foot_x,float foot_y,float foot_rhw,float head_x,float head_y,float head_rhw,float expected){
    tc2_poly *p=&b->polys[0];p->entry=0;p->model=5758;p->n=3;p->vp_size_x=320;p->vp_size_y=240;
    p->pv[0].x=foot_x;p->pv[0].y=foot_y;p->pv[0].p[0]=foot_rhw;
    p->pv[1].x=head_x;p->pv[1].y=head_y;p->pv[1].p[0]=head_rhw;
    V3 foot=qvr_tc2_vertex(b,p,0),head=qvr_tc2_vertex(b,p,1);near(head.y-foot.y,expected);
}
int main(void){
    tc2_built *b=calloc(1,sizeof *b);assert(b);
    b->fs.list.count=1;
    tc2_render_entry *e=&b->fs.list.e[0];
    e->type=TC2_RE_MODEL;e->vp_fov=772.53125f;
    /* Actual full-model extrema, local stage-one replay frame 1500. Include
     * both boots and heads, even when outside the arcade screen's viewport. */
    calibrated_height(b,532.774841f,829.517822f,.992428362f,417.978699f,-31.4299011f,.95846498f,1.70324707f);
    calibrated_height(b,-609.519226f,646.289917f,.680031538f,-677.273743f,-3.93791199f,.649282694f,1.88955684f);
    calibrated_height(b,-462.639282f,463.456726f,.363959491f,-463.522888f,103.261871f,.377207309f,1.89596544f);
    /* Frame 1800's complete low bench/sofa (model 2310). */
    calibrated_height(b,85.5061646f,461.581055f,.303947747f,-158.117188f,415.461426f,.418161869f,.600769044f);
    qvr_units_per_meter=1.f; /* Synthetic geometry below is already in metres. */
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
    // Paused edits reuse the original cache. Oblique, translated rays and
    // rendered vertices must meet at the same point throughout the slider.
    for(int distance=0;distance<=100;distance+=20)for(int percent=25;percent<=400;percent+=25){
        qvr_view_distance=distance/100.f;
        float scale=percent/100.f;qvr_world_scale=scale;
        V3 v=qvr_tc2_vertex(b,p,0);near(-v.z,10*scale+qvr_view_distance);near(cx+f*v.x/(-v.z-qvr_view_distance),xy[0][0]+20);
        V3 origin=v3(.04f,.01f,.1f),dir=v3(.006f,-.002f,-1);float t=10*scale+qvr_view_distance+origin.z;
        assert(qvr_aim(origin,dir,&nx,&ny,&hit));near(-hit.z,10*scale+qvr_view_distance);near(hit.x,origin.x+dir.x*t);near(hit.y,origin.y+dir.y*t);
        near(nx,(cx+f*hit.x/(10*scale))/640);near(ny,(cy-f*hit.y/(10*scale))/480);
        /* Physical 64-mm eye baseline stays unchanged across world settings. */
        float left=f*(v.x+.032f)/-v.z,right=f*(v.x-.032f)/-v.z;
        near(left-right,f*.064f/(10*scale+qvr_view_distance));
    }
    // Leave scale at 400% to verify all overlays/fallbacks keep their depth.
    // No polygons: preserve the optical camera for menus and the fallback ray.
    b->npoly=0;qvr_scene_prepare(b);assert(qvr_aim(v3(.2f,0,0),v3(0,0,-1),&nx,&ny,&hit));near(-hit.z,2.5f);near(nx,404.f/640);
    assert(!qvr_aim(v3(0,0,0),v3(0,0,1),&nx,&ny,&hit));
    qvr_flat_view=1;assert(!qvr_flat_camera(&cx,&cy,&f));near(f,500);
    assert(qvr_aim(v3(0,0,0),v3(0,0,-1),&nx,&ny,&hit));near(nx,.5f);near(ny,.5f);
    qvr_flat_view=0;e->type=TC2_RE_DIRECT;V3 flat=qvr_tc2_vertex(b,p,0);near(-flat.z,2.5f);
    // HUD model depths must not detach health/ammo from the text-layer frames,
    // nor intercept aiming rays. A neighbouring world model retains its depth.
    e->type=TC2_RE_MODEL;b->npoly=1;
    const int hud_models[]={3474,3475,3476,3477,5077,5078,5079,5080,5081,5082,6083};
    for(unsigned i=0;i<sizeof hud_models/sizeof *hud_models;i++){
        int model=hud_models[i];
        p->model=model;qvr_scene_prepare(b);flat=qvr_tc2_vertex(b,p,0);near(-flat.z,2.5f);
        assert(qvr_flat_camera(&cx,&cy,&f));near(cx+f*flat.x/-flat.z,xy[0][0]+20);
        assert(qvr_aim(v3(0,0,0),v3(0,0,-1),&nx,&ny,&hit));near(-hit.z,2.5f);
    }
    p->model=3473;qvr_scene_prepare(b);near(-qvr_tc2_vertex(b,p,0).z,41);
    // Only the known wide edge masks are letterboxing. A full-screen mask
    // remains visible for fades; similarly shaped world polygons are retained.
    p->model=3478;p->n=4;p->vp_offset_x=p->vp_offset_y=0;
    const float bar[4][2]={{-.631f,-175.971f},{640.631f,-175.971f},{640.631f,64.502f},{-.631f,64.502f}};
    for(int i=0;i<4;i++){p->pv[i].x=bar[i][0];p->pv[i].y=bar[i][1];p->pv[i].p[0]=32;}
    assert(qvr_tc2_layer(b,p)==QTC2_LETTERBOX);qvr_scene_prepare(b);
    int top,bottom;qvr_tc2_letterbox(&top,&bottom);assert(top==65&&bottom==480);
    assert(qvr_aim(v3(0,0,0),v3(0,0,-1),&nx,&ny,&hit));near(-hit.z,2.5f);
    for(int i=0;i<4;i++)p->pv[i].y=480-bar[i][1];
    qvr_scene_prepare(b);qvr_tc2_letterbox(&top,&bottom);assert(top==0&&bottom==415);
    for(int i=0;i<4;i++)p->pv[i].y=i<2?-1:481;
    assert(qvr_tc2_layer(b,p)==QTC2_OVERLAY);qvr_scene_prepare(b);near(-qvr_tc2_vertex(b,p,0).z,2.5f);
    qvr_tc2_letterbox(&top,&bottom);assert(top==0&&bottom==480);
    p->model=3473;assert(qvr_tc2_layer(b,p)==QTC2_WORLD);
    qvr_scene_prepare(NULL);qvr_tc2_letterbox(&top,&bottom);assert(top==0&&bottom==480);
    free(b);puts("TC2 camera, stereo geometry, surface aim, direct layer and empty-space projection passed");return 0;
}
