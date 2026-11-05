cmake_minimum_required(VERSION 3.16)
project(hellfire_saga_pc C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Emulator core: Genesis Plus GX Wide (non-commercial license, see
# third_party/gpgx-wide/LICENSE.txt). Built as a static library with our own
# osd.h, and without Mega CD image formats (CHD/OGG) we never need.
# ---------------------------------------------------------------------------
set(GPGX_DIR ${CMAKE_SOURCE_DIR}/third_party/gpgx-wide/core)
file(GLOB GPGX_SOURCES
  ${GPGX_DIR}/*.c
  ${GPGX_DIR}/z80/*.c
  ${GPGX_DIR}/m68k/*.c
  ${GPGX_DIR}/sound/*.c
  ${GPGX_DIR}/input_hw/*.c
  ${GPGX_DIR}/cd_hw/*.c
  ${GPGX_DIR}/cart_hw/*.c
  ${GPGX_DIR}/cart_hw/svp/*.c
  ${GPGX_DIR}/ntsc/*.c)

add_library(gpgx STATIC ${GPGX_SOURCES})
target_include_directories(gpgx PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${GPGX_DIR}
  ${GPGX_DIR}/z80
  ${GPGX_DIR}/m68k
  ${GPGX_DIR}/sound
  ${GPGX_DIR}/input_hw
  ${GPGX_DIR}/cd_hw
  ${GPGX_DIR}/cart_hw
  ${GPGX_DIR}/cart_hw/svp
  ${GPGX_DIR}/ntsc)
target_compile_definitions(gpgx PUBLIC LSB_FIRST USE_32BPP_RENDERING)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  # The core is third-party code: build it fast and quietly.
  target_compile_options(gpgx PRIVATE -O3 -fomit-frame-pointer -fno-strict-aliasing -w)
elseif(MSVC)
  target_compile_options(gpgx PRIVATE /O2 /w)
endif()

# ---------------------------------------------------------------------------
# SDL2
# ---------------------------------------------------------------------------
find_package(SDL2 REQUIRED)

# ---------------------------------------------------------------------------
# TinyCC (LGPL 2.1, see third_party/tinycc): compiles the recompiled C in
# memory on the player's PC. Built as its own library for x86-64; the target
# (Linux ELF or Windows PE) follows the host. Elsewhere the game falls back to
# the emulated CPU.
# ---------------------------------------------------------------------------
set(HF_TCC_LIB "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  add_library(libtcc STATIC third_party/tinycc/libtcc.c)
  target_include_directories(libtcc PUBLIC ${CMAKE_SOURCE_DIR}/third_party/tinycc)
  target_compile_definitions(libtcc PRIVATE ONE_SOURCE=1 CONFIG_TCC_PREDEFS=1
    CONFIG_TCC_BACKTRACE=0 CONFIG_TCC_BCHECK=0 CONFIG_TCC_SEMLOCK=0)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(libtcc PRIVATE -O2 -fno-strict-aliasing -w)
  endif()
  target_compile_definitions(libtcc INTERFACE HF_HAVE_LIBTCC)
  if(UNIX)
    target_link_libraries(libtcc PUBLIC ${CMAKE_DL_LIBS} m)
  endif()
  set(HF_TCC_LIB libtcc)
endif()

# ---------------------------------------------------------------------------
# Recompiler: 68000 -> C, run on the player's PC (src/recomp)
# ---------------------------------------------------------------------------
set(RT_EMBED ${CMAKE_BINARY_DIR}/generated/rt_embed.inc)
add_custom_command(
  OUTPUT ${RT_EMBED}
  COMMAND ${CMAKE_COMMAND} -DOUT=${RT_EMBED} -DNAMES=rt_prelude_text,rt_helpers_text
          -DFILES=${CMAKE_SOURCE_DIR}/src/recomp/rt_prelude.h,${CMAKE_SOURCE_DIR}/src/recomp/rt_helpers.h
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_text.cmake
  DEPENDS src/recomp/rt_prelude.h src/recomp/rt_helpers.h cmake/embed_text.cmake)
set(HF_RECOMP_SOURCES
  src/native.c
  src/recomp/m68kdec.c
  src/recomp/gen68k.c
  src/recomp/tcc_build.c
  ${RT_EMBED})
include_directories(${CMAKE_BINARY_DIR}/generated)

# ---------------------------------------------------------------------------
# The game executable
# ---------------------------------------------------------------------------
set(HELLFIRE_RESOURCES "")
if(WIN32)
  enable_language(RC)
  set(HELLFIRE_RESOURCES src/hellfire.rc)
endif()

add_executable(hellfire WIN32
  ${HELLFIRE_RESOURCES}
  src/main.c
  src/emu.c
  src/audio.c
  src/video.c
  src/controls.c
  src/gfx.c
  src/sha1.c
  src/romcheck.c
  src/hf_file.c
  src/widescreen.c
  src/settings.c
  src/launcher.c
  src/gamefx.c
  src/soundtrack.c
  ${HF_RECOMP_SOURCES})

target_link_libraries(hellfire PRIVATE gpgx ${HF_TCC_LIB})
if(UNIX)
  target_link_libraries(hellfire PRIVATE ${CMAKE_DL_LIBS})
endif()
if(TARGET SDL2::SDL2main)
  target_link_libraries(hellfire PRIVATE SDL2::SDL2main)
endif()
if(TARGET SDL2::SDL2-static AND HELLFIRE_STATIC_SDL)
  target_link_libraries(hellfire PRIVATE SDL2::SDL2-static)
else()
  target_link_libraries(hellfire PRIVATE SDL2::SDL2)
endif()
if(UNIX)
  target_link_libraries(hellfire PRIVATE m)
endif()
if(UNIX AND NOT APPLE)
  # Look for a bundled libSDL2 next to the executable first, so a zip
  # build runs even where SDL2 isn't installed.
  set_target_properties(hellfire PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
endif()
if(MINGW)
  # No libgcc DLL to ship: only SDL2.dll sits next to the exe.
  target_link_options(hellfire PRIVATE -static-libgcc)
endif()
if(WIN32 AND TARGET SDL2::SDL2)
  add_custom_command(TARGET hellfire POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:hellfire>)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(hellfire PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Launcher art sits next to the executable.
add_custom_command(TARGET hellfire POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:hellfire>/assets)

# Headless smoke test: boots a ROM for a few seconds and saves a screenshot.
add_executable(hellfire_smoketest tools/smoketest.c src/emu.c src/sha1.c src/romcheck.c src/hf_file.c src/widescreen.c src/gamefx.c ${HF_RECOMP_SOURCES})
target_link_libraries(hellfire_smoketest PRIVATE gpgx ${HF_TCC_LIB})

# Frame-by-frame check of recompiled code against the interpreter.
add_executable(hellfire_recomp_check tools/recomp_check.c src/emu.c src/hf_file.c src/widescreen.c src/gamefx.c ${HF_RECOMP_SOURCES})
target_link_libraries(hellfire_recomp_check PRIVATE gpgx ${HF_TCC_LIB})
if(UNIX)
  target_link_libraries(hellfire_recomp_check PRIVATE m ${CMAKE_DL_LIBS})
  target_link_libraries(hellfire_smoketest PRIVATE ${CMAKE_DL_LIBS})
endif()
if(UNIX)
  target_link_libraries(hellfire_smoketest PRIVATE m)
endif()

# Per-instruction fuzz test of the recompiler (Linux, calls gcc).
if(UNIX AND NOT APPLE)
  add_executable(hellfire_recomp_fuzz tools/recomp_fuzz.c src/native.c
    src/recomp/m68kdec.c src/recomp/gen68k.c ${RT_EMBED})
  target_link_libraries(hellfire_recomp_fuzz PRIVATE gpgx m ${CMAKE_DL_LIBS})
endif()
