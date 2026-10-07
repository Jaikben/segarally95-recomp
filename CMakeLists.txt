cmake_minimum_required(VERSION 3.16)
project(segamod2 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Match historical `build/segamod2` layout even with multi-config generators.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR})

include(CheckIncludeFile)
include(CheckSymbolExists)

# --- PS Vita (VitaSDK) cross-compile -----------------------------------
# Configure with: -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
# The toolchain file sets VITA/BUILD_VITA to True.
if(VITA)
  include("${VITASDK}/share/vita.cmake" REQUIRED)
  if(DEFINED SEGAMOD2_VITA_GXM AND NOT SEGAMOD2_VITA_GXM)
    message(FATAL_ERROR "Vita GL support was removed. Remove the obsolete SEGAMOD2_VITA_GXM option.")
  endif()
  unset(SEGAMOD2_VITA_GXM CACHE)
  find_path(SEGAMOD2_VITA2D_INCLUDE_DIR vita2d.h)
  find_library(SEGAMOD2_VITA2D_LIBRARY vita2d)
  if(NOT SEGAMOD2_VITA2D_INCLUDE_DIR OR NOT SEGAMOD2_VITA2D_LIBRARY)
    message(FATAL_ERROR "Native GXM needs libvita2d: vdpm install libvita2d")
  endif()
endif()

# --- Optional deps (viewer / PNG dumps) ------------------------------------
set(SEGAMOD2_HAVE_SDL2 OFF)
set(SEGAMOD2_SDL_TARGET "")

if(VITA)
  find_package(SDL2 CONFIG REQUIRED
    PATHS "${VITASDK}/arm-vita-eabi/lib/cmake/SDL2" NO_DEFAULT_PATH)
  file(READ "${VITASDK}/arm-vita-eabi/lib/pkgconfig/sdl2.pc" SEGAMOD2_VITA_SDL_PC)
  if(SEGAMOD2_VITA_SDL_PC MATCHES "-lvitaGL")
    message(FATAL_ERROR "GXM-only Vita builds require ordinary sdl2, not sdl2_vitagl. Install: vdpm install sdl2")
  endif()
else()
  find_package(SDL2 CONFIG QUIET)
endif()
if(SDL2_FOUND)
  if(TARGET SDL2::SDL2)
    set(SEGAMOD2_SDL_TARGET SDL2::SDL2)
    set(SEGAMOD2_HAVE_SDL2 ON)
  elseif(TARGET SDL2::SDL2-static)
    set(SEGAMOD2_SDL_TARGET SDL2::SDL2-static)
    set(SEGAMOD2_HAVE_SDL2 ON)
  endif()
endif()
if(NOT SEGAMOD2_HAVE_SDL2)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PC_SDL2 QUIET sdl2)
    if(PC_SDL2_FOUND)
      add_library(segamod2_sdl2 INTERFACE)
      target_include_directories(segamod2_sdl2 INTERFACE ${PC_SDL2_INCLUDE_DIRS})
      target_link_libraries(segamod2_sdl2 INTERFACE ${PC_SDL2_LINK_LIBRARIES})
      target_compile_options(segamod2_sdl2 INTERFACE ${PC_SDL2_CFLAGS_OTHER})
      set(SEGAMOD2_SDL_TARGET segamod2_sdl2)
      set(SEGAMOD2_HAVE_SDL2 ON)
    endif()
  endif()
endif()
if(NOT SEGAMOD2_HAVE_SDL2)
  find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2 include/SDL2 include)
  find_library(SDL2_LIBRARY NAMES SDL2 SDL2d)
  if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
    add_library(segamod2_sdl2 INTERFACE)
    target_include_directories(segamod2_sdl2 INTERFACE ${SDL2_INCLUDE_DIR})
    target_link_libraries(segamod2_sdl2 INTERFACE ${SDL2_LIBRARY})
    set(SEGAMOD2_SDL_TARGET segamod2_sdl2)
    set(SEGAMOD2_HAVE_SDL2 ON)
  endif()
endif()

find_package(PNG QUIET)
if(NOT VITA)
  find_package(OpenGL QUIET)
endif()
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter QUIET)

set(SEGAMOD2_HAVE_GL OFF)
if(OpenGL_FOUND)
  if(WIN32)
    if(SEGAMOD2_HAVE_SDL2)
      set(SEGAMOD2_HAVE_GL ON)
    endif()
  else()
    set(SEGAMOD2_HAVE_GL ON)
  endif()
