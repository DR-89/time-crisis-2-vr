#include "quest_gpu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quest_gun.h"

typedef struct { float position[3],normal[3],uv[2],color[3],part; } GunVertex;
typedef struct {GLuint vao,vbo,ibo,texture;GLsizei index_count;V3 muzzle;bool articulated;} GunMesh;
static GunMesh meshes[QGUN_COUNT];
static QGunKind selected;
static GLuint program,flash_program,flash_vao,flash_vbo;
static GLint u_view,u_projection,u_model,u_albedo,u_recoil,u_trigger;
static GLint f_view,f_projection,f_model,f_tip,f_size,f_strength;
static const GunMesh *current_mesh(void){return meshes[selected].vao?&meshes[selected]:&meshes[QGUN_PISTOL];}
static void delete_mesh(GunMesh *m){
    glDeleteVertexArrays(1,&m->vao);glDeleteBuffers(1,&m->vbo);glDeleteBuffers(1,&m->ibo);glDeleteTextures(1,&m->texture);memset(m,0,sizeof *m);
}

static GLuint compile(GLenum type,const char *source){
    GLuint s=glCreateShader(type);qgpu_shader_source(s,source);glCompileShader(s);
    GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof log,NULL,log);fprintf(stderr,"[GUN] shader: %s\n",log);glDeleteShader(s);return 0;}return s;
}
bool qgun_load_weapon(QGunKind kind,const char *path){
    if(kind<0||kind>=QGUN_COUNT)return false;
    GunMesh mesh={0};
    FILE *f=fopen(path,"rb");if(!f){fprintf(stderr,"[GUN] missing model: %s\n",path);return false;}
    char magic[8];uint32_t n[4]={0,0,1,1};float tip[3];
    GunVertex *vertices=NULL;uint32_t *indices=NULL;uint8_t *pixels=NULL;bool ok=false;
    if(fread(magic,1,8,f)!=8)goto done;
    bool legacy=!memcmp(magic,"TCGUN001",8);
    if(!legacy&&memcmp(magic,"TCGUN002",8))goto done;
    if(fread(n,4,2,f)!=2||(legacy&&fread(n+2,4,2,f)!=2)||fread(tip,4,3,f)!=3)goto done;
    if(!n[0]||n[0]>100000||!n[1]||n[1]>300000||n[1]%3||!n[2]||n[2]>2048||!n[3]||n[3]>2048)goto done;
    vertices=calloc(n[0],sizeof *vertices);indices=malloc((size_t)n[1]*4);pixels=malloc((size_t)n[2]*n[3]*4);
    if(!vertices||!indices||!pixels)goto done;
    for(uint32_t i=0;i<n[0];i++){
        float raw[10];size_t count=legacy?8:10;
        if(fread(raw,4,count,f)!=count)goto done;
        for(size_t j=0;j<count;j++)if(!isfinite(raw[j]))goto done;
        memcpy(vertices[i].position,raw,12);memcpy(vertices[i].normal,raw+3,12);
        if(legacy){memcpy(vertices[i].uv,raw+6,8);for(int j=0;j<3;j++)vertices[i].color[j]=1;}
        else{
            if(raw[9]!=0&&raw[9]!=1&&raw[9]!=2)goto done;
            vertices[i].part=raw[9];
            for(int j=0;j<3;j++){
                float c=raw[6+j];if(c<0||c>1)goto done;
                /* Palette bytes are display-encoded, like the old sRGB atlas. */
                vertices[i].color[j]=c<=.04045f?c/12.92f:powf((c+.055f)/1.055f,2.4f);
            }
        }
    }
    if(fread(indices,4,n[1],f)!=n[1])goto done;
    if(legacy){if(fread(pixels,4,(size_t)n[2]*n[3],f)!=(size_t)n[2]*n[3])goto done;}
    else memset(pixels,255,4);
    if(fgetc(f)!=EOF)goto done;
    for(uint32_t i=0;i<n[1];i++)if(indices[i]>=n[0])goto done;
    for(int i=0;i<3;i++)if(!isfinite(tip[i]))goto done;
    if(!program){
    GLuint vs=compile(GL_VERTEX_SHADER,
        "#version 300 es\nprecision highp float;layout(location=0) in vec3 position;layout(location=1) in vec3 normal;layout(location=2) in vec2 texcoord;layout(location=3) in vec3 vertexColor;layout(location=4) in float part;uniform mat4 view,projection,model;uniform float recoil,triggerPull;out vec3 n,p,color;out vec2 uv;void main(){vec3 pos=position;if(part>1.5)pos.z+=0.034*recoil;else if(part>0.5)pos.z+=0.003*triggerPull;mat4 mv=view*model;vec4 v=mv*vec4(pos,1);p=v.xyz;n=mat3(mv)*normal;uv=texcoord;color=vertexColor;gl_Position=projection*v;}");
    GLuint fs=compile(GL_FRAGMENT_SHADER,
        "#version 300 es\nprecision highp float;in vec3 n,p,color;in vec2 uv;uniform sampler2D albedo;out vec4 frag;void main(){vec3 N=normalize(n);if(!gl_FrontFacing)N=-N;vec3 L=normalize(vec3(-0.4,0.8,0.6));vec3 V=normalize(-p);vec3 H=normalize(L+V);vec3 base=texture(albedo,uv).rgb*color;float diffuse=0.48+0.52*max(dot(N,L),0.0);float spec=0.16*pow(max(dot(N,H),0.0),40.0);frag=vec4(pow(clamp(base*diffuse+spec,0.0,1.0),vec3(1.0/2.2)),1);}");
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);goto done;}
    program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)goto done;
    u_view=glGetUniformLocation(program,"view");u_projection=glGetUniformLocation(program,"projection");u_model=glGetUniformLocation(program,"model");u_albedo=glGetUniformLocation(program,"albedo");
    u_recoil=glGetUniformLocation(program,"recoil");u_trigger=glGetUniformLocation(program,"triggerPull");
    }
    GLint binding;glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    glGenTextures(1,&mesh.texture);glBindTexture(GL_TEXTURE_2D,mesh.texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,n[2],n[3],0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,binding);
    glGenVertexArrays(1,&mesh.vao);glBindVertexArray(mesh.vao);glGenBuffers(1,&mesh.vbo);glBindBuffer(GL_ARRAY_BUFFER,mesh.vbo);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)n[0]*sizeof *vertices,vertices,GL_STATIC_DRAW);
    glGenBuffers(1,&mesh.ibo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,mesh.ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)n[1]*4,indices,GL_STATIC_DRAW);
    for(int i=0;i<3;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,i==2?2:3,GL_FLOAT,GL_FALSE,sizeof(GunVertex),(void*)(size_t)(i==0?0:i==1?12:24));}
    glEnableVertexAttribArray(3);glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,sizeof(GunVertex),(void*)32);
    glEnableVertexAttribArray(4);glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,sizeof(GunVertex),(void*)44);
    mesh.index_count=(GLsizei)n[1];mesh.muzzle=v3(tip[0],tip[1],tip[2]);
    mesh.articulated=!legacy;
    ok=glGetError()==GL_NO_ERROR;
    if(ok){
        delete_mesh(&meshes[kind]);meshes[kind]=mesh;
        fprintf(stderr,"[GUN] %s loaded: %u vertices, %u triangles; %s\n",path,n[0],n[1]/3,mesh.articulated?"animated slide and trigger":"textured mesh with recoil");
    }
