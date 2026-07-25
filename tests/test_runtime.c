#include <stdio.h>
#include <string.h>

#include "c33vm.h"
#include "compat_api.h"
#include "compat_fs.h"
#include "compat_gui.h"
#include "d300.h"

static void put_u32(unsigned char *p, unsigned value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

static int test_d300(void)
{
    unsigned char image[0x350];
    d300_image_t parsed;
    memset(image, 0, sizeof(image));
    memcpy(image, "D300", 4);
    put_u32(image + 0x04, 0x80);
    put_u32(image + 0x08, 0x330);
    put_u32(image + 0x88, 0x100);
    put_u32(image + 0x8c, 0x210);
    put_u32(image + 0x98, 0x310);
    put_u32(image + 0x9c, 0x20);
    if (d300_parse(&parsed, image, sizeof(image)) != D300_OK) {
        return 1;
    }
    if (parsed.program_size != 0x20 ||
        parsed.size != sizeof(image) ||
        parsed.declared_size != 0x330 ||
        d300_program(&parsed) != image + 0x310) {
        return 2;
    }
    put_u32(image + 0x08, sizeof(image) + 1u);
    return d300_parse(&parsed, image, sizeof(image)) == D300_ERR_DECLARED_SIZE
        ? 0 : 3;
}

static int test_fs_path_mapping(void)
{
    char path[320];
    char small[8];

    if (!compat_fs_map_guest_path("A:\\", path, sizeof(path)) ||
        strcmp(path, COMPAT_FS_NATIVE_ROOT) != 0) {
        return 1;
    }
    if (!compat_fs_map_guest_path(
            "A:\\\xcf\xb5\xcd\xb3\\\xca\xfd\xbe\xdd\\"
            "\xba\xa3\xb5\xc1\xb4\xac\xb4\xe6\xb5\xb5\xd2\xbb.sav",
            path,
            sizeof(path)
        ) ||
        strcmp(
            path,
            COMPAT_FS_NATIVE_ROOT
            "\\\xcf\xb5\xcd\xb3\\\xca\xfd\xbe\xdd\\"
            "\xba\xa3\xb5\xc1\xb4\xac\xb4\xe6\xb5\xb5\xd2\xbb.sav"
        ) != 0) {
        return 2;
    }
    if (!compat_fs_map_guest_path(
            "A:/SAVE0.DAT", path, sizeof(path)
        ) ||
        strcmp(path, COMPAT_FS_NATIVE_ROOT "\\SAVE0.DAT") != 0) {
        return 3;
    }
    if (!compat_fs_map_guest_path(
            "A:\\\xcf\xb5\xcd\xb3\\\xb3\xcc\xd0\xf2\\GAME.EXE",
            path,
            sizeof(path)
        ) ||
        strcmp(
            path, COMPAT_FS_NATIVE_PROGRAMS_ROOT "\\GAME.EXE"
        ) != 0) {
        return 8;
    }
    if (!compat_fs_map_guest_path(
            "relative\\slot.sav", path, sizeof(path)
        ) ||
        strcmp(
            path, COMPAT_FS_NATIVE_ROOT "\\relative\\slot.sav"
        ) != 0) {
        return 4;
    }
    if (!compat_fs_map_guest_path(
            "A:\\foo\\.\\bar\\..\\save.dat", path, sizeof(path)
        ) ||
        strcmp(
            path, COMPAT_FS_NATIVE_ROOT "\\foo\\save.dat"
        ) != 0) {
        return 5;
    }
    if (compat_fs_map_guest_path(
            "A:\\..\\escape.dat", path, sizeof(path)
        ) ||
        path[0] != 0) {
        return 6;
    }
    if (compat_fs_map_guest_path(
            "A:\\file.dat", small, sizeof(small)
        )) {
        return 7;
    }
    return 0;
}

static int test_vm_return(void)
{
    unsigned char iram[0x4000];
    unsigned char code[0x20];
    c33_vm_t vm;
    c33_vm_status_t status;
    memset(iram, 0, sizeof(iram));
    memset(code, 0, sizeof(code));

    /* ld.w %r4, 1; ret */
    code[0] = 0x14;
    code[1] = 0x6c;
    code[2] = 0x40;
    code[3] = 0x06;

    c33_vm_init(&vm);
    if (!c33_vm_map(&vm, 0, iram, sizeof(iram), 1) ||
        !c33_vm_map(&vm, 0x02700000, code, sizeof(code), 0)) {
        return 1;
    }
    c33_vm_reset(&vm, 0x02700000, 0x3f80, 0);
    vm.sp -= 4;
    if (!c33_vm_write(&vm, vm.sp, "\xfc\xff\xff\x0f", 4)) {
        return 2;
    }
    status = c33_vm_run(&vm, 8);
    if (status != C33_VM_DONE) {
        fprintf(stderr, "unexpected VM status: %s\n", c33_vm_status_string(status));
        return 3;
    }
    return vm.regs[4] == 1 ? 0 : 4;
}

static int test_vm_callback(void)
{
    unsigned char iram[0x4000];
    unsigned char code[0x20];
    c33_vm_t vm;
    c33_vm_status_t status;
    memset(iram, 0, sizeof(iram));
    memset(code, 0, sizeof(code));

    /* ld.w %r4, 1; ret */
    code[0] = 0x14;
    code[1] = 0x6c;
    code[2] = 0x40;
    code[3] = 0x06;

    c33_vm_init(&vm);
    if (!c33_vm_map(&vm, 0, iram, sizeof(iram), 1) ||
        !c33_vm_map(&vm, 0x02700000, code, sizeof(code), 0)) {
        return 1;
    }
    c33_vm_reset(&vm, 0x12345678, 0x3f80, 0x11223344);
    if (vm.regs[6] != 0x11223344 || vm.regs[4] != 0) {
        return 2;
    }
    status = c33_vm_call(
        &vm, 0x02700000, 10, 20, 30, 40, 8
    );
    if (status != C33_VM_OK) {
        return 3;
    }
    return vm.pc == 0x12345678 && vm.sp == 0x3f80 && vm.regs[4] == 1
        ? 0 : 4;
}

static int async_callback_ready;
static uint32_t async_callback_tail;

static c33_vm_status_t async_callback_hostcall(
    c33_vm_t *vm,
    uint32_t trap_address,
    void *opaque
)
{
    uint32_t inner_trap = *(uint32_t *)opaque;
    if (trap_address == C33_VM_API_TRAP_BASE) {
        return c33_vm_call(vm, inner_trap, 0, 0, 0, 0, 8);
    }
    if (trap_address == inner_trap) {
        if (!async_callback_ready) {
            return C33_VM_YIELD;
        }
        vm->regs[4] = 42;
        if (async_callback_tail) {
            unsigned char tail_pc[4];
            put_u32(tail_pc, async_callback_tail);
            if (!c33_vm_write(vm, vm->sp, tail_pc, sizeof(tail_pc))) {
                return C33_VM_FAULT;
            }
            /*
             * Make the suspended callback execute a NOP and RET after the
             * inner hostcall. This catches accidental clearing of the
             * callback stack by ordinary instruction cleanup.
             */
            vm->callbacks[vm->callback_depth - 1u].resume_sp += 4u;
        }
        return C33_VM_OK;
    }
    return C33_VM_UNSUPPORTED;
}

static int test_vm_suspended_callback(void)
{
    unsigned char iram[0x4000];
    unsigned char code[4] = {
        0x00, 0x00, /* nop */
        0x40, 0x06  /* ret */
    };
    c33_vm_t vm;
    c33_vm_status_t status;
    uint32_t inner_trap = C33_VM_API_TRAP_BASE + 4u;
    static const unsigned char exit_pc[4] = {
        0xfc, 0xff, 0xff, 0x0f
    };

    memset(iram, 0, sizeof(iram));
    c33_vm_init(&vm);
    if (!c33_vm_map(&vm, 0, iram, sizeof(iram), 1) ||
        !c33_vm_map(&vm, 0x02700000u, code, sizeof(code), 0)) {
        return 1;
    }
    c33_vm_reset(&vm, C33_VM_API_TRAP_BASE, 0x3f80, 0);
    vm.hostcall = async_callback_hostcall;
    vm.hostcall_opaque = &inner_trap;
    vm.sp -= 4;
    if (!c33_vm_write(&vm, vm.sp, exit_pc, sizeof(exit_pc)) ||
        !c33_vm_write(&vm, 0x3f80u, exit_pc, sizeof(exit_pc))) {
        return 2;
    }

    async_callback_ready = 0;
    async_callback_tail = 0x02700000u;
    status = c33_vm_run(&vm, 16);
    if (status != C33_VM_YIELD ||
        vm.callback_depth != 1u ||
        vm.pc != inner_trap) {
        return 3;
    }

    async_callback_ready = 1;
    status = c33_vm_run(&vm, 16);
    if (status != C33_VM_DONE ||
        vm.callback_depth != 0u ||
        vm.sp != 0x3f84u ||
        vm.regs[4] != 42u) {
        return 4;
    }
    async_callback_tail = 0u;
    return 0;
}

static int test_vm_arithmetic_extensions(void)
{
    unsigned char code[8];
    c33_vm_t vm;
    memset(code, 0, sizeof(code));

    /*
     * sll %r4, 2
     * mlt.w %r4, %r6
     * ld.h %r4, %r6
     */
    code[0] = 0x24;
    code[1] = 0x8c;
    code[2] = 0x64;
    code[3] = 0xaa;
    code[4] = 0x64;
    code[5] = 0xa9;

    c33_vm_init(&vm);
    if (!c33_vm_map(&vm, 0x02700000, code, sizeof(code), 0)) {
        return 1;
    }
    c33_vm_reset(&vm, 0x02700000, 0x3f80, 0);
    vm.regs[4] = 3;
    vm.regs[6] = 4;
    if (c33_vm_step(&vm) != C33_VM_OK || vm.regs[4] != 12) {
        return 2;
    }
    if (c33_vm_step(&vm) != C33_VM_OK ||
        vm.alr != 48 || vm.ahr != 0) {
        return 3;
    }
    vm.regs[6] = 0x0000ffff;
    if (c33_vm_step(&vm) != C33_VM_OK || vm.regs[4] != 0xffffffffu) {
        return 4;
    }
    return 0;
}

static int test_api_tables(void)
{
    unsigned char sdram[0x7000];
    c33_vm_t vm;
    compat_api_t api;
    unsigned char value[4];
    unsigned table;

    memset(sdram, 0, sizeof(sdram));
    c33_vm_init(&vm);
    if (!c33_vm_map(&vm, 0x02000000, sdram, sizeof(sdram), 1)) {
        return 1;
    }
    compat_api_init(&api, &vm, 0x02006100, 0x02007000);
    if (!compat_api_install(&api)) {
        return 2;
    }
    if (!c33_vm_read(&vm, 0x02000200, value, 4)) {
        return 3;
    }
    table = value[0] | ((unsigned)value[1] << 8) |
        ((unsigned)value[2] << 16) | ((unsigned)value[3] << 24);
    if (table != COMPAT_ROS33_TABLE_ADDR) {
        return 4;
    }

    vm.regs[6] = 16;
    if (compat_api_hostcall(
            &vm, compat_api_trap(COMPAT_API_CRTL, 0), &api
        ) != C33_VM_OK) {
        return 5;
    }
    return vm.regs[4] == 0x02006100 ? 0 : 6;
}

static int test_gui_image_header(void)
{
    unsigned char header[COMPAT_GUI_IMAGE_HEADER_SIZE] = {
        0x10, 0x00, 0x10, 0x00,
        0x00, 0x02, 0x00, 0x00,
        0xa0, 0x00, 0xf0, 0x00,
        0x80, 0x25, 0x00, 0x00
    };
    unsigned char padded_header[COMPAT_GUI_IMAGE_HEADER_SIZE] = {
        0x10, 0x00, 0x10, 0x00,
        0x00, 0x02, 0x00, 0x00,
        0x9d, 0x00, 0xf0, 0x00,
        0x80, 0x25, 0x00, 0x00
    };

    if (compat_gui_image_payload_offset(
            header, 160, 240, 9600
        ) != COMPAT_GUI_IMAGE_HEADER_SIZE) {
        return 1;
    }
    if (compat_gui_image_payload_offset(header, 80, 240, 4800) != 0u) {
        return 2;
    }
    if (compat_gui_image_payload_offset(header, 160, 243, 9600) !=
        COMPAT_GUI_IMAGE_HEADER_SIZE) {
        return 3;
    }
    if (compat_gui_image_payload_offset(
            padded_header, 160, 240, 9600
        ) != COMPAT_GUI_IMAGE_HEADER_SIZE) {
        return 4;
    }
    if (compat_gui_packed_2bpp_stride(13) != 4u ||
        compat_gui_packed_2bpp_payload_size(13, 13) != 52u ||
        compat_gui_packed_2bpp_shift(0) != 6u ||
        compat_gui_packed_2bpp_shift(3) != 0u ||
        compat_gui_packed_2bpp_shift(4) != 6u) {
        return 5;
    }
    if (compat_gui_timer_interval_ms(20) != 200u ||
        compat_gui_timer_interval_ms(0) != 10u) {
        return 6;
    }
    return 0;
}

int main(void)
{
    int rc;
    rc = test_d300();
    if (rc) {
        fprintf(stderr, "test_d300 failed: %d\n", rc);
        return 1;
    }
    rc = test_fs_path_mapping();
    if (rc) {
        fprintf(stderr, "test_fs_path_mapping failed: %d\n", rc);
        return 1;
    }
    rc = test_vm_return();
    if (rc) {
        fprintf(stderr, "test_vm_return failed: %d\n", rc);
        return 1;
    }
    rc = test_vm_callback();
    if (rc) {
        fprintf(stderr, "test_vm_callback failed: %d\n", rc);
        return 1;
    }
    rc = test_vm_suspended_callback();
    if (rc) {
        fprintf(stderr, "test_vm_suspended_callback failed: %d\n", rc);
        return 1;
    }
    rc = test_vm_arithmetic_extensions();
    if (rc) {
        fprintf(stderr, "test_vm_arithmetic_extensions failed: %d\n", rc);
        return 1;
    }
    rc = test_api_tables();
    if (rc) {
        fprintf(stderr, "test_api_tables failed: %d\n", rc);
        return 1;
    }
    rc = test_gui_image_header();
    if (rc) {
        fprintf(stderr, "test_gui_image_header failed: %d\n", rc);
        return 1;
    }
    puts("host runtime tests: ok");
    return 0;
}
