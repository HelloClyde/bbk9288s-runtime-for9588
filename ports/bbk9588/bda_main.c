#include "bda_sdk.h"

#include "../../runtime/include/c33vm.h"
#include "../../runtime/include/compat_api.h"
#include "../../runtime/include/compat_fs.h"
#include "../../runtime/include/compat_gui.h"
#include "../../runtime/include/d300.h"

#include "../../build/bbk9588/generated_game.inc"

#define SCREEN_W         320
#define SCREEN_H         240
#define GUEST_SCREEN_W   160
#define GUEST_SCREEN_H   240
#define GUEST_SCREEN_X   ((SCREEN_W - GUEST_SCREEN_W) / 2)
#define PHYSICAL_SCREEN_W 240
#define PHYSICAL_SCREEN_H 320
#define PHYSICAL_GUEST_X  ((PHYSICAL_SCREEN_W - GUEST_SCREEN_W) / 2)
#define PHYSICAL_GUEST_Y  ((PHYSICAL_SCREEN_H - GUEST_SCREEN_H) / 2)
#define QEMU_FRAMEBUFFER  0xa1f82000u
#define QEMU_EVENT_QUEUE  0xa9f00040u
#define QEMU_EVENT_MAGIC  0x514b4242u
#define QEMU_EVENT_SLOTS  8u
#define QEMU_EVENT_WORDS  5u
#define QEMU_EVENT_KIND_KEY 1u
#define QEMU_TOUCH_TRACE  0xa9f00100u
#define QEMU_TOUCH_MAGIC  0x54434b42u

#define GUEST_IRAM_SIZE  0x00004000u
#define GUEST_API_BASE   0x02000000u
#define GUEST_API_SIZE   0x00010000u
#define GUEST_HEAP_SIZE  0x00020000u
#define GUEST_CODE_BASE  0x02700000u
#define GUEST_CODE_SIZE  0x00020000u
#define GUEST_STACK_TOP  0x00003f80u
#define GUEST_HEAP_BASE  0x02600000u
#define GUEST_HEAP_END   (GUEST_HEAP_BASE + GUEST_HEAP_SIZE)
#define VM_SLICE         100000u
#define CALLBACK_BUDGET  1000000u
#define EVENT_QUEUE_SIZE 16u
#define LISTBOX_ITEM_COUNT 5u
#define LISTBOX_ITEM_SIZE  64u
#define GUEST_CONTROL_HWND 0x100u
#define LISTBOX_SELECT_X   53u
#define LISTBOX_SELECT_Y   90u
#define LISTBOX_SELECT_W   88u
#define LISTBOX_SELECT_H   16u
#define LISTBOX_ITEM_STEP  19u
#define HOST_TICK_MS     25u
#define MAX_FILE_IO_SIZE 0x00020000u

/* "海盗船" in the GBK encoding consumed by the 9588 firmware UI. */
static const char k_native_help_title[] =
    "\xba\xa3\xb5\xc1\xb4\xac";

static const char *const k_native_save_paths[5] = {
    "A:\\PIRATE1.SAV",
    "A:\\PIRATE2.SAV",
    "A:\\PIRATE3.SAV",
    "A:\\PIRATE4.SAV",
    "A:\\PIRATE5.SAV"
};

/* GBK filenames used by the original game, without the 9288S-only prefix. */
static const char *const k_guest_save_names[5] = {
    "\xba\xa3\xb5\xc1\xb4\xac\xb4\xe6\xb5\xb5\xd2\xbb.sav",
    "\xba\xa3\xb5\xc1\xb4\xac\xb4\xe6\xb5\xb5\xb6\xfe.sav",
    "\xba\xa3\xb5\xc1\xb4\xac\xb4\xe6\xb5\xb5\xc8\xfd.sav",
    "\xba\xa3\xb5\xc1\xb4\xac\xb4\xe6\xb5\xb5\xcb\xc4.sav",
    "\xba\xa3\xb5\xc1\xb4\xac\xb4\xe6\xb5\xb5\xce\xe5.sav"
};

typedef struct guest_message {
    u32 hwnd;
    u32 message;
    u32 wparam;
    u32 lparam;
    u32 time;
    u32 point_x;
    u32 point_y;
} guest_message_t;

typedef struct compat_9588_state {
    c33_vm_t *vm;
    u16 *framebuffer;
    u32 guest_window_proc;
    u32 guest_hwnd;
    u32 parent_window_proc;
    u32 parent_hwnd;
    u32 parent_pen_color;
    u32 parent_brush_color;
    u32 parent_background_color;
    s32 parent_current_x;
    s32 parent_current_y;
    int parent_instant_paint;
    u32 next_hwnd;
    u32 guest_control_hwnd;
    char listbox_items[LISTBOX_ITEM_COUNT][LISTBOX_ITEM_SIZE];
    u32 listbox_item_count;
    u32 listbox_caret;
    u32 listbox_drawn_caret;
    bda_fs_find_data_t native_find_data;
    u32 guest_find_data;
    int native_find_open;
    guest_message_t events[EVENT_QUEUE_SIZE];
    u32 event_read;
    u32 event_write;
    u32 event_count;
    u32 hardware_event_cursor;
    int hardware_events_ready;
    int touch_ready;
    int touch_down;
    int touch_captured;
    u32 touch_x;
    u32 touch_y;
    u32 timer_hwnd;
    u32 timer_id;
    u32 timer_interval;
    u32 timer_elapsed;
    u32 message_time;
    u32 pen_color;
    u32 brush_color;
    u32 background_color;
    u32 text_color;
    s32 current_x;
    s32 current_y;
    int instant_paint;
    int quit;
} compat_9588_state_t;

typedef struct compat_9588_runtime {
    d300_image_t image;
    c33_vm_t vm;
    compat_api_t api;
    compat_9588_state_t state;
} compat_9588_runtime_t;

/*
 * Keep this in the packaged .data section.  The minimal BDA loader copies the
 * file-backed image but does not reserve a separate trailing .bss range.
 */
static volatile u32 g_diagnostic[16] = {
    0x44473932u, 0x13579bdfu, 0x2468ace0u,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static int allocation_failed(const void *pointer)
{
    u32 value = (u32)pointer;
    return value == 0u || value >= 0xfffffff0u;
}

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
    char text[192];
    char *out = text;
    char *end = text + sizeof(text);
    append_text(&out, end, "VM: ");
    append_text(&out, end, c33_vm_status_string(status));
    append_text(&out, end, "\nPC: ");
    append_hex(&out, end, vm->fault_pc ? vm->fault_pc : vm->pc);
    append_text(&out, end, "\nOpcode: ");
    append_hex(&out, end, vm->fault_opcode);
    append_text(&out, end, "\nLast API: ");
    if (vm->hostcall_opaque) {
        compat_api_t *api = (compat_api_t *)vm->hostcall_opaque;
        append_hex(&out, end, api->last_group);
        append_text(&out, end, "/");
        append_hex(&out, end, api->last_slot);
    }
    *out = 0;
    bda_msgbox("9288S compatibility", text);
}

static int guest_read_u32(c33_vm_t *vm, u32 address, u32 *value)
{
    u8 bytes[4];
    if (!c33_vm_read(vm, address, bytes, sizeof(bytes))) {
        return 0;
    }
    *value = (u32)bytes[0] |
             ((u32)bytes[1] << 8) |
             ((u32)bytes[2] << 16) |
             ((u32)bytes[3] << 24);
    return 1;
}

static int guest_write_u32(c33_vm_t *vm, u32 address, u32 value)
{
    u8 bytes[4] = {
        (u8)value,
        (u8)(value >> 8),
        (u8)(value >> 16),
        (u8)(value >> 24)
    };
    return c33_vm_write(vm, address, bytes, sizeof(bytes));
}

static int guest_read_c_string(
    c33_vm_t *vm,
    u32 address,
    char *buffer,
    u32 capacity
)
{
    u32 index;
    if (!buffer || capacity == 0u) {
        return 0;
    }
    if (!address) {
        buffer[0] = 0;
        return 1;
    }
    for (index = 0; index + 1u < capacity; ++index) {
        u8 ch;
        if (!c33_vm_read(vm, address + index, &ch, 1u)) {
            buffer[0] = 0;
            return 0;
        }
        buffer[index] = (char)ch;
        if (!ch) {
            return 1;
        }
    }
    buffer[capacity - 1u] = 0;
    return 1;
}

