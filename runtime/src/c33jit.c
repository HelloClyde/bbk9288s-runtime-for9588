#include "../include/c33jit.h"

#include <stddef.h>
#include <stdint.h>

#if defined(__mips__) && UINTPTR_MAX == 0xffffffffu
#define C33_JIT_NATIVE_MIPS 1
#else
#define C33_JIT_NATIVE_MIPS 0
#endif

#define C33_JIT_MAX_ENTRIES       4096u
#define C33_JIT_MAX_BLOCK_INSNS   24u
#define C33_JIT_MAX_BLOCK_WORDS   640u
#define C33_JIT_CACHE_ALIGNMENT   16u
#define C33_JIT_CACHE_LINE        32u
#define C33_JIT_LOOKUP_SLOTS      16384u
#define C33_JIT_LOOKUP_MASK       (C33_JIT_LOOKUP_SLOTS - 1u)

#define C33_JIT_LOOKUP_EMPTY       0u
#define C33_JIT_LOOKUP_ENTRY       1u
#define C33_JIT_LOOKUP_UNSUPPORTED 2u

#define C33_JIT_MEM_SIZE_1 0u
#define C33_JIT_MEM_SIZE_2 1u
#define C33_JIT_MEM_SIZE_4 2u
#define C33_JIT_MEM_STORE  (1u << 2)
#define C33_JIT_MEM_SIGNED (1u << 3)
#define C33_JIT_MEM_BIT_CLEAR (1u << 4)
#define C33_JIT_MEM_BIT_SET   (1u << 5)
#define C33_JIT_MEM_BIT_NOT   (1u << 6)
#define C33_JIT_MEM_BIT_MASK \
    (C33_JIT_MEM_BIT_CLEAR | C33_JIT_MEM_BIT_SET | \
     C33_JIT_MEM_BIT_NOT)

typedef uint32_t (*c33_jit_entry_fn)(c33_vm_t *vm);

typedef struct c33_jit_entry {
    uint32_t guest_pc;
    uint32_t source_instructions;
    c33_jit_entry_fn native_entry;
    uint8_t verified;
    uint8_t invalid;
} c33_jit_entry_t;

typedef struct c33_jit_lookup {
    uint32_t guest_pc;
    uint16_t entry_index;
    uint8_t state;
    uint8_t reserved;
} c33_jit_lookup_t;

typedef struct c33_jit_context {
    c33_vm_t *owner;
    uint8_t *cache;
    uint32_t cache_size;
    uint32_t cache_used;
    c33_jit_entry_t entries[C33_JIT_MAX_ENTRIES];
    c33_jit_lookup_t lookup[C33_JIT_LOOKUP_SLOTS];
    uint32_t entry_count;
    c33_jit_stats_t stats;
} c33_jit_context_t;

static c33_jit_context_t g_c33_jit;

static void jit_zero(void *pointer, uint32_t size)
{
    uint8_t *bytes = (uint8_t *)pointer;
    while (size--) {
        *bytes++ = 0u;
    }
}

static void jit_clear_translation_state(c33_jit_context_t *context)
{
    context->cache_used = 0u;
    context->entry_count = 0u;
    jit_zero(context->entries, sizeof(context->entries));
    jit_zero(context->lookup, sizeof(context->lookup));
    context->stats.cache_used = 0u;
}

#if C33_JIT_NATIVE_MIPS

static void jit_copy_vm(c33_vm_t *destination, const c33_vm_t *source)
{
    uint32_t index;
    for (index = 0u; index < 16u; ++index) {
        destination->regs[index] = source->regs[index];
    }
    destination->pc = source->pc;
    destination->psr = source->psr;
    destination->sp = source->sp;
    destination->alr = source->alr;
    destination->ahr = source->ahr;
    destination->ext[0] = source->ext[0];
    destination->ext[1] = source->ext[1];
    destination->ext_count = source->ext_count;
    for (index = 0u; index < C33_VM_MAX_REGIONS; ++index) {
        destination->regions[index].guest_base =
            source->regions[index].guest_base;
        destination->regions[index].size = source->regions[index].size;
        destination->regions[index].host = source->regions[index].host;
        destination->regions[index].write_count =
            source->regions[index].write_count;
        destination->regions[index].writable =
            source->regions[index].writable;
    }
    destination->region_count = source->region_count;
    for (index = 0u; index < C33_VM_MAX_CALLBACKS; ++index) {
        destination->callbacks[index].resume_pc =
            source->callbacks[index].resume_pc;
        destination->callbacks[index].resume_sp =
            source->callbacks[index].resume_sp;
    }
    destination->callback_depth = source->callback_depth;
    destination->yield_reason = source->yield_reason;
    destination->hostcall = source->hostcall;
    destination->hostcall_opaque = source->hostcall_opaque;
    destination->fault_pc = source->fault_pc;
    destination->fault_address = source->fault_address;
    destination->fault_opcode = source->fault_opcode;
    destination->jit_value = source->jit_value;
    destination->instructions = source->instructions;
}

static int jit_same_architecture(
    const c33_vm_t *left,
    const c33_vm_t *right
)
{
    uint32_t index;
    for (index = 0u; index < 16u; ++index) {
        if (left->regs[index] != right->regs[index]) {
            return 0;
        }
    }
    if (left->pc != right->pc ||
        left->psr != right->psr ||
        left->sp != right->sp ||
        left->alr != right->alr ||
        left->ahr != right->ahr ||
        left->ext_count != right->ext_count ||
        left->fault_pc != right->fault_pc ||
        left->fault_address != right->fault_address ||
        left->fault_opcode != right->fault_opcode ||
        left->instructions != right->instructions) {
        return 0;
    }
    if (left->ext_count >= 1u &&
        left->ext[0] != right->ext[0]) {
        return 0;
    }
    if (left->ext_count >= 2u &&
        left->ext[1] != right->ext[1]) {
        return 0;
    }
    return 1;
}

static uint32_t jit_memory_access(
    c33_vm_t *vm,
    uint32_t address,
    uint32_t descriptor,
    uint32_t store_value
)
{
    uint8_t bytes[4];
    uint32_t size_code = descriptor & 3u;
    uint32_t size =
        size_code == C33_JIT_MEM_SIZE_1 ? 1u :
        size_code == C33_JIT_MEM_SIZE_2 ? 2u : 4u;
    uint32_t value;
    if (descriptor & C33_JIT_MEM_BIT_MASK) {
        if (!c33_vm_read(vm, address, bytes, 1u)) {
            vm->fault_address = address;
            return 0u;
        }
        if (descriptor & C33_JIT_MEM_BIT_CLEAR) {
            bytes[0] &= (uint8_t)~store_value;
        } else if (descriptor & C33_JIT_MEM_BIT_SET) {
            bytes[0] |= (uint8_t)store_value;
        } else {
            bytes[0] ^= (uint8_t)store_value;
        }
        if (!c33_vm_write(vm, address, bytes, 1u)) {
            vm->fault_address = address;
            return 0u;
        }
        vm->jit_value = bytes[0];
        return 1u;
    }
    if (descriptor & C33_JIT_MEM_STORE) {
        bytes[0] = (uint8_t)store_value;
        bytes[1] = (uint8_t)(store_value >> 8);
        bytes[2] = (uint8_t)(store_value >> 16);
        bytes[3] = (uint8_t)(store_value >> 24);
        if (!c33_vm_write(vm, address, bytes, size)) {
            vm->fault_address = address;
            return 0u;
        }
        return 1u;
    }
    if (!c33_vm_read(vm, address, bytes, size)) {
        vm->fault_address = address;
        return 0u;
    }
    value = bytes[0];
    if (size >= 2u) {
        value |= (uint32_t)bytes[1] << 8;
    }
    if (size == 4u) {
        value |= (uint32_t)bytes[2] << 16;
        value |= (uint32_t)bytes[3] << 24;
    } else if (descriptor & C33_JIT_MEM_SIGNED) {
        if (size == 1u) {
            value = (uint32_t)(int32_t)(int8_t)value;
        } else {
            value = (uint32_t)(int32_t)(int16_t)value;
        }
    }
    vm->jit_value = value;
    return 1u;
}

