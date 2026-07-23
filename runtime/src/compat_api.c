#include "../include/compat_api.h"

#define ROS33_SLOTS 64u
#define GUI_SLOTS   512u
#define FS_SLOTS    32u
#define AUDIO_SLOTS 32u
#define CRTL_SLOTS  16u
#define DICT_SLOTS  16u

static void zero_struct(void *ptr, uint32_t size)
{
    uint8_t *p = (uint8_t *)ptr;
    while (size--) *p++ = 0;
}

static uint32_t align4(uint32_t value)
{
    return (value + 3u) & ~3u;
}

uint32_t compat_api_trap(compat_api_group_t group, uint32_t slot)
{
    return C33_VM_API_TRAP_BASE |
           ((uint32_t)group << 16) |
           ((slot * 4u) & 0xffffu);
}

static int api_write_u32(c33_vm_t *vm, uint32_t address, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24)
    };
    return c33_vm_write(vm, address, bytes, 4);
}

static int fill_table(c33_vm_t *vm, uint32_t table,
                      compat_api_group_t group, uint32_t slots)
{
    uint32_t slot;
    for (slot = 0; slot < slots; ++slot) {
        if (!api_write_u32(vm, table + slot * 4u, compat_api_trap(group, slot))) {
            return 0;
        }
    }
    return 1;
}

void compat_api_init(compat_api_t *api, c33_vm_t *vm,
                     uint32_t heap_base, uint32_t heap_end)
{
    zero_struct(api, sizeof(*api));
    api->vm = vm;
    api->heap_base = align4(heap_base);
    api->heap_next = api->heap_base;
    api->heap_end = heap_end & ~3u;
}

int compat_api_install(compat_api_t *api)
{
    c33_vm_t *vm = api->vm;
    static const uint32_t tables[7] = {
        COMPAT_ROS33_TABLE_ADDR,
        COMPAT_GUI_TABLE_ADDR,
        COMPAT_FS_TABLE_ADDR,
        COMPAT_AUDIO_TABLE_ADDR,
        COMPAT_CRTL_TABLE_ADDR,
        COMPAT_DICT_TABLE_ADDR,
        0
    };
    uint32_t i;

    for (i = 0; i < 7; ++i) {
        if (!api_write_u32(vm, COMPAT_GENERAL_TABLE_ADDR + i * 4u, tables[i])) {
            return 0;
        }
    }
    if (!fill_table(vm, COMPAT_ROS33_TABLE_ADDR, COMPAT_API_ROS33, ROS33_SLOTS) ||
        !fill_table(vm, COMPAT_GUI_TABLE_ADDR, COMPAT_API_GUI, GUI_SLOTS) ||
        !fill_table(vm, COMPAT_FS_TABLE_ADDR, COMPAT_API_FS, FS_SLOTS) ||
        !fill_table(vm, COMPAT_AUDIO_TABLE_ADDR, COMPAT_API_AUDIO, AUDIO_SLOTS) ||
        !fill_table(vm, COMPAT_CRTL_TABLE_ADDR, COMPAT_API_CRTL, CRTL_SLOTS) ||
        !fill_table(vm, COMPAT_DICT_TABLE_ADDR, COMPAT_API_DICT, DICT_SLOTS)) {
        return 0;
    }
    vm->hostcall = compat_api_hostcall;
    vm->hostcall_opaque = api;
    return 1;
}

static uint32_t heap_alloc(compat_api_t *api, uint32_t size, int clear)
{
    uint32_t result;
    uint8_t zero[32];
    uint32_t left;
    uint32_t at;
    unsigned i;

    size = align4(size ? size : 4);
    if (api->heap_next > api->heap_end ||
        size > api->heap_end - api->heap_next) {
        return 0;
    }
    result = api->heap_next;
    api->heap_next += size;
    if (!clear) {
        return result;
    }
    for (i = 0; i < sizeof(zero); ++i) zero[i] = 0;
    at = result;
    left = size;
    while (left) {
        uint32_t chunk = left < sizeof(zero) ? left : sizeof(zero);
        if (!c33_vm_write(api->vm, at, zero, chunk)) {
            return 0;
        }
        at += chunk;
        left -= chunk;
    }
    return result;
}

static c33_vm_status_t dispatch_crtl(compat_api_t *api, uint32_t slot)
{
    c33_vm_t *vm = api->vm;
    uint32_t size;
    uint32_t result;

    switch (slot) {
    case 0: /* malloc */
        vm->regs[4] = heap_alloc(api, vm->regs[6], 0);
        return C33_VM_OK;
    case 1: /* free: bump allocator intentionally keeps the block */
        vm->regs[4] = 0;
        return C33_VM_OK;
    case 2: /* calloc */
        size = vm->regs[6] * vm->regs[7];
        if (vm->regs[6] && size / vm->regs[6] != vm->regs[7]) {
            vm->regs[4] = 0;
            return C33_VM_OK;
        }
        vm->regs[4] = heap_alloc(api, size, 1);
        return C33_VM_OK;
    case 3: /* realloc: allocate and copy is added when allocation sizes are tracked */
        result = heap_alloc(api, vm->regs[7], 0);
        vm->regs[4] = result;
        return C33_VM_OK;
    case 4: /* ansi_InitMalloc */
        api->heap_base = align4(vm->regs[6]);
        api->heap_next = api->heap_base;
        api->heap_end = vm->regs[7] & ~3u;
        vm->regs[4] = 0;
        return C33_VM_OK;
    default:
        return C33_VM_UNSUPPORTED;
    }
}

c33_vm_status_t compat_api_hostcall(c33_vm_t *vm,
                                    uint32_t trap_address, void *opaque)
{
    compat_api_t *api = (compat_api_t *)opaque;
    uint32_t relative;
    compat_api_group_t group;
    uint32_t slot;

    if (!api || api->vm != vm ||
        trap_address < C33_VM_API_TRAP_BASE ||
        trap_address > C33_VM_API_TRAP_END) {
        return C33_VM_FAULT;
    }
    relative = trap_address - C33_VM_API_TRAP_BASE;
    group = (compat_api_group_t)((relative >> 16) & 0xff);
    slot = (relative & 0xffffu) / 4u;
    api->last_group = group;
    api->last_slot = slot;

    if (group == COMPAT_API_CRTL) {
        return dispatch_crtl(api, slot);
    }
    if (!api->dispatch) {
        return C33_VM_UNSUPPORTED;
    }
    return api->dispatch(api, group, slot, api->dispatch_opaque);
}
