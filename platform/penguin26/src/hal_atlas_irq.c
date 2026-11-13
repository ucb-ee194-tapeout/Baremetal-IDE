/**
 * @file hal_atlas_irq.c
 * @brief The Atlas doorbell interrupt, see hal_atlas_irq.h. Built twice: atlas-irq and atlas-irq-timestamps
 *        (with ATLAS_IRQ_TIMESTAMPS).
 */
#include "hal_atlas_irq.h"

#include "chip_config.h"
#include "rocketcore.h"
#include "riscv_encoding.h"  /* IRQ_M_SOFT */

static volatile uint64_t doorbells;
static atlas_irq_handler irq_handler;
static void *irq_context;

#ifdef ATLAS_IRQ_TIMESTAMPS
volatile atlas_irq_timestamps_t atlas_irq_timestamps;
#define TIMESTAMP(field) (atlas_irq_timestamps.field = get_cycles())
#else
#define TIMESTAMP(field) ((void)0)
#endif

void atlas_irq_ack(void) {
  clint_clear_software_interrupt(CLINT, (uint32_t)get_hart_id());
}

void atlas_irq_ring_self(void) {
  clint_trigger_software_interrupt(CLINT, (uint32_t)get_hart_id());
}

void atlas_irq_init(void) {
  atlas_irq_ack();
  doorbells = 0;
#ifdef ATLAS_IRQ_TIMESTAMPS
  atlas_irq_timestamps.entry = 0;
  atlas_irq_timestamps.acked = 0;
  atlas_irq_timestamps.exit = 0;
#endif
  enable_irq(IRQ_M_SOFT);
}

void atlas_irq_disable(void) {
  disable_irq(IRQ_M_SOFT);
}

void atlas_irq_set_handler(atlas_irq_handler handler, void *context) {
  irq_context = context;
  irq_handler = handler;
}

uint64_t atlas_irq_count(void) {
  return doorbells;
}

void atlas_irq_wait(uint64_t target) {
  uint64_t mie_was_on = READ_CSR("mstatus") & (1U << 3U);
  disable_global_interrupt();
  while (doorbells < target) {
    asm volatile("wfi");
    enable_global_interrupt();    /* take the pending doorbell */
    disable_global_interrupt();
  }
  if (mie_was_on) {
    enable_global_interrupt();
  }
}

/* Overrides the weak default in glossy/src/trap/trap.c; trap_handler() calls it for mcause 3. */
void machine_software_interrupt_callback(void) {
  TIMESTAMP(entry);
  atlas_irq_ack();
  TIMESTAMP(acked);
  doorbells = doorbells + 1;
  if (irq_handler) {
    irq_handler(irq_context);
  }
  TIMESTAMP(exit);
}