static uint32_t jit_align(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

enum mips_register {
    MIPS_ZERO = 0,
    MIPS_V0 = 2,
    MIPS_A0 = 4,
    MIPS_A1 = 5,
    MIPS_A2 = 6,
    MIPS_A3 = 7,
    MIPS_T0 = 8,
    MIPS_T1 = 9,
    MIPS_T2 = 10,
    MIPS_T3 = 11,
    MIPS_T4 = 12,
    MIPS_T5 = 13,
    MIPS_T6 = 14,
    MIPS_T7 = 15,
    MIPS_T8 = 24,
    MIPS_T9 = 25,
    MIPS_SP = 29,
    MIPS_RA = 31
};

typedef struct mips_emitter {
    uint32_t words[C33_JIT_MAX_BLOCK_WORDS];
    uint32_t count;
    uint32_t bailout_branches[C33_JIT_MAX_BLOCK_INSNS];
    uint32_t bailout_count;
    int failed;
} mips_emitter_t;

static uint32_t mips_r(
    uint32_t rs,
    uint32_t rt,
    uint32_t rd,
    uint32_t shift,
    uint32_t function
)
{
    return (rs << 21) | (rt << 16) | (rd << 11) |
           (shift << 6) | function;
}

static uint32_t mips_i(
    uint32_t operation,
    uint32_t rs,
    uint32_t rt,
    uint32_t immediate
)
{
    return (operation << 26) | (rs << 21) | (rt << 16) |
           (immediate & 0xffffu);
}

static void mips_emit(mips_emitter_t *emitter, uint32_t instruction)
{
    if (emitter->count >= C33_JIT_MAX_BLOCK_WORDS) {
        emitter->failed = 1;
        return;
    }
    emitter->words[emitter->count++] = instruction;
}

static void mips_nop(mips_emitter_t *emitter)
{
    mips_emit(emitter, 0u);
}

static void mips_lw(
    mips_emitter_t *emitter,
    uint32_t target,
    uint32_t base,
    uint32_t offset
)
{
    mips_emit(emitter, mips_i(0x23u, base, target, offset));
}

static void mips_sw(
    mips_emitter_t *emitter,
    uint32_t source,
    uint32_t base,
    uint32_t offset
)
{
    mips_emit(emitter, mips_i(0x2bu, base, source, offset));
}

static void mips_li(
    mips_emitter_t *emitter,
    uint32_t target,
    uint32_t value
)
{
    if ((uint32_t)(int32_t)(int16_t)value == value) {
        mips_emit(emitter, mips_i(0x09u, MIPS_ZERO, target, value));
    } else if ((value & 0xffff0000u) == 0u) {
        mips_emit(emitter, mips_i(0x0du, MIPS_ZERO, target, value));
    } else {
        mips_emit(
            emitter,
            mips_i(0x0fu, MIPS_ZERO, target, value >> 16)
        );
        if (value & 0xffffu) {
            mips_emit(emitter, mips_i(0x0du, target, target, value));
        }
    }
}

static void mips_move(
    mips_emitter_t *emitter,
    uint32_t destination,
    uint32_t source
)
{
    mips_emit(
        emitter,
        mips_r(source, MIPS_ZERO, destination, 0u, 0x21u)
    );
}

static void jit_emit_call_prologue(mips_emitter_t *emitter)
{
    mips_emit(emitter, mips_i(0x09u, MIPS_SP, MIPS_SP, 0xfff0u));
    mips_sw(emitter, MIPS_RA, MIPS_SP, 12u);
    mips_sw(emitter, MIPS_A0, MIPS_SP, 8u);
}

static void jit_emit_call_restore(mips_emitter_t *emitter)
{
    mips_lw(emitter, MIPS_A0, MIPS_SP, 8u);
    mips_lw(emitter, MIPS_RA, MIPS_SP, 12u);
    mips_emit(emitter, mips_i(0x09u, MIPS_SP, MIPS_SP, 16u));
}

static uint32_t vm_reg_offset(uint32_t guest_register)
{
    return (uint32_t)offsetof(c33_vm_t, regs) +
           guest_register * sizeof(uint32_t);
}

static void mips_load_guest_reg(
    mips_emitter_t *emitter,
    uint32_t host_register,
    uint32_t guest_register
)
{
    mips_lw(emitter, host_register, MIPS_A0, vm_reg_offset(guest_register));
}

static void mips_store_guest_reg(
    mips_emitter_t *emitter,
    uint32_t host_register,
    uint32_t guest_register
)
{
    mips_sw(emitter, host_register, MIPS_A0, vm_reg_offset(guest_register));
}

static void mips_set_nz(
    mips_emitter_t *emitter,
    uint32_t result
)
{
    uint32_t psr_offset = (uint32_t)offsetof(c33_vm_t, psr);
    mips_lw(emitter, MIPS_T3, MIPS_A0, psr_offset);
    mips_emit(emitter, mips_i(0x09u, MIPS_ZERO, MIPS_T4, 0xfffcu));
    mips_emit(
        emitter,
        mips_r(MIPS_T3, MIPS_T4, MIPS_T3, 0u, 0x24u)
    );
    mips_emit(emitter, mips_r(MIPS_ZERO, result, MIPS_T4, 31u, 0x02u));
    mips_emit(
        emitter,
        mips_r(MIPS_T3, MIPS_T4, MIPS_T3, 0u, 0x25u)
    );
    mips_emit(emitter, mips_i(0x0bu, result, MIPS_T4, 1u));
    mips_emit(emitter, mips_r(MIPS_ZERO, MIPS_T4, MIPS_T4, 1u, 0x00u));
    mips_emit(
        emitter,
        mips_r(MIPS_T3, MIPS_T4, MIPS_T3, 0u, 0x25u)
    );
    mips_sw(emitter, MIPS_T3, MIPS_A0, psr_offset);
}

static void mips_set_arithmetic_flags(
    mips_emitter_t *emitter,
    uint32_t lhs,
    uint32_t rhs,
    uint32_t result,
    int subtract
)
{
    uint32_t psr_offset = (uint32_t)offsetof(c33_vm_t, psr);
    mips_lw(emitter, MIPS_T3, MIPS_A0, psr_offset);
    mips_emit(emitter, mips_i(0x09u, MIPS_ZERO, MIPS_T4, 0xfff0u));
    mips_emit(
        emitter,
        mips_r(MIPS_T3, MIPS_T4, MIPS_T3, 0u, 0x24u)
    );

    mips_emit(emitter, mips_r(MIPS_ZERO, result, MIPS_T4, 31u, 0x02u));
    mips_emit(
        emitter,
        mips_r(MIPS_T3, MIPS_T4, MIPS_T3, 0u, 0x25u)
    );
    mips_emit(emitter, mips_i(0x0bu, result, MIPS_T4, 1u));
    mips_emit(emitter, mips_r(MIPS_ZERO, MIPS_T4, MIPS_T4, 1u, 0x00u));
    mips_emit(
        emitter,
        mips_r(MIPS_T3, MIPS_T4, MIPS_T3, 0u, 0x25u)
    );

    mips_emit(
        emitter,
        mips_r(lhs, subtract ? rhs : result, MIPS_T4, 0u, 0x26u)
    );
    mips_emit(
        emitter,
        mips_r(
            lhs,
            result,
            MIPS_T5,
            0u,
            0x26u
        )
    );
    if (!subtract) {
        mips_emit(
            emitter,
            mips_r(rhs, result, MIPS_T5, 0u, 0x26u)
        );
    }
    mips_emit(
        emitter,
        mips_r(MIPS_T4, MIPS_T5, MIPS_T4, 0u, 0x24u)
    );
    mips_emit(emitter, mips_r(MIPS_ZERO, MIPS_T4, MIPS_T4, 31u, 0x02u));
    mips_emit(emitter, mips_r(MIPS_ZERO, MIPS_T4, MIPS_T4, 2u, 0x00u));
    mips_emit(
        emitter,
        mips_r(MIPS_T3, MIPS_T4, MIPS_T3, 0u, 0x25u)
    );

    mips_emit(
        emitter,
        mips_r(
            lhs,
            subtract ? rhs : result,
            MIPS_T4,
            0u,
            0x2bu
        )
    );
    if (!subtract) {
        mips_emit(
            emitter,
            mips_r(result, lhs, MIPS_T4, 0u, 0x2bu)
        );
    }
    mips_emit(emitter, mips_r(MIPS_ZERO, MIPS_T4, MIPS_T4, 3u, 0x00u));
    mips_emit(
        emitter,
        mips_r(MIPS_T3, MIPS_T4, MIPS_T3, 0u, 0x25u)
    );
    mips_sw(emitter, MIPS_T3, MIPS_A0, psr_offset);
}

static int jit_emit_register_alu(mips_emitter_t *emitter, uint16_t word)
{
    uint32_t operation;
    uint32_t source;
    uint32_t destination;
    if (word < 0x2200u || word > 0x3effu ||
        ((word >> 8) & 3u) != 2u) {
        return 0;
    }
    operation = (word >> 10) & 7u;
    source = (word >> 4) & 15u;
    destination = word & 15u;
    if (operation == 3u) {
        mips_load_guest_reg(emitter, MIPS_T0, source);
        mips_store_guest_reg(emitter, MIPS_T0, destination);
        return 1;
    }
    mips_load_guest_reg(emitter, MIPS_T0, destination);
    mips_load_guest_reg(emitter, MIPS_T1, source);
    switch (operation) {
    case 0u:
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T2, 0u, 0x21u)
        );
        mips_store_guest_reg(emitter, MIPS_T2, destination);
        mips_set_arithmetic_flags(
            emitter, MIPS_T0, MIPS_T1, MIPS_T2, 0
        );
        return 1;
    case 1u:
    case 2u:
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T2, 0u, 0x23u)
        );
        if (operation == 1u) {
            mips_store_guest_reg(emitter, MIPS_T2, destination);
        }
        mips_set_arithmetic_flags(
            emitter, MIPS_T0, MIPS_T1, MIPS_T2, 1
        );
        return 1;
    case 4u:
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T2, 0u, 0x24u)
        );
        break;
    case 5u:
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T2, 0u, 0x25u)
        );
        break;
    case 6u:
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T2, 0u, 0x26u)
        );
        break;
    default:
        mips_emit(
            emitter,
            mips_r(MIPS_T1, MIPS_ZERO, MIPS_T2, 0u, 0x27u)
        );
        break;
    }
    mips_store_guest_reg(emitter, MIPS_T2, destination);
    mips_set_nz(emitter, MIPS_T2);
    return 1;
}

