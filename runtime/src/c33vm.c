#include "../include/c33vm.h"
#include "../include/c33jit.h"

static void clear_bytes(void *ptr, size_t size)
{
    uint8_t *p = (uint8_t *)ptr;
    while (size--) {
        *p++ = 0;
    }
}

static int32_t sign_extend(uint32_t value, unsigned bits)
{
    uint32_t sign = 1u << (bits - 1);
    return (int32_t)((value ^ sign) - sign);
}

static c33_vm_region_t *find_region(c33_vm_t *vm, uint32_t address, uint32_t size)
{
    unsigned i;
    for (i = 0; i < vm->region_count; ++i) {
        c33_vm_region_t *region = &vm->regions[i];
        uint32_t offset = address - region->guest_base;
        if (address >= region->guest_base &&
            offset <= region->size &&
            size <= region->size - offset) {
            return region;
        }
    }
    return 0;
}

static int read_u8(c33_vm_t *vm, uint32_t address, uint8_t *out)
{
    return c33_vm_read(vm, address, out, 1);
}

static int read_u16(c33_vm_t *vm, uint32_t address, uint16_t *out)
{
    uint8_t b[2];
    if (!c33_vm_read(vm, address, b, 2)) {
        return 0;
    }
    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return 1;
}

static int read_u32(c33_vm_t *vm, uint32_t address, uint32_t *out)
{
    uint8_t b[4];
    if (!c33_vm_read(vm, address, b, 4)) {
        return 0;
    }
    *out = (uint32_t)b[0] |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
    return 1;
}

static int write_u8(c33_vm_t *vm, uint32_t address, uint8_t value)
{
    return c33_vm_write(vm, address, &value, 1);
}

static int write_u16(c33_vm_t *vm, uint32_t address, uint16_t value)
{
    uint8_t b[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    return c33_vm_write(vm, address, b, 2);
}

static int write_u32(c33_vm_t *vm, uint32_t address, uint32_t value)
{
    uint8_t b[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24)
    };
    return c33_vm_write(vm, address, b, 4);
}

static void clear_ext(c33_vm_t *vm)
{
    vm->ext_count = 0;
}

static uint32_t extended_imm6(c33_vm_t *vm, uint16_t word)
{
    uint32_t low = (word >> 4) & 0x3f;
    if (vm->ext_count == 0) {
        return low;
    }
    if (vm->ext_count == 1) {
        return ((uint32_t)vm->ext[0] << 6) | low;
    }
    return ((uint32_t)vm->ext[0] << 19) |
           ((uint32_t)vm->ext[1] << 6) | low;
}

static uint32_t extended_sign6(c33_vm_t *vm, uint16_t word)
{
    uint32_t value = extended_imm6(vm, word);
    if (vm->ext_count == 0) {
        return (uint32_t)sign_extend(value, 6);
    }
    if (vm->ext_count == 1) {
        return (uint32_t)sign_extend(value, 19);
    }
    return value;
}

static uint32_t extended_addr(c33_vm_t *vm, uint32_t base)
{
    if (vm->ext_count == 0) {
        return base;
    }
    if (vm->ext_count == 1) {
        return base + vm->ext[0];
    }
    return base + ((uint32_t)vm->ext[0] << 13) + vm->ext[1];
}

static uint32_t extended_imm13(c33_vm_t *vm)
{
    if (vm->ext_count == 1) {
        return vm->ext[0];
    }
    return ((uint32_t)vm->ext[0] << 13) | vm->ext[1];
}

static int32_t branch_disp(c33_vm_t *vm, uint16_t word)
{
    uint32_t sign8 = word & 0xff;
    if (vm->ext_count == 0) {
        return sign_extend(sign8, 8) * 2;
    }
    if (vm->ext_count == 1) {
        return sign_extend(((uint32_t)vm->ext[0] << 9) | (sign8 << 1), 22);
    }
    return (int32_t)(((uint32_t)(vm->ext[0] >> 3) << 22) |
                     ((uint32_t)vm->ext[1] << 9) | (sign8 << 1));
}

static void set_nz(c33_vm_t *vm, uint32_t result)
{
    vm->psr &= ~(C33_PSR_N | C33_PSR_Z);
    if (result & 0x80000000u) {
        vm->psr |= C33_PSR_N;
    }
    if (result == 0) {
        vm->psr |= C33_PSR_Z;
    }
}

