#ifndef BBK9288S_COMPAT_API_H
#define BBK9288S_COMPAT_API_H

#include <stdint.h>

#include "c33vm.h"

#define COMPAT_GENERAL_TABLE_ADDR 0x02000200u
#define COMPAT_ROS33_TABLE_ADDR   0x02001000u
#define COMPAT_GUI_TABLE_ADDR     0x02002000u
#define COMPAT_FS_TABLE_ADDR      0x02003000u
#define COMPAT_AUDIO_TABLE_ADDR   0x02004000u
#define COMPAT_CRTL_TABLE_ADDR    0x02005000u
#define COMPAT_DICT_TABLE_ADDR    0x02006000u

typedef enum compat_api_group {
    COMPAT_API_ROS33 = 0,
    COMPAT_API_GUI = 1,
    COMPAT_API_FS = 2,
    COMPAT_API_AUDIO = 3,
    COMPAT_API_CRTL = 4,
    COMPAT_API_DICT = 5
} compat_api_group_t;

struct compat_api;

typedef c33_vm_status_t (*compat_api_dispatch_fn)(
    struct compat_api *api,
    compat_api_group_t group,
    uint32_t slot,
    void *opaque
);

typedef struct compat_api {
    c33_vm_t *vm;
    compat_api_dispatch_fn dispatch;
    void *dispatch_opaque;
    uint32_t heap_base;
    uint32_t heap_next;
    uint32_t heap_end;
    uint32_t last_group;
    uint32_t last_slot;
} compat_api_t;

void compat_api_init(
    compat_api_t *api,
    c33_vm_t *vm,
    uint32_t heap_base,
    uint32_t heap_end
);
int compat_api_install(compat_api_t *api);
c33_vm_status_t compat_api_hostcall(
    c33_vm_t *vm,
    uint32_t trap_address,
    void *opaque
);
uint32_t compat_api_trap(compat_api_group_t group, uint32_t slot);

#endif
