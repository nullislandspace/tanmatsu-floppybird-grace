cmake_minimum_required(VERSION 3.16)

# Use the RISC-V cross-compiler toolchain
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain-app.cmake")
endif()

project(app C)

# App source files — add your .c files here
set(APP_SOURCES
    main/crt0.c
    main/main.c
    main/app_volume.c
)

# Automated device tests (main/testkit/, README "Automated device tests").
# Opt-in: uncomment to build them in, and set where shots go. An app
# without SynthEngine3D adds TESTKIT_NO_ENGINE to the definitions.
# list(APPEND APP_SOURCES
#     main/testkit/debugcon.c
#     main/testkit/devtest.c
#     main/testkit/profile.c
#     main/testkit/report.c
#     main/testkit/screenshot.c
#     main/testkit/showtime.c
# )
# add_compile_definitions(SCREENSHOT_DIR="/sd/myapp")

# Include directories — add paths to your headers here
set(APP_INCLUDES
    main
    include
    include/esp_additions/include
)

# Compile app sources as a relocatable object library
add_library(app_obj OBJECT ${APP_SOURCES})
target_include_directories(app_obj PRIVATE ${APP_INCLUDES})
target_compile_options(app_obj PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -Os -fPIC
    -ffunction-sections -fdata-sections -fno-common
    -D__thread=
    -DESP_PLATFORM
)

# ESP-IDF normally force-includes FreeRTOS headers for all components
target_compile_options(app_obj PRIVATE
    "SHELL:-include freertos/FreeRTOS.h"
    "SHELL:-include freertos/semphr.h"
    "SHELL:-include freertos/task.h"
    "SHELL:-include freertos/queue.h"
    "SHELL:-include freertos/event_groups.h"
)

# SynthEngine3D, the 3D engine (https://github.com/nullislandspace/synthengine3D),
# if this app has it. It is not part of the template: an app that wants 3D adds
# it as a git submodule at synthengine3D/ (`make engine`, see README), and this
# block then builds it. Without it every variable below stays empty and the
# link line is exactly the one a plain app has always had, so apps that do not
# use the engine are unaffected by merges from the template.
#
# In this plain-CMake build the engine is a relocatable OBJECT library, so
# add_subdirectory MUST come before the link script's file(GENERATE) for
# $<TARGET_OBJECTS:synthengine3d> to resolve. Linking it to app_obj propagates
# the engine's PUBLIC include directory (synthengine3D/include), which is what
# lets app sources #include "synthengine3d.h"; its object files are folded into
# app.so by the link script below, since the custom command, not CMake, links.
set(ENGINE_OBJS "")
set(ENGINE_DEPS "")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/synthengine3D/CMakeLists.txt)
    message(STATUS "SynthEngine3D: building synthengine3D/")
    add_subdirectory(synthengine3D)
    target_link_libraries(app_obj PRIVATE synthengine3d)
    set(ENGINE_OBJS "$<JOIN:$<TARGET_OBJECTS:synthengine3d>, >")
    list(APPEND ENGINE_DEPS synthengine3d $<TARGET_OBJECTS:synthengine3d>)
endif()

# Collect object files into a list file, then link via a response file
# This avoids issues with generator expressions in custom commands
set(LINK_SCRIPT ${CMAKE_BINARY_DIR}/link_app.sh)
file(GENERATE OUTPUT ${LINK_SCRIPT} CONTENT
"#!/bin/sh
${CMAKE_C_COMPILER} \
    -march=rv32imafc_zicsr_zifencei_zaamo_zalrsc_xesploop_xespv2p1 -mabi=ilp32f \
    -shared -nostdlib \
    -Wl,--gc-sections \
    -Wl,-Bsymbolic \
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/app_version.script \
    -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/app.ld \
    -Wl,-Map=${CMAKE_BINARY_DIR}/app.map \
    -L${CMAKE_CURRENT_SOURCE_DIR}/fakelib \
    $<JOIN:$<TARGET_OBJECTS:app_obj>, > ${ENGINE_OBJS} \
    -lall \
    -o ${CMAKE_BINARY_DIR}/app.so
")

add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/app.so
    COMMAND sh ${LINK_SCRIPT}
    DEPENDS app_obj
        $<TARGET_OBJECTS:app_obj>
        ${ENGINE_DEPS}
        ${CMAKE_CURRENT_SOURCE_DIR}/app.ld
        ${CMAKE_CURRENT_SOURCE_DIR}/app_version.script
        ${CMAKE_CURRENT_SOURCE_DIR}/fakelib/liball.so
    COMMENT "Linking app.so"
)

add_custom_target(app ALL DEPENDS ${CMAKE_BINARY_DIR}/app.so)

# Print size after build
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_SIZE} ${CMAKE_BINARY_DIR}/app.so
    COMMENT "app.so size:"
)
