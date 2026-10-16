# CMakeLists definitions for target `external_objects`: link object files built OUTSIDE Baremetal-IDE
# (for example the host program the Atlas MLIR compiler builds with clang) into a penguin26 ELF.
#
# Baremetal-IDE supplies what a bare object file lacks: glossy's startup code (crt0), libc/stdio
# (HTIF for PLATFORM=SIMS, UART0 for PLATFORM=CHIP), exit through HTIF tohost, the trap vector, the
# platform drivers and platform/penguin26/penguin26.ld. The objects must define main() and be built
# for this ABI: -march=${ARCH} (or a subset) -mabi=lp64d -mcmodel=medany.
#
#   make build CHIP=penguin26 PLATFORM=SIMS TARGET=external_objects \
#     EXTRA_CMAKE_ARGS="-DPENGUIN26_TESTS_EXTERNAL_OBJECTS=/abs/host.o,/abs/atlas_host.o"
#
# Separate files with ',' (or ';', quoted: the Makefile passes EXTRA_CMAKE_ARGS through a shell).
#
# The ELF is build/penguin26-tests/external_objects/<PENGUIN26_TESTS_EXTERNAL_NAME>.elf.

set(PENGUIN26_TESTS_EXTERNAL_OBJECTS "" CACHE STRING
    "external_objects: ,- or ;-separated list of object files (.o) or archives (.a) to link, one must define main()")
set(PENGUIN26_TESTS_EXTERNAL_NAME "external_objects" CACHE STRING
    "external_objects: output ELF name (without .elf)")

if (NOT PENGUIN26_TESTS_EXTERNAL_OBJECTS)
  return()
endif()

#################################
# Build Configuration
#################################

string(REPLACE "," ";" _penguin26_ext_files "${PENGUIN26_TESTS_EXTERNAL_OBJECTS}")
set(_penguin26_ext_objs "")
set(_penguin26_ext_libs "")
foreach(_f IN LISTS _penguin26_ext_files)
  get_filename_component(_f "${_f}" ABSOLUTE)
  if (NOT EXISTS "${_f}")
    message(FATAL_ERROR "external_objects: ${_f} does not exist")
  endif()
  if (_f MATCHES "\\.a$")
    list(APPEND _penguin26_ext_libs "${_f}")
  else()
    list(APPEND _penguin26_ext_objs "${_f}")
  endif()
endforeach()
set_source_files_properties(${_penguin26_ext_objs} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

# Source Files: none of our own, only the external objects
add_executable(external_objects ${_penguin26_ext_objs})
set_target_properties(external_objects PROPERTIES
  LINKER_LANGUAGE C
  OUTPUT_NAME "${PENGUIN26_TESTS_EXTERNAL_NAME}")
# relink when an object changes
set_property(TARGET external_objects APPEND PROPERTY LINK_DEPENDS
  ${_penguin26_ext_objs} ${_penguin26_ext_libs})

#################################
# Dependencies
#################################

target_link_libraries(external_objects PRIVATE ${_penguin26_ext_libs})

# glossy: startup code, stdio (HTIF for PLATFORM=SIMS, UART0 for PLATFORM=CHIP) and exit
target_link_libraries(external_objects PRIVATE
  -L${CMAKE_BINARY_DIR}/glossy -Wl,--whole-archive glossy -Wl,--no-whole-archive
)

# chip-config: chip_config.h (addresses) and the chip drivers, including hal_atlas
target_link_libraries(external_objects PRIVATE chip-config)
target_link_libraries(external_objects PRIVATE rocketcore)
