#ifndef BBK9288S_C33JIT_H
#define BBK9288S_C33JIT_H

#include <stdint.h>

#include "c33vm.h"

typedef struct c33_jit_stats {
    uint32_t enabled;
    uint32_t cache_size;
    uint32_t cache_used;
    uint32_t blocks_compiled;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t cache_flushes;
    uint32_t blocks_verified;
    uint32_t verification_failures;
    uint32_t last_failed_pc;
    uint32_t fallback_steps;
    uint64_t native_instructions;
    uint32_t negative_hits;
    uint32_t compile_failures;
    uint64_t lookup_probes;
    uint32_t max_lookup_probes;
    uint32_t dispatcher_calls;
    uint32_t native_blocks;
    uint32_t max_chain_blocks;
} c33_jit_stats_t;

/*
 * The caller owns executable_cache. On BBK 9588 it must come from bda_alloc,
 * whose heap is executable like the cache used by gba-for9588.
 */
int c33_jit_init(
    c33_vm_t *vm,
    void *executable_cache,
    uint32_t cache_size
);
void c33_jit_reset(c33_vm_t *vm);
uint32_t c33_jit_run_block(c33_vm_t *vm, uint32_t budget);
void c33_jit_note_fallback(c33_vm_t *vm);
void c33_jit_get_stats(c33_vm_t *vm, c33_jit_stats_t *stats);

#endif
