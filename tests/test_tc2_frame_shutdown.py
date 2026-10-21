"""Execute the generated Frame shutdown boundary and preserve restart routing."""
from pathlib import Path
import importlib.util
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("tc2_overlay", ROOT / "tools/tc2_overlay.py")
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)

native = ROOT / "build/steam-frame/native"
compat = native / "compat"
config_headers = native / "sdl/include-config-release"
if not (compat / "SDL2/SDL.h").is_file() or not (config_headers / "SDL2/SDL_config.h").is_file():
    raise SystemExit("Frame native build headers are missing; configure the production Frame build first")

with tempfile.TemporaryDirectory(prefix="tc2-frame-shutdown-") as tmp:
    generated = Path(tmp) / "generated"
    generated.mkdir()
    overlay.generate(generated)
    source = (generated / "tc2_main.c").read_text()
    assert "if(TC2_FRAME_SHUTDOWN_CHECKPOINT())TC2_FRAME_SHUTDOWN_CATCH();" in source
    assert "longjmp(power_on_return,1);" in source
    assert "longjmp(frame_shutdown_return,1);" in source
    exe = Path(tmp) / "fixture"
    command = [
        "cc", "-std=gnu11", "-O0", "-ffunction-sections", "-fdata-sections",
        "-DTCVR_STEAM_FRAME",
        "-Dchdir=tc2_test_chdir",
        "-I" + str(generated),
        "-I" + str(ROOT / "tests/frame_shutdown_stubs"),
        "-I" + str(ROOT / "upstream/tc2/include"),
        "-I" + str(ROOT / "upstream/tc2/src"),
        "-I" + str(ROOT / "upstream/tc2/src/eng"),
        "-I" + str(ROOT / "upstream/tc2/gen"),
        "-I" + str(ROOT / "link"),
        "-I" + str(ROOT / "quest"),
        "-I" + str(compat),
        "-I" + str(config_headers),
        str(ROOT / "tests/tc2_shutdown_overlay_fixture.c"),
        str(ROOT / "quest/quest_main.c"),
        str(ROOT / "quest/tc2_window.c"),
        ("-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"), "-lm", "-o", str(exe),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(exe)], check=True, cwd=tmp)
print("PASS: Frame overlay, production window shutdown path, SDL_main cleanup/return, and cabinet restart checkpoints execute")