static char *guest_copy_c_string(
    c33_vm_t *vm,
    u32 address,
    u32 maximum_size
)
{
    u32 length;
    char *copy;
    if (!address || maximum_size < 2u) {
        return 0;
    }
    for (length = 0u; length + 1u < maximum_size; ++length) {
        u8 ch;
        if (!c33_vm_read(vm, address + length, &ch, 1u)) {
            vm->fault_address = address + length;
            return 0;
        }
        if (!ch) {
            copy = (char *)bda_alloc(length + 1u);
            if (allocation_failed(copy)) {
                return 0;
            }
            if (!c33_vm_read(vm, address, copy, length + 1u)) {
                bda_free(copy);
                vm->fault_address = address;
                return 0;
            }
            return copy;
        }
    }
    vm->fault_address = address + maximum_size - 1u;
    return 0;
}

static int byte_string_contains(const char *text, const char *needle)
{
    const char *candidate;
    if (!text || !needle || !*needle) {
        return 0;
    }
    for (candidate = text; *candidate; ++candidate) {
        const char *left = candidate;
        const char *right = needle;
        while (*left && *right && *left == *right) {
            ++left;
            ++right;
        }
        if (!*right) {
            return 1;
        }
    }
    return 0;
}

static const char *translate_save_path(const char *guest_path)
{
    u32 index;
    for (index = 0u; index < 5u; ++index) {
        if (byte_string_contains(guest_path, k_guest_save_names[index])) {
            return k_native_save_paths[index];
        }
    }
    /*
     * 海盗船 only opens the five paths above. Return a stable private
     * fallback instead of allowing an incompatible 9288S absolute path to
     * escape into the 9588 filesystem.
     */
    return "A:\\PIRATE0.DAT";
}

static u16 logical_color_to_rgb565(u32 color)
{
    static const u16 palette[17] = {
        0x0000u, 0x52aau, 0xad55u, 0xffffu, 0x8000u, 0x8010u,
        0x8400u, 0x528au, 0xad55u, 0x001fu, 0x07e0u, 0x07ffu,
        0xf800u, 0xf81fu, 0xffe0u, 0xffffu, 0x0000u
    };
    if (color < sizeof(palette) / sizeof(palette[0])) {
        return palette[color];
    }
    return (u16)color;
}

static void fill_framebuffer(compat_9588_state_t *state, u16 color)
{
    u32 i;
    for (i = 0; i < SCREEN_W * SCREEN_H; ++i) {
        state->framebuffer[i] = color;
    }
}

static void put_guest_pixel(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 color
)
{
    s32 host_x;
    if (!state->framebuffer ||
        x < 0 || y < 0 ||
        x >= GUEST_SCREEN_W || y >= GUEST_SCREEN_H) {
        return;
    }
    host_x = x + GUEST_SCREEN_X;
    state->framebuffer[y * SCREEN_W + host_x] =
        logical_color_to_rgb565(color);
}

static void fill_guest_rect(
    compat_9588_state_t *state,
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    u32 color
)
{
    u32 px;
    u32 py;
    for (py = 0u; py < height; ++py) {
        for (px = 0u; px < width; ++px) {
            put_guest_pixel(
                state,
                (s32)(x + px),
                (s32)(y + py),
                color
            );
        }
    }
}

static void render_listbox_selection(compat_9588_state_t *state)
{
    if (!state->parent_hwnd ||
        !state->guest_control_hwnd ||
        state->listbox_caret >= state->listbox_item_count) {
        return;
    }
    if (state->listbox_drawn_caret < state->listbox_item_count &&
        state->listbox_drawn_caret != state->listbox_caret) {
        fill_guest_rect(
            state,
            LISTBOX_SELECT_X,
            LISTBOX_SELECT_Y +
                state->listbox_drawn_caret * LISTBOX_ITEM_STEP,
            LISTBOX_SELECT_W,
            LISTBOX_SELECT_H,
            3u
        );
    }
    fill_guest_rect(
        state,
        LISTBOX_SELECT_X,
        LISTBOX_SELECT_Y + state->listbox_caret * LISTBOX_ITEM_STEP,
        LISTBOX_SELECT_W,
        LISTBOX_SELECT_H,
        0u
    );
    state->listbox_drawn_caret = state->listbox_caret;
}

static void present_framebuffer(compat_9588_state_t *state)
{
    volatile u16 *output = (volatile u16 *)QEMU_FRAMEBUFFER;
    u32 x;
    u32 y;
    if (!state->framebuffer) {
        return;
    }
    render_listbox_selection(state);
    for (y = 0; y < PHYSICAL_SCREEN_H; ++y) {
        for (x = 0; x < PHYSICAL_SCREEN_W; ++x) {
            u32 output_x = PHYSICAL_SCREEN_W - 1u - x;
            u32 output_y = PHYSICAL_SCREEN_H - 1u - y;
            u16 color = 0x0000u;
            if (x >= PHYSICAL_GUEST_X &&
                x < PHYSICAL_GUEST_X + GUEST_SCREEN_W &&
                y >= PHYSICAL_GUEST_Y &&
                y < PHYSICAL_GUEST_Y + GUEST_SCREEN_H) {
                color = state->framebuffer[
                    (y - PHYSICAL_GUEST_Y) * SCREEN_W +
                    GUEST_SCREEN_X +
                    (x - PHYSICAL_GUEST_X)
                ];
            }
            output[output_y * PHYSICAL_SCREEN_W + output_x] = color;
        }
    }
}

static int draw_packed_2bpp(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    u32 guest_buffer
)
{
    c33_vm_t *vm = state->vm;
    u8 image_header[COMPAT_GUI_IMAGE_HEADER_SIZE];
    u32 pixel_count;
    u32 source_stride;
    u32 source_bytes;
    u32 source_address = guest_buffer;
    u32 index;
    int selector_frame =
        state->parent_hwnd &&
        x == 0 && y == 0 &&
        width == GUEST_SCREEN_W && height == GUEST_SCREEN_H;
    u8 packed;
    u32 loaded_byte = 0xffffffffu;

    if (!width || !height ||
        width > GUEST_SCREEN_W || height > GUEST_SCREEN_H ||
        width > 0xffffffffu / height) {
        return 0;
    }
    pixel_count = width * height;
    source_stride = compat_gui_packed_2bpp_stride(width);
    source_bytes = compat_gui_packed_2bpp_payload_size(width, height);
    /*
     * PutImageArea receives the SDK image object, not a pointer to its first
     * pixel.  The common 16-byte header stores width/height at offsets 8/10
     * and the packed payload length at offset 12.  Treating that header as
     * pixels shifts every following scanline and makes one board look like
     * two broken half-width boards.
     */
    if (c33_vm_read(
            vm, guest_buffer, image_header, sizeof(image_header)
        )) {
        source_address += compat_gui_image_payload_offset(
            image_header, width, height, source_bytes
        );
    }
    for (index = 0; index < pixel_count; ++index) {
        u32 px = index % width;
        u32 py = index / width;
        u32 byte_index = py * source_stride + px / 4u;
        if (byte_index != loaded_byte) {
            if (byte_index >= source_bytes ||
                !c33_vm_read(vm, source_address + byte_index, &packed, 1u)) {
                vm->fault_address = source_address + byte_index;
                return 0;
            }
            loaded_byte = byte_index;
        }
        put_guest_pixel(
            state,
            x + (s32)px,
            y + (s32)py,
            (packed >> compat_gui_packed_2bpp_shift(px)) & 3u
        );
    }
    if (selector_frame) {
        /*
         * A full selector repaint replaces the compatibility-drawn listbox
         * selection, so force that overlay to be drawn again.
         */
        state->listbox_drawn_caret = 0xffffffffu;
    }
    if (!state->instant_paint) {
        present_framebuffer(state);
    }
    return 1;
}

