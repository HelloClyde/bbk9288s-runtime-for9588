#include "../../../eebbk9588/reverse/sdk/bda_sdk.h"

#include "../../runtime/include/c33vm.h"
#include "../../runtime/include/compat_api.h"
#include "../../runtime/include/compat_gui.h"
#include "../../runtime/include/d300.h"

#define GUEST_IRAM_SIZE  0x00004000u
#define GUEST_SDRAM_SIZE 0x00800000u
#define GUEST_SDRAM_BASE 0x02000000u
#define GUEST_STACK_TOP  0x00003f80u
#define GUEST_HEAP_BASE  0x02600000u
#define GUEST_HEAP_END   0x026f0000u
#define VM_SLICE         20000u

static const char k_path_upper[] = "A:\\pirate.exe";
static const char k_path_lower[] = "a:\\pirate.exe";

static void append_text(char **out, char *end, const char *text)
{
    while (*text && *out + 1 < end) {
        *(*out)++ = *text++;
    }
}

static void append_hex(char **out, char *end, u32 value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    append_text(out, end, "0x");
    for (shift = 28; shift >= 0 && *out + 1 < end; shift -= 4) {
        *(*out)++ = digits[(value >> shift) & 0xf];
    }
}

static void show_vm_status(c33_vm_t *vm, c33_vm_status_t status)
{
    char text[160];
    char *out = text;
    char *end = text + sizeof(text);
    append_text(&out, end, "VM: ");
    append_text(&out, end, c33_vm_status_string(status));
    append_text(&out, end, "\nPC: ");
    append_hex(&out, end, vm->fault_pc ? vm->fault_pc : vm->pc);
    append_text(&out, end, "\nOpcode: ");
    append_hex(&out, end, vm->fault_opcode);
    append_text(&out, end, "\nAPI group/slot are logged by the adapter.");
    *out = 0;
    bda_msgbox("9288S compatibility", text);
}

static c33_vm_status_t dispatch_9588(
    compat_api_t *api,
    compat_api_group_t group,
    uint32_t slot,
    void *opaque
)
{
    static const u32 capabilities[] = {
        4u, 160u, 240u, 159u, 239u, 2u, 2u
    };
    c33_vm_t *vm = api->vm;
    (void)opaque;

    if (group != COMPAT_API_GUI) {
        return C33_VM_UNSUPPORTED;
    }
    switch (slot) {
    case COMPAT_GUI_GET_SYS_PIXEL_INDEX:
        vm->regs[4] = vm->regs[6] & 0xffu;
        return C33_VM_OK;
    case COMPAT_GUI_GET_GD_CAPABILITY:
        vm->regs[4] =
            vm->regs[7] < sizeof(capabilities) / sizeof(capabilities[0])
            ? capabilities[vm->regs[7]] : 0u;
        return C33_VM_OK;
    case COMPAT_GUI_GET_DC:
    case COMPAT_GUI_GET_CLIENT_DC:
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_RELEASE_DC:
    case COMPAT_GUI_SET_INSTANT_PAINT:
    case COMPAT_GUI_CLEAR_SCREEN:
    case COMPAT_GUI_DEFAULT_MAIN_WIN_PROC:
    case COMPAT_GUI_MAIN_WINDOW_CLEANUP:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_SET_TIMER:
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_GET_BACKGROUND_PLAY_STATE:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_GET_MESSAGE:
        /* Diagnostic adapter has no persistent message queue yet. */
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_CREATE_MAIN_WINDOW:
        {
            u8 data[4];
            u32 callback;
            c33_vm_status_t status;
            if (!c33_vm_read(
                    vm,
                    vm->regs[6] + COMPAT_MAIN_WIN_CREATE_PROC_OFFSET,
                    data,
                    sizeof(data)
                )) {
                return C33_VM_FAULT;
            }
            callback = (u32)data[0] |
                       ((u32)data[1] << 8) |
                       ((u32)data[2] << 16) |
                       ((u32)data[3] << 24);
            status = c33_vm_call(
                vm, callback, 1u, COMPAT_MSG_CREATE, 0u, 0u, 1000000u
            );
            if (status != C33_VM_OK) return status;
            status = c33_vm_call(
                vm, callback, 1u, COMPAT_MSG_TIMER, 1u, 0u, 1000000u
            );
            if (status != C33_VM_OK) return status;
            vm->regs[4] = 1u;
            return C33_VM_OK;
        }
    default:
        break;
    }
    return C33_VM_UNSUPPORTED;
}

