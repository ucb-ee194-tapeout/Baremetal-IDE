# CMakeLists definitions for target `atlas_compiled`: an Atlas program produced by the Atlas MLIR
# compiler (Merlin / atlas-mlir), run by a host program that Baremetal-IDE compiles itself.
#
# The compiler supplies only device-side output, as headers in one directory:
#   atlas_program.h        the Atlas instruction words (atlas_host_embed.py)
#   atlas_*_data.h         constants to preload and the input/output addresses (e.g. atlas_captured_data.h)
# Baremetal-IDE compiles the host C with its own toolchain and flags (like the Saturn benchmarks,
# sources are used in place), and links it with glossy, chip-config and rocketcore as usual.
#
#   make build CHIP=penguin26 PLATFORM=SIMS TARGET=atlas_compiled EXTRA_CMAKE_ARGS="\
#     -DPENGUIN26_TESTS_ATLAS_COMPILED_DIR=/abs/compiler/out/<kernel>/host \
#     -DPENGUIN26_TESTS_ATLAS_RUNTIME_DIR=/abs/atlas-mlir/runtime"
#
# By default the host is src/main.c here (written for the captured attention+MLP example). To use
# another host, e.g. the compiler's own examples/handoff/.../host.c, set
# PENGUIN26_TESTS_ATLAS_COMPILED_SOURCES to a ','-separated list of .c files.
#
# The ELF is build/penguin26-tests/atlas_compiled/<PENGUIN26_TESTS_ATLAS_COMPILED_NAME>.elf.

set(PENGUIN26_TESTS_ATLAS_COMPILED_DIR "" CACHE PATH
    "atlas_compiled: directory with the compiler's generated headers (atlas_program.h, ...)")
set(PENGUIN26_TESTS_ATLAS_RUNTIME_DIR "" CACHE PATH
    "atlas_compiled: atlas-mlir runtime/ directory (atlas_host.h, which atlas_program.h includes, and atlas_host.c)")
set(PENGUIN26_TESTS_ATLAS_COMPILED_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c" CACHE STRING
    "atlas_compiled: ,- or ;-separated host .c files, compiled by Baremetal-IDE")
# The compiler's hosts refuse to build without these (#error). They are the simulation settings
# from its README: no FPGA bitstream binding, Shuttle/Atlas treated as coherent, a poll limit.
set(PENGUIN26_TESTS_ATLAS_COMPILED_DEFINES
    "ATLAS_DIAGNOSTIC_ALLOW_UNBOUND=1,ATLAS_DIAGNOSTIC_ASSUME_COHERENT=1,ATLAS_MAX_POLLS=10000000"
    CACHE STRING "atlas_compiled: ,- or ;-separated compile definitions for the host sources")
set(PENGUIN26_TESTS_ATLAS_COMPILED_NAME "atlas_compiled" CACHE STRING
    "atlas_compiled: output ELF name (without .elf)")

if (NOT PENGUIN26_TESTS_ATLAS_COMPILED_DIR)
  return()
endif()

#################################
# Build Configuration
#################################

get_filename_component(_atlas_compiled_dir "${PENGUIN26_TESTS_ATLAS_COMPILED_DIR}" ABSOLUTE)
if (NOT EXISTS "${_atlas_compiled_dir}/atlas_program.h")
  message(FATAL_ERROR "atlas_compiled: ${_atlas_compiled_dir}/atlas_program.h does not exist")
endif()

string(REPLACE "," ";" _atlas_compiled_srcs "${PENGUIN26_TESTS_ATLAS_COMPILED_SOURCES}")
string(REPLACE "," ";" _atlas_compiled_defs "${PENGUIN26_TESTS_ATLAS_COMPILED_DEFINES}")
set(_atlas_compiled_incs "${CMAKE_CURRENT_SOURCE_DIR}/include" "${_atlas_compiled_dir}")

# The compiler's host runtime (atlas_host_init/load/start/wait); atlas_program.h needs its header.
if (PENGUIN26_TESTS_ATLAS_RUNTIME_DIR)
  get_filename_component(_atlas_runtime_dir "${PENGUIN26_TESTS_ATLAS_RUNTIME_DIR}" ABSOLUTE)
  if (NOT EXISTS "${_atlas_runtime_dir}/atlas_host.c")
    message(FATAL_ERROR "atlas_compiled: ${_atlas_runtime_dir}/atlas_host.c does not exist")
  endif()
  list(APPEND _atlas_compiled_srcs "${_atlas_runtime_dir}/atlas_host.c")
  list(APPEND _atlas_compiled_incs "${_atlas_runtime_dir}")
endif()

# Source Files
add_executable(atlas_compiled ${_atlas_compiled_srcs})
set_target_properties(atlas_compiled PROPERTIES OUTPUT_NAME "${PENGUIN26_TESTS_ATLAS_COMPILED_NAME}")
target_compile_definitions(atlas_compiled PRIVATE ${_atlas_compiled_defs})

# Header Files
target_include_directories(atlas_compiled PRIVATE ${_atlas_compiled_incs})

#################################
# Dependencies
#################################

# glossy: startup code, stdio (HTIF for PLATFORM=SIMS, UART0 for PLATFORM=CHIP) and exit
target_link_libraries(atlas_compiled PRIVATE
  -L${CMAKE_BINARY_DIR}/glossy -Wl,--whole-archive glossy -Wl,--no-whole-archive
)

# chip-config: chip_config.h (addresses) and the chip drivers, including hal_atlas
target_link_libraries(atlas_compiled PRIVATE chip-config)
target_link_libraries(atlas_compiled PRIVATE rocketcore)
