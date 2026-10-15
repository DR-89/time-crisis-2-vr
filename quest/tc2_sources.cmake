set(TC "${ROOT}/upstream/tc2")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(TC2_OVERLAY "${CMAKE_BINARY_DIR}/tc2-overlay")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${ROOT}/tools/tc2_overlay.py" "${TC2_OVERLAY}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ROOT}/tools/tc2_overlay.py" "${ROOT}/tools/tc2_reset_overlay.py")
set(TC2_EXTRA_INCLUDES "${ROOT}/link")
# The overlay drives the cabinet-ID input from the LAN link. Route it through
# the cabinet switch in a generated copy: a linked game uses its link side,
# otherwise the saved player side applies. The submodule stays reproducible.
file(READ "${TC2_OVERLAY}/tc2_h8.c" TC2_H8_SOURCE)
set(TC2_H8_OLD "return (uint8_t)~((tc2_link_side() << 2) | (!!(in & IN_PEDAL)) << 1 | (!!(in & IN_TRIGGER)));")
string(FIND "${TC2_H8_SOURCE}" "${TC2_H8_OLD}" TC2_H8_MATCH)
if(TC2_H8_MATCH EQUAL -1)
  message(FATAL_ERROR "Pinned TC2 cabinet switch hook no longer matches upstream")
endif()
string(REPLACE "${TC2_H8_OLD}" "return qtc2_cabinet_switch((uint8_t)~((!!(in & IN_PEDAL)) << 1 | (!!(in & IN_TRIGGER))),qtc2_cabinet_active_side());" TC2_H8_SOURCE "${TC2_H8_SOURCE}")
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
set_source_files_properties("${TC2_OVERLAY}/tc2_main.c" PROPERTIES COMPILE_DEFINITIONS "main=tc_game_main;SDL_MAIN_HANDLED")
set(TC2_SOURCES
  "${ROOT}/link/tc2_link.c"
  "${TC2_OVERLAY}/tc2_cpu.c" "${TC2_OVERLAY}/tc2_mem.c" "${TC2_OVERLAY}/tc2_ctl.c"
  "${TC2_OVERLAY}/tc2_chips.c" "${TC2_OVERLAY}/tc2_screen.c" "${TC}/src/tc2_text.c"
  "${TC2_OVERLAY}/tc2_main.c" "${TC}/src/tc2_input.c" "${TC2_OVERLAY}/tc2_c435.c"
  "${TC2_OVERLAY}/tc2_render.c" "${TC}/src/h8_periph.c" "${CMAKE_BINARY_DIR}/tc2_h8_adapter.c"
  "${TC}/src/tc2_audio.c" "${TC}/src/tc2_rom.c" "${TC}/src/eng/romzip.c"
  "${TC}/src/eng/c352.c" "${TC}/gen/tc2_sub.c" "${TC}/gen/tc2_io.c" ${LIFTED}
  "${ROOT}/quest/tc2_window.c" "${ROOT}/quest/tc2_gl.c" "${ROOT}/quest/tc2_cabinet.c"
  "${ROOT}/quest/quest_host.c" "${ROOT}/quest/quest_gl.c" "${ROOT}/quest/tc2_scene.c"
  "${ROOT}/quest/quest_keyboard.c" "${ROOT}/quest/quest_openvr_keyboard.c" "${ROOT}/quest/quest_lobby.c" "${ROOT}/quest/quest_gun.c" "${ROOT}/quest/quest_options.c" "${ROOT}/quest/quest_cover.c" "${ROOT}/quest/quest_ui.c" "${ROOT}/quest/quest_safe.c")