endif()
# Darwin Makefile enabled GL whenever SDL2 was present (system OpenGL framework).
if(APPLE AND SEGAMOD2_HAVE_SDL2)
  set(SEGAMOD2_HAVE_GL ON)
endif()
if(VITA)
  set(SEGAMOD2_HAVE_GL OFF)
endif()

set(HAVE_PTHREAD_H OFF)
set(HAVE_CLOCK_GETTIME OFF)
if(WIN32)
  unset(HAVE_PTHREAD_H)
  unset(HAVE_CLOCK_GETTIME)
  check_include_file("pthread.h" HAVE_PTHREAD_H)
  check_symbol_exists(clock_gettime "time.h" HAVE_CLOCK_GETTIME)
else()
  set(HAVE_PTHREAD_H ON)
  set(HAVE_CLOCK_GETTIME ON)
endif()

# --- Common flags / includes ------------------------------------------------
add_library(segamod2_common INTERFACE)
if(VITA)
  target_compile_definitions(segamod2_common INTERFACE I960_HOST_VITA I960_HOST_VITA_GXM)
  target_include_directories(segamod2_common INTERFACE ${SEGAMOD2_VITA2D_INCLUDE_DIR})
  target_link_libraries(segamod2_common INTERFACE
    ${SEGAMOD2_VITA2D_LIBRARY} jpeg)
endif()
target_include_directories(segamod2_common INTERFACE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/lib/model2/include
  ${CMAKE_SOURCE_DIR}/lib/model2/host
  ${CMAKE_SOURCE_DIR}/lib/model2/geo/include
  ${CMAKE_SOURCE_DIR}/lib/model2/hw/include
  ${CMAKE_SOURCE_DIR}/lib/model2/tgp/include
  ${CMAKE_SOURCE_DIR}/lib/model2/snd/include
  ${CMAKE_SOURCE_DIR}/lib/host_compat
)
if(WIN32 AND NOT HAVE_PTHREAD_H)
  target_include_directories(segamod2_common BEFORE INTERFACE
    ${CMAKE_SOURCE_DIR}/lib/host_compat/win32)
endif()

if(MSVC)
  target_compile_options(segamod2_common INTERFACE /W3 /wd4100 /wd4244 /wd4312 /utf-8)
  target_compile_definitions(segamod2_common INTERFACE
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS)
else()
  target_compile_options(segamod2_common INTERFACE
    -Wall -Wextra -Wno-unused-parameter)
endif()

if(WIN32)
  if(MSVC)
    target_compile_options(segamod2_common INTERFACE
      "/FI${CMAKE_SOURCE_DIR}/lib/host_compat/host_compat.h")
  else()
    target_compile_options(segamod2_common INTERFACE
      "SHELL:-include ${CMAKE_SOURCE_DIR}/lib/host_compat/host_compat.h")
  endif()
endif()

if(HAVE_CLOCK_GETTIME)
  target_compile_definitions(segamod2_common INTERFACE HOST_COMPAT_HAVE_CLOCK_GETTIME)
endif()
if(SEGAMOD2_HAVE_SDL2)
  target_compile_definitions(segamod2_common INTERFACE
    I960_HOST_HAVE_SDL SDL_MAIN_HANDLED)
  target_link_libraries(segamod2_common INTERFACE ${SEGAMOD2_SDL_TARGET})
  if(VITA)
    # Static SDK stubs must follow SDL2.
    target_link_libraries(segamod2_common INTERFACE
      SceIme_stub SceGxm_stub SceDisplay_stub
      SceCtrl_stub SceAppMgr_stub SceAppUtil_stub SceAudio_stub SceAudioIn_stub
      SceSysmodule_stub SceIofilemgr_stub SceCommonDialog_stub SceTouch_stub
      SceHid_stub SceMotion_stub ScePower_stub SceProcessmgr_stub)
  endif()
endif()
if(SEGAMOD2_HAVE_GL)
  target_compile_definitions(segamod2_common INTERFACE I960_HOST_HAVE_GL)
  if(TARGET OpenGL::GL)
    target_link_libraries(segamod2_common INTERFACE OpenGL::GL)
  elseif(APPLE)
    target_link_libraries(segamod2_common INTERFACE "-framework OpenGL")
  elseif(WIN32)
    target_link_libraries(segamod2_common INTERFACE opengl32)
  else()
    target_link_libraries(segamod2_common INTERFACE GL)
  endif()
