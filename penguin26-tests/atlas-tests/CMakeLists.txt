########################################################################################################################
# Atlas NPU baremetal tests for the penguin26 chip.
#
# One target `atlas_<name>` per generators/atlas-npu/baremetal/assembly/<name>.S (top level only, the
# same set atlas-npu's Makefile and run_asm_tests.sh use). The test C file is generated at BUILD time
# into ${PENGUIN26_TESTS_ATLAS_GEN_DIR} by scripts/atlas/gen_atlas_tests.py, which imports atlas-npu's
# atlas_asm.py and runs its generators/gen_*.py; the generated harness uses platform/penguin26 hal_atlas.
# Nothing in atlas-npu is copied or edited.
#
#   make build CHIP=penguin26 PLATFORM=SIMS TARGET=atlas_alu_basic \
#     EXTRA_CMAKE_ARGS="-DPENGUIN26_TESTS_ATLAS_PYTHON=/path/to/venv/bin/python"
#   make build CHIP=penguin26 PLATFORM=SIMS TARGET=atlas-tests          # all of them
#
# The golden generators need numpy, torch and numba: point PENGUIN26_TESTS_ATLAS_PYTHON at an interpreter
# that has them.
########################################################################################################################

set(PENGUIN26_TESTS_ATLAS_DIR    "${CMAKE_SOURCE_DIR}/../atlas-npu/baremetal"
    CACHE PATH "atlas-npu/baremetal directory (assembly/, atlas_asm.py, generators/)")
set(PENGUIN26_TESTS_ATLAS_PYTHON "python3"
    CACHE STRING "Python used to run gen_atlas_tests.py and atlas-npu's gen_*.py (needs numpy/torch/numba)")
set(PENGUIN26_TESTS_ATLAS_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
    CACHE PATH "Where the generated atlas_<name>.c (and golden JSON) are written")

get_filename_component(_penguin26_atlas_dir "${PENGUIN26_TESTS_ATLAS_DIR}" ABSOLUTE)
set(_penguin26_atlas_gen_script "${CMAKE_SOURCE_DIR}/scripts/atlas/gen_atlas_tests.py")

if (NOT EXISTS "${_penguin26_atlas_dir}/atlas_asm.py")
  message(WARNING "penguin26-tests: no atlas_asm.py in PENGUIN26_TESTS_ATLAS_DIR=${_penguin26_atlas_dir}; Atlas tests skipped")
  return()
endif()


option(PMU_ENABLE "Enable Atlas tile PMU collection and final counter dump" OFF)
option(PMU_POLL_ENABLE "Snapshot the Atlas PMU during execution" OFF)
set(PMU_POLL_COUNTER_ID 0 CACHE STRING "PMU counter ID or generated event macro to poll")
set(ATLAS_PMU_HEADER "" CACHE FILEPATH "Optional hardware-generated PMU event/address header")
set(PMU_POLL_INTERVAL 1000 CACHE STRING "Execution polls between PMU snapshots")
option(PMU_POLL_PRINT "Print each PMU polling sample" OFF)
if(PMU_POLL_ENABLE AND NOT PMU_ENABLE)
  message(FATAL_ERROR "PMU_POLL_ENABLE requires PMU_ENABLE")
endif()
if(PMU_POLL_ENABLE AND NOT PMU_POLL_INTERVAL MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "PMU_POLL_INTERVAL must be a positive integer")
endif()


# Generate once per hardware configuration, never during target execution.
set(PMU_MANIFEST "" CACHE FILEPATH "Schema-v2 tile PMU event manifest")
set(PMU_PLATFORM "" CACHE FILEPATH "Matching Chipyard JSON device tree")
set(PMU_SOFTWARE_DIR "${CMAKE_SOURCE_DIR}/../pmu/software" CACHE PATH "pmu/software directory")
if(PMU_MANIFEST OR PMU_PLATFORM)
  if(NOT PMU_MANIFEST OR NOT PMU_PLATFORM)
    message(FATAL_ERROR "Set both PMU_MANIFEST and PMU_PLATFORM")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(_pmu_header "${CMAKE_CURRENT_BINARY_DIR}/tile_pmu_platform.h")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PMU_MANIFEST}" "${PMU_PLATFORM}" "${PMU_SOFTWARE_DIR}/generate_platform_header.py")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${PMU_SOFTWARE_DIR}/generate_platform_header.py"
    --manifest "${PMU_MANIFEST}" --platform "${PMU_PLATFORM}" --output "${_pmu_header}"
    RESULT_VARIABLE _pmu_result)
  if(NOT _pmu_result EQUAL 0)
    message(FATAL_ERROR "PMU platform header generation failed")
  endif()
  set(ATLAS_PMU_HEADER "${_pmu_header}")
