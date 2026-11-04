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
# The game executable
# ---------------------------------------------------------------------------
add_executable(hellfire WIN32
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
  src/gamefx.c)

target_link_libraries(hellfire PRIVATE gpgx)
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
add_executable(hellfire_smoketest tools/smoketest.c src/emu.c src/sha1.c src/romcheck.c src/hf_file.c src/widescreen.c src/gamefx.c)
target_link_libraries(hellfire_smoketest PRIVATE gpgx)
if(UNIX)
  target_link_libraries(hellfire_smoketest PRIVATE m)
endif()