static void draw_line(
    compat_9588_state_t *state,
    s32 x0,
    s32 y0,
    s32 x1,
    s32 y1,
    u32 color
)
{
    s32 dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    s32 sx = x0 < x1 ? 1 : -1;
    s32 dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    s32 sy = y0 < y1 ? 1 : -1;
    s32 error = dx + dy;
    for (;;) {
        s32 twice;
        put_guest_pixel(state, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static u8 glyph_row(char ch, u32 row)
{
    static const u8 digits[10][7] = {
        {14,17,19,21,25,17,14},
        {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},
        {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},
        {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };
    if (row >= 7u) return 0;
    if (ch >= '0' && ch <= '9') return digits[ch - '0'][row];
    if (ch == ':') return row == 2u || row == 5u ? 4u : 0u;
    if (ch == '-') return row == 3u ? 14u : 0u;
    if (ch == '/') {
        static const u8 slash[7] = {1,1,2,4,8,16,16};
        return slash[row];
    }
    return 0;
}

static void draw_guest_text(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 guest_text,
    s32 length
)
{
    u32 index;
    u32 limit = length < 0 ? 32u : (u32)length;
    if (limit > 32u) limit = 32u;
    for (index = 0; index < limit; ++index) {
        u8 ch;
        u32 row;
        if (!c33_vm_read(state->vm, guest_text + index, &ch, 1u) || !ch) {
            break;
        }
        for (row = 0; row < 7u; ++row) {
            u8 bits = glyph_row((char)ch, row);
            u32 column;
            for (column = 0; column < 5u; ++column) {
                if (bits & (1u << (4u - column))) {
                    put_guest_pixel(
                        state,
                        x + (s32)(index * 12u + column * 2u),
                        y + (s32)(row * 2u),
                        state->text_color
                    );
                    put_guest_pixel(
                        state,
                        x + (s32)(index * 12u + column * 2u + 1u),
                        y + (s32)(row * 2u),
                        state->text_color
                    );
                    put_guest_pixel(
                        state,
                        x + (s32)(index * 12u + column * 2u),
                        y + (s32)(row * 2u + 1u),
                        state->text_color
                    );
                    put_guest_pixel(
                        state,
                        x + (s32)(index * 12u + column * 2u + 1u),
                        y + (s32)(row * 2u + 1u),
                        state->text_color
                    );
                }
            }
        }
    }
    if (!state->instant_paint) {
        present_framebuffer(state);
    }
}

static int queue_message(
    compat_9588_state_t *state,
    u32 hwnd,
    u32 message,
    u32 wparam,
    u32 lparam
)
{
    guest_message_t *event;
    if (!state || state->event_count >= EVENT_QUEUE_SIZE) {
        return 0;
    }
    event = &state->events[state->event_write];
    event->hwnd = hwnd;
    event->message = message;
    event->wparam = wparam;
    event->lparam = lparam;
    event->time = ++state->message_time;
    event->point_x = 0;
    event->point_y = 0;
    state->event_write = (state->event_write + 1u) % EVENT_QUEUE_SIZE;
    state->event_count++;
    return 1;
}

static int queue_pointer_message(
    compat_9588_state_t *state,
    u32 message,
    u32 x,
    u32 y,
    u32 buttons
)
{
    guest_message_t *event;
    if (!state || state->event_count >= EVENT_QUEUE_SIZE) {
        return 0;
    }
    event = &state->events[state->event_write];
    event->hwnd = state->guest_hwnd;
    event->message = message;
    event->wparam = buttons;
    event->lparam = (x & 0xffffu) | ((y & 0xffffu) << 16);
    event->time = ++state->message_time;
    event->point_x = x;
    event->point_y = y;
    state->event_write = (state->event_write + 1u) % EVENT_QUEUE_SIZE;
    state->event_count++;
    return 1;
}

static u32 translate_native_key_value(u32 value);

static int pop_message(compat_9588_state_t *state, guest_message_t *event)
{
    if (!state || !state->event_count) {
        return 0;
    }
    *event = state->events[state->event_read];
    state->event_read = (state->event_read + 1u) % EVENT_QUEUE_SIZE;
    state->event_count--;
    return 1;
}

static u32 touch_raw_to_panel_x(u32 raw_x)
{
    /*
     * Invert the four-point C200/9588 calibration used by the frontend.
     * X raw values decrease from about 0xe74 on the left to 0x172 on the
     * right.  Extending that line to the physical 0..239 panel avoids a
     * dead strip outside the original 10..230 calibration targets.
     */
    if (raw_x >= 3841u) return 0u;
    if (raw_x <= 238u) return PHYSICAL_SCREEN_W - 1u;
    return (3841u - raw_x) * (PHYSICAL_SCREEN_W - 1u) / 3603u;
}

static u32 touch_raw_to_panel_y(u32 raw_y)
{
    if (raw_y >= 3660u) return 0u;
    if (raw_y <= 141u) return PHYSICAL_SCREEN_H - 1u;
    return (3660u - raw_y) * (PHYSICAL_SCREEN_H - 1u) / 3519u;
}

static void service_touch_input(compat_9588_state_t *state)
{
    volatile u32 *trace = (volatile u32 *)QEMU_TOUCH_TRACE;
    u32 down;
    u32 panel_x;
    u32 panel_y;
    u32 guest_x;
    u32 guest_y;

    if (trace[0] != QEMU_TOUCH_MAGIC) {
        state->touch_ready = 0;
        return;
    }
    down = trace[4] != 0u;
    panel_x = touch_raw_to_panel_x(trace[5] & 0xfffu);
    panel_y = touch_raw_to_panel_y(trace[6] & 0xfffu);

    if (!state->touch_ready) {
        state->touch_ready = 1;
        state->touch_down = down;
        state->touch_captured = 0;
        state->touch_x = panel_x;
        state->touch_y = panel_y;
        return;
    }

    /*
     * The 9288S game surface is centered inside the 9588 panel. Ignore the
     * black border while retaining the last in-surface point for pen-up.
     */
    if (panel_x < PHYSICAL_GUEST_X ||
        panel_x >= PHYSICAL_GUEST_X + GUEST_SCREEN_W ||
        panel_y < PHYSICAL_GUEST_Y ||
        panel_y >= PHYSICAL_GUEST_Y + GUEST_SCREEN_H) {
        if (!down && state->touch_captured) {
            guest_x = state->touch_x - PHYSICAL_GUEST_X;
            guest_y = state->touch_y - PHYSICAL_GUEST_Y;
            queue_pointer_message(
                state, COMPAT_MSG_LBUTTONUP, guest_x, guest_y, 0u
            );
            state->touch_captured = 0;
        }
        state->touch_down = down;
        return;
    }

    guest_x = panel_x - PHYSICAL_GUEST_X;
    guest_y = panel_y - PHYSICAL_GUEST_Y;
    if (down && !state->touch_captured) {
        if (state->parent_hwnd &&
            guest_x >= 16u && guest_x < 142u &&
            guest_y >= LISTBOX_SELECT_Y &&
            guest_y <
                LISTBOX_SELECT_Y +
                LISTBOX_ITEM_COUNT * LISTBOX_ITEM_STEP) {
            u32 item =
                (guest_y - LISTBOX_SELECT_Y) / LISTBOX_ITEM_STEP;
            if (item < state->listbox_item_count) {
                state->listbox_caret = item;
            }
        }
        queue_pointer_message(
            state, COMPAT_MSG_LBUTTONDOWN, guest_x, guest_y, 4u
        );
        state->touch_captured = 1;
        g_diagnostic[14] += 1u;
    } else if (down &&
               (panel_x != state->touch_x || panel_y != state->touch_y)) {
        queue_pointer_message(
            state, COMPAT_MSG_MOUSEMOVE, guest_x, guest_y, 4u
        );
    } else if (!down && state->touch_captured) {
        queue_pointer_message(
            state, COMPAT_MSG_LBUTTONUP, guest_x, guest_y, 0u
        );
        if (state->parent_hwnd && guest_y >= 190u) {
            if (guest_x < 80u) {
                queue_message(
                    state,
                    state->guest_hwnd,
                    COMPAT_MSG_KEYDOWN,
                    COMPAT_SCANCODE_ENTER,
                    0u
                );
            } else {
                queue_message(
                    state,
                    state->guest_hwnd,
                    COMPAT_MSG_KEYDOWN,
                    COMPAT_SCANCODE_ESCAPE,
                    0u
                );
            }
        } else if (state->parent_hwnd &&
                   guest_x >= 132u && guest_y < 24u) {
            queue_message(
                state,
                state->guest_hwnd,
                COMPAT_MSG_KEYDOWN,
                COMPAT_SCANCODE_ESCAPE,
                0u
            );
        }
        state->touch_captured = 0;
    }
    state->touch_down = down;
    state->touch_x = panel_x;
    state->touch_y = panel_y;
}

static void service_hardware_input(compat_9588_state_t *state)
{
    volatile u32 *queue = (volatile u32 *)QEMU_EVENT_QUEUE;
    u32 write_index;
    u32 remaining;

    if (queue[0] != QEMU_EVENT_MAGIC) {
        state->hardware_events_ready = 0;
        return;
    }
    write_index = queue[2];
    if (write_index >= QEMU_EVENT_SLOTS) {
        state->hardware_events_ready = 0;
        return;
    }
    if (!state->hardware_events_ready) {
        /*
         * Discard the launcher navigation and the key that opened this BDA.
         * Only events arriving after the guest is ready belong to the game.
         */
        state->hardware_event_cursor = write_index;
        state->hardware_events_ready = 1;
        return;
    }

    remaining = QEMU_EVENT_SLOTS;
    while (state->hardware_event_cursor != write_index && remaining--) {
        u32 slot = state->hardware_event_cursor;
        volatile u32 *event =
            queue + 4u + slot * QEMU_EVENT_WORDS;
        u32 kind = event[1];
        u32 key_code = event[2];
        u32 down = event[3];

        state->hardware_event_cursor =
            (state->hardware_event_cursor + 1u) % QEMU_EVENT_SLOTS;
        if (event[0] == 3u && kind == QEMU_EVENT_KIND_KEY && down) {
            u32 scancode = translate_native_key_value(key_code);
            if (scancode) {
                if (state->parent_hwnd && state->listbox_item_count) {
                    if (scancode == COMPAT_SCANCODE_UP &&
                        state->listbox_caret > 0u) {
                        state->listbox_caret--;
                    } else if (scancode == COMPAT_SCANCODE_DOWN &&
                               state->listbox_caret + 1u <
                                   state->listbox_item_count) {
                        state->listbox_caret++;
                    }
                }
                queue_message(
                    state,
                    state->guest_hwnd,
                    COMPAT_MSG_KEYDOWN,
                    scancode,
                    0u
                );
                g_diagnostic[15] += 1u;
            }
        }
    }
    service_touch_input(state);
}

static int write_guest_message(
    c33_vm_t *vm,
    u32 address,
    const guest_message_t *event
)
{
    return guest_write_u32(vm, address + 0u, event->hwnd) &&
           guest_write_u32(vm, address + 4u, event->message) &&
           guest_write_u32(vm, address + 8u, event->wparam) &&
           guest_write_u32(vm, address + 12u, event->lparam) &&
           guest_write_u32(vm, address + 16u, event->time) &&
           guest_write_u32(vm, address + 20u, event->point_x) &&
           guest_write_u32(vm, address + 24u, event->point_y);
}

static int read_guest_message(
    c33_vm_t *vm,
    u32 address,
    guest_message_t *event
)
{
    return guest_read_u32(vm, address + 0u, &event->hwnd) &&
           guest_read_u32(vm, address + 4u, &event->message) &&
           guest_read_u32(vm, address + 8u, &event->wparam) &&
           guest_read_u32(vm, address + 12u, &event->lparam) &&
           guest_read_u32(vm, address + 16u, &event->time) &&
           guest_read_u32(vm, address + 20u, &event->point_x) &&
           guest_read_u32(vm, address + 24u, &event->point_y);
}

static u32 translate_native_key_value(u32 value)
{
    u32 code = value & 0xffffu;
    switch (code) {
    case 4: return COMPAT_SCANCODE_UP;
    case 5: return COMPAT_SCANCODE_DOWN;
    case 6: return COMPAT_SCANCODE_LEFT;
    case 7: return COMPAT_SCANCODE_RIGHT;
    case 9: return COMPAT_SCANCODE_ESCAPE;
    case 10: return COMPAT_SCANCODE_ENTER;
    case 13: return COMPAT_SCANCODE_ENTER;
    case 27: return COMPAT_SCANCODE_ESCAPE;
    case COMPAT_SCANCODE_ENTER:
    case COMPAT_SCANCODE_UP:
    case COMPAT_SCANCODE_LEFT:
    case COMPAT_SCANCODE_RIGHT:
    case COMPAT_SCANCODE_DOWN:
        return code;
    default:
        break;
    }
    return 0;
}

static c33_vm_status_t call_guest_window_proc(
    compat_9588_state_t *state,
    u32 hwnd,
    u32 message,
    u32 wparam,
    u32 lparam
)
{
    if (!state->guest_window_proc) {
        state->vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    return c33_vm_call(
        state->vm,
        state->guest_window_proc,
        hwnd,
        message,
        wparam,
        lparam,
        CALLBACK_BUDGET
    );
}

static c33_vm_status_t dispatch_listbox_message(
    compat_9588_state_t *state,
    u32 message,
    u32 wparam,
    u32 lparam
)
{
    c33_vm_t *vm = state->vm;
    u32 index;
    u32 length;

    switch (message) {
    case COMPAT_LB_ADDSTRING:
        if (state->listbox_item_count >= LISTBOX_ITEM_COUNT ||
            !guest_read_c_string(
                vm,
                lparam,
                state->listbox_items[state->listbox_item_count],
                LISTBOX_ITEM_SIZE
            )) {
            vm->regs[4] = 0xfffffffdu;
            return C33_VM_OK;
        }
        vm->regs[4] = state->listbox_item_count++;
        return C33_VM_OK;
    case COMPAT_LB_SETCURSEL:
    case COMPAT_LB_SETCARETINDEX:
        index = wparam;
        if (index >= state->listbox_item_count) {
            vm->regs[4] = 0xfffffffdu;
        } else {
            state->listbox_caret = index;
            vm->regs[4] = 0u;
        }
        return C33_VM_OK;
    case COMPAT_LB_GETCURSEL:
    case COMPAT_LB_GETCARETINDEX:
        vm->regs[4] = state->listbox_caret;
        return C33_VM_OK;
    case COMPAT_LB_GETTEXTLEN:
        index = wparam;
        if (index >= state->listbox_item_count) {
            vm->regs[4] = 0xfffffffdu;
            return C33_VM_OK;
        }
        length = 0u;
        while (length < LISTBOX_ITEM_SIZE &&
               state->listbox_items[index][length]) {
            ++length;
        }
        vm->regs[4] = length;
        return C33_VM_OK;
    case COMPAT_LB_GETTEXT:
        index = wparam;
        if (index >= state->listbox_item_count) {
            vm->regs[4] = 0xfffffffdu;
            return C33_VM_OK;
        }
        length = 0u;
        while (length < LISTBOX_ITEM_SIZE &&
               state->listbox_items[index][length]) {
            ++length;
        }
        if (!c33_vm_write(
                vm,
                lparam,
                state->listbox_items[index],
                length + 1u
            )) {
            vm->fault_address = lparam;
            return C33_VM_FAULT;
        }
        vm->regs[4] = length;
        return C33_VM_OK;
    case COMPAT_LB_GETCOUNT:
        vm->regs[4] = state->listbox_item_count;
        return C33_VM_OK;
    case COMPAT_LB_SETITEMHEIGHT:
    case 0x0131u: /* MSG_SETFONT */
        vm->regs[4] = 0u;
        return C33_VM_OK;
    default:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    }
}

static c33_vm_status_t dispatch_9588_fs(
    compat_api_t *api,
    u32 slot,
    compat_9588_state_t *state
)
{
    c33_vm_t *vm = api->vm;

    switch (slot) {
    case COMPAT_FS_OPEN:
        {
            char guest_path[192];
            char mode[8];
            const char *native_path;
            if (!guest_read_c_string(
                    vm, vm->regs[6], guest_path, sizeof(guest_path)
                ) ||
                !guest_read_c_string(
                    vm, vm->regs[7], mode, sizeof(mode)
                )) {
                return C33_VM_FAULT;
            }
            native_path = translate_save_path(guest_path);
            vm->regs[4] = (u32)bda_fs_fopen_raw(native_path, mode);
            return C33_VM_OK;
        }
    case COMPAT_FS_CLOSE:
        if (!bda_fs_file_is_valid((int)vm->regs[6])) {
            vm->regs[4] = 0xffffffffu;
        } else {
            vm->regs[4] = (u32)bda_fs_close_raw((int)vm->regs[6]);
        }
        return C33_VM_OK;
    case COMPAT_FS_READ:
        {
            u32 size = vm->regs[7];
            u32 count = vm->regs[8];
            u32 byte_count;
            int items_read;
            u8 *buffer;
            if (!size || !count ||
                size > MAX_FILE_IO_SIZE ||
                count > MAX_FILE_IO_SIZE / size ||
                !bda_fs_file_is_valid((int)vm->regs[9])) {
                vm->regs[4] = 0u;
                return C33_VM_OK;
            }
            byte_count = size * count;
            buffer = (u8 *)bda_alloc(byte_count);
            if (allocation_failed(buffer)) {
                vm->regs[4] = 0u;
                return C33_VM_OK;
            }
            items_read = bda_fs_fread_raw(
                buffer, size, count, (int)vm->regs[9]
            );
            if (items_read > 0 &&
                !c33_vm_write(
                    vm,
                    vm->regs[6],
                    buffer,
                    (u32)items_read * size
                )) {
                bda_free(buffer);
                vm->fault_address = vm->regs[6];
                return C33_VM_FAULT;
            }
            bda_free(buffer);
            vm->regs[4] = items_read > 0 ? (u32)items_read : 0u;
            return C33_VM_OK;
        }
    case COMPAT_FS_WRITE:
        {
            u32 size = vm->regs[7];
            u32 count = vm->regs[8];
            u32 byte_count;
            int items_written;
            u8 *buffer;
            if (!size || !count ||
                size > MAX_FILE_IO_SIZE ||
                count > MAX_FILE_IO_SIZE / size ||
                !bda_fs_file_is_valid((int)vm->regs[9])) {
                vm->regs[4] = 0u;
                return C33_VM_OK;
            }
            byte_count = size * count;
            buffer = (u8 *)bda_alloc(byte_count);
            if (allocation_failed(buffer)) {
                vm->regs[4] = 0u;
                return C33_VM_OK;
            }
            if (!c33_vm_read(vm, vm->regs[6], buffer, byte_count)) {
                bda_free(buffer);
                vm->fault_address = vm->regs[6];
                return C33_VM_FAULT;
            }
            items_written = bda_fs_fwrite_raw(
                buffer, size, count, (int)vm->regs[9]
            );
            bda_free(buffer);
            vm->regs[4] =
                items_written > 0 ? (u32)items_written : 0u;
            return C33_VM_OK;
        }
    case COMPAT_FS_SEEK:
        vm->regs[4] = (u32)bda_fs_seek_raw(
            (int)vm->regs[6], (s32)vm->regs[7], (int)vm->regs[8]
        );
        return C33_VM_OK;
    case COMPAT_FS_TELL:
        vm->regs[4] = (u32)bda_fs_tell_raw((int)vm->regs[6]);
        return C33_VM_OK;
    case COMPAT_FS_EOF:
        /*
         * The public 9588 table has no standalone feof entry. 海盗船 uses
         * fixed-size records and checks fread's item count, so "not EOF yet"
         * is the compatible result when this legacy slot is queried.
         */
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_FS_ERROR:
        vm->regs[4] = (u32)bda_fs_error((int)vm->regs[6]);
        return C33_VM_OK;
    case COMPAT_FS_FIND_FIRST:
        {
            char guest_path[192];
            const char *native_path;
            int result;
            if (!guest_read_c_string(
                    vm, vm->regs[6], guest_path, sizeof(guest_path)
                )) {
                return C33_VM_FAULT;
            }
            if (state->native_find_open) {
                (void)bda_fs_findclose(&state->native_find_data);
                state->native_find_open = 0;
            }
            native_path = translate_save_path(guest_path);
            bda_fs_find_data_init(&state->native_find_data);
            result = bda_fs_findfirst(
                native_path,
                vm->regs[7],
                &state->native_find_data
            );
            if (result != -1) {
                state->native_find_open = 1;
                state->guest_find_data = vm->regs[8];
                if (!c33_vm_write(
                        vm,
                        vm->regs[8],
                        &state->native_find_data,
                        sizeof(state->native_find_data)
                    )) {
                    (void)bda_fs_findclose(&state->native_find_data);
                    state->native_find_open = 0;
                    return C33_VM_FAULT;
                }
            }
            vm->regs[4] = (u32)result;
            return C33_VM_OK;
        }
    case COMPAT_FS_FIND_NEXT:
        if (!state->native_find_open ||
            state->guest_find_data != vm->regs[6]) {
            vm->regs[4] = 0xffffffffu;
        } else {
            int result = bda_fs_findnext(&state->native_find_data);
            if (result != -1 &&
                !c33_vm_write(
                    vm,
                    vm->regs[6],
                    &state->native_find_data,
                    sizeof(state->native_find_data)
                )) {
                return C33_VM_FAULT;
            }
            vm->regs[4] = (u32)result;
        }
        return C33_VM_OK;
    case COMPAT_FS_FIND_CLOSE:
        if (state->native_find_open &&
            state->guest_find_data == vm->regs[6]) {
            vm->regs[4] = (u32)bda_fs_findclose(
                &state->native_find_data
            );
            state->native_find_open = 0;
            state->guest_find_data = 0u;
        } else {
            /* The 9288S game closes even after a failed exact-file lookup. */
            vm->regs[4] = 0u;
        }
        return C33_VM_OK;
    case COMPAT_FS_DISK_INFO:
        /*
         * Four 32-bit fields: total clusters, free clusters, sectors per
         * cluster and bytes per sector. Advertise ample persistent space so
         * the game's original 128 KiB guard permits saving.
         */
        if (!guest_write_u32(vm, vm->regs[7] + 0u, 4096u) ||
            !guest_write_u32(vm, vm->regs[7] + 4u, 2048u) ||
            !guest_write_u32(vm, vm->regs[7] + 8u, 8u) ||
            !guest_write_u32(vm, vm->regs[7] + 12u, 512u)) {
            return C33_VM_FAULT;
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_FS_STAT:
        {
            char guest_path[192];
            if (!guest_read_c_string(
                    vm, vm->regs[6], guest_path, sizeof(guest_path)
                )) {
                return C33_VM_FAULT;
            }
            /*
             * The two legacy directories are virtual in this adapter. Exact
             * save-file existence is handled by FIND_FIRST above.
             */
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    default:
        return C33_VM_UNSUPPORTED;
    }
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
    compat_9588_state_t *state = (compat_9588_state_t *)opaque;
    c33_vm_t *vm = api->vm;

    if (!state) {
        return C33_VM_UNSUPPORTED;
    }
    if (group == COMPAT_API_FS) {
        return dispatch_9588_fs(api, slot, state);
    }
    if (group != COMPAT_API_GUI) {
        return C33_VM_UNSUPPORTED;
    }

    switch (slot) {
    case COMPAT_GUI_GET_MESSAGE:
        {
            guest_message_t event;
            if (!pop_message(state, &event)) {
                return C33_VM_YIELD;
            }
            if (!write_guest_message(vm, vm->regs[6], &event)) {
                vm->fault_address = vm->regs[6];
                return C33_VM_FAULT;
            }
            vm->regs[4] = event.message == 0x0140u ? 0u : 1u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_HAVE_PENDING_MESSAGE:
        vm->regs[4] = state->event_count != 0u;
        return C33_VM_OK;
    case COMPAT_GUI_POST_MESSAGE:
    case COMPAT_GUI_SEND_NOTIFY_MESSAGE:
        vm->regs[4] = queue_message(
            state, vm->regs[6], vm->regs[7], vm->regs[8], vm->regs[9]
        );
        return C33_VM_OK;
    case COMPAT_GUI_SEND_MESSAGE:
        if (vm->regs[6] == state->guest_control_hwnd &&
            state->guest_control_hwnd) {
            return dispatch_listbox_message(
                state, vm->regs[7], vm->regs[8], vm->regs[9]
            );
        }
        return call_guest_window_proc(
            state, vm->regs[6], vm->regs[7], vm->regs[8], vm->regs[9]
        );
    case COMPAT_GUI_POST_QUIT_MESSAGE:
        if (state->parent_hwnd) {
            /*
             * A save/load selector owns a nested 9288S main window. Its
             * MSG_DESTROY posts a private quit for that selector's loop.
             * Queue the message so the suspended modal loop can unwind, but
             * do not mark the outer game as quitting.
             */
            vm->regs[4] = queue_message(
                state, vm->regs[6], 0x0140u, 0u, 0u
            );
            return C33_VM_OK;
        }
        vm->regs[4] = queue_message(
            state, vm->regs[6], 0x0140u, 0u, 0u
        );
        state->quit = 1;
        return C33_VM_OK;
    case COMPAT_GUI_TRANSLATE_MESSAGE:
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_DISPATCH_MESSAGE:
        {
            guest_message_t event;
            if (!read_guest_message(vm, vm->regs[6], &event)) {
                vm->fault_address = vm->regs[6];
                return C33_VM_FAULT;
            }
            return call_guest_window_proc(
                state,
                event.hwnd,
                event.message,
                event.wparam,
                event.lparam
            );
        }
    case COMPAT_GUI_SET_INSTANT_PAINT:
        g_diagnostic[8] += 1u;
        g_diagnostic[9] = vm->regs[6];
        state->instant_paint = vm->regs[6] != 0u;
        if (!state->instant_paint) {
            present_framebuffer(state);
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_CREATE_MAIN_WINDOW:
        {
            c33_vm_status_t status;
            u32 callback;
            if (!guest_read_u32(
                    vm,
                    vm->regs[6] + COMPAT_MAIN_WIN_CREATE_PROC_OFFSET,
                    &callback
                )) {
                vm->fault_address =
                    vm->regs[6] + COMPAT_MAIN_WIN_CREATE_PROC_OFFSET;
                return C33_VM_FAULT;
            }
            if (state->guest_hwnd) {
                if (state->parent_hwnd) {
                    return C33_VM_UNSUPPORTED;
                }
                state->parent_window_proc = state->guest_window_proc;
                state->parent_hwnd = state->guest_hwnd;
                state->parent_pen_color = state->pen_color;
                state->parent_brush_color = state->brush_color;
                state->parent_background_color =
                    state->background_color;
                state->parent_current_x = state->current_x;
                state->parent_current_y = state->current_y;
                state->parent_instant_paint = state->instant_paint;
            } else {
                state->guest_hwnd = 1u;
                state->next_hwnd = 2u;
            }
            state->guest_window_proc = callback;
            status = call_guest_window_proc(
                state, state->guest_hwnd, COMPAT_MSG_CREATE, 0u, 0u
            );
            if (status != C33_VM_OK) return status;
            status = call_guest_window_proc(
                state, state->guest_hwnd, COMPAT_MSG_PAINT, 0u, 0u
            );
            if (status != C33_VM_OK) return status;
            vm->regs[4] = state->guest_hwnd;
            return C33_VM_OK;
        }
    case COMPAT_GUI_DESTROY_MAIN_WINDOW:
        {
            int nested =
                state->parent_hwnd != 0u &&
                vm->regs[6] == state->guest_hwnd;
            c33_vm_status_t status = call_guest_window_proc(
                state,
                vm->regs[6],
                COMPAT_MSG_DESTROY,
                0u,
                0u
            );
            if (status != C33_VM_OK) {
                return status;
            }
            if (nested) {
                u32 parent_proc = state->parent_window_proc;
                u32 parent = state->parent_hwnd;
                state->guest_window_proc = state->parent_window_proc;
                state->guest_hwnd = state->parent_hwnd;
                state->parent_window_proc = 0u;
                state->parent_hwnd = 0u;
                state->pen_color = state->parent_pen_color;
                state->brush_color = state->parent_brush_color;
                state->background_color =
                    state->parent_background_color;
                /*
                 * The board relies on its DC's default black text. The
                 * selector changes its own DC to white before creating the
                 * nested window, so a single global color snapshot is
                 * already too late to represent the parent's independent
                 * DC state.
                 */
                state->text_color = 16u;
                state->current_x = state->parent_current_x;
                state->current_y = state->parent_current_y;
                state->instant_paint = state->parent_instant_paint;
                state->guest_control_hwnd = 0u;
                state->listbox_item_count = 0u;
                state->listbox_caret = 0u;
                state->listbox_drawn_caret = 0xffffffffu;
                status = c33_vm_call(
                    state->vm,
                    parent_proc,
                    parent,
                    COMPAT_MSG_PAINT,
                    0u,
                    0u,
                    CALLBACK_BUDGET
                );
                if (status != C33_VM_OK) {
                    return status;
                }
                /*
                 * 海盗船's WM_PAINT draws the board background; its status
                 * values are refreshed by the timer callback. The 9288S
                 * window manager follows a restored modal window with that
                 * refresh, so reproduce it before presenting the first
                 * parent frame.
                 */
                status = c33_vm_call(
                    state->vm,
                    parent_proc,
                    parent,
                    COMPAT_MSG_TIMER,
                    1u,
                    0u,
                    CALLBACK_BUDGET
                );
                if (status != C33_VM_OK) {
                    return status;
                }
                present_framebuffer(state);
            } else {
                state->quit = 1;
            }
            vm->regs[4] = 1u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_DEFAULT_MAIN_WIN_PROC:
    case COMPAT_GUI_MAIN_WINDOW_CLEANUP:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_SET_TIMER:
        state->timer_hwnd = vm->regs[6];
        state->timer_id = vm->regs[7];
        /*
         * MiniGUI's legacy SetTimer "speed" is counted in 10 ms system
         * ticks, not milliseconds.  海盗船 passes 20, which is 200 ms.
         */
        state->timer_interval =
            compat_gui_timer_interval_ms(vm->regs[8]);
        state->timer_elapsed = 0u;
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_SHOW_WINDOW:
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_CREATE_CONTROL:
        state->guest_control_hwnd = GUEST_CONTROL_HWND;
        state->listbox_item_count = 0u;
        state->listbox_caret = 0u;
        state->listbox_drawn_caret = 0xffffffffu;
        vm->regs[4] = state->guest_control_hwnd;
        return C33_VM_OK;
    case COMPAT_GUI_DESTROY_CONTROL:
        if (vm->regs[6] == state->guest_control_hwnd) {
            state->guest_control_hwnd = 0u;
            state->listbox_item_count = 0u;
            state->listbox_caret = 0u;
            state->listbox_drawn_caret = 0xffffffffu;
        }
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_KILL_TIMER:
        if (state->timer_hwnd == vm->regs[6] &&
            state->timer_id == vm->regs[7]) {
            state->timer_id = 0u;
        }
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_MESSAGE_BOX:
        {
            char text[192];
            char caption[64];
            if (!guest_read_c_string(
                    vm, vm->regs[7], text, sizeof(text)
                ) ||
                !guest_read_c_string(
                    vm, vm->regs[8], caption, sizeof(caption)
                )) {
                vm->fault_address =
                    vm->regs[7] ? vm->regs[7] : vm->regs[8];
                return C33_VM_FAULT;
            }
            /*
             * The original save-success box is informational. A native 9588
             * modal drawn over the direct compatibility framebuffer leaves
             * both surfaces visible at once, so acknowledge this one message
             * immediately and let the original selector close and repaint
             * its parent board.
             */
            if (state->parent_hwnd &&
                (byte_string_contains(
                     text, "\xb4\xe6\xb5\xb5\xb3\xc9\xb9\xa6"
                 ) ||
                 byte_string_contains(
                     text, "\xb4\xe6\xc5\xcc\xb3\xc9\xb9\xa6"
                 ))) {
                vm->regs[4] = 1u;
                return C33_VM_OK;
            }
            vm->regs[4] = (u32)bda_msgbox_ex(
                0,
                caption[0] ? caption : "9288S",
                text,
                vm->regs[9]
            );
            return C33_VM_OK;
        }
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
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_GET_BK_COLOR:
        vm->regs[4] = state->background_color;
        return C33_VM_OK;
    case COMPAT_GUI_GET_TEXT_COLOR:
        vm->regs[4] = state->text_color;
        return C33_VM_OK;
    case COMPAT_GUI_GET_PEN_COLOR:
        vm->regs[4] = state->pen_color;
        return C33_VM_OK;
    case COMPAT_GUI_GET_BRUSH_COLOR:
        vm->regs[4] = state->brush_color;
        return C33_VM_OK;
    case COMPAT_GUI_GET_BK_MODE:
    case COMPAT_GUI_GET_PEN_TYPE:
    case COMPAT_GUI_GET_BRUSH_TYPE:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_SET_BK_COLOR:
        vm->regs[4] = state->background_color;
        state->background_color = vm->regs[7];
        return C33_VM_OK;
    case COMPAT_GUI_SET_TEXT_COLOR:
        vm->regs[4] = state->text_color;
        state->text_color = vm->regs[7];
        return C33_VM_OK;
    case COMPAT_GUI_SET_PEN_COLOR:
        vm->regs[4] = state->pen_color;
        state->pen_color = vm->regs[7];
        return C33_VM_OK;
    case COMPAT_GUI_SET_BRUSH_COLOR:
        vm->regs[4] = state->brush_color;
        state->brush_color = vm->regs[7];
        return C33_VM_OK;
    case COMPAT_GUI_SET_BK_MODE:
    case COMPAT_GUI_SET_PEN_TYPE:
    case COMPAT_GUI_SET_BRUSH_TYPE:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_SET_PIXEL:
        put_guest_pixel(
            state, (s32)vm->regs[7], (s32)vm->regs[8], vm->regs[9]
        );
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_SET_PIXEL_RGB:
        {
            u32 green;
            u32 blue;
            if (!guest_read_u32(vm, vm->sp + 4u, &green) ||
                !guest_read_u32(vm, vm->sp + 8u, &blue)) {
                return C33_VM_FAULT;
            }
            put_guest_pixel(
                state,
                (s32)vm->regs[7],
                (s32)vm->regs[8],
                ((vm->regs[9] & 0xf8u) << 8) |
                ((green & 0xfcu) << 3) |
                ((blue & 0xf8u) >> 3)
            );
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_RGB_TO_PIXEL:
        {
            u32 blue;
            if (!guest_read_u32(vm, vm->sp + 4u, &blue)) {
                return C33_VM_FAULT;
            }
            vm->regs[4] =
                ((vm->regs[7] & 0xf8u) << 8) |
                ((vm->regs[8] & 0xfcu) << 3) |
                ((blue & 0xf8u) >> 3);
            return C33_VM_OK;
        }
    case COMPAT_GUI_MOVE_TO:
        state->current_x = (s32)vm->regs[7];
        state->current_y = (s32)vm->regs[8];
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_LINE_TO:
        draw_line(
            state,
            state->current_x,
            state->current_y,
            (s32)vm->regs[7],
            (s32)vm->regs[8],
            state->pen_color
        );
        state->current_x = (s32)vm->regs[7];
        state->current_y = (s32)vm->regs[8];
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_RECTANGLE:
        {
            u32 y1;
            s32 x0 = (s32)vm->regs[7];
            s32 y0 = (s32)vm->regs[8];
            s32 x1 = (s32)vm->regs[9];
            s32 y;
            s32 x;
            if (!guest_read_u32(vm, vm->sp + 4u, &y1)) {
                return C33_VM_FAULT;
            }
            for (y = y0; y <= (s32)y1; ++y) {
                for (x = x0; x <= x1; ++x) {
                    put_guest_pixel(state, x, y, state->brush_color);
                }
            }
            draw_line(state, x0, y0, x1, y0, state->pen_color);
            draw_line(state, x1, y0, x1, (s32)y1, state->pen_color);
            draw_line(state, x1, (s32)y1, x0, (s32)y1, state->pen_color);
            draw_line(state, x0, (s32)y1, x0, y0, state->pen_color);
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_SAVE_SCREEN_BOX:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_SET_RECT:
        {
            u32 bottom;
            if (!guest_read_u32(vm, vm->sp + 4u, &bottom) ||
                !guest_write_u32(vm, vm->regs[6] + 0u, vm->regs[7]) ||
                !guest_write_u32(vm, vm->regs[6] + 4u, vm->regs[8]) ||
                !guest_write_u32(vm, vm->regs[6] + 8u, vm->regs[9]) ||
                !guest_write_u32(vm, vm->regs[6] + 12u, bottom)) {
                return C33_VM_FAULT;
            }
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_SET_RECT_EMPTY:
        if (!guest_write_u32(vm, vm->regs[6] + 0u, 0u) ||
            !guest_write_u32(vm, vm->regs[6] + 4u, 0u) ||
            !guest_write_u32(vm, vm->regs[6] + 8u, 0u) ||
            !guest_write_u32(vm, vm->regs[6] + 12u, 0u)) {
            return C33_VM_FAULT;
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_POINT_IN_RECT:
        {
            u32 left, top, right, bottom;
            if (!guest_read_u32(vm, vm->regs[6] + 0u, &left) ||
                !guest_read_u32(vm, vm->regs[6] + 4u, &top) ||
                !guest_read_u32(vm, vm->regs[6] + 8u, &right) ||
                !guest_read_u32(vm, vm->regs[6] + 12u, &bottom)) {
                return C33_VM_FAULT;
            }
            vm->regs[4] =
                (s32)vm->regs[7] >= (s32)left &&
                (s32)vm->regs[7] <= (s32)right &&
                (s32)vm->regs[8] >= (s32)top &&
                (s32)vm->regs[8] <= (s32)bottom;
            return C33_VM_OK;
        }
    case COMPAT_GUI_CREATE_LOG_FONT:
        /* The compatibility surface uses its own fixed bitmap font. */
        vm->regs[4] = 3u;
        return C33_VM_OK;
    case COMPAT_GUI_DESTROY_LOG_FONT:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_TEXT_OUT_LEN:
        {
            u32 length;
            if (!guest_read_u32(vm, vm->sp + 4u, &length)) {
                return C33_VM_FAULT;
            }
            draw_guest_text(
                state,
                (s32)vm->regs[7],
                (s32)vm->regs[8],
                vm->regs[9],
                (s32)length
            );
            vm->regs[4] = 1u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_PUT_IMAGE_AREA:
        {
            u32 height;
            u32 buffer;
            g_diagnostic[10] += 1u;
            g_diagnostic[11] = vm->regs[9];
            if (!guest_read_u32(vm, vm->sp + 4u, &height) ||
                !guest_read_u32(vm, vm->sp + 8u, &buffer) ||
                !draw_packed_2bpp(
                    state,
                    (s32)vm->regs[7],
                    (s32)vm->regs[8],
                    vm->regs[9],
                    height,
                    buffer
                )) {
                return C33_VM_FAULT;
            }
            g_diagnostic[12] = height;
            g_diagnostic[13] = buffer;
            g_diagnostic[14] =
                state->framebuffer[
                    (GUEST_SCREEN_H / 2) * SCREEN_W +
                    GUEST_SCREEN_X +
                    (GUEST_SCREEN_W / 2)
                ];
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_CLEAR_SCREEN:
        fill_framebuffer(state, 0xffffu);
        present_framebuffer(state);
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_GET_CURRENT_DATETIME:
        {
            static const u8 time_info[8] = {
                12, 0, 0, 0, 0, 0, 0, 0
            };
            static const u8 date_info[6] = {
                0xea, 0x07, 7, 23, 4, 0
            };
            if (!c33_vm_write(
                    vm, vm->regs[6], time_info, sizeof(time_info)
                ) ||
                !c33_vm_write(
                    vm, vm->regs[7], date_info, sizeof(date_info)
                )) {
                return C33_VM_FAULT;
            }
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_SHOW_STATUS_AND_DESKTOP:
    case COMPAT_GUI_GET_BACKGROUND_PLAY_STATE:
    case COMPAT_GUI_TRACE_INIT:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_HELP2:
        {
            char *help = guest_copy_c_string(vm, vm->regs[7], 4096u);
            if (!help) {
                return C33_VM_FAULT;
            }
            vm->regs[4] = (u32)bda_help_page(
                0, k_native_help_title, help
            );
            bda_free(help);
            /*
             * parent=0 is the SDK-supported form for a BDA without a native
             * Frame. The modal firmware page owns the display until it
             * returns, so restore the compatibility surface afterwards.
             */
            present_framebuffer(state);
            return C33_VM_OK;
        }
    default:
        break;
    }
    return C33_VM_UNSUPPORTED;
}

static void service_timer(compat_9588_state_t *state, u32 elapsed)
{
    if (!state->timer_id || state->quit) {
        return;
    }
    state->timer_elapsed += elapsed;
    if (state->timer_elapsed >= state->timer_interval) {
        state->timer_elapsed = 0u;
        queue_message(
            state,
            state->timer_hwnd,
            COMPAT_MSG_TIMER,
            state->timer_id,
            0u
        );
    }
}

__attribute__((section(".text.bda_main")))
int bda_main(void)
{
    const u8 *file_bytes = k_embedded_game;
    u32 file_size = K_EMBEDDED_GAME_SIZE;
    u8 *iram = 0;
    u8 *api_ram = 0;
    u8 *heap_ram = 0;
    u8 *code_ram = 0;
    u16 *framebuffer = 0;
    compat_9588_runtime_t *runtime;
    d300_image_t *image;
    d300_status_t image_status;
    c33_vm_t *vm;
    compat_api_t *api;
    compat_9588_state_t *state;
    c33_vm_status_t vm_status;
    u32 host_tick;
    u8 exit_pc[4] = {0xfc, 0xff, 0xff, 0x0f};

    g_diagnostic[1] = 1u;
    runtime = (compat_9588_runtime_t *)bda_alloc(sizeof(*runtime));
    if (allocation_failed(runtime)) {
        bda_msgbox("9288S compatibility", "Not enough runtime memory");
        return 1;
    }
    g_diagnostic[1] = 2u;
    bda_memset(runtime, 0, sizeof(*runtime));
    image = &runtime->image;
    vm = &runtime->vm;
    api = &runtime->api;
    state = &runtime->state;
    state->vm = vm;
    state->pen_color = 16u;
    state->brush_color = 15u;
    state->background_color = 15u;
    state->text_color = 16u;
    framebuffer = (u16 *)bda_alloc(SCREEN_W * SCREEN_H * 2u);
    if (allocation_failed(framebuffer)) {
        bda_msgbox("9288S compatibility", "Not enough display memory");
        bda_free(runtime);
        return 2;
    }
    state->framebuffer = framebuffer;
    g_diagnostic[1] = 3u;
    fill_framebuffer(state, 0x0000u);
    present_framebuffer(state);

    image_status = d300_parse(image, file_bytes, file_size);
    if (image_status != D300_OK) {
        bda_msgbox("9288S compatibility", d300_status_string(image_status));
        bda_free(framebuffer);
        bda_free(runtime);
        return 4;
    }
    g_diagnostic[1] = 4u;
    if (image->program_size > GUEST_CODE_SIZE) {
        bda_msgbox("9288S compatibility", "D300 program is too large");
        bda_free(framebuffer);
        bda_free(runtime);
        return 5;
    }

    iram = (u8 *)bda_alloc(GUEST_IRAM_SIZE);
    api_ram = (u8 *)bda_alloc(GUEST_API_SIZE);
    heap_ram = (u8 *)bda_alloc(GUEST_HEAP_SIZE);
    code_ram = (u8 *)bda_alloc(GUEST_CODE_SIZE);
    if (allocation_failed(iram) ||
        allocation_failed(api_ram) ||
        allocation_failed(heap_ram) ||
        allocation_failed(code_ram)) {
        if (!allocation_failed(iram)) bda_free(iram);
        if (!allocation_failed(api_ram)) bda_free(api_ram);
        if (!allocation_failed(heap_ram)) bda_free(heap_ram);
        if (!allocation_failed(code_ram)) bda_free(code_ram);
        bda_free(framebuffer);
        bda_free(runtime);
        bda_msgbox("9288S compatibility", "Not enough memory for guest RAM");
        return 6;
    }
    g_diagnostic[1] = 5u;
    bda_memset(iram, 0, GUEST_IRAM_SIZE);
    bda_memset(api_ram, 0, GUEST_API_SIZE);
    bda_memset(heap_ram, 0, GUEST_HEAP_SIZE);
    bda_memset(code_ram, 0, GUEST_CODE_SIZE);
    bda_memcpy(
        code_ram,
        d300_program(image),
        image->program_size
    );

    c33_vm_init(vm);
    c33_vm_map(vm, 0, iram, GUEST_IRAM_SIZE, 1);
    c33_vm_map(vm, GUEST_API_BASE, api_ram, GUEST_API_SIZE, 1);
    c33_vm_map(vm, GUEST_HEAP_BASE, heap_ram, GUEST_HEAP_SIZE, 1);
    c33_vm_map(vm, GUEST_CODE_BASE, code_ram, GUEST_CODE_SIZE, 1);
    compat_api_init(api, vm, GUEST_HEAP_BASE, GUEST_HEAP_END);
    api->dispatch = dispatch_9588;
    api->dispatch_opaque = state;
    if (!compat_api_install(api)) {
        bda_msgbox("9288S compatibility", "Could not install API tables");
        bda_free(code_ram);
        bda_free(heap_ram);
        bda_free(api_ram);
        bda_free(iram);
        bda_free(framebuffer);
        bda_free(runtime);
        return 7;
    }
    g_diagnostic[1] = 6u;

    c33_vm_reset(vm, D300_GUEST_LOAD_BASE, GUEST_STACK_TOP, 0);
    vm->sp -= 4u;
    c33_vm_write(vm, vm->sp, exit_pc, sizeof(exit_pc));
    host_tick = bda_gui_tick_count_25ms();
    g_diagnostic[1] = 7u;

    for (;;) {
        vm_status = c33_vm_run(vm, VM_SLICE);
        g_diagnostic[1] = 8u;
        g_diagnostic[2] = (u32)vm_status;
        g_diagnostic[3] = vm->pc;
        g_diagnostic[4] = vm->fault_pc;
        g_diagnostic[5] = vm->fault_opcode;
        g_diagnostic[6] = api->last_group;
        g_diagnostic[7] = api->last_slot;
        if (vm_status != C33_VM_YIELD) {
            break;
        }

        /*
         * GetMessage yields while the guest queue is empty. The firmware
         * delay is a calibrated busy-wait, not a microsecond sleep, so keep
         * it at one unit and derive guest timer progress from the verified
         * 25 ms monotonic 9588 clock.
         */
        {
            u32 current_tick;
            u32 elapsed_ticks;
            bda_sys_delay(1u);
            service_hardware_input(state);
            current_tick = bda_gui_tick_count_25ms();
            elapsed_ticks = current_tick - host_tick;
            if (elapsed_ticks) {
                service_timer(state, elapsed_ticks * HOST_TICK_MS);
                host_tick = current_tick;
            }
        }
        present_framebuffer(state);
    }

    if (vm_status == C33_VM_DONE && !state->quit) {
        char text[128];
        char *out = text;
        char *end = text + sizeof(text);
        append_text(&out, end, "Unexpected guest return\nparent: ");
        append_hex(&out, end, state->parent_hwnd);
        append_text(&out, end, "\ncallbacks: ");
        append_hex(&out, end, vm->callback_depth);
        append_text(&out, end, "\nlast group: ");
        append_hex(&out, end, api->last_group);
        append_text(&out, end, "\nlast slot: ");
        append_hex(&out, end, api->last_slot);
        *out = 0;
        bda_msgbox("9288S compatibility", text);
    } else if (vm_status != C33_VM_DONE) {
        show_vm_status(vm, vm_status);
    }
    bda_free(code_ram);
    bda_free(heap_ram);
    bda_free(api_ram);
    bda_free(iram);
    bda_free(framebuffer);
    state->framebuffer = 0;
    bda_free(runtime);
    return vm_status == C33_VM_DONE ? 0 : 8;
}

/* The existing one-source BDA builder compiles a single translation unit. */
#include "../../runtime/src/d300.c"
#include "../../runtime/src/c33vm.c"
#include "../../runtime/src/compat_api.c"