endif()

file(GLOB _penguin26_atlas_asm "${_penguin26_atlas_dir}/assembly/*.S")
list(SORT _penguin26_atlas_asm)

add_custom_target(atlas-tests)
set(_penguin26_atlas_n 0)

foreach(_asm IN LISTS _penguin26_atlas_asm)
  get_filename_component(_name ${_asm} NAME_WE)
  set(_tgt "atlas_${_name}")
  set(_c   "${PENGUIN26_TESTS_ATLAS_GEN_DIR}/atlas_${_name}.c")

  # Generator lookup, same as atlas-npu's Makefile: gen_<name>.py, else mxu[01]_ -> mxu_.
  set(_deps "${_asm}" "${_penguin26_atlas_dir}/atlas_asm.py" "${_penguin26_atlas_gen_script}")
  if(_name MATCHES "^dma_perf_" AND EXISTS "${_penguin26_atlas_dir}/generators/gen_dma_perf.py")
    list(APPEND _deps "${_penguin26_atlas_dir}/generators/gen_dma_perf.py")
  endif()
  string(REGEX REPLACE "mxu[01]_" "mxu_" _gen_base "${_name}")
  if (EXISTS "${_penguin26_atlas_dir}/generators/gen_${_name}.py")
    list(APPEND _deps "${_penguin26_atlas_dir}/generators/gen_${_name}.py")
  elseif (EXISTS "${_penguin26_atlas_dir}/generators/gen_${_gen_base}.py")
    list(APPEND _deps "${_penguin26_atlas_dir}/generators/gen_${_gen_base}.py")
  endif()

  add_custom_command(
    OUTPUT  "${_c}"
    COMMAND "${PENGUIN26_TESTS_ATLAS_PYTHON}" "${_penguin26_atlas_gen_script}"
            --atlas-dir "${_penguin26_atlas_dir}"
            --out-dir "${PENGUIN26_TESTS_ATLAS_GEN_DIR}"
            --python "${PENGUIN26_TESTS_ATLAS_PYTHON}"
            "${_name}"
    DEPENDS ${_deps}
    COMMENT "Generating atlas_${_name}.c"
    VERBATIM
  )

  add_executable(${_tgt} "${_c}")
  target_compile_definitions(${_tgt} PRIVATE
    PMU_ENABLE=$<BOOL:${PMU_ENABLE}> PMU_POLL_ENABLE=$<BOOL:${PMU_POLL_ENABLE}>
    PMU_POLL_COUNTER_ID=${PMU_POLL_COUNTER_ID} PMU_POLL_INTERVAL=${PMU_POLL_INTERVAL}
    PMU_POLL_PRINT=$<BOOL:${PMU_POLL_PRINT}>)
  if(ATLAS_PMU_HEADER)
    target_include_directories(${_tgt} PRIVATE "${PMU_SOFTWARE_DIR}")
    target_compile_definitions(${_tgt} PRIVATE ATLAS_PMU_HEADER="${ATLAS_PMU_HEADER}")
  endif()


  # Same compile flags as atlas-npu's tests/CMakeLists.txt (-O2 overrides the global -O1).
  target_compile_options(${_tgt} PRIVATE -std=gnu99 -O2 -fno-common -fno-builtin-printf)

  target_link_libraries(${_tgt} PRIVATE
    -L${CMAKE_BINARY_DIR}/glossy -Wl,--whole-archive glossy -Wl,--no-whole-archive
  )
  target_link_libraries(${_tgt} PRIVATE chip-config)
  target_link_libraries(${_tgt} PRIVATE rocketcore)

  add_dependencies(atlas-tests ${_tgt})
  math(EXPR _penguin26_atlas_n "${_penguin26_atlas_n}+1")
endforeach()

message(STATUS "penguin26-tests: ${_penguin26_atlas_n} Atlas test target(s) from ${_penguin26_atlas_dir}/assembly")
