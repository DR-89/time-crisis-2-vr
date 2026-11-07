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
# tc2_online_adapter(<pinned source> <generated name> old new ...) is the same for any upstream file.
function(tc2_online_adapter path out)
  file(READ "${path}" source)
  math(EXPR last "${ARGC} - 1")
  foreach(i RANGE 2 ${last} 2)
    math(EXPR j "${i} + 1")
    set(old "${ARGV${i}}")
    set(new "${ARGV${j}}")
    string(FIND "${source}" "${old}" at)
    if(at EQUAL -1)
      message(FATAL_ERROR "Pinned online hook no longer matches upstream ${path}: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" source "${source}")
  endforeach()
  file(WRITE "${CMAKE_BINARY_DIR}/${out}.c" "/* Generated from the pinned upstream source by quest/tc2_sources.cmake (online play hooks). */\n#include \"tc2_link.h\"\n#include \"tc2_voice.h\"\n${source}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
endfunction()
function(tc2_link_adapter name)
  # The pairs cannot pass through a CMake list (they contain semicolons): forward them by count.
  if(ARGC EQUAL 3)
    tc2_online_adapter("${TC}/src/${name}.c" "${name}_link" "${ARGV1}" "${ARGV2}")
  elseif(ARGC EQUAL 5)
    tc2_online_adapter("${TC}/src/${name}.c" "${name}_link" "${ARGV1}" "${ARGV2}" "${ARGV3}" "${ARGV4}")
  else()
    message(FATAL_ERROR "tc2_link_adapter: one or two (old, new) pairs")
  endif()
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
# Voice chat is mixed into the game's own output (Android has one output device): one sample added per output sample.
tc2_online_adapter("${TC}/src/tc2_audio.c" tc2_audio_voice
  "        int l = (s4[4 * i] + s4[4 * i + 2]) * volume / 100, r = (s4[4 * i + 1] + s4[4 * i + 3]) * volume / 100;\n"
  "        int l = (s4[4 * i] + s4[4 * i + 2]) * volume / 100, r = (s4[4 * i + 1] + s4[4 * i + 3]) * volume / 100;\n        { const int voice = tc2_voice_mix(); l += voice; r += voice; }\n")
# Online play: upstream's NMN2 client and built-in host (engine/net.c, engine/net_host.c). The client gains a room request
# for the next connect (tc2_online.c: room codes and the lobby browser join a chosen room, hosting creates a named one) and
# an accessor for the room it is in. Everything else in the library is unchanged.
tc2_online_adapter("${ROOT}/upstream/engine/net.c" net_rooms
  "static char hello_room_name[32];\n"
  "static char hello_room_name[32];\nint eng_net_room_request; char eng_net_room_request_name[32];   /* tc2_online.c: the room every connect asks for */\n"
  "    hello_room_sel = 0; hello_room_name[0] = 0; switching = 0; rooms_n = 0;\n"
  "    hello_room_sel = eng_net_room_request; snprintf(hello_room_name, sizeof hello_room_name, \"%s\", eng_net_room_request_name); switching = 0; rooms_n = 0;\n"
  "int eng_net_room_count(void) { return rooms_n; }\n"
  "int eng_net_room_count(void) { return rooms_n; }\nint eng_net_my_room(void) { return st == ST_LOBBY || st == ST_SESSION ? my_room : 0; }\n")
set(TC2_NET_INCLUDE "${ROOT}/upstream/engine;${ROOT}/.tools/opus/opus-1.5.2/include")
set_source_files_properties("${ROOT}/quest/tc2_online.c" "${ROOT}/quest/tc2_rooms.c" "${ROOT}/quest/tc2_voice.c" "${CMAKE_BINARY_DIR}/net_rooms.c"
  PROPERTIES INCLUDE_DIRECTORIES "${TC2_NET_INCLUDE}")
# Opus (BSD) for voice chat, from the pinned download (tools/bootstrap.py). The GitHub archive lacks the version file.
set(OPUS_SRC "${ROOT}/.tools/opus/opus-1.5.2")
if(NOT EXISTS "${OPUS_SRC}/package_version")
  file(WRITE "${OPUS_SRC}/package_version" "PACKAGE_VERSION=\"1.5.2\"\n")
endif()
foreach(option OPUS_BUILD_SHARED_LIBRARY OPUS_BUILD_TESTING OPUS_BUILD_PROGRAMS OPUS_INSTALL_PKG_CONFIG_MODULE OPUS_INSTALL_CMAKE_CONFIG_MODULE)
  set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${OPUS_SRC}" "${CMAKE_BINARY_DIR}/opus" EXCLUDE_FROM_ALL)
set(TC2_SOURCES
  "${TC}/src/tc2_cpu.c" "${CMAKE_BINARY_DIR}/tc2_mem_link.c" "${CMAKE_BINARY_DIR}/tc2_ctl_link.c"
  "${CMAKE_BINARY_DIR}/tc2_chips_link.c" "${TC}/src/tc2_screen.c" "${TC}/src/tc2_text.c"
  "${CMAKE_BINARY_DIR}/tc2_main_link.c" "${TC}/src/tc2_input.c" "${TC}/src/tc2_c435.c"
  "${ROOT}/quest/tc2_link.c" "${ROOT}/quest/tc2_online.c" "${ROOT}/quest/tc2_rooms.c" "${ROOT}/quest/tc2_voice.c"
  "${CMAKE_BINARY_DIR}/net_rooms.c" "${ROOT}/upstream/engine/net_host.c"
  "${TC}/src/tc2_render.c" "${TC}/src/h8_periph.c" "${CMAKE_BINARY_DIR}/tc2_h8_adapter.c"
  "${CMAKE_BINARY_DIR}/tc2_audio_voice.c" "${TC}/src/tc2_rom.c" "${TC}/src/eng/romzip.c"
  "${TC}/src/eng/c352.c" "${TC}/gen/tc2_sub.c" "${TC}/gen/tc2_io.c" ${LIFTED}
  "${ROOT}/quest/tc2_window.c" "${ROOT}/quest/tc2_gl.c" "${ROOT}/quest/tc2_cabinet.c"
  "${ROOT}/quest/quest_host.c" "${ROOT}/quest/quest_gl.c" "${ROOT}/quest/tc2_scene.c"
  "${ROOT}/quest/quest_gun.c" "${ROOT}/quest/quest_options.c" "${ROOT}/quest/quest_cover.c" "${ROOT}/quest/quest_ui.c")
