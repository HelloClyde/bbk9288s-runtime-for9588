#ifndef BBK9288S_C33VM_H
#define BBK9288S_C33VM_H

#include <stddef.h>
#include <stdint.h>

#define C33_VM_MAX_REGIONS 6u
#define C33_VM_API_TRAP_BASE 0x0f000000u
#define C33_VM_API_TRAP_END  0x0ffffffcu
#define C33_VM_EXIT_PC       0x0ffffffcu

#define C33_PSR_N (1u << 0)
#define C33_PSR_Z (1u << 1)
#define C33_PSR_V (1u << 2)
#define C33_PSR_C (1u << 3)
#define C33_PSR_DS (1u << 12)

typedef enum c33_vm_status {
    C33_VM_OK = 0,
    C33_VM_YIELD,
    C33_VM_DONE,
    C33_VM_SLEEP,
    C33_VM_HALT,
    C33_VM_BREAK,
    C33_VM_FAULT,
    C33_VM_UNSUPPORTED
} c33_vm_status_t;

typedef struct c33_vm_region {
    uint32_t guest_base;
    uint32_t size;
    uint8_t *host;
    uint8_t writable;
} c33_vm_region_t;

struct c33_vm;

typedef c33_vm_status_t (*c33_vm_hostcall_fn)(
    struct c33_vm *vm,
    uint32_t trap_address,
    void *opaque
);

typedef struct c33_vm {
    uint32_t regs[16];
    uint32_t pc;
    uint32_t psr;
    uint32_t sp;
    uint32_t alr;
    uint32_t ahr;
    uint16_t ext[2];
    uint8_t ext_count;
    c33_vm_region_t regions[C33_VM_MAX_REGIONS];
    uint8_t region_count;
    c33_vm_hostcall_fn hostcall;
    void *hostcall_opaque;
    uint32_t fault_pc;
    uint32_t fault_address;
    uint16_t fault_opcode;
    uint64_t instructions;
} c33_vm_t;

void c33_vm_init(c33_vm_t *vm);
int c33_vm_map(
    c33_vm_t *vm,
    uint32_t guest_base,
    void *host,
    uint32_t size,
    int writable
);
void c33_vm_reset(
    c33_vm_t *vm,
    uint32_t entry,
    uint32_t stack_top,
    uint32_t argument
);
c33_vm_status_t c33_vm_step(c33_vm_t *vm);
c33_vm_status_t c33_vm_run(c33_vm_t *vm, uint32_t budget);
c33_vm_status_t c33_vm_call(
    c33_vm_t *vm,
    uint32_t target,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3,
    uint32_t budget
);
int c33_vm_read(c33_vm_t *vm, uint32_t address, void *out, uint32_t size);
int c33_vm_write(c33_vm_t *vm, uint32_t address, const void *data, uint32_t size);
const char *c33_vm_status_string(c33_vm_status_t status);

#endif
