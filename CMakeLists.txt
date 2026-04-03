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
)

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

# Collect object files into a list file, then link via a response file
# This avoids issues with generator expressions in custom commands
set(LINK_SCRIPT ${CMAKE_BINARY_DIR}/link_app.sh)
file(GENERATE OUTPUT ${LINK_SCRIPT} CONTENT
"#!/bin/sh
${CMAKE_C_COMPILER} \
    -march=rv32imafc_zicsr_zifencei_xesppie -mabi=ilp32f \
    -shared -nostdlib \
    -Wl,--gc-sections \
    -Wl,-Bsymbolic \
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/app_version.script \
    -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/app.ld \
    -Wl,-Map=${CMAKE_BINARY_DIR}/app.map \
    -L${CMAKE_CURRENT_SOURCE_DIR}/fakelib \
    $<JOIN:$<TARGET_OBJECTS:app_obj>, > \
    -lall \
    -o ${CMAKE_BINARY_DIR}/app.so
")

add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/app.so
    COMMAND sh ${LINK_SCRIPT}
    DEPENDS app_obj
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