static void set_add_flags(c33_vm_t *vm, uint32_t lhs, uint32_t rhs, uint32_t result)
{
    vm->psr &= ~(C33_PSR_N | C33_PSR_Z | C33_PSR_V | C33_PSR_C);
    if (result & 0x80000000u) vm->psr |= C33_PSR_N;
    if (result == 0) vm->psr |= C33_PSR_Z;
    if (((lhs ^ result) & (rhs ^ result)) >> 31) vm->psr |= C33_PSR_V;
    if (result < lhs) vm->psr |= C33_PSR_C;
}

static void set_sub_flags(c33_vm_t *vm, uint32_t lhs, uint32_t rhs, uint32_t result)
{
    vm->psr &= ~(C33_PSR_N | C33_PSR_Z | C33_PSR_V | C33_PSR_C);
    if (result & 0x80000000u) vm->psr |= C33_PSR_N;
    if (result == 0) vm->psr |= C33_PSR_Z;
    if (((lhs ^ rhs) & (lhs ^ result)) >> 31) vm->psr |= C33_PSR_V;
    if (lhs < rhs) vm->psr |= C33_PSR_C;
}

static unsigned count_leading_zeros32(uint32_t value)
{
    unsigned count = 0;
    if (!value) return 32;
    while ((value & 0x80000000u) == 0) {
        value <<= 1;
        ++count;
    }
    return count;
}

static uint64_t div_extend33(uint32_t value, int sign)
{
    return (uint64_t)value | ((uint64_t)(sign != 0) << 32);
}

static void execute_div_step(c33_vm_t *vm, unsigned operation, uint32_t divisor)
{
    const uint64_t mask33 = 0x1ffffffffULL;
    int dividend_sign;
    int divisor_sign;
    uint64_t pair;
    uint64_t lhs;
    uint64_t rhs;
    uint64_t temporary;
    uint32_t temporary32;

    switch (operation) {
    case 0: /* div0s */
        vm->ahr = (vm->alr & 0x80000000u) ? 0xffffffffu : 0u;
        vm->psr &= ~(C33_PSR_DS | C33_PSR_N);
        if (vm->alr & 0x80000000u) vm->psr |= C33_PSR_DS;
        if (divisor & 0x80000000u) vm->psr |= C33_PSR_N;
        break;
    case 1: /* div0u */
        vm->ahr = 0;
        vm->psr &= ~(C33_PSR_DS | C33_PSR_N);
        break;
    case 2: /* div1 */
        pair = (((uint64_t)vm->ahr << 32) | vm->alr) << 1;
        vm->ahr = (uint32_t)(pair >> 32);
        vm->alr = (uint32_t)pair;
        dividend_sign = !!(vm->psr & C33_PSR_DS);
        divisor_sign = !!(vm->psr & C33_PSR_N);
        lhs = div_extend33(vm->ahr, dividend_sign);
        rhs = div_extend33(divisor, divisor_sign);
        if (!dividend_sign && !divisor_sign) {
            temporary = (lhs - rhs) & mask33;
            if ((temporary >> 32) == 0) {
                vm->ahr = (uint32_t)temporary;
                vm->alr |= 1u;
            }
        } else if (dividend_sign && !divisor_sign) {
            temporary = (lhs + rhs) & mask33;
            if ((temporary >> 32) != 0) {
                vm->ahr = (uint32_t)temporary;
                vm->alr |= 1u;
            }
        } else if (!dividend_sign && divisor_sign) {
            temporary = (lhs + rhs) & mask33;
            if ((temporary >> 32) == 0) {
                vm->ahr = (uint32_t)temporary;
                vm->alr |= 1u;
            }
        } else {
            temporary = (lhs - rhs) & mask33;
            if ((temporary >> 32) != 0) {
                vm->ahr = (uint32_t)temporary;
                vm->alr |= 1u;
            }
        }
        break;
    case 3: /* div2s */
        if (vm->psr & C33_PSR_DS) {
            temporary32 = (vm->psr & C33_PSR_N)
                ? vm->ahr - divisor : vm->ahr + divisor;
            if (temporary32 == 0) {
                vm->ahr = 0;
                vm->alr++;
            }
        }
        break;
    case 4: /* div3s */
        if (!!(vm->psr & C33_PSR_DS) != !!(vm->psr & C33_PSR_N)) {
            vm->alr = 0u - vm->alr;
        }
        break;
    }
}

