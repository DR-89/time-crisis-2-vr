#include <GLES3/gl3.h>
#include <stdint.h>
#include "quest_gun.h"
#include "tc2_weapon.h"

__declspec(dllexport) int gun_fixture(const char *asset,int variant,int w,int h,uint8_t *out){
    if(!qgun_init(asset))return 0;
    /* ANGLE pbuffer already provides depth via the EGL config. */
    float v[16],p[16];
    view_matrix(v,v3((variant==2||variant==3)?.032f:variant==1?-.032f:0,0,0),(Q4){0,0,0,1});projection(p,-.4f,.4f,-.4f,.4f);
    glViewport(0,0,w,h);glClearColor(.10f,.13f,.17f,1);glColorMask(1,1,1,1);glClear(GL_COLOR_BUFFER_BIT);
    bool side=variant==0||variant>=5;
    Q4 rotation=side?(Q4){0,sinf(.785398f),0,cosf(.785398f)}:variant==4?(Q4){0,1,0,0}:product((Q4){0,sinf(.10f),0,cosf(.10f)},(Q4){sinf(-.10f),0,0,cosf(-.10f)});
    V3 tip=qgun_muzzle(v3(0,0,0),(Q4){0,0,0,1});
    if(fabsf(tip.x)>.0001f||fabsf(tip.y)>.0001f||fabsf(tip.z+.150763f)>.0001f)return -1;
    if(qgun_recoil(0)!=0||qgun_recoil(150)!=0||qgun_recoil(18)<.999f)return -2;
    for(int ms=0;ms<180;ms++)if(qgun_recoil(ms)<0||qgun_recoil(ms)>1)return -3;
    float recoil=variant==3||variant==5?qgun_recoil(18):variant==6?qgun_recoil(150):0;
    if(qgun_flash(-1)!=0||qgun_flash(0)!=1||qgun_flash(42)!=0)return -5;
    if(variant==9){
        /* Exercise replacing/selecting a second slot without changing the
         * geometry, so the output must match the pistol's resting image. */
        if(!qgun_load_weapon(QGUN_MACHINE_GUN,asset))return -6;
        qgun_select(QGUN_MACHINE_GUN);
        if(qgun_selected()!=QGUN_MACHINE_GUN)return -7;
    }
    float age=variant>=10?(variant-10)*10.f:-1;
    if(variant>=10)recoil=qgun_recoil(age);
    qgun_draw(v,p,v3(side?.08f:0,.045f,variant==4?-.6f:-.37f),rotation,recoil,variant==7?1:0,variant==8?qgun_flash(0):qgun_flash(age));
    V3 after=qgun_muzzle(v3(0,0,0),(Q4){0,0,0,1});
    if(tip.x!=after.x||tip.y!=after.y||tip.z!=after.z)return -4;
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,out);GLenum error=glGetError();
    qgun_shutdown();return error?-(int)error:1;
}

/* The second asset is deliberately invalid once, then valid with a distinct
 * muzzle. Failure must preserve the loaded pistol, and slot switching must
 * change the laser/aim origin along with the visible mesh. */
__declspec(dllexport) int gun_switch_fixture(const char *pistol,const char *second){
    if(!qgun_init(pistol))return 0;
    V3 a=qgun_muzzle(v3(0,0,0),(Q4){0,0,0,1});
    qgun_select(QGUN_MACHINE_GUN);
    V3 fallback=qgun_muzzle(v3(0,0,0),(Q4){0,0,0,1});
    if(a.z!=fallback.z||qgun_load_weapon(QGUN_MACHINE_GUN,"missing-model.tcgun"))return -1;
    if(!qgun_load_weapon(QGUN_MACHINE_GUN,second))return -2;
    V3 b=qgun_muzzle(v3(0,0,0),(Q4){0,0,0,1});
    if(fabsf(b.z+.3f)>.0001f)return -3;
    qgun_select(QGUN_PISTOL);
    if(qgun_muzzle(v3(0,0,0),(Q4){0,0,0,1}).z!=a.z)return -4;
    GLenum error=glGetError();qgun_shutdown();return error?-(int)error:1;
}

__declspec(dllexport) int machine_gun_fixture(const char *pistol,const char *smg,int view,float age,int w,int h,uint8_t *out){
    if(!qgun_init(pistol)||!qgun_load_weapon(QGUN_MACHINE_GUN,smg))return 0;
    static uint8_t ram[0x300000];
    ram[0x2f43d3]=8;qgun_select(qtc2_weapon_kind(ram,sizeof ram));
    if(qgun_selected()!=QGUN_MACHINE_GUN)return -1;
    V3 muzzle=qgun_muzzle(v3(0,0,0),(Q4){0,0,0,1});
    if(fabsf(muzzle.x)>.0001f||fabsf(muzzle.y)>.0001f||fabsf(muzzle.z+.2605585f)>.0001f)return -2;
    if(view==4){ram[0x2f43d3]=0;qgun_select(qtc2_weapon_kind(ram,sizeof ram));if(qgun_selected()!=QGUN_PISTOL)return -3;}
    float v[16],p[16];
    view_matrix(v,v3(view==1?-.032f:view==2?.032f:0,0,0),(Q4){0,0,0,1});projection(p,-.4f,.4f,-.4f,.4f);
    Q4 rotation=view==0||view==4?(Q4){0,sinf(.785398f),0,cosf(.785398f)}:view==3?(Q4){0,sinf(-.45f),0,cosf(-.45f)}:(Q4){0,0,0,1};
    V3 position=view==1||view==2?v3(.1f,-.06f,-.45f):v3(.025f,.03f,-.8f);
    glViewport(0,0,w,h);glClearColor(.10f,.13f,.17f,1);glColorMask(1,1,1,1);glClear(GL_COLOR_BUFFER_BIT);
    qgun_draw(v,p,position,rotation,qgun_recoil(age),age>=0&&age<80?1:0,qgun_flash(age));
    if(view!=4){V3 after=qgun_muzzle(v3(0,0,0),(Q4){0,0,0,1});if(after.z!=muzzle.z)return -4;}
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,out);GLenum error=glGetError();qgun_shutdown();return error?-(int)error:1;
}
