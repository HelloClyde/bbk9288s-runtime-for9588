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

static uint32_t main_window_callback;
static unsigned char listbox_items[5][64];
static uint32_t listbox_item_count;
static uint32_t listbox_caret;

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

static int write_guest_u32(c33_vm_t *vm, uint32_t address, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char)value,
        (unsigned char)(value >> 8),
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 24)
    };
    return c33_vm_write(vm, address, bytes, sizeof(bytes));
}

static void inspect_put_image(c33_vm_t *vm)
{
    static unsigned full_frame_count;
    uint32_t height;
    uint32_t buffer_address;
    uint32_t width = vm->regs[9];
    size_t raw_size;
    size_t pixel_count;
    unsigned char *packed;
    unsigned char *pixels;
    unsigned long histogram[256];
    unsigned unique = 0;
    unsigned maximum = 0;
    size_t index;

    unsigned char header[COMPAT_GUI_IMAGE_HEADER_SIZE];
    uint32_t pixel_address;

    if (!read_guest_u32(vm, vm->sp + 4u, &height) ||
        !read_guest_u32(vm, vm->sp + 8u, &buffer_address) ||
        width != 160u || height != 240u) {
        return;
    }
    pixel_count = (size_t)width * height;
    raw_size = (pixel_count + 3u) / 4u;
    packed = (unsigned char *)malloc(raw_size);
    pixels = (unsigned char *)malloc(pixel_count);
    pixel_address = buffer_address;
    if (c33_vm_read(vm, buffer_address, header, sizeof(header))) {
        pixel_address += compat_gui_image_payload_offset(
            header, width, height, (unsigned)raw_size
        );
    }
    if (!packed || !pixels ||
        !c33_vm_read(vm, pixel_address, packed, (uint32_t)raw_size)) {
        free(packed);
        free(pixels);
        return;
    }
    memset(histogram, 0, sizeof(histogram));
    for (index = 0; index < pixel_count; ++index) {
        pixels[index] =
            (unsigned char)((packed[index / 4u] >> (6u - 2u * (index & 3u))) & 3u);
        histogram[pixels[index]]++;
    }
    for (index = 0; index < 256u; ++index) {
        if (histogram[index]) {
            ++unique;
            maximum = (unsigned)index;
        }
    }
    ++full_frame_count;
    printf("  full frame header:");
    for (index = 0; index < sizeof(header); ++index) {
        printf(" %02x", header[index]);
    }
    printf("\n");
    printf(
        "  full frame #%u buffer=0x%08lx unique=%u max=%u "
        "counts[0..3]=%lu,%lu,%lu,%lu\n",
        full_frame_count,
        (unsigned long)buffer_address,
        unique,
        maximum,
        histogram[0],
        histogram[1],
        histogram[2],
        histogram[3]
    );
    if (full_frame_count <= 4u) {
        char path[64];
        FILE *file;
        snprintf(
            path,
            sizeof(path),
            "build/host-probe/pirate-frame-%u.pgm",
            full_frame_count
        );
        file = fopen(path, "wb");
        if (file) {
            fprintf(file, "P5\n%lu %lu\n255\n",
                    (unsigned long)width, (unsigned long)height);
            for (index = 0; index < pixel_count; ++index) {
                unsigned char gray = (unsigned char)(pixels[index] * 85u);
                fwrite(&gray, 1, 1, file);
            }
            fclose(file);
            printf("  wrote %s\n", path);
        }
    }
    free(packed);
    free(pixels);
}

