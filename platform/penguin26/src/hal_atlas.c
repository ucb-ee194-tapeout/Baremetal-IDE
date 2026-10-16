#include <stdio.h>

#include "chip_config.h"
#include "hal_atlas.h"

void atlas_reset_for_load(ATLAS_Type *atlas) {
  atlas_stop(atlas);
  atlas_set_dbg0(atlas, 0);
  atlas_set_dbg1(atlas, 0);
  atlas_fence();
}

int atlas_load_program(volatile uint32_t *imem, const uint32_t *program, uint32_t n_words) {
  if ((uint64_t)n_words * 4U > ATLAS_IMEM_SIZE) {
    return -1;
  }
  for (uint32_t i = 0; i < n_words; i++) {
    imem[i] = program[i];
  }
  atlas_fence();
  return 0;
}

uint32_t atlas_verify_program(volatile uint32_t *imem, const uint32_t *program, uint32_t n_words,
                              int verbose) {
  uint32_t fail = 0;
  for (uint32_t i = 0; i < n_words; i++) {
    uint32_t got = imem[i];
    if (got != program[i]) {
      if (verbose) {
        printf("MISMATCH word[%u]: expected 0x%016x, got 0x%08x\n",
               (unsigned)i, (unsigned)program[i], (unsigned)got);
      }
      fail++;
    }
  }
  return fail;
}

uint32_t atlas_wait_dbg0(ATLAS_Type *atlas, uint32_t max_polls) {
  uint32_t dbg0 = 0;
  for (uint32_t i = 0; i < max_polls; i++) {
    dbg0 = atlas_get_dbg0(atlas);
    if (dbg0 != 0) break;
  }
  return dbg0;
}

int atlas_wait_halted(ATLAS_Type *atlas, uint32_t max_polls) {
  for (uint32_t i = 0; i < max_polls; i++) {
    if (atlas_is_halted(atlas)) return 1;
  }
  return 0;
}

void atlas_mem_preload(const uint64_t *addrs, const uint32_t *words, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    atlas_mem_write32(addrs[i], words[i]);
  }
  atlas_fence();
}

uint32_t atlas_mem_check(const uint64_t *addrs, const uint32_t *expected, uint32_t n, int verbose) {
  uint32_t fail = 0;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t got = atlas_mem_read32(addrs[i]);
    if (got != expected[i]) {
      if (verbose) {
        printf("  DRAM MISMATCH word[%u] @ 0x%08X%08X: expected 0x%08x, got 0x%08x\n",
               (unsigned)i,
               (unsigned)((uint64_t)addrs[i] >> 32),
               (unsigned)((uint64_t)addrs[i] & 0xFFFFFFFFU),
               (unsigned)expected[i], (unsigned)got);
      }
      fail++;
    }
  }
  return fail;
}

static const char *const atlas_perf_base_names[ATLAS_PERF_NUM_BASE] = {
  "run_cycles",
  "dma_active_cycles",
  "dma_commands",
  "dma_beats_issued",
  "dma_beats_completed",
  "dma_vmem_stall_cycles",
  "dma_saturated_cycles",
  "dma_inflight_limit_blocked_cycles",
  "dma_outstanding_cycles",
  "dma_response_wait_beat_cycles",
  "dma_fence_wait_cycles",
  "vpu_active_cycles",
  "vpu_commands",
  "mxu0_compute_cycles",
  "mxu0_data_cycles",
  "mxu0_commands",
  "mxu1_compute_cycles",
  "mxu1_data_cycles",
  "mxu1_commands",
};

static const char *const atlas_perf_mreg_kinds[4] = {
  "read_cycles", "write_cycles", "read_contended_cycles", "write_contended_cycles",
};

static const char *const atlas_perf_vmem_kinds[3] = {
  "read_cycles", "write_cycles", "contended_cycles",
};

const char *atlas_perf_name(uint32_t idx, char *buf, size_t len) {
  if (idx < ATLAS_PERF_NUM_BASE) {
    snprintf(buf, len, "%s", atlas_perf_base_names[idx]);
    return buf;
  }
  idx -= ATLAS_PERF_NUM_BASE;
  if (idx < 4U * ATLAS_PERF_MREG_BANKS) {
    snprintf(buf, len, "mreg_bank_%u_%s", (unsigned)(idx / 4U), atlas_perf_mreg_kinds[idx % 4U]);
    return buf;
  }
  idx -= 4U * ATLAS_PERF_MREG_BANKS;
  if (idx < 3U * ATLAS_PERF_VMEM_BANKS) {
    snprintf(buf, len, "vmem_bank_%u_%s", (unsigned)(idx / 3U), atlas_perf_vmem_kinds[idx % 3U]);
    return buf;
  }
  return NULL;
}

/* Only nonzero counters are printed (plus PERF_COUNT, after which a missing name means 0): in
 * simulation, HTIF console output costs ~200 target cycles per byte (measured with TACIT: printing all
 * 19 base counters took ~240k cycles, almost all in htif_syscall). One printf for the whole block. */
void atlas_perf_print(ATLAS_Type *atlas) {
  static char out[ATLAS_PERF_NUM_COUNTERS * 56 + 32];
  char name[48];
  size_t n = 0;
  for (uint32_t i = 0; i < ATLAS_PERF_NUM_COUNTERS; i++) {
    uint32_t v = atlas_perf_read(atlas, i);
    if (v != 0) {
      n += (size_t)snprintf(out + n, sizeof(out) - n, "PERF %s=%u\n",
                            atlas_perf_name(i, name, sizeof(name)), (unsigned)v);
    }
  }
  snprintf(out + n, sizeof(out) - n, "PERF_COUNT n=%u\n", (unsigned)ATLAS_PERF_NUM_COUNTERS);
  printf("%s", out);
}
