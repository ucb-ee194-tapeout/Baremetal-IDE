/*
 * main.c - atlas_compiled: run an Atlas program built by the Atlas MLIR compiler, with a host
 * program compiled by Baremetal-IDE.
 *
 * The compiler supplies only device-side output (see ../CMakeLists.txt):
 *   atlas_program.h        atlas_program_words[] / ATLAS_PROGRAM_WORD_COUNT
 *   atlas_captured_data.h  constants to preload, input address, output slots
 * Everything else here is ordinary Baremetal-IDE code: hal_atlas for IMEM/CSR access, printf over
 * HTIF (PLATFORM=SIMS) or UART0 (PLATFORM=CHIP), and main()'s return value as the verdict.
 *
 * This host is written for the captured attention+MLP example
 * (atlas-mlir examples/handoff/captured_attention_mlp). Its inputs and expected outputs are the
 * ones in that example's host.c: one or two BF16 result tiles of 32x32, every cell checked, plus a
 * 32-byte guard after the last tile that the program must not touch.
 */
#include "main.h"

#include "atlas_program.h"
#include "atlas_captured_data.h"

#if ATLAS_CAPTURED_OUTPUT_COUNT < 1 || ATLAS_CAPTURED_OUTPUT_COUNT > 2
#error "This host supports one or two result tiles"
#endif

#define OUTPUT_CELLS 1024U  /* one 32x32 BF16 tile */
#define GUARD_BYTES  32U

void app_init() {
#if defined(TERMINAL_DEVICE_UART0)
  /* PLATFORM=CHIP: stdio goes to UART0, whose divisor comes from SYS_CLK_FREQ. */
  UART_InitType UART_init_config;
  UART_init_config.baudrate = 115200;
  UART_init_config.mode = UART_MODE_TX_RX;
  UART_init_config.stopbits = UART_STOPBITS_2;
  uart_init(UART0, &UART_init_config);
#endif
}

/* Weights and other constants the compiled program expects in DRAM. */
static void copy_constants(void) {
  for (size_t blob = 0; blob < ATLAS_CAPTURED_CONSTANT_COUNT; blob++) {
    const atlas_init_blob *item = &atlas_captured_constants[blob];
    volatile uint8_t *dst = (volatile uint8_t *)item->address;
    for (size_t i = 0; i < item->size; i++) {
      dst[i] = item->data[i];
    }
  }
  atlas_fence();
}

static uintptr_t guard_address(void) {
  const atlas_output_slot *last = &atlas_captured_outputs[ATLAS_CAPTURED_OUTPUT_COUNT - 1];
  return last->address + last->size;
}

/* FP8 E4M3 input (0x30 = 0.5, 0x38 = 1.0); outputs and guard filled with known garbage. */
static void prepare_input(unsigned run) {
  volatile uint8_t *input = (volatile uint8_t *)ATLAS_CAPTURED_INPUT_ADDRESS;
  for (size_t i = 0; i < ATLAS_CAPTURED_INPUT_BYTES; i++) {
    if (run == 2) {
      input[i] = (i % 32U) % 2U ? 0x38U : 0x30U;
    } else {
      input[i] = run == 0 ? 0x30U : 0x38U;
    }
  }
  for (size_t r = 0; r < ATLAS_CAPTURED_OUTPUT_COUNT; r++) {
    volatile uint8_t *out = (volatile uint8_t *)atlas_captured_outputs[r].address;
    for (size_t i = 0; i < atlas_captured_outputs[r].size; i++) {
      out[i] = 0xA5U;
    }
  }
  volatile uint8_t *guard = (volatile uint8_t *)guard_address();
  for (size_t i = 0; i < GUARD_BYTES; i++) {
    guard[i] = 0x5AU;
  }
  atlas_fence();
}

/* Expected BF16 cell, as in the example's host.c. */
static uint16_t expected_cell(unsigned run, size_t result, size_t physical_index) {
  if (ATLAS_CAPTURED_OUTPUT_COUNT == 2 && result == 0) {
    return run == 0 ? 0x3E00U : 0x3E80U;
  }
  if (run == 0) return 0x3D80U;
  if (run == 1) return 0x3E00U;
  /* BF16 register pairs store columns 0-15 before columns 16-31. */
  size_t column = (physical_index / 512U) * 16U + physical_index % 16U;
  return column % 2U ? 0x3D80U : 0x3E00U;
}

