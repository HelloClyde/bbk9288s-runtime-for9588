#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c33vm.h"
#include "compat_api.h"
#include "compat_gui.h"
#include "d300.h"

#define IRAM_SIZE  0x00004000u
#define SDRAM_SIZE 0x00800000u
#define SDRAM_BASE 0x02000000u

static int read_guest_u32(c33_vm_t *vm, uint32_t address, uint32_t *value)
{
    unsigned char bytes[4];
    if (!c33_vm_read(vm, address, bytes, sizeof(bytes))) {
        return 0;
    }
    *value = (uint32_t)bytes[0] |
             ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) |
             ((uint32_t)bytes[3] << 24);
    return 1;
}

static c33_vm_status_t report_api(
    compat_api_t *api,
    compat_api_group_t group,
    uint32_t slot,
    void *opaque
)
{
    static unsigned long calls;
    c33_vm_t *vm = api->vm;
    (void)opaque;
    ++calls;
    printf(
        "host API #%lu: group=%u slot=%u "
        "args=0x%08lx,0x%08lx,0x%08lx,0x%08lx\n",
        calls,
        (unsigned)group,
        (unsigned)slot,
        (unsigned long)vm->regs[6],
        (unsigned long)vm->regs[7],
        (unsigned long)vm->regs[8],
        (unsigned long)vm->regs[9]
    );

    /*
     * GUI slot 191 is GetSysPixelIndex in the SDK configuration used by the
     * 9288S application environment. Returning the small logical color ID is
     * sufficient for headless startup discovery; the 9588 port translates
     * the value to RGB565 when a real surface is involved.
     */
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_GET_SYS_PIXEL_INDEX) {
        vm->regs[4] = vm->regs[6] & 0xffu;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_GET_GD_CAPABILITY) {
        static const uint32_t capabilities[] = {
            4u, 160u, 240u, 159u, 239u, 2u, 2u
        };
        vm->regs[4] = vm->regs[7] < sizeof(capabilities) / sizeof(capabilities[0])
            ? capabilities[vm->regs[7]] : 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        (slot == COMPAT_GUI_GET_DC || slot == COMPAT_GUI_GET_CLIENT_DC)) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_RELEASE_DC) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_CREATE_MAIN_WINDOW) {
        uint32_t callback;
        c33_vm_status_t status;
        if (!read_guest_u32(
                vm,
                vm->regs[6] + COMPAT_MAIN_WIN_CREATE_PROC_OFFSET,
                &callback
            )) {
            vm->fault_address =
                vm->regs[6] + COMPAT_MAIN_WIN_CREATE_PROC_OFFSET;
            return C33_VM_FAULT;
        }
        printf("CreateMainWindow callback=0x%08lx\n", (unsigned long)callback);
        status = c33_vm_call(
            vm, callback, 1u, COMPAT_MSG_CREATE, 0u, 0u, 1000000u
        );
        if (status != C33_VM_OK) {
            return status;
        }
        status = c33_vm_call(
            vm, callback, 1u, COMPAT_MSG_PAINT, 0u, 0u, 1000000u
        );
        if (status != C33_VM_OK) {
            return status;
        }
        status = c33_vm_call(
            vm, callback, 1u, COMPAT_MSG_TIMER, 1u, 0u, 1000000u
        );
        if (status != C33_VM_OK) {
            return status;
        }
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_DEFAULT_MAIN_WIN_PROC) {
        /* DefaultMainWinProc */
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_SET_INSTANT_PAINT) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_CLEAR_SCREEN) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_GET_MESSAGE) {
        /* No queued messages in the headless probe: terminate the app loop. */
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_MAIN_WINDOW_CLEANUP) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_SET_TIMER) {
        /* Record-only timer for startup discovery; no ticks are injected. */
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot == COMPAT_GUI_GET_BACKGROUND_PLAY_STATE) {
        /* No background MP3 state in the headless compatibility host. */
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    return C33_VM_UNSUPPORTED;
}

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    unsigned char *bytes;
    long length;
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    bytes = (unsigned char *)malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return 0;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

int main(int argc, char **argv)
{
    unsigned char *file_bytes;
    unsigned char *iram;
    unsigned char *sdram;
    size_t file_size;
    d300_image_t image;
    c33_vm_t vm;
    compat_api_t api;
    c33_vm_status_t status;
    unsigned char exit_pc[4] = {0xfc, 0xff, 0xff, 0x0f};

    if (argc != 2) {
        fprintf(stderr, "usage: d300-core-probe path-to-app.exe\n");
        return 2;
    }
    file_bytes = read_file(argv[1], &file_size);
    if (!file_bytes) {
        fprintf(stderr, "could not read %s\n", argv[1]);
        return 2;
    }
    if (d300_parse(&image, file_bytes, file_size) != D300_OK) {
        fprintf(stderr, "invalid D300 image\n");
        free(file_bytes);
        return 2;
    }
    iram = (unsigned char *)calloc(1, IRAM_SIZE);
    sdram = (unsigned char *)calloc(1, SDRAM_SIZE);
    if (!iram || !sdram) {
        fprintf(stderr, "allocation failed\n");
        free(iram);
        free(sdram);
        free(file_bytes);
        return 2;
    }
    memcpy(
        sdram + (D300_GUEST_LOAD_BASE - SDRAM_BASE),
        d300_program(&image),
        image.program_size
    );
    c33_vm_init(&vm);
    c33_vm_map(&vm, 0, iram, IRAM_SIZE, 1);
    c33_vm_map(&vm, SDRAM_BASE, sdram, SDRAM_SIZE, 1);
    compat_api_init(&api, &vm, 0x02600000, 0x026f0000);
    api.dispatch = report_api;
    compat_api_install(&api);
    c33_vm_reset(&vm, D300_GUEST_LOAD_BASE, 0x3f80, 0);
    vm.sp -= 4;
    c33_vm_write(&vm, vm.sp, exit_pc, sizeof(exit_pc));

    status = c33_vm_run(&vm, 5000000);
    printf(
        "status=%s instructions=%llu pc=0x%08lx opcode=0x%04x "
        "fault-address=0x%08lx last-api=%lu/%lu\n",
        c33_vm_status_string(status),
        (unsigned long long)vm.instructions,
        (unsigned long)(vm.fault_pc ? vm.fault_pc : vm.pc),
        vm.fault_opcode,
        (unsigned long)vm.fault_address,
        (unsigned long)api.last_group,
        (unsigned long)api.last_slot
    );

    free(sdram);
    free(iram);
    free(file_bytes);
    return status == C33_VM_DONE ? 0 : 1;
}