static int32_t jit_sign6(uint16_t word)
{
    uint32_t value = (word >> 4) & 0x3fu;
    return (int32_t)((value ^ 0x20u) - 0x20u);
}

static int jit_emit_immediate_alu(mips_emitter_t *emitter, uint16_t word)
{
    uint32_t destination;
    uint32_t value;
    destination = word & 15u;
    if (word >= 0x6000u && word <= 0x6bffu) {
        value = word < 0x6800u
            ? (word >> 4) & 0x3fu
            : (uint32_t)jit_sign6(word);
        mips_load_guest_reg(emitter, MIPS_T0, destination);
        mips_li(emitter, MIPS_T1, value);
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T2, 0u, 0x23u)
        );
        if (word < 0x6400u) {
            mips_emit(
                emitter,
                mips_r(MIPS_T0, MIPS_T1, MIPS_T2, 0u, 0x21u)
            );
            mips_store_guest_reg(emitter, MIPS_T2, destination);
            mips_set_arithmetic_flags(
                emitter, MIPS_T0, MIPS_T1, MIPS_T2, 0
            );
        } else {
            if (word < 0x6800u) {
                mips_store_guest_reg(emitter, MIPS_T2, destination);
            }
            mips_set_arithmetic_flags(
                emitter, MIPS_T0, MIPS_T1, MIPS_T2, 1
            );
        }
        return 1;
    }
    if (word < 0x6c00u || word > 0x7fffu) {
        return 0;
    }
    value = (uint32_t)jit_sign6(word);
    if (word < 0x7000u) {
        mips_li(emitter, MIPS_T0, value);
        mips_store_guest_reg(emitter, MIPS_T0, destination);
        return 1;
    }
    mips_load_guest_reg(emitter, MIPS_T0, destination);
    mips_li(emitter, MIPS_T1, value);
    if (word < 0x7400u) {
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T2, 0u, 0x24u)
        );
    } else if (word < 0x7800u) {
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T2, 0u, 0x25u)
        );
    } else if (word < 0x7c00u) {
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T2, 0u, 0x26u)
        );
    } else {
        mips_emit(
            emitter,
            mips_r(MIPS_T1, MIPS_ZERO, MIPS_T2, 0u, 0x27u)
        );
    }
    mips_store_guest_reg(emitter, MIPS_T2, destination);
    mips_set_nz(emitter, MIPS_T2);
    return 1;
}

static int jit_emit_stack_adjust(mips_emitter_t *emitter, uint16_t word)
{
    uint32_t amount;
    uint32_t sp_offset = (uint32_t)offsetof(c33_vm_t, sp);
    if (word < 0x8000u || word > 0x87ffu) {
        return 0;
    }
    amount = (word & 0x3ffu) * 4u;
    mips_lw(emitter, MIPS_T0, MIPS_A0, sp_offset);
    mips_li(emitter, MIPS_T1, amount);
    mips_emit(
        emitter,
        mips_r(
            MIPS_T0,
            MIPS_T1,
            MIPS_T0,
            0u,
            word < 0x8400u ? 0x21u : 0x23u
        )
    );
    mips_sw(emitter, MIPS_T0, MIPS_A0, sp_offset);
    return 1;
}

static int jit_emit_shift(mips_emitter_t *emitter, uint16_t word)
{
    uint32_t operation;
    uint32_t destination;
    uint32_t count;
    if (word < 0x8800u || word > 0x9dffu ||
        ((word >> 8) & 3u) != 0u ||
        ((word >> 10) & 7u) < 2u) {
        return 0;
    }
    operation = (word >> 10) & 7u;
    destination = word & 15u;
    count = (word >> 4) & 15u;
    if (count & 8u) count = 8u;
    mips_load_guest_reg(emitter, MIPS_T0, destination);
    if (!count) {
        mips_move(emitter, MIPS_T2, MIPS_T0);
    } else if (operation == 2u) {
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T0, MIPS_T2, count, 0x02u)
        );
    } else if (operation == 3u || operation == 5u) {
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T0, MIPS_T2, count, 0x00u)
        );
    } else if (operation == 4u) {
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T0, MIPS_T2, count, 0x03u)
        );
    } else if (operation == 6u) {
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T0, MIPS_T2, count, 0x02u)
        );
        mips_emit(
            emitter,
            mips_r(
                MIPS_ZERO,
                MIPS_T0,
                MIPS_T3,
                32u - count,
                0x00u
            )
        );
        mips_emit(
            emitter,
            mips_r(MIPS_T2, MIPS_T3, MIPS_T2, 0u, 0x25u)
        );
    } else {
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T0, MIPS_T2, count, 0x00u)
        );
        mips_emit(
            emitter,
            mips_r(
                MIPS_ZERO,
                MIPS_T0,
                MIPS_T3,
                32u - count,
                0x02u
            )
        );
        mips_emit(
            emitter,
            mips_r(MIPS_T2, MIPS_T3, MIPS_T2, 0u, 0x25u)
        );
    }
    mips_store_guest_reg(emitter, MIPS_T2, destination);
    mips_set_nz(emitter, MIPS_T2);
    return 1;
}

