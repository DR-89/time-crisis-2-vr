cmake_minimum_required(VERSION 3.22)
project(tc2_headless C)
set(CMAKE_C_STANDARD 11)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(TC "${ROOT}/upstream/tc2")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
set(OVERLAY "${CMAKE_BINARY_DIR}/overlay")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${ROOT}/tools/tc2_overlay.py" "${OVERLAY}" --headless COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ROOT}/tools/tc2_overlay.py" "${ROOT}/tools/tc2_reset_overlay.py")
file(GLOB LIFTED "${TC}/gen/tc2_lifted_*.c")
set_source_files_properties(${LIFTED} PROPERTIES COMPILE_OPTIONS "-O1;-w;-include;${TC}/include/tc2_mips_rt.h")
add_executable(tc2_headless host.c "${ROOT}/link/tc2_link.c"
  "${OVERLAY}/tc2_cpu.c" "${OVERLAY}/tc2_main.c" "${OVERLAY}/tc2_chips.c" "${OVERLAY}/tc2_h8.c"
  "${OVERLAY}/tc2_mem.c" "${OVERLAY}/tc2_ctl.c" "${OVERLAY}/tc2_screen.c" "${TC}/src/tc2_text.c"
  "${OVERLAY}/tc2_c435.c" "${OVERLAY}/tc2_render.c" "${TC}/src/h8_periph.c"
  "${TC}/src/tc2_rom.c" "${TC}/src/eng/romzip.c" "${TC}/src/eng/c352.c"
  "${TC}/gen/tc2_sub.c" "${TC}/gen/tc2_io.c" ${LIFTED})
target_include_directories(tc2_headless PRIVATE "${ROOT}/link" "${TC}/src" "${TC}/include" "${TC}/gen" "${TC}/src/eng")
target_compile_options(tc2_headless PRIVATE -O2 -fno-strict-aliasing -fwrapv)
target_link_libraries(tc2_headless PRIVATE ZLIB::ZLIB Threads::Threads m)
