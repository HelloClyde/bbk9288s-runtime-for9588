#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c33vm.h"
#include "compat_api.h"
#include "compat_fs.h"
#include "compat_gui.h"
#include "d300.h"

#define IRAM_SIZE  0x00004000u
#define API_BASE   0x02000000u
#define API_SIZE   0x00010000u
#define HEAP_BASE  0x02600000u
#define HEAP_SIZE  0x00080000u
#define CODE_BASE  0x02700000u
#define CODE_SIZE  0x00100000u

static uint32_t main_window_callback;
static unsigned char listbox_items[5][64];
static uint32_t listbox_item_count;
static uint32_t listbox_caret;
static const char *guest_image_path;
static FILE *guest_image_file;
static unsigned timer_message_sent;
static int probe_five_options;

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
    static unsigned unusual_count;
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
        !read_guest_u32(vm, vm->sp + 8u, &buffer_address)) {
        return;
    }
    if ((int32_t)width <= 0 || (int32_t)height <= 0 ||
        width > 160u || height > 240u) {
        size_t header_index;
        if (unusual_count++ < 40u) {
            printf(
                "  PutImageArea unusual x=%ld y=%ld sp=0x%08lx "
                "w=%ld h=%ld buffer=0x%08lx header:",
                (long)(int32_t)vm->regs[7],
                (long)(int32_t)vm->regs[8],
                (unsigned long)vm->sp,
                (long)(int32_t)width,
                (long)(int32_t)height,
                (unsigned long)buffer_address
            );
            if (c33_vm_read(
                    vm, buffer_address, header, sizeof(header)
                )) {
                for (header_index = 0;
                     header_index < sizeof(header);
                     ++header_index) {
                    printf(" %02x", header[header_index]);
                }
            } else {
                printf(" unmapped");
            }
            printf("\n");
        }
    }
    if (width != 160u || height != 240u) {
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

static void inspect_show_picture_screen(c33_vm_t *vm)
{
    static unsigned dump_count;
    uint32_t height = 0;
    uint32_t picture = 0;
    uint32_t mode = 0;
    uint32_t width = vm->regs[9];
    size_t pixels;
    size_t bytes_1bpp;
    size_t bytes_2bpp;
    unsigned char *packed;
    size_t index;
    FILE *file;

    if (++dump_count != 1u ||
        !read_guest_u32(vm, vm->sp + 4u, &height) ||
        !read_guest_u32(vm, vm->sp + 8u, &picture) ||
        !read_guest_u32(vm, vm->sp + 12u, &mode) ||
        !width || !height) {
        return;
    }
    pixels = (size_t)width * height;
    bytes_1bpp = (size_t)((width + 7u) / 8u) * height;
    bytes_2bpp = (pixels + 3u) / 4u;
    packed = (unsigned char *)malloc(
        bytes_1bpp > bytes_2bpp ? bytes_1bpp : bytes_2bpp
    );
    if (!packed ||
        !c33_vm_read(
            vm,
            picture,
            packed,
            (uint32_t)(bytes_1bpp > bytes_2bpp
                ? bytes_1bpp : bytes_2bpp)
        )) {
        free(packed);
        return;
    }
    printf(
        "  SysShowPicS picture=0x%08lx w=%lu h=%lu mode=%lu bytes:",
        (unsigned long)picture,
        (unsigned long)width,
        (unsigned long)height,
        (unsigned long)mode
    );
    for (index = 0; index < 64u &&
                    index < (bytes_1bpp > bytes_2bpp
                        ? bytes_1bpp : bytes_2bpp); ++index) {
        printf(" %02x", packed[index]);
    }
    printf("\n");

    file = fopen("build/host-probe/sanguo-show-screen-1bpp.pgm", "wb");
    if (file) {
        fprintf(file, "P5\n%lu %lu\n255\n",
                (unsigned long)width, (unsigned long)height);
        for (index = 0; index < pixels; ++index) {
            size_t row = index / width;
            size_t column = index % width;
            unsigned char gray =
                packed[row * ((width + 7u) / 8u) + column / 8u] &
                (0x80u >> (column & 7u)) ? 0u : 255u;
            fwrite(&gray, 1, 1, file);
        }
        fclose(file);
    }
    file = fopen("build/host-probe/sanguo-show-screen-2bpp.pgm", "wb");
    if (file) {
        fprintf(file, "P5\n%lu %lu\n255\n",
                (unsigned long)width, (unsigned long)height);
        for (index = 0; index < pixels; ++index) {
            unsigned value =
                (packed[index / 4u] >> (6u - 2u * (index & 3u))) & 3u;
            unsigned char gray = (unsigned char)(255u - value * 85u);
            fwrite(&gray, 1, 1, file);
        }
        fclose(file);
    }
    free(packed);
}

static void inspect_show_picture_virtual(c33_vm_t *vm)
{
    static unsigned full_frame_dumped;
    uint32_t width = vm->regs[8];
    uint32_t height = vm->regs[9];
    uint32_t picture = 0;
    size_t pixels;
    size_t bytes_2bpp;
    unsigned char *packed;
    size_t index;
    FILE *file;

    if (full_frame_dumped ||
        width != 160u || height != 240u ||
        !read_guest_u32(vm, vm->sp + 4u, &picture)) {
        return;
    }
    pixels = (size_t)width * height;
    bytes_2bpp = ((size_t)width + 3u) / 4u * height;
    packed = (unsigned char *)malloc(bytes_2bpp);
    if (!packed ||
        !c33_vm_read(vm, picture, packed, (uint32_t)bytes_2bpp)) {
        free(packed);
        return;
    }
    full_frame_dumped = 1u;

    file = fopen("build/host-probe/sanguo-show-virtual-1bpp.pgm", "wb");
    if (file) {
        fprintf(file, "P5\n%lu %lu\n255\n",
                (unsigned long)width, (unsigned long)height);
        for (index = 0; index < pixels; ++index) {
            size_t row = index / width;
            size_t column = index % width;
            unsigned char gray =
                packed[row * (width / 8u) + column / 8u] &
                (0x80u >> (column & 7u)) ? 0u : 255u;
            fwrite(&gray, 1, 1, file);
        }
        fclose(file);
    }
    file = fopen("build/host-probe/sanguo-show-virtual-2bpp.pgm", "wb");
    if (file) {
        fprintf(file, "P5\n%lu %lu\n255\n",
                (unsigned long)width, (unsigned long)height);
        for (index = 0; index < pixels; ++index) {
            size_t row = index / width;
            size_t column = index % width;
            unsigned value =
                (packed[row * (width / 4u) + column / 4u] >>
                 (6u - 2u * (column & 3u))) & 3u;
            unsigned char gray = (unsigned char)(255u - value * 85u);
            fwrite(&gray, 1, 1, file);
        }
        fclose(file);
    }
    printf("  wrote virtual full-frame 1bpp/2bpp candidates\n");
    free(packed);
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
        if (slot == COMPAT_GUI_SHOW_PICTURE_VIRTUAL) {
            uint32_t picture = 0;
            uint32_t virtual_screen = 0;
            uint32_t mode = 0;
            unsigned char sample[64];
            unsigned long nibbles[16];
            uint32_t i;
            memset(nibbles, 0, sizeof(nibbles));
            if (read_guest_u32(vm, vm->sp + 4u, &picture) &&
                read_guest_u32(vm, vm->sp + 8u, &virtual_screen) &&
                read_guest_u32(vm, vm->sp + 12u, &mode) &&
                c33_vm_read(vm, picture, sample, sizeof(sample))) {
                printf(
                    "  SysShowPicV picture=0x%08lx vscr=0x%08lx "
                    "mode=%lu bytes:",
                    (unsigned long)picture,
                    (unsigned long)virtual_screen,
                    (unsigned long)mode
                );
                for (i = 0; i < sizeof(sample); ++i) {
                    printf(" %02x", sample[i]);
                    nibbles[sample[i] >> 4]++;
                    nibbles[sample[i] & 0x0fu]++;
                }
                printf("\n  sample nibble histogram:");
                for (i = 0; i < 16u; ++i) {
                    printf(" %lu", nibbles[i]);
                }
                printf("\n");
            }
            inspect_show_picture_virtual(vm);
        } else if (slot == COMPAT_GUI_SHOW_PICTURE_SCREEN) {
            inspect_show_picture_screen(vm);
        }
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
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_DRAW_TEXT_EX) {
        uint32_t rect[4] = {0, 0, 0, 0};
        uint32_t stack[3] = {0, 0, 0};
        unsigned char text_bytes[64];
        uint32_t i;
        c33_vm_read(vm, vm->regs[9], rect, sizeof(rect));
        for (i = 0; i < 3u; ++i) {
            read_guest_u32(vm, vm->sp + 4u + i * 4u, &stack[i]);
        }
        memset(text_bytes, 0, sizeof(text_bytes));
        c33_vm_read(vm, vm->regs[7], text_bytes, sizeof(text_bytes));
        printf(
            "  DrawTextEx count=%ld rect=%ld,%ld,%ld,%ld "
            "indent=%lu format=0x%08lx bytes:",
            (long)(int32_t)vm->regs[8],
            (long)(int32_t)rect[0],
            (long)(int32_t)rect[1],
            (long)(int32_t)rect[2],
            (long)(int32_t)rect[3],
            (unsigned long)stack[0],
            (unsigned long)stack[1]
        );
        for (i = 0; i < sizeof(text_bytes) && text_bytes[i]; ++i) {
            printf(" %02x", text_bytes[i]);
        }
        printf("\n");
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
         slot == COMPAT_GUI_TEXT_OUT_LEN ||
         slot == COMPAT_GUI_DRAW_TEXT_EX)) {
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
    if (group == COMPAT_API_GUI &&
        slot == COMPAT_GUI_DRAW_3D_CONTROL_FRAME) {
        vm->regs[4] = 0u;
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
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_TRANSLATE_MESSAGE) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_DISPATCH_MESSAGE) {
        uint32_t message[7];
        if (!main_window_callback ||
            !c33_vm_read(vm, vm->regs[6], message, sizeof(message))) {
            return C33_VM_FAULT;
        }
        return c33_vm_call(
            vm,
            main_window_callback,
            message[0],
            message[1],
            message[2],
            message[3],
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
        if (!probe_five_options) {
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
            COMPAT_KEYSTATE_LEFT_BUTTON,
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
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_SELECT_FONT) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == 77u) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 0u) {
        unsigned char mode_bytes[8];
        if (c33_vm_read(
                vm, vm->regs[7], mode_bytes, sizeof(mode_bytes)
            )) {
            mode_bytes[sizeof(mode_bytes) - 1u] = 0;
            if (strchr((const char *)mode_bytes, 'w') ||
                strchr((const char *)mode_bytes, 'a') ||
                strchr((const char *)mode_bytes, '+')) {
                vm->regs[4] = 0xffffffffu;
                return C33_VM_OK;
            }
        }
        /*
         * The probe starts with an empty 9288S data root. Returning the EXE
         * bytes for every requested save/config file hid first-run paths.
         */
        if (guest_image_file) {
            fclose(guest_image_file);
            guest_image_file = 0;
        }
        vm->regs[4] = 0xffffffffu;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 1u) {
        if (guest_image_file) {
            fclose(guest_image_file);
            guest_image_file = 0;
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 2u) {
        size_t size = vm->regs[7];
        size_t count = vm->regs[8];
        size_t byte_count;
        unsigned char *buffer;
        size_t items;
        if (!guest_image_file || !size || !count ||
            size > 0x100000u || count > 0x100000u / size) {
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
        byte_count = size * count;
        buffer = (unsigned char *)malloc(byte_count);
        if (!buffer) {
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
        items = fread(buffer, size, count, guest_image_file);
        if (items &&
            !c33_vm_write(vm, vm->regs[6], buffer, (uint32_t)(items * size))) {
            free(buffer);
            return C33_VM_FAULT;
        }
        free(buffer);
        vm->regs[4] = (uint32_t)items;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 4u) {
        vm->regs[4] = guest_image_file
            ? (uint32_t)fseek(
                guest_image_file, (long)(int32_t)vm->regs[7], (int)vm->regs[8]
            )
            : 0xffffffffu;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 5u) {
        vm->regs[4] = guest_image_file
            ? (uint32_t)ftell(guest_image_file) : 0xffffffffu;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 6u) {
        vm->regs[4] = guest_image_file ? (uint32_t)feof(guest_image_file) : 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 7u) {
        vm->regs[4] = guest_image_file
            ? (uint32_t)ferror(guest_image_file) : 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS &&
        (slot == 15u || slot == 16u)) {
        vm->regs[4] = 0xffffffffu;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == COMPAT_FS_MKDIR) {
        vm->regs[4] = 0u;
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
    if (group == COMPAT_API_FS && slot == 19u) {
        static const unsigned char root_path[2] = {'\\', 0};
        uint32_t buffer = vm->regs[6];
        if (!buffer) {
            buffer = compat_api_heap_alloc(api, sizeof(root_path), 0);
        }
        if (!buffer ||
            vm->regs[7] < sizeof(root_path) ||
            !c33_vm_write(vm, buffer, root_path, sizeof(root_path))) {
            vm->regs[4] = 0u;
        } else {
            vm->regs[4] = buffer;
        }
        return C33_VM_OK;
    }
    if (group == COMPAT_API_FS && slot == 27u) {
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
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_UPDATE_WINDOW) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_SET_INSTANT_PAINT) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_INVALIDATE_RECT) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_BEGIN_PAINT) {
        vm->regs[4] = 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_END_PAINT) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_CLIENT_TO_SCREEN) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot >= COMPAT_GUI_SET_HDC_FONT &&
        slot <= COMPAT_GUI_SET_SYS_FONT) {
        vm->regs[4] = slot == COMPAT_GUI_GET_HDC_FONT
            ? 0u : 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_CLEAR_SCREEN) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_CLEAR_RECT) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_SAVE_SCREEN_BOX) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot == COMPAT_GUI_PUT_SAVED_BOX_ON_SCREEN) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_FILL_BOX) {
        uint32_t height = 0;
        read_guest_u32(vm, vm->sp + 4u, &height);
        printf(
            "  FillBox hdc=%lu x=%ld y=%ld w=%lu h=%lu\n",
            (unsigned long)vm->regs[6],
            (long)(int32_t)vm->regs[7],
            (long)(int32_t)vm->regs[8],
            (unsigned long)vm->regs[9],
            (unsigned long)height
        );
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
        if (timer_message_sent < 64u) {
            uint32_t message[7] = {
                1u, COMPAT_MSG_TIMER, 1u, 0u, 0u, 0u, 0u
            };
            if (probe_five_options && timer_message_sent == 8u) {
                message[1] = COMPAT_MSG_KEYDOWN;
                message[2] = COMPAT_SCANCODE_DOWN;
            } else if (probe_five_options &&
                       timer_message_sent == 9u) {
                message[1] = COMPAT_MSG_KEYDOWN;
                message[2] = COMPAT_SCANCODE_ENTER;
            } else if (!probe_five_options &&
                       timer_message_sent >= 32u &&
                (timer_message_sent - 32u) % 8u == 0u) {
                message[1] = COMPAT_MSG_KEYDOWN;
                message[2] = COMPAT_SCANCODE_ENTER;
            }
            if (!c33_vm_write(
                    vm, vm->regs[6], message, sizeof(message)
                )) {
                return C33_VM_FAULT;
            }
            ++timer_message_sent;
            vm->regs[4] = 1u;
            return C33_VM_OK;
        }
        /* Stop once the scripted input/timer sequence is exhausted. */
        return C33_VM_YIELD;
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
        if (slot == COMPAT_GUI_DRAW_ASCII) {
            uint32_t virtual_screen = 0u;
            printf(
                "  SysAscii sp=0x%08lx stack5=%s0x%08lx\n",
                (unsigned long)vm->sp,
                read_guest_u32(vm, vm->sp + 4u, &virtual_screen)
                    ? "" : "unmapped/",
                (unsigned long)virtual_screen
            );
        }
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
        slot >= COMPAT_GUI_RESET_AUTO_CLOSE_TIMER &&
        slot <= COMPAT_GUI_RESET_AUTO_CLOSE_LED) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot == COMPAT_GUI_SHOW_STATUS_AND_DESKTOP) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_REVERSE_RECT) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_GET_SCREEN_WIDTH) {
        vm->regs[4] = 160u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI && slot == COMPAT_GUI_GET_SCREEN_HEIGHT) {
        vm->regs[4] = 240u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot >= COMPAT_GUI_MUSIC_VOLUME_SET &&
        slot <= COMPAT_GUI_MUSIC_OUTPUT_GET) {
        vm->regs[4] =
            slot == COMPAT_GUI_MUSIC_VOLUME_GET ? 5u : 1u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot == COMPAT_GUI_SCAN_GAME_COMBO_KEYS) {
        static const unsigned char keys[6] = {0, 0, 0, 0, 0, 0};
        if (!c33_vm_write(vm, vm->regs[6], keys, sizeof(keys))) {
            return C33_VM_FAULT;
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        (slot == COMPAT_GUI_SET_LANDSCAPE ||
         slot == COMPAT_GUI_GET_LANDSCAPE)) {
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (group == COMPAT_API_GUI &&
        slot >= COMPAT_GUI_ATTACH_ENABLE &&
        slot <= COMPAT_GUI_DISPLAY_BACKLIGHT_STATUS) {
        vm->regs[4] =
            slot == COMPAT_GUI_GET_BACKLIGHT_STATUS ? 1u : 0u;
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
    unsigned char *api_ram;
    unsigned char *heap_ram;
    unsigned char *code_ram;
    size_t file_size;
    d300_image_t image;
    c33_vm_t vm;
    compat_api_t api;
    c33_vm_status_t status;
    unsigned char exit_pc[4] = {0xfc, 0xff, 0xff, 0x0f};

    if (argc == 3 && strcmp(argv[1], "--five-options") == 0) {
        probe_five_options = 1;
        ++argv;
        --argc;
    }
    if (argc != 2) {
        fprintf(
            stderr,
            "usage: d300-core-probe [--five-options] path-to-app.exe\n"
        );
        return 2;
    }
    file_bytes = read_file(argv[1], &file_size);
    if (!file_bytes) {
        fprintf(stderr, "could not read %s\n", argv[1]);
        return 2;
    }
    guest_image_path = argv[1];
    if (d300_parse(&image, file_bytes, file_size) != D300_OK) {
        fprintf(stderr, "invalid D300 image\n");
        free(file_bytes);
        return 2;
    }
    iram = (unsigned char *)calloc(1, IRAM_SIZE);
    api_ram = (unsigned char *)calloc(1, API_SIZE);
    heap_ram = (unsigned char *)calloc(1, HEAP_SIZE);
    code_ram = (unsigned char *)calloc(1, CODE_SIZE);
    if (!iram || !api_ram || !heap_ram || !code_ram) {
        fprintf(stderr, "allocation failed\n");
        free(iram);
        free(api_ram);
        free(heap_ram);
        free(code_ram);
        free(file_bytes);
        return 2;
    }
    memcpy(
        code_ram,
        d300_program(&image),
        image.program_size
    );
    c33_vm_init(&vm);
    c33_vm_map(&vm, 0, iram, IRAM_SIZE, 1);
    c33_vm_map(&vm, API_BASE, api_ram, API_SIZE, 1);
    c33_vm_map(&vm, HEAP_BASE, heap_ram, HEAP_SIZE, 1);
    c33_vm_map(&vm, CODE_BASE, code_ram, CODE_SIZE, 1);
    compat_api_init(&api, &vm, HEAP_BASE, HEAP_BASE + HEAP_SIZE);
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

    free(code_ram);
    free(heap_ram);
    free(api_ram);
    free(iram);
    if (guest_image_file) {
        fclose(guest_image_file);
    }
    free(file_bytes);
    return status == C33_VM_DONE ? 0 : 1;
}