static int jit_emit_extend(mips_emitter_t *emitter, uint16_t word)
{
    uint32_t operation;
    uint32_t source;
    uint32_t destination;
    if (word < 0xa100u || word > 0xadffu ||
        ((word >> 8) & 3u) != 1u ||
        ((word >> 10) & 7u) > 3u) {
        return 0;
    }
    operation = (word >> 10) & 7u;
    source = (word >> 4) & 15u;
    destination = word & 15u;
    mips_load_guest_reg(emitter, MIPS_T0, source);
    if (operation == 0u) {
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T0, MIPS_T0, 24u, 0x00u)
        );
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T0, MIPS_T0, 24u, 0x03u)
        );
    } else if (operation == 1u) {
        mips_emit(emitter, mips_i(0x0cu, MIPS_T0, MIPS_T0, 0xffu));
    } else if (operation == 2u) {
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T0, MIPS_T0, 16u, 0x00u)
        );
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T0, MIPS_T0, 16u, 0x03u)
        );
    } else {
        mips_emit(emitter, mips_i(0x0cu, MIPS_T0, MIPS_T0, 0xffffu));
    }
    mips_store_guest_reg(emitter, MIPS_T0, destination);
    return 1;
}

static int jit_emit_multiply(mips_emitter_t *emitter, uint16_t word)
{
    uint32_t operation;
    uint32_t source;
    uint32_t destination;
    if (word < 0xa200u || word > 0xaeffu ||
        ((word >> 8) & 3u) != 2u ||
        ((word >> 10) & 7u) > 3u) {
        return 0;
    }
    operation = (word >> 10) & 7u;
    source = (word >> 4) & 15u;
    destination = word & 15u;
    mips_load_guest_reg(emitter, MIPS_T0, destination);
    mips_load_guest_reg(emitter, MIPS_T1, source);
    if (operation == 0u) {
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T0, MIPS_T0, 16u, 0x00u)
        );
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T0, MIPS_T0, 16u, 0x03u)
        );
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T1, MIPS_T1, 16u, 0x00u)
        );
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T1, MIPS_T1, 16u, 0x03u)
        );
    } else if (operation == 1u) {
        mips_emit(emitter, mips_i(0x0cu, MIPS_T0, MIPS_T0, 0xffffu));
        mips_emit(emitter, mips_i(0x0cu, MIPS_T1, MIPS_T1, 0xffffu));
    }
    mips_emit(
        emitter,
        mips_r(
            MIPS_T0,
            MIPS_T1,
            MIPS_ZERO,
            0u,
            operation == 1u || operation == 3u ? 0x19u : 0x18u
        )
    );
    mips_emit(emitter, mips_r(MIPS_ZERO, MIPS_ZERO, MIPS_T2, 0u, 0x12u));
    mips_sw(
        emitter,
        MIPS_T2,
        MIPS_A0,
        (uint32_t)offsetof(c33_vm_t, alr)
    );
    if (operation >= 2u) {
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_ZERO, MIPS_T2, 0u, 0x10u)
        );
        mips_sw(
            emitter,
            MIPS_T2,
            MIPS_A0,
            (uint32_t)offsetof(c33_vm_t, ahr)
        );
    }
    return 1;
}

static int jit_emit_special_transfer(
    mips_emitter_t *emitter,
    uint16_t word
)
{
    uint32_t special_offset;
    uint32_t guest_register;
    if (word >= 0xa000u && word <= 0xa0f3u && (word & 0xcu) == 0u) {
        guest_register = (word >> 4) & 15u;
        switch (word & 3u) {
        case 0u: special_offset = (uint32_t)offsetof(c33_vm_t, psr); break;
        case 1u: special_offset = (uint32_t)offsetof(c33_vm_t, sp); break;
        case 2u: special_offset = (uint32_t)offsetof(c33_vm_t, alr); break;
        default: special_offset = (uint32_t)offsetof(c33_vm_t, ahr); break;
        }
        mips_load_guest_reg(emitter, MIPS_T0, guest_register);
        mips_sw(emitter, MIPS_T0, MIPS_A0, special_offset);
        return 1;
    }
    if (word >= 0xa400u && word <= 0xa43fu) {
        guest_register = word & 15u;
        switch ((word >> 4) & 3u) {
        case 0u: special_offset = (uint32_t)offsetof(c33_vm_t, psr); break;
        case 1u: special_offset = (uint32_t)offsetof(c33_vm_t, sp); break;
        case 2u: special_offset = (uint32_t)offsetof(c33_vm_t, alr); break;
        default: special_offset = (uint32_t)offsetof(c33_vm_t, ahr); break;
        }
        mips_lw(emitter, MIPS_T0, MIPS_A0, special_offset);
        mips_store_guest_reg(emitter, MIPS_T0, guest_register);
        return 1;
    }
    return 0;
}

static int jit_emit_psr_bit(mips_emitter_t *emitter, uint16_t word)
{
    uint32_t mask;
    uint32_t psr_offset = (uint32_t)offsetof(c33_vm_t, psr);
    if (word < 0xbf40u || word > 0xbf7fu) {
        return 0;
    }
    mask = 1u << (word & 0x1fu);
    mips_lw(emitter, MIPS_T0, MIPS_A0, psr_offset);
    mips_li(emitter, MIPS_T1, mask);
    if (word < 0xbf60u) {
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T0, 0u, 0x25u)
        );
    } else {
        mips_emit(
            emitter,
            mips_r(MIPS_T1, MIPS_ZERO, MIPS_T1, 0u, 0x27u)
        );
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T0, 0u, 0x24u)
        );
    }
    mips_sw(emitter, MIPS_T0, MIPS_A0, psr_offset);
    return 1;
}

static int jit_is_memory_instruction(uint16_t word)
{
    if (word >= 0x2000u && word <= 0x3dffu &&
        ((((word >> 8) & 3u) == 0u) ||
         (((word >> 8) & 3u) == 1u))) {
        return 1;
    }
    return word >= 0x4000u && word <= 0x5fffu;
}

static int jit_is_memory_bit_operation(uint16_t word)
{
    uint16_t operation = word & 0xff08u;
    return operation == 0xa800u ||
           operation == 0xac00u ||
           operation == 0xb000u;
}

static uint32_t jit_memory_size_code(uint32_t operation)
{
    if (operation == 0u || operation == 1u || operation == 5u) {
        return C33_JIT_MEM_SIZE_1;
    }
    if (operation == 2u || operation == 3u || operation == 6u) {
        return C33_JIT_MEM_SIZE_2;
    }
    return C33_JIT_MEM_SIZE_4;
}