static int branch_condition(c33_vm_t *vm, unsigned op)
{
    unsigned n = !!(vm->psr & C33_PSR_N);
    unsigned z = !!(vm->psr & C33_PSR_Z);
    unsigned v = !!(vm->psr & C33_PSR_V);
    unsigned c = !!(vm->psr & C33_PSR_C);
    unsigned nv = n ^ v;
    switch (op) {
    case 4: return !z && !nv;
    case 5: return !nv;
    case 6: return nv;
    case 7: return z || nv;
    case 8: return !z && !c;
    case 9: return !c;
    case 10: return c;
    case 11: return z || c;
    case 12: return z;
    case 13: return !z;
    default: return 0;
    }
}

static c33_vm_status_t fault(c33_vm_t *vm, c33_vm_status_t status,
                             uint32_t instruction_pc, uint16_t opcode)
{
    vm->fault_pc = instruction_pc;
    vm->fault_opcode = opcode;
    return status;
}

static c33_vm_status_t guest_return(c33_vm_t *vm)
{
    uint32_t target;
    if (!read_u32(vm, vm->sp, &target)) {
        vm->fault_address = vm->sp;
        return C33_VM_FAULT;
    }
    vm->sp += 4;
    vm->pc = target;
    return C33_VM_OK;
}

static c33_vm_status_t run_delay_slot(c33_vm_t *vm, uint32_t target)
{
    uint32_t expected_pc = vm->pc + 2;
    c33_vm_status_t status = c33_vm_step(vm);
    if (status != C33_VM_OK) {
        return status;
    }
    if (vm->pc == expected_pc) {
        vm->pc = target;
    }
    return C33_VM_OK;
}

void c33_vm_init(c33_vm_t *vm)
{
    clear_bytes(vm, sizeof(*vm));
}

int c33_vm_map(c33_vm_t *vm, uint32_t guest_base, void *host,
               uint32_t size, int writable)
{
    c33_vm_region_t *region;
    unsigned i;
    if (!vm || !host || !size || vm->region_count >= C33_VM_MAX_REGIONS) {
        return 0;
    }
    if (guest_base + size < guest_base) {
        return 0;
    }
    for (i = 0; i < vm->region_count; ++i) {
        uint32_t a0 = vm->regions[i].guest_base;
        uint32_t a1 = a0 + vm->regions[i].size;
        uint32_t b1 = guest_base + size;
        if (guest_base < a1 && a0 < b1) {
            return 0;
        }
    }
    region = &vm->regions[vm->region_count++];
    region->guest_base = guest_base;
    region->size = size;
    region->host = (uint8_t *)host;
    region->writable = writable ? 1 : 0;
    return 1;
}

void c33_vm_reset(c33_vm_t *vm, uint32_t entry,
                  uint32_t stack_top, uint32_t argument)
{
    unsigned i;
    for (i = 0; i < 16; ++i) {
        vm->regs[i] = 0;
    }
    vm->pc = entry;
    vm->psr = 0;
    vm->sp = stack_top;
    vm->alr = 0;
    vm->ahr = 0;
    vm->ext_count = 0;
    vm->callback_depth = 0;
    /* S1C33 GNU ABI: the first argument is passed in R6. */
    vm->regs[6] = argument;
    vm->fault_pc = 0;
    vm->fault_address = 0;
    vm->fault_opcode = 0;
    vm->jit_value = 0;
    vm->instructions = 0;
}

int c33_vm_read(c33_vm_t *vm, uint32_t address, void *out, uint32_t size)
{
    c33_vm_region_t *region = find_region(vm, address, size);
    uint8_t *dst = (uint8_t *)out;
    uint8_t *src;
    uint32_t i;
    if (!region || !out) {
        return 0;
    }
    src = region->host + (address - region->guest_base);
    for (i = 0; i < size; ++i) {
        dst[i] = src[i];
    }
    return 1;
}

int c33_vm_write(c33_vm_t *vm, uint32_t address,
                 const void *data, uint32_t size)
{
    c33_vm_region_t *region = find_region(vm, address, size);
    const uint8_t *src = (const uint8_t *)data;
    uint8_t *dst;
    uint32_t i;
    if (!region || !region->writable || !data) {
        return 0;
    }
    dst = region->host + (address - region->guest_base);
    for (i = 0; i < size; ++i) {
        dst[i] = src[i];
    }
    return 1;
}

