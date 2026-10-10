/* Local renderer diagnostics, enabled only by TC2_FRAME_CAPTURE on Windows.
 * Snapshots contain game data and belong in ignored build/artifacts folders. */
#include <stdio.h>
#include <stdlib.h>
static void tc2_capture_frame(const tc2_built *b,unsigned frame_number){
    static int init;static const char *dir;static unsigned first=1200,last=28500,step=300;
    if(!init){init=1;dir=getenv("TC2_FRAME_CAPTURE");const char *range=getenv("TC2_FRAME_RANGE");if(range)sscanf(range,"%u:%u:%u",&first,&last,&step);}
    if(!dir||!step||frame_number<first||frame_number>last||(frame_number-first)%step)return;
    char path[1024];static int assets;
    if(!assets){
        const uint8_t *rom,*attr;const uint32_t *map;
        void tc2_render_assets(const uint8_t **,const uint32_t **,const uint8_t **);
        tc2_render_assets(&rom,&map,&attr);if(!rom)return;
        snprintf(path,sizeof path,"%s/assets.bin",dir);FILE *f=fopen(path,"wb");if(!f)return;
        fwrite(rom,1,0x2000000,f);fwrite(map,4,0x200000,f);fwrite(attr,1,0x200000,f);fclose(f);assets=1;
    }
    snprintf(path,sizeof path,"%s/frame-%06u.bin",dir,frame_number);FILE *f=fopen(path,"wb");if(!f)return;
    fwrite(b,1,sizeof *b,f);fclose(f);
}
