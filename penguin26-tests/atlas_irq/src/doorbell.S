# doorbell.S - the smallest Atlas kernel that ends with the doorbell (hal_atlas_irq.h).
#
# It copies the host's run tag (dbg1) into one 32-byte line in DRAM at 0x9000_6000, waits for that DMA, and only
# then rings the doorbell: a DMA store of {1, 0, ..., 0} to CLINT msip of hart 0. A handler that sees the tag in
# DRAM therefore proves the output is visible when the doorbell arrives.
#
# For reference; not compiled. src/main.c holds the words, encoded with the Atlas team's assembler
# (sp26-atlas-acc baremetal/assembler.py @2ae0bef). The epilogue is the one documented in hal_atlas_irq.h.

_start:
    CSRR  x10, 0xC11              # x10 = run tag (dbg1, written by the host)
    ADDI  x8, x0, 0
    SW    x10, x8, 0              # VMEM line 0 = tag x 8
    SW    x10, x8, 4
    SW    x10, x8, 8
    SW    x10, x8, 12
    SW    x10, x8, 16
    SW    x10, x8, 20
    SW    x10, x8, 24
    SW    x10, x8, 28
    DELAY 2
    LI    x3, 0x90006000
    ADDI  x13, x0, 32
    DMA.STORE x3, x8, x13, 1      # the "result": VMEM line 0 -> DRAM
    DMA.WAIT  1

# doorbell epilogue (hal_atlas_irq.h)
    ADDI  x9, x0, 1
    SW    x9, x8, 0               # VMEM line 0 = {1, 0, ..., 0}
    SW    x0, x8, 4
    SW    x0, x8, 8
    SW    x0, x8, 12
    SW    x0, x8, 16
    SW    x0, x8, 20
    SW    x0, x8, 24
    SW    x0, x8, 28
    DELAY 2
    LI    x3, 0x02000000          # CLINT msip, hart 0
    DMA.STORE x3, x8, x13, 1      # ring the doorbell
    DMA.WAIT  1

    ADDI  x30, x0, 1
    CSRRW x0, 0xC10, x30          # dbg0 = 1
    ECALL
