/*
 * main.c - atlas_irq: correctness of the Atlas doorbell interrupt (platform/penguin26/include/hal_atlas_irq.h).
 *
 * The host cases ring the doorbell from the host itself (atlas_irq_ring_self(), the same msip write an Atlas kernel
 * does); the Atlas cases run src/doorbell.S, which copies the run's tag (dbg1) to a line in DRAM, waits for that
 * DMA, then rings the doorbell and halts. A handler that sees the tag shows that Atlas's output is visible when
 * its doorbell arrives. The host cases come first, so on Spike (no Atlas) they still print before the first Atlas
 * access stops the program.
 *
 * Cases, one "case <name> ok=<0|1>" line each:
 *   ring      10 x: interrupts on, ring, atlas_irq_wait(). Exactly one doorbell and one handler call per ring, the
 *             handler got its context and found msip already cleared, msip is 0 afterwards, the timestamps are in
 *             order (ring <= entry <= acked <= exit), and atlas_irq_wait() left interrupts on.
 *   masked    interrupts off, ring: not taken (count unchanged, msip still 1); atlas_irq_wait() then takes it and
 *             leaves interrupts off.
 *   disabled  atlas_irq_disable(), interrupts on, ring: not taken; atlas_irq_ack() clears msip; atlas_irq_init()
 *             enables it again and resets the count.
 *   single    10 Atlas runs started from main: one doorbell per run, the handler saw this run's tag and STATUS
 *             halted by ECALL, msip is 0 afterwards.
 *   chain     one Atlas run started from main, then the handler starts each next run itself, 10 in all: 10
 *             doorbells, each with its own tag.
 * A watchdog (CLINT timer, 1 s) exits with code 3 if a doorbell never comes.
 * The return value of main() is the verdict: 0 = pass, nonzero = fail.
 */
#include <stdlib.h>

#include "main.h"
#include "hal_atlas_irq.h"

/* src/doorbell.S, encoded with sp26-atlas-acc baremetal/assembler.py @2ae0bef (one 32-bit word each). */
static const uint32_t atlas_program[] = {
  /* the result: VMEM line 0 = tag (dbg1) x 8, DMA it to DRAM_TAG_LINE, wait */
  0xc1102573,  /* CSRR x10, 0xC11            */
  0x00000413,  /* ADDI x8, x0, 0             */
  0x00a42023,  /* SW x10, x8, 0              */
  0x00a42223,  /* SW x10, x8, 4              */
  0x00a42423,  /* SW x10, x8, 8              */
  0x00a42623,  /* SW x10, x8, 12             */
  0x00a42823,  /* SW x10, x8, 16             */
  0x00a42a23,  /* SW x10, x8, 20             */
  0x00a42c23,  /* SW x10, x8, 24             */
  0x00a42e23,  /* SW x10, x8, 28             */
  0x00201067,  /* DELAY 2                    */
  0x900061b7,  /* LI x3, 0x90006000          */
  0x02000693,  /* ADDI x13, x0, 32           */
  0x02d411fb,  /* DMA.STORE x3, x8, x13, 1   */
  0x0200107f,  /* DMA.WAIT 1                 */
  /* the doorbell epilogue (hal_atlas_irq.h): VMEM line 0 = {1, 0, ...}, DMA it to CLINT msip, wait */
  0x00100493,  /* ADDI x9, x0, 1             */
  0x00942023,  /* SW x9, x8, 0               */
  0x00042223,  /* SW x0, x8, 4               */
  0x00042423,  /* SW x0, x8, 8               */
  0x00042623,  /* SW x0, x8, 12              */
  0x00042823,  /* SW x0, x8, 16              */
  0x00042a23,  /* SW x0, x8, 20              */
  0x00042c23,  /* SW x0, x8, 24              */
  0x00042e23,  /* SW x0, x8, 28              */
  0x00201067,  /* DELAY 2                    */
  0x020001b7,  /* LI x3, 0x02000000          */
  0x02d411fb,  /* DMA.STORE x3, x8, x13, 1   */
  0x0200107f,  /* DMA.WAIT 1                 */
  /* dbg0 = 1, halt */
  0x00100f13,  /* ADDI x30, x0, 1            */
  0xc10f1073,  /* CSRRW x0, 0xC10, x30       */
  0x00000073,  /* ECALL                      */
};
#define ATLAS_PROGRAM_WORDS (sizeof(atlas_program) / sizeof(atlas_program[0]))

