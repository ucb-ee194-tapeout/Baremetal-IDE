# CMakeLists definitions for target `atlas_irq`: correctness of the Atlas doorbell interrupt
# (platform/penguin26/include/hal_atlas_irq.h).

#################################
# Build Configuration
#################################

# Source Files (src/doorbell.S is the Atlas program in source form, for reference; it is not compiled)
add_executable(atlas_irq
  src/main.c
)

# Header Files
target_include_directories(atlas_irq PUBLIC include)

#################################
# Dependencies
#################################

# glossy: startup code, stdio (HTIF for PLATFORM=SIMS, UART0 for PLATFORM=CHIP) and exit
target_link_libraries(atlas_irq PRIVATE
  -L${CMAKE_BINARY_DIR}/glossy -Wl,--whole-archive glossy -Wl,--no-whole-archive
)

# chip-config: chip_config.h (addresses) and the chip drivers, including hal_atlas
target_link_libraries(atlas_irq PRIVATE chip-config)
target_link_libraries(atlas_irq PRIVATE rocketcore)

# atlas-irq-timestamps: the doorbell interrupt, with the timestamps the test checks
target_link_libraries(atlas_irq PRIVATE atlas-irq-timestamps)

if (PROF_COV)
  target_link_libraries(atlas_irq PRIVATE gcov)
endif()
