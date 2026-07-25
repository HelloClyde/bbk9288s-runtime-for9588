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
    uint32_t candidate;
    uint32_t end;
    uint32_t record_index;
    uint8_t zero[32];
    uint32_t left;
    uint32_t at;
    unsigned i;

    size = align4(size ? size : 4);
    if (size < 4u || api->heap_base > api->heap_end) {
        return 0;
    }
    record_index = COMPAT_HEAP_MAX_BLOCKS;
    for (i = 0u; i < COMPAT_HEAP_MAX_BLOCKS; ++i) {
        if (!api->heap_blocks[i].used) {
            record_index = i;
            break;
        }
    }
    if (record_index == COMPAT_HEAP_MAX_BLOCKS) {
        return 0;
    }

    candidate = api->heap_base;
    for (;;) {
        uint32_t next_address = api->heap_end;
        uint32_t next_size = 0u;
        for (i = 0u; i < COMPAT_HEAP_MAX_BLOCKS; ++i) {
            compat_heap_block_t *block = &api->heap_blocks[i];
            if (block->used &&
                block->address >= candidate &&
                block->address < next_address) {
                next_address = block->address;
                next_size = block->size;
            }
        }
        if (candidate <= next_address &&
            size <= next_address - candidate) {
            break;
        }
        if (!next_size ||
            next_address > 0xffffffffu - next_size) {
            return 0;
        }
        candidate = next_address + next_size;
        if (candidate > api->heap_end) {
            return 0;
        }
    }
    if (candidate > api->heap_end ||
        size > api->heap_end - candidate) {
        return 0;
    }

    result = candidate;
    api->heap_blocks[record_index].address = result;
    api->heap_blocks[record_index].size = size;
    api->heap_blocks[record_index].used = 1u;
    end = result + size;
    if (end > api->heap_next) {
        api->heap_next = end;
    }
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

uint32_t compat_api_heap_alloc(
    compat_api_t *api,
    uint32_t size,
    int clear
)
{
    if (!api) {
        return 0u;
    }
    return heap_alloc(api, size, clear);
}

static compat_heap_block_t *heap_find_block(
    compat_api_t *api,
    uint32_t address
)
{
    unsigned i;
    for (i = 0u; i < COMPAT_HEAP_MAX_BLOCKS; ++i) {
        compat_heap_block_t *block = &api->heap_blocks[i];
        if (block->used && block->address == address) {
            return block;
        }
    }
    return 0;
}

static void heap_free(compat_api_t *api, uint32_t address)
{
    compat_heap_block_t *block;
    if (!address) {
        return;
    }
    block = heap_find_block(api, address);
    if (block) {
        block->address = 0u;
        block->size = 0u;
        block->used = 0u;
    }
}

static uint32_t heap_realloc(
    compat_api_t *api,
    uint32_t address,
    uint32_t requested_size
)
{
    compat_heap_block_t *block;
    uint32_t size;
    uint32_t next_address;
    uint32_t result;
    uint32_t copied;
    uint8_t buffer[64];
    unsigned i;

    if (!address) {
        return heap_alloc(api, requested_size, 0);
    }
    if (!requested_size) {
        heap_free(api, address);
        return 0u;
    }
    block = heap_find_block(api, address);
    if (!block) {
        return 0u;
    }
    size = align4(requested_size);
    if (size < 4u) {
        return 0u;
    }
    if (size <= block->size) {
        block->size = size;
        return address;
    }

    next_address = api->heap_end;
    for (i = 0u; i < COMPAT_HEAP_MAX_BLOCKS; ++i) {
        compat_heap_block_t *other = &api->heap_blocks[i];
        if (other->used &&
            other->address > address &&
            other->address < next_address) {
            next_address = other->address;
        }
    }
    if (address <= next_address &&
        size <= next_address - address) {
        block->size = size;
        if (address + size > api->heap_next) {
            api->heap_next = address + size;
        }
        return address;
    }

    result = heap_alloc(api, size, 0);
    if (!result) {
        return 0u;
    }
    copied = 0u;
    while (copied < block->size) {
        uint32_t chunk = block->size - copied;
        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }
        if (!c33_vm_read(api->vm, address + copied, buffer, chunk) ||
            !c33_vm_write(api->vm, result + copied, buffer, chunk)) {
            heap_free(api, result);
            return 0u;
        }
        copied += chunk;
    }
    heap_free(api, address);
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
    case 1: /* free */
        heap_free(api, vm->regs[6]);
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
    case 3: /* realloc */
        result = heap_realloc(api, vm->regs[6], vm->regs[7]);
        vm->regs[4] = result;
        return C33_VM_OK;
    case 4: /* ansi_InitMalloc */
        api->heap_base = align4(vm->regs[6]);
        api->heap_next = api->heap_base;
        api->heap_end = vm->regs[7] & ~3u;
        zero_struct(api->heap_blocks, sizeof(api->heap_blocks));
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
