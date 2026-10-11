"""Reproducible, fail-closed overlays; never modify the pinned upstream checkout."""
from pathlib import Path
import argparse
from tc2_reset_overlay import RESETS, MAIN_API
ROOT = Path(__file__).resolve().parents[1]
PIN = "0837a397ebe554dbf5341d95629aa1e29fc58a52"

def generate(output, headless=False):
    import subprocess
    upstream = ROOT / "upstream"
    if subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=upstream, text=True).strip() != PIN:
        raise RuntimeError("Unexpected upstream revision")
    output = Path(output); output.mkdir(parents=True, exist_ok=True)
    def emit(name, replacements):
        text = subprocess.check_output(["git", "show", "HEAD:tc2/src/" + name], cwd=upstream).decode()
        for old, new in replacements:
            if text.count(old) != 1:
                raise RuntimeError(f"Overlay anchor mismatch: {name}: {old[:80]}")
            text = text.replace(old, new)
        text = "#include <string.h>\n" + text
        text += RESETS.get(name, "")
        path = output / name
        if not path.exists() or path.read_text() != text:
            path.write_text(text)
    emit("tc2_chips.c", [
        ('#include "tc2_host.h"', '#include "tc2_host.h"\n#include "tc2_link.h"'),
        ('*v = c422[(p - 0x06400000u) >> 1];', '*v = tc2_link_read((p - 0x06400000u) >> 1, c422);'),
        ('        if (k == 1) { if (v == 0xFFFBu)', '        if (tc2_link_enabled()) { tc2_link_write(k, (uint16_t)v, c422); return 1; }\n        if (k == 1) { if (v == 0xFFFBu)'),
    ])
    emit("tc2_h8.c", [
        ("static h8p subp, iop;", "static h8p subp, iop;\nstatic h8_cpu cold_sub,cold_io; static h8p cold_subp,cold_iop;"),
        ("    c352_init(&c352, wave, 0x1000000);", "    c352_init(&c352, wave, 0x1000000);\n    cold_sub=sub; cold_io=io; cold_subp=subp; cold_iop=iop;"),
        ('static const uint8_t io_dsw = 0xFF, io_sw = 0x03;', '#include "tc2_link.h"\n#define io_dsw (tc2_link_enabled() ? 0xF7 : 0xFF)\nstatic const uint8_t io_sw = 0x03;'),
        ('return (uint8_t)~((!!(in & IN_PEDAL)) << 1 | (!!(in & IN_TRIGGER)));', 'return (uint8_t)~((tc2_link_side() << 2) | (!!(in & IN_PEDAL)) << 1 | (!!(in & IN_TRIGGER)));'),
    ])
    for name in ("tc2_mem.c", "tc2_ctl.c", "tc2_c435.c", "tc2_render.c"):
        changes=[]
        if name=="tc2_mem.c":
            changes=[('backup_path = e && *e ? e : window ? "tc2_backup.nv" : NULL;', 'extern const char *tc2_link_backup(void);\n    backup_path = e && *e ? e : window ? tc2_link_backup() : NULL;')]
        emit(name,changes)
    emit("tc2_screen.c",[("static uint64_t pic = ~0ull;", "/* resettable picture counter */"),
        ("static void update_rowscroll(void)", "static uint64_t pic = ~0ull;\nstatic void update_rowscroll(void)"),
        ("    cyc = 0; off_pix = VBL_LINE * LINE_PIX; vbl_active = 1; c361_line = 0x1FF;\n    next_vbl_on = off_pix + FRAME_PIX; next_vbl_off = FRAME_PIX; next_c361 = NEVER;\n    cause", "    memset(rowscroll,0,sizeof rowscroll); lastrow=0; rowscroll_frame=0; pic=~0ull; c361_xscroll=c361_yscroll=0;\n    cyc = 0; off_pix = VBL_LINE * LINE_PIX; vbl_active = 1; c361_line = 0x1FF;\n    next_vbl_on = off_pix + FRAME_PIX; next_vbl_off = FRAME_PIX; next_c361 = NEVER;\n    cause")])
    replacements = [
        ("int main(int argc, char **argv)", MAIN_API + "\nint main(int argc, char **argv)"),
        ('    const int power_on = !strcmp(argv[1], "reset");', '    const int power_on = !strcmp(argv[1], "reset");\n    if(setjmp(power_on_return))machine_reset();\n    restart_armed=1;'),
        ('    rr_jump(pc, pc);', '#ifdef TCVR_STEAM_FRAME\n    if(TC2_FRAME_SHUTDOWN_CHECKPOINT())TC2_FRAME_SHUTDOWN_CATCH();\n    frame_shutdown_armed=1;\n#endif\n    rr_jump(pc, pc);\n#ifdef TCVR_STEAM_FRAME\n    frame_shutdown_armed=0;\n#endif'),
        ('#include "tc2_host.h"', '#include "tc2_host.h"\n#include "tc2_link.h"'),
        ('    scripted_inputs(frame);', '    tc2_link_vblank(frame);\n    scripted_inputs(frame);'),
        ('    ins_total += n;', '    ins_total += n;\n    tc2_link_poll();'),
        ('    tc2_io_flush();', '    tc2_link_report();\n    tc2_io_flush();'),
    ]
    if headless:
        replacements += [
            ('#include <SDL2/SDL.h>', '/* Headless host has no SDL dependency. */'),
            ('static void gpu_gate(uint32_t frame)\n{', '#if 0\nstatic void gpu_gate(uint32_t frame)\n{'),
            ('/* THE SESSION RECORDER:', '#endif\nstatic void gpu_gate(uint32_t frame) { (void)frame; }\n/* THE SESSION RECORDER:'),
            ('        { char *bp = SDL_GetBasePath();', '#if 0\n        { char *bp = SDL_GetBasePath();'),
            ('        int tc2_rom_setup(', '#endif\n        int tc2_rom_setup('),
            ('            if (!strcmp(argv[1], "--window")) SDL_ShowSimpleMessageBox', '#if 0\n            if (!strcmp(argv[1], "--window")) SDL_ShowSimpleMessageBox'),
            ('            return 1;\n        }\n    }', '#endif\n            return 1;\n        }\n    }'),
        ]
    emit("tc2_main.c", replacements)
    cpu_changes=[]
    if headless:
        cpu_changes = [
            ('int rd_on, rd_stop_on, rd_poll_rec;', 'int rd_on = 1, rd_stop_on, rd_poll_rec;'),
            ('int rd_hook(uint32_t ep) { (void)ep; return 0; }', '#include "tc2_link.h"\nint rd_hook(uint32_t ep) { tc2_link_observe(ep); return 0; }'),
        ]
    emit("tc2_cpu.c",cpu_changes)

if __name__ == "__main__":
    p = argparse.ArgumentParser(); p.add_argument("output", type=Path); p.add_argument("--headless", action="store_true")
    a = p.parse_args(); generate(a.output, a.headless)