static int jit_emit_memory(
    mips_emitter_t *emitter,
    uint16_t word,
    uint32_t ext_count,
    uint16_t ext0,
    uint16_t ext1
)
{
    uint32_t operation = (word >> 10) & 7u;
    uint32_t destination = word & 15u;
    uint32_t size_code = jit_memory_size_code(operation);
    uint32_t size = 1u << size_code;
    uint32_t descriptor = size_code;
    uint32_t offset;
    int post_increment = 0;

    if (!jit_is_memory_instruction(word)) {
        return 0;
    }
    if (operation > 4u) {
        descriptor |= C33_JIT_MEM_STORE;
    } else if (operation == 0u || operation == 2u) {
        descriptor |= C33_JIT_MEM_SIGNED;
    }

    if (word < 0x4000u) {
        uint32_t base = (word >> 4) & 15u;
        post_increment = (word & 0x100u) != 0u;
        if (post_increment || ext_count == 0u) {
            offset = 0u;
        } else if (ext_count == 1u) {
            offset = ext0;
        } else {
            offset = ((uint32_t)ext0 << 13) + ext1;
        }
        mips_load_guest_reg(emitter, MIPS_T0, base);
        mips_li(emitter, MIPS_T1, offset);
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_A1, 0u, 0x21u)
        );
    } else {
        uint32_t immediate = (word >> 4) & 0x3fu;
        if (ext_count == 0u) {
            offset = immediate * size;
        } else if (ext_count == 1u) {
            offset = ((uint32_t)ext0 << 6) + immediate;
        } else {
            offset = ((uint32_t)ext0 << 19) +
                     ((uint32_t)ext1 << 6) + immediate;
        }
        mips_lw(
            emitter,
            MIPS_T0,
            MIPS_A0,
            (uint32_t)offsetof(c33_vm_t, sp)
        );
        mips_li(emitter, MIPS_T1, offset);
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_A1, 0u, 0x21u)
        );
    }

    mips_li(emitter, MIPS_A2, descriptor);
    if (descriptor & C33_JIT_MEM_STORE) {
        mips_load_guest_reg(emitter, MIPS_A3, destination);
    } else {
        mips_move(emitter, MIPS_A3, MIPS_ZERO);
    }
    mips_li(
        emitter,
        MIPS_T9,
        (uint32_t)(uintptr_t)&jit_memory_access
    );
    mips_emit(
        emitter,
        mips_r(MIPS_T9, MIPS_ZERO, MIPS_RA, 0u, 0x09u)
    );
    mips_nop(emitter);

    if (emitter->bailout_count >= C33_JIT_MAX_BLOCK_INSNS) {
        emitter->failed = 1;
        return 0;
    }
    emitter->bailout_branches[emitter->bailout_count++] =
        emitter->count;
    mips_emit(
        emitter,
        mips_i(0x04u, MIPS_V0, MIPS_ZERO, 0u)
    );
    mips_nop(emitter);
    mips_lw(emitter, MIPS_A0, MIPS_SP, 8u);

    if (!(descriptor & C33_JIT_MEM_STORE)) {
        mips_lw(
            emitter,
            MIPS_T0,
            MIPS_A0,
            (uint32_t)offsetof(c33_vm_t, jit_value)
        );
        mips_store_guest_reg(emitter, MIPS_T0, destination);
    }
    if (post_increment) {
        uint32_t base = (word >> 4) & 15u;
        mips_load_guest_reg(emitter, MIPS_T0, base);
        mips_li(emitter, MIPS_T1, size);
        mips_emit(
            emitter,
            mips_r(MIPS_T0, MIPS_T1, MIPS_T0, 0u, 0x21u)
        );
        mips_store_guest_reg(emitter, MIPS_T0, base);
    }
    return !emitter->failed;
}

static int jit_emit_memory_bit_operation(
    mips_emitter_t *emitter,
    uint16_t word,
    uint32_t ext_count,
    uint16_t ext0,
    uint16_t ext1
)
{
    uint32_t base;
    uint32_t offset;
    uint32_t mask;
    uint32_t descriptor = C33_JIT_MEM_SIZE_1;
    uint16_t operation = word & 0xff08u;
    uint32_t psr_offset = (uint32_t)offsetof(c33_vm_t, psr);

    if (!jit_is_memory_bit_operation(word)) {
        return 0;
    }
    base = (word >> 4) & 15u;
    mask = 1u << (word & 7u);
    if (ext_count == 0u) {
        offset = 0u;
    } else if (ext_count == 1u) {
        offset = ext0;
    } else {
        offset = ((uint32_t)ext0 << 13) + ext1;
    }

    mips_load_guest_reg(emitter, MIPS_T0, base);
    mips_li(emitter, MIPS_T1, offset);
    mips_emit(
        emitter,
        mips_r(MIPS_T0, MIPS_T1, MIPS_A1, 0u, 0x21u)
    );
    if (operation == 0xac00u) {
        descriptor |= C33_JIT_MEM_BIT_CLEAR;
    } else if (operation == 0xb000u) {
        descriptor |= C33_JIT_MEM_BIT_SET;
    } else if (operation == 0xb400u) {
        descriptor |= C33_JIT_MEM_BIT_NOT;
    }
    mips_li(emitter, MIPS_A2, descriptor);
    mips_li(emitter, MIPS_A3, mask);
    mips_li(
        emitter,
        MIPS_T9,
        (uint32_t)(uintptr_t)&jit_memory_access
    );
    mips_emit(
        emitter,
        mips_r(MIPS_T9, MIPS_ZERO, MIPS_RA, 0u, 0x09u)
    );
    mips_nop(emitter);

    if (emitter->bailout_count >= C33_JIT_MAX_BLOCK_INSNS) {
        emitter->failed = 1;
        return 0;
    }
    emitter->bailout_branches[emitter->bailout_count++] =
        emitter->count;
    mips_emit(
        emitter,
        mips_i(0x04u, MIPS_V0, MIPS_ZERO, 0u)
    );
    mips_nop(emitter);
    mips_lw(emitter, MIPS_A0, MIPS_SP, 8u);

    if (operation != 0xa800u) {
        return !emitter->failed;
    }

    /*
     * Preserve all PSR bits except Z. sltiu produces one exactly when the
     * selected source bit is zero, which is then shifted into C33_PSR_Z.
     */
    mips_lw(
        emitter,
        MIPS_T0,
        MIPS_A0,
        (uint32_t)offsetof(c33_vm_t, jit_value)
    );
    mips_li(emitter, MIPS_T1, mask);
    mips_emit(
        emitter,
        mips_r(MIPS_T0, MIPS_T1, MIPS_T0, 0u, 0x24u)
    );
    mips_emit(emitter, mips_i(0x0bu, MIPS_T0, MIPS_T0, 1u));
    mips_emit(
        emitter,
        mips_r(MIPS_ZERO, MIPS_T0, MIPS_T0, 1u, 0x00u)
    );
    mips_lw(emitter, MIPS_T1, MIPS_A0, psr_offset);
    mips_li(emitter, MIPS_T2, ~C33_PSR_Z);
    mips_emit(
        emitter,
        mips_r(MIPS_T1, MIPS_T2, MIPS_T1, 0u, 0x24u)
    );
    mips_emit(
        emitter,
        mips_r(MIPS_T1, MIPS_T0, MIPS_T1, 0u, 0x25u)
    );
    mips_sw(emitter, MIPS_T1, MIPS_A0, psr_offset);
    return !emitter->failed;
}

static void jit_emit_dynamic_epilogue(
    mips_emitter_t *emitter,
    uint32_t next_pc_register,
    uint32_t source_instructions,
    int uses_call
);

static int jit_emit_source_instruction(
    mips_emitter_t *emitter,
    uint16_t word
)
{
    if (word == 0x0000u) {
        return 1;
    }
    if (jit_emit_register_alu(emitter, word) ||
        jit_emit_immediate_alu(emitter, word) ||
        jit_emit_stack_adjust(emitter, word) ||
        jit_emit_shift(emitter, word) ||
        jit_emit_extend(emitter, word) ||
        jit_emit_multiply(emitter, word) ||
        jit_emit_special_transfer(emitter, word) ||
        jit_emit_psr_bit(emitter, word)) {
        return 1;
    }
    return 0;
}

static int jit_is_conditional_branch(uint16_t word)
{
    uint32_t operation = (word >> 9) & 15u;
    return word >= 0x0800u && word <= 0x1bffu &&
           operation >= 4u && operation <= 13u;
}