c33_vm_status_t c33_vm_step(c33_vm_t *vm)
{
    uint32_t instruction_pc;
    uint16_t word;
    unsigned op1;
    unsigned rb;
    unsigned rd;
    unsigned rs;

    if (vm->pc == C33_VM_EXIT_PC) {
        return C33_VM_DONE;
    }
    if (vm->pc >= C33_VM_API_TRAP_BASE && vm->pc <= C33_VM_API_TRAP_END) {
        c33_vm_status_t result;
        if (!vm->hostcall) {
            return C33_VM_UNSUPPORTED;
        }
        result = vm->hostcall(vm, vm->pc, vm->hostcall_opaque);
        if (result != C33_VM_OK) {
            return result;
        }
        return guest_return(vm);
    }

    instruction_pc = vm->pc;
    if (!read_u16(vm, vm->pc, &word)) {
        vm->fault_pc = vm->pc;
        vm->fault_address = vm->pc;
        return C33_VM_FAULT;
    }
    vm->pc += 2;
    vm->instructions++;

    if ((word & 0xe000) == 0xc000) {
        uint16_t imm = word & 0x1fff;
        if (vm->ext_count == 1) {
            vm->ext[1] = imm;
            vm->ext_count = 2;
        } else {
            vm->ext[0] = imm;
            vm->ext[1] = 0;
            vm->ext_count = 1;
        }
        return C33_VM_OK;
    }

    if (word == 0x0000) {
        clear_ext(vm);
        return C33_VM_OK;
    }
    if (word == 0x0400) return fault(vm, C33_VM_BREAK, instruction_pc, word);
    if (word == 0x0040) return fault(vm, C33_VM_SLEEP, instruction_pc, word);
    if (word == 0x0080) return fault(vm, C33_VM_HALT, instruction_pc, word);
    if (word == 0x04c0) return fault(vm, C33_VM_UNSUPPORTED, instruction_pc, word);

    if (word >= 0x0200 && word <= 0x020f) {
        int r;
        for (r = word & 0xf; r >= 0; --r) {
            vm->sp -= 4;
            if (!write_u32(vm, vm->sp, vm->regs[r])) {
                vm->fault_address = vm->sp;
                return fault(vm, C33_VM_FAULT, instruction_pc, word);
            }
        }
        clear_ext(vm);
        return C33_VM_OK;
    }
    if (word >= 0x0240 && word <= 0x024f) {
        for (rd = 0; rd <= (word & 0xf); ++rd) {
            if (!read_u32(vm, vm->sp, &vm->regs[rd])) {
                vm->fault_address = vm->sp;
                return fault(vm, C33_VM_FAULT, instruction_pc, word);
            }
            vm->sp += 4;
        }
        clear_ext(vm);
        return C33_VM_OK;
    }
    if (word == 0x0640 || word == 0x0740) {
        uint32_t target;
        int delayed = word == 0x0740;
        if (!read_u32(vm, vm->sp, &target)) {
            vm->fault_address = vm->sp;
            return fault(vm, C33_VM_FAULT, instruction_pc, word);
        }
        vm->sp += 4;
        clear_ext(vm);
        if (delayed) {
            return run_delay_slot(vm, target);
        }
        vm->pc = target;
        return C33_VM_OK;
    }

    if ((word >= 0x0600 && word <= 0x060f) ||
        (word >= 0x0680 && word <= 0x068f) ||
        (word >= 0x0700 && word <= 0x070f) ||
        (word >= 0x0780 && word <= 0x078f)) {
        uint32_t target = vm->regs[word & 0xf] & 0x0ffffffeu;
        int call = (word & 0x80) == 0;
        int delayed = (word & 0x100) != 0;
        if (call) {
            vm->sp -= 4;
            if (!write_u32(vm, vm->sp, vm->pc + (delayed ? 2u : 0u))) {
                vm->fault_address = vm->sp;
                return fault(vm, C33_VM_FAULT, instruction_pc, word);
            }
        }
        clear_ext(vm);
        if (delayed) {
            return run_delay_slot(vm, target);
        }
        vm->pc = target;
        return C33_VM_OK;
    }

    if (word >= 0x0800 && word <= 0x1bff &&
        ((word >> 9) & 0xf) >= 4 && ((word >> 9) & 0xf) <= 13) {
        int32_t disp = branch_disp(vm, word);
        int take = branch_condition(vm, (word >> 9) & 0xf);
        clear_ext(vm);
        if (word & 0x100) {
            uint32_t target = take
                ? instruction_pc + (uint32_t)disp : vm->pc + 2;
            return run_delay_slot(vm, target);
        }
        if (take) vm->pc = instruction_pc + (uint32_t)disp;
        return C33_VM_OK;
    }

    if (word >= 0x1c00 && word <= 0x1fff) {
        int call = word < 0x1e00;
        int32_t disp = branch_disp(vm, word);
        int delayed = (word & 0x100) != 0;
        uint32_t target = instruction_pc + (uint32_t)disp;
        if (call) {
            vm->sp -= 4;
            if (!write_u32(vm, vm->sp, vm->pc + (delayed ? 2u : 0u))) {
                vm->fault_address = vm->sp;
                return fault(vm, C33_VM_FAULT, instruction_pc, word);
            }
        }
        clear_ext(vm);
        if (delayed) {
            return run_delay_slot(vm, target);
        }
        vm->pc = target;
        return C33_VM_OK;
    }

    if (word >= 0x2000 && word <= 0x3dff &&
        (((word >> 8) & 3) == 0 || ((word >> 8) & 3) == 1)) {
        uint32_t address;
        uint32_t value;
        unsigned size;
        int postinc = !!(word & 0x100);
        op1 = (word >> 10) & 7;
        rb = (word >> 4) & 0xf;
        rd = word & 0xf;
        address = postinc ? vm->regs[rb] : extended_addr(vm, vm->regs[rb]);
        size = (op1 == 0 || op1 == 1 || op1 == 5) ? 1 :
               (op1 == 2 || op1 == 3 || op1 == 6) ? 2 : 4;
        if (op1 <= 4) {
            if (size == 1) {
                uint8_t v8;
                if (!read_u8(vm, address, &v8)) goto memory_fault;
                value = v8;
                if (op1 == 0) value = (uint32_t)(int32_t)(int8_t)v8;
            } else if (size == 2) {
                uint16_t v16;
                if (!read_u16(vm, address, &v16)) goto memory_fault;
                value = v16;
                if (op1 == 2) value = (uint32_t)(int32_t)(int16_t)v16;
            } else {
                if (!read_u32(vm, address, &value)) goto memory_fault;
            }
            vm->regs[rd] = value;
        } else {
            value = vm->regs[rd];
            if (size == 1) {
                if (!write_u8(vm, address, (uint8_t)value)) goto memory_fault;
            } else if (size == 2) {
                if (!write_u16(vm, address, (uint16_t)value)) goto memory_fault;
            } else if (!write_u32(vm, address, value)) {
                goto memory_fault;
            }
        }
        if (postinc) vm->regs[rb] += size;
        clear_ext(vm);
        return C33_VM_OK;
memory_fault:
        vm->fault_address = address;
        return fault(vm, C33_VM_FAULT, instruction_pc, word);
    }

    if (word >= 0x2200 && word <= 0x3eff && ((word >> 8) & 3) == 2) {
        uint32_t lhs;
        uint32_t rhs;
        uint32_t result;
        op1 = (word >> 10) & 7;
        rs = (word >> 4) & 0xf;
        rd = word & 0xf;
        if (vm->ext_count) {
            lhs = vm->regs[rs];
            rhs = extended_imm13(vm);
        } else {
            lhs = vm->regs[rd];
            rhs = vm->regs[rs];
        }
        switch (op1) {
        case 0:
            result = lhs + rhs;
            vm->regs[rd] = result;
            set_add_flags(vm, lhs, rhs, result);
            break;
        case 1:
            result = lhs - rhs;
            vm->regs[rd] = result;
            set_sub_flags(vm, lhs, rhs, result);
            break;
        case 2:
            result = lhs - rhs;
            set_sub_flags(vm, lhs, rhs, result);
            break;
        case 3:
            vm->regs[rd] = vm->regs[rs];
            break;
        case 4:
            vm->regs[rd] = vm->ext_count ? (lhs & rhs) :
                (vm->regs[rd] & vm->regs[rs]);
            set_nz(vm, vm->regs[rd]);
            break;
        case 5:
            vm->regs[rd] = vm->ext_count ? (lhs | rhs) :
                (vm->regs[rd] | vm->regs[rs]);
            set_nz(vm, vm->regs[rd]);
            break;
        case 6:
            vm->regs[rd] = vm->ext_count ? (lhs ^ rhs) :
                (vm->regs[rd] ^ vm->regs[rs]);
            set_nz(vm, vm->regs[rd]);
            break;
        case 7:
            vm->regs[rd] = vm->ext_count ? ~rhs : ~vm->regs[rs];
            set_nz(vm, vm->regs[rd]);
            break;
        }
        clear_ext(vm);
        return C33_VM_OK;
    }

    if (word >= 0x4000 && word <= 0x5fff) {
        uint32_t address;
        uint32_t value;
        uint32_t imm6 = (word >> 4) & 0x3f;
        unsigned size;
        op1 = (word >> 10) & 7;
        rd = word & 0xf;
        size = (op1 == 0 || op1 == 1 || op1 == 5) ? 1 :
               (op1 == 2 || op1 == 3 || op1 == 6) ? 2 : 4;
        if (vm->ext_count == 0) {
            address = vm->sp + imm6 * size;
        } else if (vm->ext_count == 1) {
            address = vm->sp + ((uint32_t)vm->ext[0] << 6) + imm6;
        } else {
            address = vm->sp + ((uint32_t)vm->ext[0] << 19) +
                ((uint32_t)vm->ext[1] << 6) + imm6;
        }
        if (op1 <= 4) {
            if (size == 1) {
                uint8_t v8;
                if (!read_u8(vm, address, &v8)) goto stack_fault;
                value = op1 == 0 ? (uint32_t)(int32_t)(int8_t)v8 : v8;
            } else if (size == 2) {
                uint16_t v16;
                if (!read_u16(vm, address, &v16)) goto stack_fault;
                value = op1 == 2 ? (uint32_t)(int32_t)(int16_t)v16 : v16;
            } else if (!read_u32(vm, address, &value)) {
                goto stack_fault;
            }
            vm->regs[rd] = value;
        } else {
            value = vm->regs[rd];
            if (size == 1) {
                if (!write_u8(vm, address, (uint8_t)value)) goto stack_fault;
            } else if (size == 2) {
                if (!write_u16(vm, address, (uint16_t)value)) goto stack_fault;
            } else if (!write_u32(vm, address, value)) {
                goto stack_fault;
            }
        }
        clear_ext(vm);
        return C33_VM_OK;
stack_fault:
        vm->fault_address = address;
        return fault(vm, C33_VM_FAULT, instruction_pc, word);
    }

    if (word >= 0x6000 && word <= 0x6bff) {
        uint32_t value = (word < 0x6800)
            ? extended_imm6(vm, word) : extended_sign6(vm, word);
        uint32_t old;
        uint32_t result;
        rd = word & 0xf;
        old = vm->regs[rd];
        if (word < 0x6400) {
            result = old + value;
            vm->regs[rd] = result;
            set_add_flags(vm, old, value, result);
        } else if (word < 0x6800) {
            result = old - value;
            vm->regs[rd] = result;
            set_sub_flags(vm, old, value, result);
        } else {
            result = old - value;
            set_sub_flags(vm, old, value, result);
        }
        clear_ext(vm);
        return C33_VM_OK;
    }

    if (word >= 0x6c00 && word <= 0x7fff) {
        uint32_t value = extended_sign6(vm, word);
        rd = word & 0xf;
        if (word < 0x7000) vm->regs[rd] = value;
        else if (word < 0x7400) vm->regs[rd] &= value;
        else if (word < 0x7800) vm->regs[rd] |= value;
        else if (word < 0x7c00) vm->regs[rd] ^= value;
        else vm->regs[rd] = ~value;
        if (word >= 0x7000) set_nz(vm, vm->regs[rd]);
        clear_ext(vm);
        return C33_VM_OK;
    }

    if (word >= 0x8000 && word <= 0x87ff) {
        uint32_t amount = (word & 0x3ff) * 4;
        if (word < 0x8400) vm->sp += amount;
        else vm->sp -= amount;
        clear_ext(vm);
        return C33_VM_OK;
    }

    if (word >= 0x8800 && word <= 0x9dff &&
        ((word >> 8) & 3u) <= 1u &&
        ((word >> 10) & 7u) >= 2u) {
        uint32_t value;
        unsigned count;
        op1 = (word >> 10) & 7u;
        rd = word & 0xfu;
        if (((word >> 8) & 3u) == 0u) {
            count = (word >> 4) & 0xfu;
        } else {
            count = vm->regs[(word >> 4) & 0xfu] & 0xfu;
        }
        if (count & 8u) count = 8u;
        value = vm->regs[rd];
        switch (op1) {
        case 2:
            value >>= count;
            break;
        case 3:
        case 5:
            value <<= count;
            break;
        case 4:
            value = (uint32_t)((int32_t)value >> count);
            break;
        case 6:
            if (count) value = (value >> count) | (value << (32u - count));
            break;
        case 7:
            if (count) value = (value << count) | (value >> (32u - count));
            break;
        default:
            return fault(vm, C33_VM_UNSUPPORTED, instruction_pc, word);
        }
        vm->regs[rd] = value;
        set_nz(vm, value);
        clear_ext(vm);
        return C33_VM_OK;
    }

    if (word >= 0x8a00 && word <= 0x8eff &&
        ((word >> 8) & 3u) == 2u &&
        ((word >> 10) & 7u) >= 2u &&
        ((word >> 10) & 7u) <= 3u) {
        uint32_t value;
        unsigned count;
        op1 = (word >> 10) & 7u;
        rs = (word >> 4) & 0xfu;
        rd = word & 0xfu;
        value = vm->regs[rs] >> 24;
        if (op1 == 2u) value ^= 0xffu;
        count = count_leading_zeros32(value) - 24u;
        vm->regs[rd] = count;
        vm->psr &= ~(C33_PSR_N | C33_PSR_Z | C33_PSR_V | C33_PSR_C);
        if (count == 0) vm->psr |= C33_PSR_Z;
        if (count == 8) vm->psr |= C33_PSR_C;
        clear_ext(vm);
        return C33_VM_OK;
    }

    if (word >= 0x8b00 && word <= 0x9bf0 &&
        ((word >> 8) & 3u) == 3u &&
        ((word >> 10) & 7u) >= 2u &&
        ((word >> 10) & 7u) <= 6u &&
        (word & 0xfu) == 0u) {
        op1 = (word >> 10) & 7u;
        rs = (word >> 4) & 0xfu;
        execute_div_step(vm, op1 - 2u, vm->regs[rs]);
        clear_ext(vm);
        return C33_VM_OK;
    }

    if (word >= 0xa100 && word <= 0xadff &&
        ((word >> 8) & 3u) == 1u &&
        ((word >> 10) & 7u) <= 3u) {
        op1 = (word >> 10) & 7u;
        rs = (word >> 4) & 0xfu;
        rd = word & 0xfu;
        switch (op1) {
        case 0:
            vm->regs[rd] = (uint32_t)(int32_t)(int8_t)vm->regs[rs];
            break;
        case 1:
            vm->regs[rd] = (uint8_t)vm->regs[rs];
            break;
        case 2:
            vm->regs[rd] = (uint32_t)(int32_t)(int16_t)vm->regs[rs];
            break;
        default:
            vm->regs[rd] = (uint16_t)vm->regs[rs];
            break;
        }
        clear_ext(vm);
        return C33_VM_OK;
    }

    if (word >= 0xa200 && word <= 0xaeff &&
        ((word >> 8) & 3u) == 2u &&
        ((word >> 10) & 7u) <= 3u) {
        uint64_t product;
        op1 = (word >> 10) & 7u;
        rs = (word >> 4) & 0xfu;
        rd = word & 0xfu;
        if (op1 == 0u) {
            int32_t signed_product =
                (int32_t)(int16_t)vm->regs[rd] *
                (int32_t)(int16_t)vm->regs[rs];
            vm->alr = (uint32_t)signed_product;
        } else if (op1 == 1u) {
            vm->alr = (uint32_t)(uint16_t)vm->regs[rd] *
                      (uint32_t)(uint16_t)vm->regs[rs];
        } else if (op1 == 2u) {
            int64_t signed_product =
                (int64_t)(int32_t)vm->regs[rd] *
                (int64_t)(int32_t)vm->regs[rs];
            product = (uint64_t)signed_product;
            vm->alr = (uint32_t)product;
            vm->ahr = (uint32_t)(product >> 32);
        } else {
            product = (uint64_t)vm->regs[rd] * (uint64_t)vm->regs[rs];
            vm->alr = (uint32_t)product;
            vm->ahr = (uint32_t)(product >> 32);
        }
        clear_ext(vm);
        return C33_VM_OK;
    }

    if (word >= 0xa000 && word <= 0xa0f3 && (word & 0xc) == 0) {
        rs = (word >> 4) & 0xf;
        switch (word & 3) {
        case 0: vm->psr = vm->regs[rs]; break;
        case 1: vm->sp = vm->regs[rs]; break;
        case 2: vm->alr = vm->regs[rs]; break;
        case 3: vm->ahr = vm->regs[rs]; break;
        }
        clear_ext(vm);
        return C33_VM_OK;
    }
    if (word >= 0xa400 && word <= 0xa43f) {
        rd = word & 0xf;
        switch ((word >> 4) & 3) {
        case 0: vm->regs[rd] = vm->psr; break;
        case 1: vm->regs[rd] = vm->sp; break;
        case 2: vm->regs[rd] = vm->alr; break;
        case 3: vm->regs[rd] = vm->ahr; break;
        }
        clear_ext(vm);
        return C33_VM_OK;
    }

    if (word >= 0xbf40 && word <= 0xbf7f) {
        uint32_t mask = 1u << (word & 0x1f);
        if (word < 0xbf60) vm->psr |= mask;
        else vm->psr &= ~mask;
        clear_ext(vm);
        return C33_VM_OK;
    }

    return fault(vm, C33_VM_UNSUPPORTED, instruction_pc, word);
}

