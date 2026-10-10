set(TC "${ROOT}/upstream/tc2")
file(GLOB LIFTED "${TC}/gen/tc2_lifted_*.c")
set_source_files_properties(${LIFTED} PROPERTIES COMPILE_OPTIONS "-O1;-w;-include;${TC}/include/tc2_mips_rt.h")
set_source_files_properties("${TC}/src/tc2_main.c" PROPERTIES COMPILE_DEFINITIONS "main=tc_game_main;SDL_MAIN_HANDLED")
set(TC2_SOURCES
  "${TC}/src/tc2_cpu.c" "${TC}/src/tc2_mem.c" "${TC}/src/tc2_ctl.c"
  "${TC}/src/tc2_chips.c" "${TC}/src/tc2_screen.c" "${TC}/src/tc2_text.c"
  "${TC}/src/tc2_main.c" "${TC}/src/tc2_input.c" "${TC}/src/tc2_c435.c"
  "${TC}/src/tc2_render.c" "${TC}/src/h8_periph.c" "${TC}/src/tc2_h8.c"
  "${TC}/src/tc2_audio.c" "${TC}/src/tc2_rom.c" "${TC}/src/eng/romzip.c"
  "${TC}/src/eng/c352.c" "${TC}/gen/tc2_sub.c" "${TC}/gen/tc2_io.c" ${LIFTED}
  "${ROOT}/quest/tc2_window.c" "${ROOT}/quest/tc2_gl.c"
  "${ROOT}/quest/quest_host.c" "${ROOT}/quest/quest_gl.c" "${ROOT}/quest/tc2_scene.c"
  "${ROOT}/quest/quest_gun.c" "${ROOT}/quest/quest_options.c" "${ROOT}/quest/quest_cover.c" "${ROOT}/quest/quest_ui.c")