static int load_file(const char *path, u8 **out_bytes, u32 *out_size)
{
    int file = bda_fs_fopen_raw(path, "rb");
    int length;
    u8 *bytes;
    if (!file) {
        return 0;
    }
    if (bda_fs_seek_raw(file, 0, BDA_SEEK_END) != 0) {
        bda_fs_close_raw(file);
        return 0;
    }
    length = bda_fs_tell_raw(file);
    if (length <= 0 || bda_fs_seek_raw(file, 0, BDA_SEEK_SET) != 0) {
        bda_fs_close_raw(file);
        return 0;
    }
    bytes = (u8 *)bda_alloc((u32)length);
    if (!bytes) {
        bda_fs_close_raw(file);
        return 0;
    }
    if (bda_fs_fread_raw(bytes, 1, (u32)length, file) != length) {
        bda_free(bytes);
        bda_fs_close_raw(file);
        return 0;
    }
    bda_fs_close_raw(file);
    *out_bytes = bytes;
    *out_size = (u32)length;
    return 1;
}

__attribute__((section(".text.bda_main")))
int bda_main(void)
{
    u8 *file_bytes = 0;
    u32 file_size = 0;
    u8 *iram = 0;
    u8 *sdram = 0;
    d300_image_t image;
    d300_status_t image_status;
    c33_vm_t vm;
    compat_api_t api;
    c33_vm_status_t vm_status;
    uint8_t exit_pc[4] = {0xfc, 0xff, 0xff, 0x0f};

    if (!load_file(k_path_upper, &file_bytes, &file_size) &&
        !load_file(k_path_lower, &file_bytes, &file_size)) {
        bda_msgbox(
            "9288S compatibility",
            "Copy the authorized D300 app to A:\\pirate.exe"
        );
        return 1;
    }
    image_status = d300_parse(&image, file_bytes, file_size);
    if (image_status != D300_OK) {
        bda_msgbox("9288S compatibility", d300_status_string(image_status));
        bda_free(file_bytes);
        return 2;
    }
    if (image.program_size > GUEST_SDRAM_SIZE - 0x700000u) {
        bda_msgbox("9288S compatibility", "D300 program is too large");
        bda_free(file_bytes);
        return 3;
    }

    iram = (u8 *)bda_alloc(GUEST_IRAM_SIZE);
    sdram = (u8 *)bda_alloc(GUEST_SDRAM_SIZE);
    if (!iram || !sdram) {
        if (iram) bda_free(iram);
        if (sdram) bda_free(sdram);
        bda_free(file_bytes);
        bda_msgbox("9288S compatibility", "Not enough memory for guest RAM");
        return 4;
    }
    bda_memset(iram, 0, GUEST_IRAM_SIZE);
    bda_memset(sdram, 0, GUEST_SDRAM_SIZE);
    bda_memcpy(
        sdram + (D300_GUEST_LOAD_BASE - GUEST_SDRAM_BASE),
        d300_program(&image),
        image.program_size
    );

    c33_vm_init(&vm);
    c33_vm_map(&vm, 0, iram, GUEST_IRAM_SIZE, 1);
    c33_vm_map(&vm, GUEST_SDRAM_BASE, sdram, GUEST_SDRAM_SIZE, 1);
    compat_api_init(&api, &vm, GUEST_HEAP_BASE, GUEST_HEAP_END);
    api.dispatch = dispatch_9588;
    if (!compat_api_install(&api)) {
        bda_msgbox("9288S compatibility", "Could not install API tables");
        bda_free(sdram);
        bda_free(iram);
        bda_free(file_bytes);
        return 5;
    }

    c33_vm_reset(&vm, D300_GUEST_LOAD_BASE, GUEST_STACK_TOP, 0);
    vm.sp -= 4;
    c33_vm_write(&vm, vm.sp, exit_pc, sizeof(exit_pc));
    do {
        vm_status = c33_vm_run(&vm, VM_SLICE);
        if (vm_status == C33_VM_YIELD) {
            bda_sys_delay_like(1000);
        }
    } while (vm_status == C33_VM_YIELD);

    show_vm_status(&vm, vm_status);
    bda_free(sdram);
    bda_free(iram);
    bda_free(file_bytes);
    return vm_status == C33_VM_DONE ? 0 : 6;
}

/* The existing one-source BDA builder compiles a single translation unit. */
#include "../../runtime/src/d300.c"
#include "../../runtime/src/c33vm.c"
#include "../../runtime/src/compat_api.c"