static uint32_t check_output(unsigned run) {
  uint32_t mismatches = 0;
  for (size_t r = 0; r < ATLAS_CAPTURED_OUTPUT_COUNT; r++) {
    volatile const uint16_t *out = (volatile const uint16_t *)atlas_captured_outputs[r].address;
    for (size_t i = 0; i < OUTPUT_CELLS; i++) {
      uint16_t want = expected_cell(run, r, i);
      if (out[i] != want) {
        if (mismatches < 8) {
          printf("  run %u result %u cell %u: got 0x%04x expected 0x%04x\n", run, (unsigned)r,
                 (unsigned)i, (unsigned)out[i], (unsigned)want);
        }
        mismatches++;
      }
    }
  }
  volatile const uint8_t *guard = (volatile const uint8_t *)guard_address();
  for (size_t i = 0; i < GUARD_BYTES; i++) {
    if (guard[i] != 0x5AU) {
      if (mismatches < 8) {
        printf("  run %u guard byte %u overwritten: 0x%02x\n", run, (unsigned)i, (unsigned)guard[i]);
      }
      mismatches++;
    }
  }
  return mismatches;
}

/*
 * Start the loaded program and wait for it to halt. Returns the halt reason, or -1 on timeout.
 * A halt reason left over from the previous launch can stay visible until the start write reaches
 * the CSR file, so a halt only counts after the core has been seen running (or with the reason
 * cleared) once. This is the same rule as the compiler's runtime (atlas_host_poll).
 */
static int run_program(void) {
  atlas_start(ATLAS);
  atlas_fence();
  int started = 0;
  for (uint32_t i = 0; i < ATLAS_DEFAULT_POLL_LIMIT; i++) {
    uint32_t status = atlas_get_status(ATLAS);
    uint32_t reason = (status & ATLAS_STATUS_REASON_MSK) >> ATLAS_STATUS_REASON_POS;
    if (!(status & ATLAS_STATUS_HALTED_MSK) || reason == ATLAS_HALT_NONE) {
      started = 1;
    } else if (started) {
      return (int)reason;
    }
  }
  return -1;
}

int app_main() {
  int fail = 0;

  printf("Atlas program: %u words, sha256 %s\n", (unsigned)ATLAS_PROGRAM_WORD_COUNT,
         ATLAS_PROGRAM_SHA256);

  copy_constants();

  atlas_reset_for_load(ATLAS);
  if (!atlas_is_halted(ATLAS)) {
    printf("Atlas core is not halted; cannot load IMEM\n");
    return 1;
  }
  if (atlas_load_program(ATLAS_IMEM, atlas_program_words, ATLAS_PROGRAM_WORD_COUNT) != 0) {
    printf("Program does not fit in Atlas IMEM\n");
    return 1;
  }
  if (atlas_verify_program(ATLAS_IMEM, atlas_program_words, ATLAS_PROGRAM_WORD_COUNT, 1) != 0) {
    printf("IMEM readback mismatch\n");
    return 1;
  }

  const unsigned runs = ATLAS_CAPTURED_OUTPUT_COUNT == 1 ? 3U : 2U;
  for (unsigned run = 0; run < runs; run++) {
    prepare_input(run);
    uint32_t cycles_before = atlas_get_cycles(ATLAS);
    int reason = run_program();
    uint32_t cycles = atlas_get_cycles(ATLAS) - cycles_before;
    if (reason != ATLAS_HALT_ECALL) {
      printf("run %u: Atlas did not halt with ECALL (reason %d, illegal pc 0x%x)\n", run, reason,
             (unsigned)atlas_get_illegal_pc(ATLAS));
      fail = 1;
      break;
    }
    uint32_t mismatches = check_output(run);
    printf("run %u: %u mismatches, about %u Atlas cycles\n", run, (unsigned)mismatches,
           (unsigned)cycles);
    if (mismatches) {
      fail = 1;
    }
  }

  printf(fail ? "*** FAILED ***\n" : "*** PASSED ***\n");
  return fail;
}

int main(void) {
  app_init();
  return app_main();
}