#define DRAM_TAG_LINE  0x90006000UL   /* the kernel's 32-byte result line */
#define RINGS          10
#define RUNS           10
#define WATCHDOG_TICKS 500000         /* 1 s: mtime runs at core clock / 1000 */
#define STATUS_DONE    (ATLAS_STATUS_HALTED_MSK | (ATLAS_HALT_ECALL << ATLAS_STATUS_REASON_POS))   /* 5 */

static volatile uint32_t *const msip = (volatile uint32_t *)ATLAS_IRQ_DOORBELL_ADDR;
static volatile uint32_t *const tag_line = (volatile uint32_t *)DRAM_TAG_LINE;

/* ---- what the handlers saw ---- */
static volatile uint64_t handler_calls;
static volatile int handler_bad;        /* host cases: wrong context, or msip still set inside the handler */
static int context_token;
static volatile uint32_t current_tag;   /* Atlas cases: the tag of the run in flight */
static volatile int runs_left;          /* chain: runs the handler still starts */
static volatile int seen_ok, seen_bad;  /* Atlas cases: handler checks that passed / failed */

static void on_ring(void *context) {
  handler_calls = handler_calls + 1;
  if (context != &context_token || *msip != 0) {
    handler_bad = 1;
  }
}

/* Clear the result line and start one Atlas run with @p tag. Atlas must be halted. */
static void start_run(uint32_t tag) {
  for (int i = 0; i < 8; i++) {
    tag_line[i] = 0;
  }
  current_tag = tag;
  atlas_set_dbg1(ATLAS, tag);
  atlas_set_dbg0(ATLAS, 0);
  atlas_fence();
  atlas_start(ATLAS);
}

static void on_atlas_doorbell(void *context) {
  (void)context;
  uint32_t status = atlas_get_status(ATLAS);
  for (int i = 0; i < 1000 && status != STATUS_DONE; i++) {
    status = atlas_get_status(ATLAS);   /* the ECALL follows the doorbell store by a few Atlas cycles */
  }
  int ok = (status == STATUS_DONE);
  for (int i = 0; i < 8; i++) {
    ok &= (tag_line[i] == current_tag);
  }
  if (ok) {
    seen_ok = seen_ok + 1;
  } else {
    seen_bad = seen_bad + 1;
  }
  if (runs_left > 0) {
    runs_left = runs_left - 1;
    start_run(current_tag + 1);
  }
}

/* ---- helpers ---- */
void machine_timer_interrupt_callback(void) {
  printf("watchdog: no doorbell\n");
  exit(3);
}

static void watchdog_arm(void) {
  clint_set_timer_interrupt_target(CLINT, (uint32_t)get_hart_id(), clint_get_time(CLINT) + WATCHDOG_TICKS);
  enable_timer_interrupt();
}

static void watchdog_off(void) {
  clint_set_timer_interrupt_target(CLINT, (uint32_t)get_hart_id(), UINT64_MAX);
  disable_timer_interrupt();
}

static int mie_on(void) {
  return (READ_CSR("mstatus") & (1U << 3U)) != 0;
}

/* Long enough for a pending interrupt to be taken, or a second, wrong doorbell to show up. */
static void settle(void) {
  for (volatile int i = 0; i < 2000; i++) {
  }
}

static int report(const char *name, int ok) {
  printf("case %s ok=%d\n", name, ok);
  return ok ? 0 : 1;
}