static void jit_emit_branch_condition(
    mips_emitter_t *emitter,
    uint32_t operation
)
{
    uint32_t psr_offset = (uint32_t)offsetof(c33_vm_t, psr);
    mips_lw(emitter, MIPS_T0, MIPS_A0, psr_offset);
    mips_emit(emitter, mips_i(0x0cu, MIPS_T0, MIPS_T1, 1u));
    mips_emit(
        emitter,
        mips_r(MIPS_ZERO, MIPS_T0, MIPS_T2, 1u, 0x02u)
    );
    mips_emit(emitter, mips_i(0x0cu, MIPS_T2, MIPS_T2, 1u));
    mips_emit(
        emitter,
        mips_r(MIPS_ZERO, MIPS_T0, MIPS_T3, 2u, 0x02u)
    );
    mips_emit(emitter, mips_i(0x0cu, MIPS_T3, MIPS_T3, 1u));
    mips_emit(
        emitter,
        mips_r(MIPS_ZERO, MIPS_T0, MIPS_T4, 3u, 0x02u)
    );
    mips_emit(emitter, mips_i(0x0cu, MIPS_T4, MIPS_T4, 1u));
    mips_emit(
        emitter,
        mips_r(MIPS_T1, MIPS_T3, MIPS_T5, 0u, 0x26u)
    );

    switch (operation) {
    case 4u:
        mips_emit(
            emitter,
            mips_r(MIPS_T2, MIPS_T5, MIPS_T5, 0u, 0x25u)
        );
        mips_emit(emitter, mips_i(0x0bu, MIPS_T5, MIPS_T8, 1u));
        break;
    case 5u:
        mips_emit(emitter, mips_i(0x0bu, MIPS_T5, MIPS_T8, 1u));
        break;
    case 6u:
        mips_move(emitter, MIPS_T8, MIPS_T5);
        break;
    case 7u:
        mips_emit(
            emitter,
            mips_r(MIPS_T2, MIPS_T5, MIPS_T5, 0u, 0x25u)
        );
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T5, MIPS_T8, 0u, 0x2bu)
        );
        break;
    case 8u:
        mips_emit(
            emitter,
            mips_r(MIPS_T2, MIPS_T4, MIPS_T5, 0u, 0x25u)
        );
        mips_emit(emitter, mips_i(0x0bu, MIPS_T5, MIPS_T8, 1u));
        break;
    case 9u:
        mips_emit(emitter, mips_i(0x0bu, MIPS_T4, MIPS_T8, 1u));
        break;
    case 10u:
        mips_move(emitter, MIPS_T8, MIPS_T4);
        break;
    case 11u:
        mips_emit(
            emitter,
            mips_r(MIPS_T2, MIPS_T4, MIPS_T5, 0u, 0x25u)
        );
        mips_emit(
            emitter,
            mips_r(MIPS_ZERO, MIPS_T5, MIPS_T8, 0u, 0x2bu)
        );
        break;
    case 12u:
        mips_move(emitter, MIPS_T8, MIPS_T2);
        break;
    default:
        mips_emit(emitter, mips_i(0x0bu, MIPS_T2, MIPS_T8, 1u));
        break;
    }
}

static int jit_emit_conditional_branch(
    mips_emitter_t *emitter,
    uint32_t instruction_pc,
    uint16_t word,
    uint16_t delay_word,
    int delayed,
    uint32_t source_instructions,
    int uses_call
)
{
    uint32_t saved_count = emitter->count;
    uint32_t branch;
    uint32_t displacement;
    uint32_t fallthrough =
        instruction_pc + (delayed ? 4u : 2u);
    int32_t signed_disp =
        (int32_t)(int8_t)(word & 0xffu) * 2;
    uint32_t target = instruction_pc + (uint32_t)signed_disp;

    jit_emit_branch_condition(emitter, (word >> 9) & 15u);
    if (delayed &&
        !jit_emit_source_instruction(emitter, delay_word)) {
        emitter->count = saved_count;
        return 0;
    }

    mips_li(emitter, MIPS_T0, fallthrough);
    branch = emitter->count;
    mips_emit(
        emitter,
        mips_i(0x04u, MIPS_T8, MIPS_ZERO, 0u)
    );
    mips_nop(emitter);
    mips_li(emitter, MIPS_T0, target);
    displacement = emitter->count - (branch + 1u);
    if (displacement > 0x7fffu) {
        emitter->count = saved_count;
        return 0;
    }
    emitter->words[branch] =
        (emitter->words[branch] & 0xffff0000u) |
        (displacement & 0xffffu);
    jit_emit_dynamic_epilogue(
        emitter,
        MIPS_T0,
        source_instructions,
        uses_call
    );
    return !emitter->failed;
}

static void jit_emit_finish(
    mips_emitter_t *emitter,
    uint32_t source_instructions,
    int uses_call
)
{
    uint32_t instructions_offset =
        (uint32_t)offsetof(c33_vm_t, instructions);

    mips_lw(emitter, MIPS_T0, MIPS_A0, instructions_offset);
    mips_li(emitter, MIPS_T1, source_instructions);
    mips_emit(
        emitter,
        mips_r(MIPS_T0, MIPS_T1, MIPS_T2, 0u, 0x21u)
    );
    mips_emit(
        emitter,
        mips_r(MIPS_T2, MIPS_T0, MIPS_T3, 0u, 0x2bu)
    );
    mips_sw(emitter, MIPS_T2, MIPS_A0, instructions_offset);
    mips_lw(emitter, MIPS_T0, MIPS_A0, instructions_offset + 4u);
    mips_emit(
        emitter,
        mips_r(MIPS_T0, MIPS_T3, MIPS_T0, 0u, 0x21u)
    );
    mips_sw(emitter, MIPS_T0, MIPS_A0, instructions_offset + 4u);

    mips_li(emitter, MIPS_V0, source_instructions);
    if (uses_call) {
        jit_emit_call_restore(emitter);
    }
    mips_emit(emitter, mips_r(MIPS_RA, MIPS_ZERO, MIPS_ZERO, 0u, 0x08u));
    mips_nop(emitter);
}

static void jit_emit_epilogue(
    mips_emitter_t *emitter,
    uint32_t next_pc,
    uint32_t source_instructions,
    int uses_call
)
{
    mips_li(emitter, MIPS_T0, next_pc);
    mips_sw(
        emitter,
        MIPS_T0,
        MIPS_A0,
        (uint32_t)offsetof(c33_vm_t, pc)
    );
    jit_emit_finish(emitter, source_instructions, uses_call);
}

static void jit_emit_dynamic_epilogue(
    mips_emitter_t *emitter,
    uint32_t next_pc_register,
    uint32_t source_instructions,
    int uses_call
)
{
    mips_sw(
        emitter,
        next_pc_register,
        MIPS_A0,
        (uint32_t)offsetof(c33_vm_t, pc)
    );
    jit_emit_finish(emitter, source_instructions, uses_call);
}

static void jit_emit_bailout(mips_emitter_t *emitter)
{
    uint32_t target = emitter->count;
    uint32_t index;
    for (index = 0u; index < emitter->bailout_count; ++index) {
        uint32_t branch = emitter->bailout_branches[index];
        uint32_t displacement = target - (branch + 1u);
        if (displacement > 0x7fffu) {
            emitter->failed = 1;
            return;
        }
        emitter->words[branch] =
            (emitter->words[branch] & 0xffff0000u) |
            (displacement & 0xffffu);
    }
    mips_move(emitter, MIPS_V0, MIPS_ZERO);
    jit_emit_call_restore(emitter);
    mips_emit(
        emitter,
        mips_r(MIPS_RA, MIPS_ZERO, MIPS_ZERO, 0u, 0x08u)
    );
    mips_nop(emitter);
}

static void jit_cache_sync(void *start, void *end)
{
    uintptr_t first =
        (uintptr_t)start & ~(uintptr_t)(C33_JIT_CACHE_LINE - 1u);
    uintptr_t limit =
        ((uintptr_t)end + C33_JIT_CACHE_LINE - 1u) &
        ~(uintptr_t)(C33_JIT_CACHE_LINE - 1u);
    uintptr_t address;

    for (address = first; address < limit; address += C33_JIT_CACHE_LINE) {
        __asm__ volatile(
            "cache 0x15, 0(%0)"
            :
            : "r"(address)
            : "memory"
        );
    }
    __asm__ volatile("sync" : : : "memory");
    for (address = first; address < limit; address += C33_JIT_CACHE_LINE) {
        __asm__ volatile(
            "cache 0x10, 0(%0)"
            :
            : "r"(address)
            : "memory"
        );
    }
    __asm__ volatile("sync" : : : "memory");
}

