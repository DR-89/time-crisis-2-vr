"""Explicit power-on state resets for the pinned core. No game-state rewrites."""
RESETS = {
 'tc2_cpu.c': '''
void tc2_cpu_reset(void) {
    memset(R,0,sizeof R); rr_budget=granted=TC2_SLICE;
    rr_pc=rr_frame=rr_n_traps=rr_n_fpe=rr_ret_to=0; rr_in_irq=0; rr_n_irq=0;
    sh_n=0; sh_target=-1; count_frac=cycles_done=0; irq_lines=irq_src[0]=irq_src[1]=0;
}
''',
 'tc2_chips.c': '''
void tc2_chips_reset(void) {
    memset(&c417,0,sizeof c417); memset(c422,0,sizeof c422);
    memset(&c412,0,sizeof c412); memset(&c421,0,sizeof c421);
}
''',
 'tc2_mem.c': '''
void tc2_mem_reset(void) {
    memset(tc2_ram,0,sizeof tc2_ram); memset(vram_mem,0,sizeof vram_mem);
    memset(shared_ram,0,sizeof shared_ram); backup_on=0; backup_path=NULL;
    memset(backup_saved,0,sizeof backup_saved); tc2_crashing=0;
}
''',
 'tc2_ctl.c': '''
void tc2_ctl_reset(void) { shifter[0]=shifter[1]=0; tc2_ctl_leds=0; }
''',
 'tc2_c435.c': '''
void tc2_c435_reset(void) {
#define ZERO(x) memset(&(x),0,sizeof(x))
    ZERO(c435); ZERO(matrices); ZERO(vectors); ZERO(light_vector); ZERO(spv); ZERO(spm);
    absolute_priority=model_blend_factor=light_power=light_ambient=scaling=0;
    tx=ty=vp_offset_x=vp_offset_y=vp_size_x=vp_size_y=0;
    ZERO(clip_data); clip_line=0; ZERO(direct_buf); direct_pos=direct_nonempty=direct_open=0;
    tc2_render[0].count=tc2_render[1].count=0; tc2_render_cur=0;
#undef ZERO
}
''',
 'tc2_h8.c': '''
void tc2_h8_reset(void) {
    memset(sub_onchip,0,sizeof sub_onchip); memset(io_ram,0,sizeof io_ram);
    sub=cold_sub; io=cold_io; subp=cold_subp; iop=cold_iop;
    sub_running=0; line_k=c352_done=main_now=off_j=0;
    sub_p8=2; sub_pa=0; sub_pb=0x50; jvs_sense_init=io_out=irq1_pin=0;
    rtc_bits=0; rtc_pos=rtc_ce=0; c352_init(&c352,wave,0x1000000);
}
''',
 'tc2_render.c': '''
void tc2_render_reset(void) {
    if(started)wait_idle(); /* no lock held when the lifted C stack unwinds */
    have_frame=front=0; memset(frames,0,sizeof frames);
    if(B[0])memset(B[0],0,sizeof *B[0]); if(B[1])memset(B[1],0,sizeof *B[1]);
}
''',
}

MAIN_API = '''
#include <setjmp.h>
static jmp_buf power_on_return;
static int restart_armed;
/* Called only after the host finished an XR frame and the renderer is idle. */
void tc2_machine_restart(void) {
    if(restart_armed)longjmp(power_on_return,1);
}
static void machine_reset(void) {
    void tc2_render_reset(void),tc2_cpu_reset(void),tc2_mem_reset(void),tc2_chips_reset(void);
    void tc2_h8_reset(void),tc2_ctl_reset(void),tc2_c435_reset(void),tc2_link_device_reset(void);
    void tc2_backup_save(int);
    tc2_backup_save(1); tc2_render_reset(); tc2_cpu_reset(); tc2_mem_reset();
    tc2_chips_reset(); tc2_h8_reset(); tc2_ctl_reset(); tc2_c435_reset(); tc2_link_device_reset();
    ins_total=0;
}
'''
