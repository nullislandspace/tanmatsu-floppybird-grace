# Cross-compilation toolchain for Graceloader app plugins
# Requires the RISC-V toolchain from ESP-IDF tools (riscv32-esp-elf-*)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)

# Find the compiler - search IDF_TOOLS_PATH, local esp-idf-tools, then PATH
# Use CMAKE_CURRENT_LIST_DIR (always the dir of this file) not CMAKE_CURRENT_SOURCE_DIR
# (which changes during try_compile)
file(GLOB _toolchain_hints
    "$ENV{IDF_TOOLS_PATH}/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin"
    "${CMAKE_CURRENT_LIST_DIR}/esp-idf-tools/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin"
)

find_program(RISCV_CC riscv32-esp-elf-gcc
    HINTS ${_toolchain_hints}
)

if(NOT RISCV_CC)
    message(FATAL_ERROR
        "Could not find riscv32-esp-elf-gcc.\n"
        "Make sure ESP-IDF tools are installed and either:\n"
        "  - Set IDF_TOOLS_PATH environment variable, or\n"
        "  - Have esp-idf-tools/ in this directory, or\n"
        "  - source esp-idf/export.sh before running cmake"
    )
endif()

get_filename_component(TOOLCHAIN_DIR ${RISCV_CC} DIRECTORY)

set(CMAKE_C_COMPILER "${TOOLCHAIN_DIR}/riscv32-esp-elf-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_DIR}/riscv32-esp-elf-g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_DIR}/riscv32-esp-elf-gcc")
set(CMAKE_AR "${TOOLCHAIN_DIR}/riscv32-esp-elf-ar")
set(CMAKE_RANLIB "${TOOLCHAIN_DIR}/riscv32-esp-elf-ranlib")
set(CMAKE_OBJCOPY "${TOOLCHAIN_DIR}/riscv32-esp-elf-objcopy")
set(CMAKE_SIZE "${TOOLCHAIN_DIR}/riscv32-esp-elf-size")

# ESP-IDF 6 builds against PicoLibC rather than the toolchain's default
# newlib. The graceloader is compiled that way, so apps must use the same
# libc headers or they pick up incompatible newlib stdio/stdlib definitions.
set(PICOLIBC_INCLUDE "${TOOLCHAIN_DIR}/../picolibc/include")
if(EXISTS "${PICOLIBC_INCLUDE}")
    get_filename_component(PICOLIBC_INCLUDE "${PICOLIBC_INCLUDE}" ABSOLUTE)
    set(LIBC_FLAGS " -isystem ${PICOLIBC_INCLUDE}")
else()
    set(LIBC_FLAGS "")
endif()

set(ARCH_FLAGS "-march=rv32imafc_zicsr_zifencei_zaamo_zalrsc_xesploop_xespv2p1 -mabi=ilp32f")
set(CMAKE_C_FLAGS_INIT "${ARCH_FLAGS}${LIBC_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ARCH_FLAGS}${LIBC_FLAGS}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
