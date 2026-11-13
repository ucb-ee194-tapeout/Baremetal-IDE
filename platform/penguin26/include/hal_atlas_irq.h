/**
 * @file hal_atlas_irq.h
 * @brief The Atlas doorbell: Atlas interrupts the host when a kernel is done, instead of the host polling STATUS.
 *
 * How it works. An Atlas kernel ends with a DMA store of the 32-bit value 1 to the CLINT msip register of hart 0
 * (0x0200_0000). That raises a machine software interrupt (mcause 3) on the host. glossy's trap_vector saves the
 * integer registers and calls machine_software_interrupt_callback(), which this file defines: it clears msip
 * first (msip is level-sensitive; without the clear, mret would take the same interrupt again), counts the
 * doorbell and calls the program's handler.
 *
 * The kernel side, 15 words inserted before the kernel's final "dbg0 = 1; ECALL", after its last DMA.WAIT (Atlas
 * assembler syntax, sp26-atlas-acc baremetal/assembler.py):
 *     ADDI  x8, x0, 0
 *     ADDI  x9, x0, 1
 *     SW    x9, x8, 0            # VMEM word 0 = 1
 *     SW    x0, x8, 4  ...  SW x0, x8, 28   # words 1-7 = 0: the DMA moves a 32-byte line (msip of harts 1-7)
 *     DELAY 2
 *     LI    x3, 0x02000000       # CLINT msip, hart 0
 *     ADDI  x13, x0, 32
 *     DMA.STORE x3, x8, x13, 1   # ring the doorbell
 *     DMA.WAIT  1                # halt only after the store is out
 * Atlas's DMA reaches the CLINT through SBUS with no address filter (sp26-atlas-acc AtlasTile.scala:119-129).
 * Measured on FireSim penguin26 RTL: ~137 host cycles from the msip store to the start of the callback.
 *
 * Rules:
 *   - On penguin26 the machine software interrupt belongs to Atlas: nothing else may use msip of hart 0.
 *   - The handler runs in interrupt context with interrupts off: keep it short, no FP or vector instructions
 *     (glossy's trap entry saves the integer registers only).
 *   - atlas_irq_init() does not set mstatus.MIE: the program decides when it can be interrupted.
 *
 * Timestamps. Link atlas-irq-timestamps instead of atlas-irq to also record mcycle at three points of every
 * doorbell (callback start, msip cleared, callback end); it defines ATLAS_IRQ_TIMESTAMPS for its users. atlas-irq
 * has no timestamp code at all.
 */
#ifndef __HAL_ATLAS_IRQ_H
#define __HAL_ATLAS_IRQ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** CLINT msip of hart 0, the doorbell register an Atlas kernel writes. */
#define ATLAS_IRQ_DOORBELL_ADDR 0x02000000UL

/** Called on every doorbell, after msip is cleared and the doorbell counted. Interrupts are off. */
typedef void (*atlas_irq_handler)(void *context);

/** Clear a stale doorbell, reset the count (and timestamps) and enable the machine software interrupt (mie.MSIE). */
void atlas_irq_init(void);

/** Install the handler called on every doorbell (NULL: only count). */
void atlas_irq_set_handler(atlas_irq_handler handler, void *context);

/** Doorbells taken since atlas_irq_init(). */
uint64_t atlas_irq_count(void);

/** Sleep in wfi until atlas_irq_count() reaches @p target. Race-free: the count is checked with interrupts off,
 *  and wfi wakes on a pending interrupt even then. Leaves mstatus.MIE as it found it. Does not time out. */
void atlas_irq_wait(uint64_t target);

/** Clear msip of this hart (the callback does it itself; use after polling a doorbell kernel with interrupts off). */
void atlas_irq_ack(void);

/** Ring the doorbell from the host itself, e.g. to measure the interrupt path without Atlas. */
void atlas_irq_ring_self(void);

/** Disable the machine software interrupt again (mie.MSIE = 0). */
void atlas_irq_disable(void);

#ifdef ATLAS_IRQ_TIMESTAMPS
/** mcycle at three points of the last doorbell. */
typedef struct {
  uint64_t entry;   /* the callback started (glossy's trap entry and register save are before it) */
  uint64_t acked;   /* msip cleared */
  uint64_t exit;    /* the callback finished, handler included (glossy's restore and mret follow) */
} atlas_irq_timestamps_t;

extern volatile atlas_irq_timestamps_t atlas_irq_timestamps;
#endif

#ifdef __cplusplus
}
#endif

#endif /* __HAL_ATLAS_IRQ_H */