static c33_jit_lookup_t *jit_find_lookup(
    c33_jit_context_t *context,
    uint32_t guest_pc,
    int allow_empty
)
{
    uint32_t probe;
    uint32_t index =
        ((guest_pc >> 1) * 2654435761u) & C33_JIT_LOOKUP_MASK;
    for (probe = 1u; probe <= C33_JIT_LOOKUP_SLOTS; ++probe) {
        c33_jit_lookup_t *slot = &context->lookup[index];
        ++context->stats.lookup_probes;
        if (probe > context->stats.max_lookup_probes) {
            context->stats.max_lookup_probes = probe;
        }
        if (slot->state == C33_JIT_LOOKUP_EMPTY) {
            return allow_empty ? slot : 0;
        }
        if (slot->guest_pc == guest_pc) {
            return slot;
        }
        index = (index + 1u) & C33_JIT_LOOKUP_MASK;
    }
    return 0;
}

static c33_jit_entry_t *jit_find_entry(
    c33_jit_context_t *context,
    uint32_t guest_pc,
    int *known_unsupported
)
{
    c33_jit_lookup_t *slot =
        jit_find_lookup(context, guest_pc, 0);
    *known_unsupported = 0;
    if (!slot) {
        return 0;
    }
    if (slot->state == C33_JIT_LOOKUP_UNSUPPORTED) {
        *known_unsupported = 1;
        return 0;
    }
    if (slot->state != C33_JIT_LOOKUP_ENTRY ||
        slot->entry_index >= context->entry_count ||
        context->entries[slot->entry_index].guest_pc != guest_pc) {
        return 0;
    }
    return &context->entries[slot->entry_index];
}

static void jit_set_lookup(
    c33_jit_context_t *context,
    uint32_t guest_pc,
    uint8_t state,
    uint32_t entry_index
)
{
    c33_jit_lookup_t *slot =
        jit_find_lookup(context, guest_pc, 1);
    if (!slot) {
        return;
    }
    slot->guest_pc = guest_pc;
    slot->entry_index = (uint16_t)entry_index;
    slot->state = state;
}

static c33_jit_entry_t *jit_compile_block(
    c33_jit_context_t *context,
    c33_vm_t *vm,
    uint32_t guest_pc
)
{
    mips_emitter_t emitter;
    uint32_t source_count = 0u;
    uint32_t source_pc = guest_pc;
    uint32_t ext_count = 0u;
    uint16_t ext0 = 0u;
    uint16_t ext1 = 0u;
    uint32_t byte_count;
    uint32_t cache_offset;
    uint32_t *destination;
    c33_jit_entry_t *entry;
    int uses_call = 0;
    int memory_emitted = 0;
    int terminal_emitted = 0;

    jit_zero(&emitter, sizeof(emitter));
    {
        uint32_t scan_pc = guest_pc;
        uint32_t scan_prefixes = 0u;
        while (scan_prefixes <= 2u) {
            uint8_t bytes[2];
            uint16_t word;
            if (!c33_vm_read(
                    vm, scan_pc, bytes, sizeof(bytes)
                )) {
                break;
            }
            word = (uint16_t)bytes[0] |
                   ((uint16_t)bytes[1] << 8);
            if ((word & 0xe000u) == 0xc000u &&
                scan_prefixes < 2u) {
                ++scan_prefixes;
                scan_pc += 2u;
                continue;
            }
            uses_call = jit_is_memory_instruction(word) ||
                        jit_is_memory_bit_operation(word);
            break;
        }
    }
    if (uses_call) {
        jit_emit_call_prologue(&emitter);
    }
    while (source_count < C33_JIT_MAX_BLOCK_INSNS) {
        uint8_t bytes[2];
        uint16_t word;
        if (!c33_vm_read(vm, source_pc, bytes, sizeof(bytes))) {
            break;
        }
        word = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
        if ((word & 0xe000u) == 0xc000u) {
            if (!uses_call ||
                source_count != ext_count ||
                ext_count >= 2u) {
                break;
            }
            if (ext_count == 0u) {
                ext0 = word & 0x1fffu;
            } else {
                ext1 = word & 0x1fffu;
            }
            ++ext_count;
            ++source_count;
            source_pc += 2u;
            continue;
        }
        if (ext_count == 0u &&
            jit_is_conditional_branch(word)) {
            int delayed = (word & 0x100u) != 0u;
            uint16_t delay_word = 0u;
            uint32_t branch_count =
                source_count + 1u + (delayed ? 1u : 0u);
            if (branch_count > C33_JIT_MAX_BLOCK_INSNS) {
                break;
            }
            if (delayed) {
                uint8_t delay_bytes[2];
                if (!c33_vm_read(
                        vm,
                        source_pc + 2u,
                        delay_bytes,
                        sizeof(delay_bytes)
                    )) {
                    break;
                }
                delay_word = (uint16_t)delay_bytes[0] |
                    ((uint16_t)delay_bytes[1] << 8);
            }
            if (!jit_emit_conditional_branch(
                    &emitter,
                    source_pc,
                    word,
                    delay_word,
                    delayed,
                    branch_count,
                    uses_call
                )) {
                break;
            }
            source_count = branch_count;
            source_pc += delayed ? 4u : 2u;
            terminal_emitted = 1;
            break;
        }
        if (jit_is_memory_instruction(word)) {
            if (!uses_call ||
                memory_emitted ||
                source_count != ext_count ||
                !jit_emit_memory(
                    &emitter, word, ext_count, ext0, ext1
                )) {
                break;
            }
            memory_emitted = 1;
            ext_count = 0u;
            ++source_count;
            source_pc += 2u;
            continue;
        }
        if (jit_is_memory_bit_operation(word)) {
            if (!uses_call ||
                memory_emitted ||
                source_count != ext_count ||
                !jit_emit_memory_bit_operation(
                    &emitter, word, ext_count, ext0, ext1
                )) {
                break;
            }
            memory_emitted = 1;
            ext_count = 0u;
            ++source_count;
            source_pc += 2u;
            continue;
        }
        if (ext_count != 0u ||
            !jit_emit_source_instruction(&emitter, word)) {
            break;
        }
        if (emitter.failed) {
            return 0;
        }
        ++source_count;
        source_pc += 2u;
    }
    if (!source_count ||
        ext_count != 0u ||
        (uses_call && !memory_emitted)) {
        return 0;
    }
    if (!terminal_emitted) {
        jit_emit_epilogue(
            &emitter, source_pc, source_count, uses_call
        );
    }
    if (uses_call) {
        jit_emit_bailout(&emitter);
    }
    if (emitter.failed) {
        return 0;
    }
    byte_count = emitter.count * sizeof(uint32_t);
    cache_offset = jit_align(
        context->cache_used, C33_JIT_CACHE_ALIGNMENT
    );
    if (cache_offset > context->cache_size ||
        byte_count > context->cache_size - cache_offset ||
        context->entry_count >= C33_JIT_MAX_ENTRIES) {
        jit_clear_translation_state(context);
        ++context->stats.cache_flushes;
        cache_offset = 0u;
    }
    if (byte_count > context->cache_size ||
        context->entry_count >= C33_JIT_MAX_ENTRIES) {
        return 0;
    }

    destination = (uint32_t *)(void *)(context->cache + cache_offset);
    {
        uint32_t index;
        for (index = 0u; index < emitter.count; ++index) {
            destination[index] = emitter.words[index];
        }
    }
    jit_cache_sync(destination, (uint8_t *)destination + byte_count);
    context->cache_used = cache_offset + byte_count;
    context->stats.cache_used = context->cache_used;

    entry = &context->entries[context->entry_count];
    entry->guest_pc = guest_pc;
    entry->source_instructions = source_count;
    entry->native_entry = (c33_jit_entry_fn)(void *)destination;
    entry->verified = 0u;
    entry->invalid = 0u;
    jit_set_lookup(
        context,
        guest_pc,
        C33_JIT_LOOKUP_ENTRY,
        context->entry_count
    );
    ++context->entry_count;
    ++context->stats.blocks_compiled;
    return entry;
}