done:
    fclose(f);free(vertices);free(indices);free(pixels);
    if(!ok){fprintf(stderr,"[GUN] model initialization failed: %s\n",path);delete_mesh(&mesh);}return ok;
}
static bool init_flash(void){
    GLuint vs=compile(GL_VERTEX_SHADER,
        "#version 300 es\nprecision highp float;layout(location=0) in vec3 position;layout(location=1) in float edge;uniform mat4 view,projection,model;uniform vec3 tip;uniform float size;out float glow;void main(){glow=edge;gl_Position=projection*view*model*vec4(tip+position*size,1);}");
    GLuint fs=compile(GL_FRAGMENT_SHADER,
        "#version 300 es\nprecision highp float;in float glow;uniform float strength;out vec4 frag;void main(){float a=glow*strength;frag=vec4(vec3(1.0,0.62+0.28*glow,0.12+0.60*glow)*a,a);}");
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return false;}
    flash_program=glCreateProgram();glAttachShader(flash_program,vs);glAttachShader(flash_program,fs);glLinkProgram(flash_program);glDeleteShader(vs);glDeleteShader(fs);
    GLint ok;glGetProgramiv(flash_program,GL_LINK_STATUS,&ok);if(!ok)return false;
    f_view=glGetUniformLocation(flash_program,"view");f_projection=glGetUniformLocation(flash_program,"projection");f_model=glGetUniformLocation(flash_program,"model");
    f_tip=glGetUniformLocation(flash_program,"tip");f_size=glGetUniformLocation(flash_program,"size");f_strength=glGetUniformLocation(flash_program,"strength");
    /* A small pointed volume has parallax from every angle, including straight
     * down the barrel. No billboard locked to one eye and no screen-wide flash. */
    float vertices[8*6*4];int n=0;
    for(int i=0;i<8;i++){
        float a=i*(6.283185307f/8),b=(i+1)*(6.283185307f/8);
        float ra=(i&1)?.16f:.38f,rb=(i&1)?.38f:.16f;
        const float tri[24]={
            0,0,-.01f,1, ra*cosf(a),ra*sinf(a),-.35f,.25f, rb*cosf(b),rb*sinf(b),-.35f,.25f,
            0,0,-1.3f,0, rb*cosf(b),rb*sinf(b),-.35f,.25f, ra*cosf(a),ra*sinf(a),-.35f,.25f};
        memcpy(vertices+n,tri,sizeof tri);n+=24;
    }
    glGenVertexArrays(1,&flash_vao);glBindVertexArray(flash_vao);glGenBuffers(1,&flash_vbo);glBindBuffer(GL_ARRAY_BUFFER,flash_vbo);glBufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,4*sizeof(float),0);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,1,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(3*sizeof(float)));
    return glGetError()==GL_NO_ERROR;
}
bool qgun_init(const char *path){
    qgun_shutdown();selected=QGUN_PISTOL;
    if(qgun_load_weapon(QGUN_PISTOL,path)&&init_flash())return true;
    qgun_shutdown();return false;
}
void qgun_select(QGunKind kind){if(kind>=0&&kind<QGUN_COUNT)selected=kind;}
QGunKind qgun_selected(void){return selected;}
V3 qgun_muzzle(V3 position,Q4 rotation){return add(position,rotate(rotation,current_mesh()->muzzle));}
void qgun_draw(const float view[16],const float projection[16],V3 position,Q4 rotation,float recoil,float trigger_pull,float flash){
    const GunMesh *mesh=current_mesh();
    if(!program||!mesh->vao)return;
    /* Recoil is visual only: it cannot change the game's shot coordinates. */
    float kick=fminf(1,fmaxf(0,recoil));
    if(!mesh->articulated){
        position=add(position,rotate(rotation,v3(0,0,.014f*kick)));
        rotation=product(rotation,(Q4){sinf(.045f*kick),0,0,cosf(.045f*kick)});
    }
    V3 x=rotate(rotation,v3(1,0,0)),y=rotate(rotation,v3(0,1,0)),z=rotate(rotation,v3(0,0,1));
    float model[16]={x.x,x.y,x.z,0,y.x,y.y,y.z,0,z.x,z.y,z.z,0,position.x,position.y,position.z,1};
    GLint binding,active_texture;glGetIntegerv(GL_ACTIVE_TEXTURE,&active_texture);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);
    /* The arcade has painter-sorted polygons. This foreground pass uses its own
     * cleared depth buffer for the weapon's self-occlusion. */
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);glClearDepthf(1);glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);glUniformMatrix4fv(u_view,1,GL_FALSE,view);glUniformMatrix4fv(u_projection,1,GL_FALSE,projection);glUniformMatrix4fv(u_model,1,GL_FALSE,model);glUniform1i(u_albedo,0);
    glUniform1f(u_recoil,kick);glUniform1f(u_trigger,fminf(1,fmaxf(0,trigger_pull)));
    glBindTexture(GL_TEXTURE_2D,mesh->texture);glBindVertexArray(mesh->vao);glDrawElements(GL_TRIANGLES,mesh->index_count,GL_UNSIGNED_INT,0);
    if(flash>0&&flash_program){
        GLint src_rgb,dst_rgb,src_a,dst_a;glGetIntegerv(GL_BLEND_SRC_RGB,&src_rgb);glGetIntegerv(GL_BLEND_DST_RGB,&dst_rgb);glGetIntegerv(GL_BLEND_SRC_ALPHA,&src_a);glGetIntegerv(GL_BLEND_DST_ALPHA,&dst_a);
        glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);glDepthMask(GL_FALSE);
        glUseProgram(flash_program);glUniformMatrix4fv(f_view,1,GL_FALSE,view);glUniformMatrix4fv(f_projection,1,GL_FALSE,projection);glUniformMatrix4fv(f_model,1,GL_FALSE,model);
        glUniform3f(f_tip,mesh->muzzle.x,mesh->muzzle.y,mesh->muzzle.z);glUniform1f(f_size,selected==QGUN_MACHINE_GUN?.060f:.045f);glUniform1f(f_strength,fminf(1,flash));
        glBindVertexArray(flash_vao);glDrawArrays(GL_TRIANGLES,0,48);
        glDepthMask(GL_TRUE);glDisable(GL_BLEND);glBlendFuncSeparate(src_rgb,dst_rgb,src_a,dst_a);
    }
    glDisable(GL_DEPTH_TEST);glBindTexture(GL_TEXTURE_2D,binding);glActiveTexture(active_texture);
}
void qgun_shutdown(void){
    for(int i=0;i<QGUN_COUNT;i++)delete_mesh(&meshes[i]);
    glDeleteProgram(program);glDeleteProgram(flash_program);glDeleteVertexArrays(1,&flash_vao);glDeleteBuffers(1,&flash_vbo);
    program=flash_program=flash_vao=flash_vbo=0;selected=QGUN_PISTOL;
}
