/*
 * tacit_trace.c - opt-in TACIT trace of a whole program (PLATFORM=SIMS, -DPENGUIN26_TACIT_TRACE=ON).
 *
 * platform/penguin26/CMakeLists.txt links such builds with -Wl,--wrap=main, so glossy's crt0 calls
 * __wrap_main() below. It points trace encoder 0 at sink target 0 (TraceSinkAlways: always ready, so
 * the core is never back-pressured), selects branch-target mode (as examples/pmu-tests/trace-hello.c),
 * starts the encoder, runs the program's own main(), then stops it and prints one line:
 *   TACIT stall=<encoder stall cycles> rc=<main's return value>
 * The packets are captured by the simulation-only TraceSinkMonitor on the trace arbiter, which writes
 * every fired byte to trace_monitor_shuttle_tile_0.encoded.trace in the simulator's working directory
 * (VCS, FireSim metasim). Decode with software/tacit_decoder (--binary <elf> --encoded-trace <file>).
 *
 * A program that drives the encoder itself (e.g. saturn vec-mx-axpy) still works: its own
 * start/stop calls take over from the first one.
 *
 * Default builds (option OFF): this file compiles to nothing and no --wrap is added.
 */
#include "chip_config.h"

#if defined(PENGUIN26_TACIT_TRACE)

#include <stdio.h>

int __real_main(int argc, char **argv, char **envp);

int __wrap_main(int argc, char **argv, char **envp) {
  l_trace_encoder_configure_target(TRACE_ENCODER0, TARGET_PRINT);
  l_trace_encoder_configure_branch_mode(TRACE_ENCODER0, BRANCH_MODE_TARGET);
  l_trace_encoder_start(TRACE_ENCODER0);
  asm volatile ("fence" ::: "memory");

  int rc = __real_main(argc, argv, envp);

  l_trace_encoder_stop(TRACE_ENCODER0);
  asm volatile ("fence" ::: "memory");
  printf("TACIT stall=%llu rc=%d\n",
         (unsigned long long)PENGUIN26_TRACE_ENCODER0->STALL, rc);
  return rc;
}

#endif