#endif

int c33_jit_init(
    c33_vm_t *vm,
    void *executable_cache,
    uint32_t cache_size
)
{
    jit_zero(&g_c33_jit, sizeof(g_c33_jit));
    g_c33_jit.owner = vm;
    g_c33_jit.cache = (uint8_t *)executable_cache;
    g_c33_jit.cache_size = cache_size;
    g_c33_jit.stats.cache_size = cache_size;
#if C33_JIT_NATIVE_MIPS
    if (!vm || !executable_cache ||
        cache_size < 4096u ||
        (((uintptr_t)executable_cache >> 28) !=
         ((uintptr_t)&c33_jit_init >> 28)) ||
        ((((uintptr_t)executable_cache + cache_size - 1u) >> 28) !=
         ((uintptr_t)&c33_jit_init >> 28))) {
        return 0;
    }
    g_c33_jit.stats.enabled = 1u;
    return 1;
#else
    (void)vm;
    (void)executable_cache;
    (void)cache_size;
    return 0;
#endif
}

void c33_jit_reset(c33_vm_t *vm)
{
    if (g_c33_jit.owner != vm) {
        return;
    }
    jit_clear_translation_state(&g_c33_jit);
    g_c33_jit.stats.blocks_compiled = 0u;
    g_c33_jit.stats.cache_hits = 0u;
    g_c33_jit.stats.cache_misses = 0u;
    g_c33_jit.stats.cache_flushes = 0u;
    g_c33_jit.stats.blocks_verified = 0u;
    g_c33_jit.stats.verification_failures = 0u;
    g_c33_jit.stats.last_failed_pc = 0u;
    g_c33_jit.stats.fallback_steps = 0u;
    g_c33_jit.stats.native_instructions = 0u;
    g_c33_jit.stats.negative_hits = 0u;
    g_c33_jit.stats.compile_failures = 0u;
    g_c33_jit.stats.lookup_probes = 0u;
    g_c33_jit.stats.max_lookup_probes = 0u;
    g_c33_jit.stats.dispatcher_calls = 0u;
    g_c33_jit.stats.native_blocks = 0u;
    g_c33_jit.stats.max_chain_blocks = 0u;
}

uint32_t c33_jit_run_block(c33_vm_t *vm, uint32_t budget)
{
#if C33_JIT_NATIVE_MIPS
    uint32_t total_executed = 0u;
    uint32_t chain_blocks = 0u;
    if (!g_c33_jit.stats.enabled ||
        g_c33_jit.owner != vm ||
        vm->ext_count != 0u ||
        !budget) {
        return 0u;
    }
    ++g_c33_jit.stats.dispatcher_calls;
    /*
     * Stay in the JIT dispatcher while consecutive compiled blocks remain
     * available. LavaXOS averages fewer than three guest instructions per
     * block, so returning through c33_vm_run after every block used to add
     * tens of millions of avoidable C call/return pairs on real hardware.
     * Unsupported PCs still return to the interpreter after one lookup.
     */
    while (total_executed < budget && vm->ext_count == 0u) {
        c33_jit_entry_t *entry;
        uint32_t executed;
        uint32_t remaining = budget - total_executed;
        int known_unsupported;

        entry = jit_find_entry(
            &g_c33_jit, vm->pc, &known_unsupported
        );
        if (known_unsupported) {
            ++g_c33_jit.stats.negative_hits;
            break;
        }
        if (entry) {
            ++g_c33_jit.stats.cache_hits;
        } else {
            ++g_c33_jit.stats.cache_misses;
            entry = jit_compile_block(&g_c33_jit, vm, vm->pc);
            if (!entry) {
                jit_set_lookup(
                    &g_c33_jit,
                    vm->pc,
                    C33_JIT_LOOKUP_UNSUPPORTED,
                    0u
                );
                ++g_c33_jit.stats.compile_failures;
            }
        }
        if (!entry ||
            entry->source_instructions > remaining ||
            entry->invalid) {
            break;
        }
        if (!entry->verified) {
            c33_vm_t reference;
            c33_vm_status_t status = C33_VM_OK;
            uint64_t reference_start;
            jit_copy_vm(&reference, vm);
            reference_start = reference.instructions;
            while (status == C33_VM_OK &&
                   reference.instructions - reference_start <
                       entry->source_instructions) {
                status = c33_vm_step(&reference);
            }
            executed = entry->native_entry(vm);
            if (status != C33_VM_OK ||
                reference.instructions - reference_start !=
                    entry->source_instructions ||
                executed != entry->source_instructions ||
                !jit_same_architecture(vm, &reference)) {
                entry->invalid = 1u;
                ++g_c33_jit.stats.verification_failures;
                g_c33_jit.stats.last_failed_pc = entry->guest_pc;
                jit_set_lookup(
                    &g_c33_jit,
                    entry->guest_pc,
                    C33_JIT_LOOKUP_UNSUPPORTED,
                    0u
                );
                jit_copy_vm(vm, &reference);
                return total_executed + entry->source_instructions;
            }
            entry->verified = 1u;
            ++g_c33_jit.stats.blocks_verified;
            g_c33_jit.stats.native_instructions += executed;
            ++g_c33_jit.stats.native_blocks;
            ++chain_blocks;
            if (chain_blocks > g_c33_jit.stats.max_chain_blocks) {
                g_c33_jit.stats.max_chain_blocks = chain_blocks;
            }
            return total_executed + executed;
        }
        executed = entry->native_entry(vm);
        if (executed != entry->source_instructions) {
            return total_executed;
        }
        g_c33_jit.stats.native_instructions += executed;
        ++g_c33_jit.stats.native_blocks;
        ++chain_blocks;
        if (chain_blocks > g_c33_jit.stats.max_chain_blocks) {
            g_c33_jit.stats.max_chain_blocks = chain_blocks;
        }
        total_executed += executed;
    }
    return total_executed;
#else
    (void)vm;
    (void)budget;
    return 0u;
#endif
}

void c33_jit_note_fallback(c33_vm_t *vm)
{
    if (g_c33_jit.owner == vm) {
        ++g_c33_jit.stats.fallback_steps;
    }
}

void c33_jit_get_stats(c33_vm_t *vm, c33_jit_stats_t *stats)
{
    if (!stats) {
        return;
    }
    jit_zero(stats, sizeof(*stats));
    if (g_c33_jit.owner == vm) {
        stats->enabled = g_c33_jit.stats.enabled;
        stats->cache_size = g_c33_jit.stats.cache_size;
        stats->blocks_compiled = g_c33_jit.stats.blocks_compiled;
        stats->cache_hits = g_c33_jit.stats.cache_hits;
        stats->cache_misses = g_c33_jit.stats.cache_misses;
        stats->cache_flushes = g_c33_jit.stats.cache_flushes;
        stats->blocks_verified = g_c33_jit.stats.blocks_verified;
        stats->verification_failures =
            g_c33_jit.stats.verification_failures;
        stats->last_failed_pc = g_c33_jit.stats.last_failed_pc;
        stats->fallback_steps = g_c33_jit.stats.fallback_steps;
        stats->native_instructions =
            g_c33_jit.stats.native_instructions;
        stats->negative_hits = g_c33_jit.stats.negative_hits;
        stats->compile_failures =
            g_c33_jit.stats.compile_failures;
        stats->lookup_probes = g_c33_jit.stats.lookup_probes;
        stats->max_lookup_probes =
            g_c33_jit.stats.max_lookup_probes;
        stats->dispatcher_calls =
            g_c33_jit.stats.dispatcher_calls;
        stats->native_blocks = g_c33_jit.stats.native_blocks;
        stats->max_chain_blocks =
            g_c33_jit.stats.max_chain_blocks;
        stats->cache_used = g_c33_jit.cache_used;
    }
}