endif()
# sys24_viewer_bring_to_front uses objc_msgSend / NSApplication on Darwin.
# SDL2 pkg-config used to pull Cocoa into the Makefile link line; CMake's
# SDL2::SDL2 import often does not, so link it explicitly when the viewer is on.
if(APPLE AND SEGAMOD2_HAVE_SDL2)
  target_link_libraries(segamod2_common INTERFACE "-framework Cocoa")
endif()
if(PNG_FOUND)
  target_compile_definitions(segamod2_common INTERFACE I960_HOST_HAVE_PNG)
  target_link_libraries(segamod2_common INTERFACE PNG::PNG)
endif()
if(UNIX OR VITA)
  target_link_libraries(segamod2_common INTERFACE m)
endif()
target_link_libraries(segamod2_common INTERFACE Threads::Threads)

# --- Model 2 static libs ----------------------------------------------------
file(GLOB MODEL2_GEO_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/lib/model2/geo/*.c)
file(GLOB MODEL2_HW_SRCS  CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/lib/model2/hw/*.c)
file(GLOB MODEL2_TGP_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/lib/model2/tgp/*.c)
file(GLOB MODEL2_SND_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/lib/model2/snd/*.c)

add_library(model2_geo STATIC ${MODEL2_GEO_SRCS})
add_library(model2_hw  STATIC ${MODEL2_HW_SRCS})
add_library(model2_tgp STATIC ${MODEL2_TGP_SRCS})
add_library(model2_snd STATIC ${MODEL2_SND_SRCS})

foreach(lib model2_geo model2_hw model2_tgp model2_snd)
  target_link_libraries(${lib} PUBLIC segamod2_common)
endforeach()

# --- Host compat (pthread/time/GL loader) -----------------------------------
add_library(host_compat STATIC lib/host_compat/model2_gl.c)
target_link_libraries(host_compat PUBLIC segamod2_common)
if(WIN32 AND NOT HAVE_PTHREAD_H)
  target_sources(host_compat PRIVATE lib/host_compat/win32/host_pthread.c)
endif()
if(WIN32 AND NOT HAVE_CLOCK_GETTIME)
  target_sources(host_compat PRIVATE lib/host_compat/win32/host_time.c)
endif()

# --- Lift executable --------------------------------------------------------
set(LIFT_HOST_RUNTIME
  lib/model2/host/i960_regs.c
  lib/model2/host/i960_mem.c
  lib/model2/host/model2_rom.c
  lib/model2/host/model2_rom_check.c
  lib/model2/host/model2_rom_interleave.c
  lib/model2/host/i960_host.c
  lib/model2/host/i960_host_scene.c
  lib/model2/host/i960_host_staging.c
  lib/model2/host/i960_host_staging_dispatch.c
  lib/model2/host/i960_host_dispatch.c
  lib/model2/host/lift_stubs.c
)
set(LIFT_GAME_GLUE
  src/i960_host_syms.c
  src/i960_host_invoke.c
  src/lift_main.c
)
if(VITA)
  list(REMOVE_ITEM LIFT_GAME_GLUE src/lift_main.c)
  list(APPEND LIFT_GAME_GLUE src/vita/main.c)
endif()
file(GLOB LIFT_GAME CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/game/*.c)
file(GLOB LIFT_HOST CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/host/*.c)
file(GLOB LIFT_LIBC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/libc/*.c)
file(GLOB LIFT_BOOT CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/boot/*.c)
file(GLOB LIFT_IRQ  CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/irq/*.c)

add_executable(segamod2
  ${LIFT_HOST_RUNTIME}
  ${LIFT_GAME_GLUE}
  ${LIFT_GAME}
  ${LIFT_HOST}
  ${LIFT_LIBC}
  ${LIFT_BOOT}
  ${LIFT_IRQ}
)
target_link_libraries(segamod2 PRIVATE
  model2_geo model2_hw model2_tgp model2_snd host_compat)
if(VITA)
  target_sources(segamod2 PRIVATE src/vita/gxm_renderer.c src/vita/menu.c src/vita/frontend.c)
  target_link_options(segamod2 PRIVATE
    "LINKER:-T,${CMAKE_CURRENT_SOURCE_DIR}/src/vita/sce_padding.ld")
  set_property(TARGET segamod2 APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vita/sce_padding.ld")
endif()

option(SEGAMOD2_BUILD_VITA_TESTS "Build ROM-free Vita helper tests on the host" OFF)
if(SEGAMOD2_BUILD_VITA_TESTS)
  if(VITA)
    message(FATAL_ERROR "Vita helper tests must run in a desktop build directory")
  endif()
  enable_testing()
  add_executable(segamod2_vita_tests
    tests/vita_helpers_test.c src/vita/menu.c lib/model2/geo/model2_geo_order.c)
  target_include_directories(segamod2_vita_tests PRIVATE
    src/vita lib/model2/geo/include lib/model2/include)
  if(UNIX)
    target_link_libraries(segamod2_vita_tests PRIVATE m)
  endif()
  add_test(NAME vita-helpers COMMAND segamod2_vita_tests)
  add_executable(segamod2_rom_cache_tests
    tests/rom_cache_test.c lib/model2/host/model2_rom_interleave.c)
  target_include_directories(segamod2_rom_cache_tests PRIVATE lib/model2/host)
  add_test(NAME vita-rom-cache COMMAND segamod2_rom_cache_tests)
  add_executable(segamod2_boot_reset_tests tests/boot_reset_test.c
    src/boot/maincpu_reset_entry.c lib/model2/host/i960_regs.c)
  target_include_directories(segamod2_boot_reset_tests PRIVATE
    lib/model2/include lib/model2/host)
  add_test(NAME vita-boot-reset COMMAND segamod2_boot_reset_tests)
  add_executable(segamod2_runtime_stack_tests tests/runtime_stack_test.c
    src/game/geo_draw_frame_entry.c src/game/geo_view_mode_apply.c
    src/game/catalog_draw_setup.c src/libc/libc_printf.c lib/model2/host/i960_regs.c)
  target_include_directories(segamod2_runtime_stack_tests PRIVATE
    include lib/model2/include lib/model2/host)
  if(UNIX)
    target_link_libraries(segamod2_runtime_stack_tests PRIVATE m)
  endif()
  add_test(NAME vita-runtime-stack COMMAND segamod2_runtime_stack_tests)
  add_executable(segamod2_sys24_tile_tests tests/sys24_tile_test.c
    src/host/sys24_tile.c src/host/sys24_gfx.c)
  target_include_directories(segamod2_sys24_tile_tests PRIVATE include lib/model2/host)
  add_test(NAME vita-tile-compositor COMMAND segamod2_sys24_tile_tests)
  add_executable(segamod2_vita_menu_tests tests/vita_menu_test.c src/vita/menu.c)
  target_include_directories(segamod2_vita_menu_tests PRIVATE src/vita)
  add_test(NAME vita-menu COMMAND segamod2_vita_menu_tests
    "${CMAKE_CURRENT_BINARY_DIR}/vita-settings-test.cfg")
  add_executable(segamod2_vita_gxm_tests tests/vita_gxm_test.c
    src/vita/gxm_renderer.c src/vita/menu.c lib/model2/geo/model2_geo_order.c)
  target_include_directories(segamod2_vita_gxm_tests PRIVATE
    tests/vita_shim src/vita include lib/model2/geo/include lib/model2/include lib/model2/host)
  if(UNIX)
    target_link_libraries(segamod2_vita_gxm_tests PRIVATE m)
  endif()
  add_test(NAME vita-gxm COMMAND segamod2_vita_gxm_tests)
endif()

# --- PS Vita packaging (.self / .vpk) ---------------------------------------
if(VITA)
  vita_create_self(segamod2.self segamod2 UNSAFE)
  vita_create_vpk(segamod2.vpk "SEGARALY1" segamod2.self
    VERSION "01.01"
    NAME "Sega Rally Championship (Vita fixes)")
  # vita_create_vpk/vita_create_self name their real (ALL, auto-building)
  # target "<file>-vpk"/"<file>-self" on CMake >= 3.20; alias a stable name.
  if(CMAKE_VERSION VERSION_LESS "3.20.0")
    add_custom_target(lift-vpk DEPENDS segamod2.vpk)
  else()
    add_custom_target(lift-vpk DEPENDS segamod2.vpk-vpk)
  endif()

  # segamod2.vpk keeps its stable name for existing build/test tooling (see
  # .github/agents/psvita-build.agent.md). This tagged copy is just so test
  # VPKs downloaded from the codespace over multiple rounds of fixes don't
  # overwrite each other by filename — bump the tag when notable fixes land.
  set(SEGAMOD2_VITA_BUILD_TAG "shadowfix-tessfix" CACHE STRING
    "Descriptive suffix for the tagged copy of segamod2.vpk")
  add_custom_command(TARGET lift-vpk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      ${CMAKE_BINARY_DIR}/segamod2.vpk
      ${CMAKE_BINARY_DIR}/segamod2-${SEGAMOD2_VITA_BUILD_TAG}.vpk
    COMMENT "Copying tagged VPK: segamod2-${SEGAMOD2_VITA_BUILD_TAG}.vpk")
endif()

# --- ROM extract ------------------------------------------------------------
if(DEFINED ENV{SEGAMOD2_ROM_DIR} AND NOT "$ENV{SEGAMOD2_ROM_DIR}" STREQUAL "")
  set(SEGAMOD2_ROM_DIR "$ENV{SEGAMOD2_ROM_DIR}")
else()
  set(SEGAMOD2_ROM_DIR "${CMAKE_SOURCE_DIR}/ROMS/srallyc-b")
endif()

set(ROM_MAINCPU ${CMAKE_SOURCE_DIR}/out/i960/maincpu_deinterleaved.bin)
set(ROM_MAINDATA ${CMAKE_SOURCE_DIR}/out/i960/main_data_deinterleaved.bin)
set(EXTRACT_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/extract_rom_blocks.py)

if(Python3_Interpreter_FOUND)
  add_custom_command(
    OUTPUT ${ROM_MAINCPU} ${ROM_MAINDATA}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/out/i960
    COMMAND ${Python3_EXECUTABLE} ${EXTRACT_SCRIPT}
            --rom-dir ${SEGAMOD2_ROM_DIR}
            --out-dir ${CMAKE_SOURCE_DIR}/out/i960
    DEPENDS ${EXTRACT_SCRIPT}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Extracting ROM blocks into out/i960"
    VERBATIM
  )
  add_custom_target(rom-blocks DEPENDS ${ROM_MAINCPU} ${ROM_MAINDATA})
else()
  add_custom_target(rom-blocks
    COMMAND ${CMAKE_COMMAND} -E echo "error: Python3 is required for rom-blocks"
    COMMAND ${CMAKE_COMMAND} -E false)
endif()

add_custom_target(lift-check DEPENDS segamod2)
if(VITA)
  # Cross-compiled ARM .self/.vpk cannot run on the build host.
  add_custom_target(lift DEPENDS lift-vpk)
else()
  # Host auto-extracts out/i960 bins on first run; rom-blocks is optional (Python).
  add_custom_target(lift DEPENDS segamod2)

  function(segamod2_run_target name)
    set(extra_args ${ARGN})
    add_custom_target(${name}
      COMMAND ${CMAKE_COMMAND} -E env
              SEGAMOD2_ROOT=${CMAKE_SOURCE_DIR}
              SEGAMOD2_ROM_DIR=${SEGAMOD2_ROM_DIR}
              $<TARGET_FILE:segamod2> ${extra_args}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      DEPENDS segamod2
      USES_TERMINAL
    )
  endfunction()

  segamod2_run_target(lift-boot-viewer)
  segamod2_run_target(lift-boot-practice --practice)
  segamod2_run_target(lift-boot-headless --headless)
  add_custom_target(lift-boot-live DEPENDS lift-boot-viewer)
endif()

if(NOT VITA)
  add_custom_target(lift-cgm-decode
    COMMAND ${CMAKE_COMMAND} -E env
            SEGAMOD2_ROOT=${CMAKE_SOURCE_DIR}
            SEGAMOD2_ROM_DIR=${SEGAMOD2_ROM_DIR}
            $<TARGET_FILE:segamod2> --decode-cgm 0x2879db0
            --palette-dump ${CMAKE_BINARY_DIR}/lift/cgm_decode
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS segamod2
    USES_TERMINAL
  )
endif()

add_custom_target(lift-main
  COMMAND ${CMAKE_COMMAND} -E echo
          "Clone tools/ then: python -m tools.decomp.gen_lift_main (see README)")

# --- Status -----------------------------------------------------------------
message(STATUS "segamod2: SDL2=${SEGAMOD2_HAVE_SDL2} PNG=${PNG_FOUND} GL=${SEGAMOD2_HAVE_GL}")
if(NOT SEGAMOD2_HAVE_SDL2)
  message(STATUS "  live viewer compiled out — install SDL2 and reconfigure")
endif()
if(NOT PNG_FOUND)
  message(STATUS "  PNG dumps compiled out — install libpng and reconfigure")
endif()
if(NOT SEGAMOD2_HAVE_GL)
  message(STATUS "  OpenGL composite compiled out")
endif()