static c33_vm_status_t report_api(
    compat_api_t *api,
    compat_api_group_t group,
    uint32_t slot,
    void *opaque
)
{
    static unsigned long calls;
    static unsigned put_image_calls;
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
    if (group == COMPAT_API_GUI &&
        slot == COMPAT_GUI_PUT_IMAGE_AREA &&
        ++put_image_calls <= 24u) {
        uint32_t index;
        uint32_t buffer_address = 0;
        unsigned char header[COMPAT_GUI_IMAGE_HEADER_SIZE];
        printf("  PutImageArea stack sp=0x%08lx:",
               (unsigned long)vm->sp);
        for (index = 0; index < 8u; ++index) {
            uint32_t value = 0;
            if (!read_guest_u32(vm, vm->sp + index * 4u, &value)) {
                printf(" <fault>");
                break;
            }
            printf(" %08lx", (unsigned long)value);
        }
        printf("\n");
        if (read_guest_u32(vm, vm->sp + 8u, &buffer_address) &&
            c33_vm_read(vm, buffer_address, header, sizeof(header))) {
            printf("  image bytes @0x%08lx:",
                   (unsigned long)buffer_address);
            for (index = 0; index < sizeof(header); ++index) {
                printf(" %02x", header[index]);
            }
            printf("\n");
        }
    }
    if (group == COMPAT_API_GUI &&
        slot >= COMPAT_GUI_SHOW_PICTURE_VIRTUAL &&
        slot <= COMPAT_GUI_PRINT_STRING) {
        uint32_t index;
        printf("  regs:");
        for (index = 0; index < 16u; ++index) {
            printf(" r%lu=%08lx",
                   (unsigned long)index,
                   (unsigned long)vm->regs[index]);
        }
        printf("\n");
        printf("  stack sp=0x%08lx:", (unsigned long)vm->sp);
        for (index = 0; index < 24u; ++index) {
            uint32_t value = 0;
            if (!read_guest_u32(vm, vm->sp + index * 4u, &value)) {
                printf(" <fault>");
                break;
            }
            printf(" %08lx", (unsigned long)value);
        }
        printf("\n");
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_TEXT_OUT_LEN) {
        uint32_t length = 0;
        unsigned char text_bytes[64];
        uint32_t i;
        printf("  TextOutLen stack:");
        for (i = 0; i < 8u; ++i) {
            uint32_t value = 0;
            if (!read_guest_u32(vm, vm->sp + i * 4u, &value)) break;
            printf(" %08lx", (unsigned long)value);
        }
        printf("\n");
        if (read_guest_u32(vm, vm->sp + 4u, &length)) {
            if (length > sizeof(text_bytes)) length = sizeof(text_bytes);
            if (c33_vm_read(vm, vm->regs[9], text_bytes, length)) {
                printf("  TextOutLen len=%lu bytes:",
                       (unsigned long)length);
                for (i = 0; i < length; ++i) {
                    printf(" %02x", text_bytes[i]);
                }
                printf("\n");
            }
        }
    }
    if (group == COMPAT_API_GUI && slot == 105u) {
        uint32_t i;
        printf("  CreateWindowEx stack:");
        for (i = 0; i < 12u; ++i) {
            uint32_t value = 0;
            if (!read_guest_u32(vm, vm->sp + i * 4u, &value)) break;
            printf(" %08lx", (unsigned long)value);
        }
        printf("\n");
    }
    if (group == COMPAT_API_FS) {
        unsigned char bytes[96];
        uint32_t argument;
        for (argument = 0; argument < 4u; ++argument) {
            uint32_t i;
            uint32_t address = vm->regs[6u + argument];
            if (!c33_vm_read(vm, address, bytes, sizeof(bytes))) {
                continue;
            }
            printf("  FS arg%lu bytes:", (unsigned long)argument);
            for (i = 0; i < sizeof(bytes) && bytes[i]; ++i) {
                printf(" %02x", bytes[i]);
            }
            printf("\n");
        }
    }

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
    if (group == COMPAT_API_GUI &&
        (slot == COMPAT_GUI_GET_BK_COLOR ||
         slot == COMPAT_GUI_GET_TEXT_COLOR ||
         slot == COMPAT_GUI_GET_PEN_COLOR ||
         slot == COMPAT_GUI_GET_BRUSH_COLOR)) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        (slot == COMPAT_GUI_GET_BK_MODE ||
         slot == COMPAT_GUI_GET_PEN_TYPE ||
         slot == COMPAT_GUI_GET_BRUSH_TYPE)) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        ((slot >= COMPAT_GUI_SET_BK_COLOR &&
          slot <= COMPAT_GUI_SET_BRUSH_TYPE) ||
         slot == COMPAT_GUI_SET_PIXEL ||
         slot == COMPAT_GUI_SET_PIXEL_RGB ||
         slot == COMPAT_GUI_RGB_TO_PIXEL ||
         slot == COMPAT_GUI_LINE_TO ||
         slot == COMPAT_GUI_MOVE_TO ||
         slot == COMPAT_GUI_RECTANGLE ||
         slot == COMPAT_GUI_TEXT_OUT_LEN)) {
        vm->regs[4] = vm->regs[7];
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_SET_RECT) {
        uint32_t bottom;
        if (!read_guest_u32(vm, vm->sp + 4u, &bottom) ||
            !write_guest_u32(vm, vm->regs[6] + 0u, vm->regs[7]) ||
            !write_guest_u32(vm, vm->regs[6] + 4u, vm->regs[8]) ||
            !write_guest_u32(vm, vm->regs[6] + 8u, vm->regs[9]) ||
            !write_guest_u32(vm, vm->regs[6] + 12u, bottom)) {
            return C33_VM_FAULT;
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_SET_RECT_EMPTY) {
        unsigned i;
        for (i = 0; i < 4u; ++i) {
            if (!write_guest_u32(vm, vm->regs[6] + i * 4u, 0u)) {
                return C33_VM_FAULT;
            }
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_POINT_IN_RECT) {
        uint32_t left, top, right, bottom;
        if (!read_guest_u32(vm, vm->regs[6] + 0u, &left) ||
            !read_guest_u32(vm, vm->regs[6] + 4u, &top) ||
            !read_guest_u32(vm, vm->regs[6] + 8u, &right) ||
            !read_guest_u32(vm, vm->regs[6] + 12u, &bottom)) {
            return C33_VM_FAULT;
        }
        vm->regs[4] =
            (int32_t)vm->regs[7] >= (int32_t)left &&
            (int32_t)vm->regs[7] <= (int32_t)right &&
            (int32_t)vm->regs[8] >= (int32_t)top &&
            (int32_t)vm->regs[8] <= (int32_t)bottom;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_SEND_MESSAGE) {
        if (vm->regs[6] == 2u) {
            uint32_t message = vm->regs[7];
            if (message == 0xf180u &&
                listbox_item_count < 5u) {
                uint32_t i;
                for (i = 0; i + 1u < sizeof(listbox_items[0]); ++i) {
                    if (!c33_vm_read(
                            vm,
                            vm->regs[9] + i,
                            &listbox_items[listbox_item_count][i],
                            1u
                        ) ||
                        !listbox_items[listbox_item_count][i]) {
                        break;
                    }
                }
                listbox_items[listbox_item_count][i] = 0;
                vm->regs[4] = listbox_item_count++;
            } else if (message == 0xf186u ||
                       message == 0xf19eu) {
                listbox_caret = vm->regs[8];
                vm->regs[4] = 0u;
            } else if (message == 0xf188u ||
                       message == 0xf19fu) {
                vm->regs[4] = listbox_caret;
            } else if (message == 0xf18au &&
                       listbox_caret < listbox_item_count) {
                vm->regs[4] = (uint32_t)strlen(
                    (const char *)listbox_items[listbox_caret]
                );
            } else if (message == 0xf189u &&
                       listbox_caret < listbox_item_count) {
                uint32_t length = (uint32_t)strlen(
                    (const char *)listbox_items[listbox_caret]
                );
                if (!c33_vm_write(
                        vm,
                        vm->regs[9],
                        listbox_items[listbox_caret],
                        length + 1u
                    )) {
                    return C33_VM_FAULT;
                }
                vm->regs[4] = length;
            } else {
                vm->regs[4] = 0u;
            }
            return C33_VM_OK;
        }
        if (!main_window_callback) {
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
        return c33_vm_call(
            vm,
            main_window_callback,
            vm->regs[6],
            vm->regs[7],
            vm->regs[8],
            vm->regs[9],
            1000000u
        );
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
        main_window_callback = callback;
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
        status = c33_vm_call(
            vm,
            callback,
            1u,
            COMPAT_MSG_KEYDOWN,
            COMPAT_SCANCODE_ENTER,
            0u,
            1000000u
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
        status = c33_vm_call(
            vm,
            callback,
            1u,
            COMPAT_MSG_LBUTTONDOWN,
            4u,
            120u | (170u << 16),
            1000000u
        );
        if (status != C33_VM_OK) {
            return status;
        }
        status = c33_vm_call(
            vm,
            callback,
            1u,
            COMPAT_MSG_LBUTTONUP,
            0u,
            120u | (170u << 16),
            1000000u
        );
        if (status != C33_VM_OK) {
            return status;
        }
        status = c33_vm_call(
            vm,
            callback,
            1u,
            COMPAT_MSG_KEYDOWN,
            COMPAT_SCANCODE_RIGHT,
            0u,
            1000000u
        );
        if (status != C33_VM_OK) {
            return status;
        }
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == 105u) {
        vm->regs[4] = 2u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == 106u) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == 292u) {
        vm->regs[4] = 3u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == 294u) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == 77u) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS &&
        (slot == 15u || slot == 16u)) {
        vm->regs[4] = 0xffffffffu;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 17u) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 18u) {
        if (!write_guest_u32(vm, vm->regs[7] + 0u, 4096u) ||
            !write_guest_u32(vm, vm->regs[7] + 4u, 2048u) ||
            !write_guest_u32(vm, vm->regs[7] + 8u, 8u) ||
            !write_guest_u32(vm, vm->regs[7] + 12u, 512u)) {
            return C33_VM_FAULT;
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 27u) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 0u) {
        vm->regs[4] = 4u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 1u) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 3u) {
        vm->regs[4] = vm->regs[8];
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
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_SAVE_SCREEN_BOX) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_MESSAGE_BOX) {
        /* The scripted exit-path probe confirms the MB_YESNO dialog. */
        vm->regs[4] = 6u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot == COMPAT_GUI_DESTROY_MAIN_WINDOW) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_POST_QUIT_MESSAGE) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_PUT_IMAGE_AREA) {
        inspect_put_image(vm);
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
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_KILL_TIMER) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot >= COMPAT_GUI_SHOW_PICTURE_VIRTUAL &&
        slot <= COMPAT_GUI_PRINT_STRING) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot == COMPAT_GUI_GET_BACKGROUND_PLAY_STATE) {
        /* No background MP3 state in the headless compatibility host. */
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot == COMPAT_GUI_SHOW_STATUS_AND_DESKTOP) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        (slot == COMPAT_GUI_HELP2 ||
         slot == COMPAT_GUI_TRACE_INIT)) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot == COMPAT_GUI_GET_CURRENT_DATETIME) {
        static const unsigned char time_info[8] = {
            12, 0, 0, 0, 0, 0, 0, 0
        };
        static const unsigned char date_info[6] = {
            0xea, 0x07, 7, 23, 4, 0
        };
        if (!c33_vm_write(vm, vm->regs[6], time_info, sizeof(time_info)) ||
            !c33_vm_write(vm, vm->regs[7], date_info, sizeof(date_info))) {
            return C33_VM_FAULT;
        }
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