c33_vm_status_t c33_vm_run(c33_vm_t *vm, uint32_t budget)
{
    while (budget) {
        uint32_t native_count = c33_jit_run_block(vm, budget);
        if (native_count) {
            budget -= native_count;
            continue;
        }
        c33_jit_note_fallback(vm);
        --budget;
        {
        c33_vm_status_t status = c33_vm_step(vm);
        if (status == C33_VM_DONE && vm->callback_depth) {
            c33_vm_callback_t *callback =
                &vm->callbacks[--vm->callback_depth];
            if (vm->sp != callback->resume_sp) {
                vm->fault_address = vm->sp;
                return C33_VM_FAULT;
            }
            vm->pc = callback->resume_pc;
            if (vm->pc >= C33_VM_API_TRAP_BASE &&
                vm->pc <= C33_VM_API_TRAP_END) {
                status = guest_return(vm);
                if (status != C33_VM_OK) {
                    return status;
                }
            }
            continue;
        }
        if (status != C33_VM_OK) {
            return status;
        }
        }
    }
    return C33_VM_YIELD;
}

c33_vm_status_t c33_vm_call(c33_vm_t *vm, uint32_t target,
                            uint32_t arg0, uint32_t arg1,
                            uint32_t arg2, uint32_t arg3,
                            uint32_t budget)
{
    static const uint8_t exit_pc[4] = {0xfc, 0xff, 0xff, 0x0f};
    uint32_t resume_pc;
    uint32_t resume_sp;
    c33_vm_status_t status;

    if (!vm || !budget) {
        return C33_VM_FAULT;
    }
    if (target & 1u) {
        vm->fault_address = target;
        return C33_VM_FAULT;
    }
    resume_pc = vm->pc;
    resume_sp = vm->sp;
    if (resume_sp < 4u ||
        !c33_vm_write(vm, resume_sp - 4u, exit_pc, sizeof(exit_pc))) {
        vm->fault_address = resume_sp - 4u;
        return C33_VM_FAULT;
    }

    vm->sp = resume_sp - 4u;
    vm->pc = target;
    vm->regs[6] = arg0;
    vm->regs[7] = arg1;
    vm->regs[8] = arg2;
    vm->regs[9] = arg3;
    vm->ext_count = 0;
    status = C33_VM_OK;
    while (budget) {
        uint32_t native_count = c33_jit_run_block(vm, budget);
        if (native_count) {
            budget -= native_count;
            continue;
        }
        c33_jit_note_fallback(vm);
        --budget;
        status = c33_vm_step(vm);
        if (status != C33_VM_OK) {
            break;
        }
    }
    if (status == C33_VM_OK) {
        status = C33_VM_YIELD;
    }

    /*
     * A normal callback returns through the private sentinel and restores the
     * original stack position. The outer hostcall still has its own guest
     * return address below this boundary.
     */
    if (status == C33_VM_DONE) {
        if (vm->sp == resume_sp) {
            vm->pc = resume_pc;
            return C33_VM_OK;
        }
        vm->fault_address = vm->sp;
    }
    if (status == C33_VM_YIELD) {
        c33_vm_callback_t *callback;
        if (vm->callback_depth >= C33_VM_MAX_CALLBACKS) {
            vm->pc = resume_pc;
            vm->sp = resume_sp;
            return C33_VM_FAULT;
        }
        callback = &vm->callbacks[vm->callback_depth++];
        callback->resume_pc = resume_pc;
        callback->resume_sp = resume_sp;
        return C33_VM_YIELD;
    }
    vm->pc = resume_pc;
    return status == C33_VM_DONE ? C33_VM_FAULT : status;
}

const char *c33_vm_status_string(c33_vm_status_t status)
{
    switch (status) {
    case C33_VM_OK: return "ok";
    case C33_VM_YIELD: return "yield";
    case C33_VM_DONE: return "done";
    case C33_VM_SLEEP: return "sleep";
    case C33_VM_HALT: return "halt";
    case C33_VM_BREAK: return "break";
    case C33_VM_FAULT: return "fault";
    case C33_VM_UNSUPPORTED: return "unsupported opcode/API";
    default: return "unknown";
    }
}
