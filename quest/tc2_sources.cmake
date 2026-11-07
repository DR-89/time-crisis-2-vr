set(TC "${ROOT}/upstream/tc2")
# Pinned upstream hardcodes the link-ID input high. Add the real active-low
# right/blue switch in a generated copy; leave the submodule reproducible.
file(READ "${TC}/src/tc2_h8.c" TC2_H8_SOURCE)
set(TC2_H8_OLD "return (uint8_t)~((!!(in & IN_PEDAL)) << 1 | (!!(in & IN_TRIGGER)));")
string(FIND "${TC2_H8_SOURCE}" "${TC2_H8_OLD}" TC2_H8_MATCH)
if(TC2_H8_MATCH EQUAL -1)
  message(FATAL_ERROR "Pinned TC2 cabinet switch hook no longer matches upstream")
endif()
string(REPLACE "${TC2_H8_OLD}" "return qtc2_cabinet_switch((uint8_t)~((!!(in & IN_PEDAL)) << 1 | (!!(in & IN_TRIGGER))),qtc2_cabinet_side);" TC2_H8_SOURCE "${TC2_H8_SOURCE}")
configure_file("${ROOT}/quest/tc2_h8_adapter.c.in" "${CMAKE_BINARY_DIR}/tc2_h8_adapter.c" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TC}/src/tc2_h8.c")
file(GLOB LIFTED "${TC}/gen/tc2_lifted_*.c")
# The single-cabinet core always initializes local input slot 0. Select Robert's
# original route before its native character initialization, not per frame.
file(READ "${TC}/gen/tc2_lifted_01.c" TC2_ROUTE_SOURCE)
set(TC2_ROUTE_OLD "A_80012E8C: RR_INS(0x80012E8CU); /* lw a0,0x3fd4(a0) */")
string(FIND "${TC2_ROUTE_SOURCE}" "${TC2_ROUTE_OLD}" TC2_ROUTE_MATCH)
if(TC2_ROUTE_MATCH EQUAL -1)
  message(FATAL_ERROR "Pinned TC2 player initialization hook no longer matches upstream")
endif()
string(REPLACE "${TC2_ROUTE_OLD}" "${TC2_ROUTE_OLD}\n  qtc2_cabinet_prepare_player();" TC2_ROUTE_SOURCE "${TC2_ROUTE_SOURCE}")
configure_file("${ROOT}/quest/tc2_route_adapter.c.in" "${CMAKE_BINARY_DIR}/tc2_route_adapter.c" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TC}/gen/tc2_lifted_01.c")
list(REMOVE_ITEM LIFTED "${TC}/gen/tc2_lifted_01.c")
list(APPEND LIFTED "${CMAKE_BINARY_DIR}/tc2_route_adapter.c")
set_source_files_properties(${LIFTED} PROPERTIES COMPILE_OPTIONS "-O1;-w;-include;${TC}/include/tc2_mips_rt.h")
# The C422 twin-cabinet link (quest/tc2_link.c): small hooks in generated copies of four
# pinned core files, (old, new) text pairs. Each fails the configure if upstream changes
# under it. While the link is disabled tc2_link.c reproduces the original register stub.
# Arguments are read one by one (ARGV<n>): the C text contains semicolons.
function(tc2_link_adapter name)
  file(READ "${TC}/src/${name}.c" source)
  math(EXPR last "${ARGC} - 1")
  foreach(i RANGE 1 ${last} 2)
    math(EXPR j "${i} + 1")
    set(old "${ARGV${i}}")
    set(new "${ARGV${j}}")
    string(FIND "${source}" "${old}" at)
    if(at EQUAL -1)
      message(FATAL_ERROR "Pinned TC2 link hook no longer matches upstream ${name}.c: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" source "${source}")
  endforeach()
  file(WRITE "${CMAKE_BINARY_DIR}/${name}_link.c" "/* Generated from the pinned upstream source by quest/tc2_sources.cmake (C422 link hooks). */\n#include \"tc2_link.h\"\n${source}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TC}/src/${name}.c")
endfunction()
tc2_link_adapter(tc2_chips
  "{ *v = c422[(p - 0x06400000u) >> 1]; return 1; }"
  "{ *v = tc2_link_read((p - 0x06400000u) >> 1); return 1; }"
  "if (k == 1) { if (v == 0xFFFBu) cause_set(IP_C422, 1); else if (v == 0x000Fu) cause_set(IP_C422, 0); }"
  "tc2_link_write(k, (uint16_t)v); return 1;")
tc2_link_adapter(tc2_ctl
  "if (p == 0x0D000000u) { tc2_ctl_leds = (uint8_t)v; return 1; }"
  "if (p == 0x0D000000u) { tc2_ctl_leds = (uint8_t)v; return 1; }\n    if (p == 0x0D000008u) { tc2_link_ack(); return 1; }   /* the C422 handler's acknowledge (8000084C) */")
tc2_link_adapter(tc2_main
  "    tc2_screen_advance(n);\n"
  "    tc2_screen_advance(n);\n    tc2_link_step(n);\n")
tc2_link_adapter(tc2_mem
  "static uint8_t vram_mem[sizeof vram_win / sizeof vram_win[0]][0x30000];\n"
  "static uint8_t vram_mem[sizeof vram_win / sizeof vram_win[0]][0x30000];\nuint8_t *tc2_c422_ram(void) { return vram_mem[1]; }   /* vram_win[1]: the C422 RAM */\n")
set_source_files_properties("${CMAKE_BINARY_DIR}/tc2_main_link.c" PROPERTIES COMPILE_DEFINITIONS "main=tc_game_main;SDL_MAIN_HANDLED")
# Online play: upstream's NMN2 client and built-in host (engine/net.c, engine/net_host.c).
set_source_files_properties("${ROOT}/quest/tc2_online.c" PROPERTIES INCLUDE_DIRECTORIES "${ROOT}/upstream/engine")
set(TC2_SOURCES
  "${TC}/src/tc2_cpu.c" "${CMAKE_BINARY_DIR}/tc2_mem_link.c" "${CMAKE_BINARY_DIR}/tc2_ctl_link.c"
  "${CMAKE_BINARY_DIR}/tc2_chips_link.c" "${TC}/src/tc2_screen.c" "${TC}/src/tc2_text.c"
  "${CMAKE_BINARY_DIR}/tc2_main_link.c" "${TC}/src/tc2_input.c" "${TC}/src/tc2_c435.c"
  "${ROOT}/quest/tc2_link.c" "${ROOT}/quest/tc2_online.c" "${ROOT}/upstream/engine/net.c" "${ROOT}/upstream/engine/net_host.c"
  "${TC}/src/tc2_render.c" "${TC}/src/h8_periph.c" "${CMAKE_BINARY_DIR}/tc2_h8_adapter.c"
  "${TC}/src/tc2_audio.c" "${TC}/src/tc2_rom.c" "${TC}/src/eng/romzip.c"
  "${TC}/src/eng/c352.c" "${TC}/gen/tc2_sub.c" "${TC}/gen/tc2_io.c" ${LIFTED}
  "${ROOT}/quest/tc2_window.c" "${ROOT}/quest/tc2_gl.c" "${ROOT}/quest/tc2_cabinet.c"
  "${ROOT}/quest/quest_host.c" "${ROOT}/quest/quest_gl.c" "${ROOT}/quest/tc2_scene.c"
  "${ROOT}/quest/quest_gun.c" "${ROOT}/quest/quest_options.c" "${ROOT}/quest/quest_cover.c" "${ROOT}/quest/quest_ui.c")