/* ---- host cases ---- */
static int case_ring(void) {
  atlas_irq_init();
  atlas_irq_set_handler(on_ring, &context_token);
  int ok = (atlas_irq_count() == 0);
  enable_global_interrupt();
  for (int i = 0; i < RINGS; i++) {
    uint64_t calls = handler_calls;
    uint64_t t_ring = get_cycles();
    atlas_irq_ring_self();
    atlas_irq_wait((uint64_t)i + 1);
    settle();
    ok &= (atlas_irq_count() == (uint64_t)i + 1) && (handler_calls == calls + 1) && (*msip == 0) && mie_on();
    ok &= (t_ring <= atlas_irq_timestamps.entry) && (atlas_irq_timestamps.entry <= atlas_irq_timestamps.acked) &&
          (atlas_irq_timestamps.acked <= atlas_irq_timestamps.exit);
  }
  disable_global_interrupt();
  return report("ring", ok && !handler_bad);
}

static int case_masked(void) {
  uint64_t before = atlas_irq_count();
  atlas_irq_ring_self();                        /* interrupts are off */
  settle();
  int ok = (atlas_irq_count() == before) && (*msip == 1);
  atlas_irq_wait(before + 1);
  ok &= (atlas_irq_count() == before + 1) && (*msip == 0) && !mie_on();
  return report("masked", ok);
}

static int case_disabled(void) {
  atlas_irq_disable();
  enable_global_interrupt();
  uint64_t before = atlas_irq_count();
  atlas_irq_ring_self();
  settle();
  int ok = (atlas_irq_count() == before) && (*msip == 1);
  disable_global_interrupt();
  atlas_irq_ack();
  ok &= (*msip == 0);
  atlas_irq_init();
  ok &= (atlas_irq_count() == 0);
  enable_global_interrupt();
  atlas_irq_ring_self();
  atlas_irq_wait(1);
  settle();
  disable_global_interrupt();
  ok &= (atlas_irq_count() == 1) && !handler_bad;
  return report("disabled", ok);
}

/* ---- Atlas cases ---- */
static int case_single(void) {
  atlas_irq_set_handler(on_atlas_doorbell, NULL);
  int ok = 1;
  runs_left = 0;
  for (int r = 0; r < RUNS; r++) {
    uint64_t before = atlas_irq_count();
    int good = seen_ok;
    watchdog_arm();
    start_run(0xA7100000u + (uint32_t)r);
    enable_global_interrupt();
    atlas_irq_wait(before + 1);
    settle();
    disable_global_interrupt();
    watchdog_off();
    ok &= (atlas_irq_count() == before + 1) && (seen_ok == good + 1) && (*msip == 0);
  }
  return report("single", ok && seen_bad == 0);
}

static int case_chain(void) {
  uint64_t before = atlas_irq_count();
  int good = seen_ok;
  runs_left = RUNS - 1;
  watchdog_arm();
  start_run(0xA7200000u);
  enable_global_interrupt();
  atlas_irq_wait(before + RUNS);
  settle();
  disable_global_interrupt();
  watchdog_off();
  int ok = (atlas_irq_count() == before + RUNS) && (seen_ok == good + RUNS) && (seen_bad == 0) && (*msip == 0);
  return report("chain", ok);
}

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

int app_main() {
  int fail = 0;

  fail |= case_ring();
  fail |= case_masked();
  fail |= case_disabled();

  atlas_reset_for_load(ATLAS);
  if (atlas_load_program(ATLAS_IMEM, atlas_program, ATLAS_PROGRAM_WORDS) != 0 ||
      atlas_verify_program(ATLAS_IMEM, atlas_program, ATLAS_PROGRAM_WORDS, 1) != 0) {
    printf("Atlas program load failed\n");
    return 1;
  }
  fail |= case_single();
  fail |= case_chain();
  atlas_irq_disable();

  printf(fail ? "*** FAILED ***\n" : "*** PASSED ***\n");
  return fail;
}

int main(void) {
  app_init();
  return app_main();
}
