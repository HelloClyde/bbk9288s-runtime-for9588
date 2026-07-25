#include "bda_sdk.h"

#include "../../runtime/include/c33vm.h"
#include "../../runtime/include/c33jit.h"
#include "../../runtime/include/compat_api.h"
#include "../../runtime/include/compat_fs.h"
#include "../../runtime/include/compat_gui.h"
#include "../../runtime/include/d300.h"

#define SCREEN_W         240
#define SCREEN_H         320
#define GUEST_SCREEN_W   160
#define GUEST_SCREEN_H   240
#define GAME_VIEW_W      160
#define GAME_VIEW_H      240
#define GAME_VIEW_X      ((SCREEN_W - GAME_VIEW_W) / 2)
#define BOTTOM_BAR_TOP   GAME_VIEW_H
#define FULLSCREEN_W     SCREEN_W
#define FULLSCREEN_H     SCREEN_H
#define FULLSCREEN_VIEW_W 213
#define FULLSCREEN_VIEW_H FULLSCREEN_H
#define FULLSCREEN_VIEW_X \
    ((FULLSCREEN_W - FULLSCREEN_VIEW_W) / 2)
#define FULLSCREEN_VIEW_Y 0
#define FULLSCREEN_FLOAT_W 42
#define FULLSCREEN_FLOAT_H 32
#define FULLSCREEN_FLOAT_DEFAULT_X \
    (FULLSCREEN_W - FULLSCREEN_FLOAT_W - 8)
#define FULLSCREEN_FLOAT_DEFAULT_Y 8
#define FULLSCREEN_DRAG_THRESHOLD 3
#define RAW_EVENT_MAX_PER_POLL 16u

#define GUEST_IRAM_SIZE  0x00004000u
#define GUEST_API_BASE   0x02000000u
#define GUEST_API_SIZE   0x00010000u
#define GUEST_HEAP_SIZE  0x00080000u
#define GUEST_CODE_BASE  0x02700000u
#define GUEST_CODE_MIN_SIZE 0x00020000u
#define GUEST_CODE_MAX_SIZE 0x00100000u
#define GUEST_STACK_TOP  0x00003f80u
#define GUEST_HEAP_BASE  0x02600000u
#define GUEST_HEAP_END   (GUEST_HEAP_BASE + GUEST_HEAP_SIZE)
#define VM_SLICE         100000u
#define CALLBACK_BUDGET  1000000u
#define C33_JIT_CODE_CACHE_SIZE 0x00100000u
#define EVENT_QUEUE_SIZE 16u
#define GUEST_TIMER_COUNT 16u
#define LISTBOX_ITEM_COUNT 5u
#define LISTBOX_ITEM_SIZE  64u
#define GUEST_CONTROL_HWND 0x100u
#define LISTBOX_SELECT_X   53u
#define LISTBOX_SELECT_Y   90u
#define LISTBOX_SELECT_W   88u
#define LISTBOX_SELECT_H   16u
#define LISTBOX_ITEM_STEP  19u
#define MAX_FILE_IO_SIZE 0x00020000u
#define MAX_D300_FILE_SIZE 0x00400000u
#define NATIVE_PATH_CAPACITY 320u
#define GUEST_PATH_CAPACITY  192u
#define HZK_GLYPH_SIZE     24u
#define HZK_BASE_OFFSET    0x001a84b0u
#define PROGRAM_MAX_ENTRIES 32u
#define PROGRAMS_PER_PAGE   5u
#define PROGRAM_ROW_TOP     40u
#define PROGRAM_ROW_HEIGHT  46u
#define PROGRAM_ICON_HEADER_SIZE 16u
#define PROGRAM_ICON_WIDTH  32u
#define PROGRAM_ICON_HEIGHT 32u
#define PROGRAM_ICON_FRAME_SIZE 256u
#define PROGRAM_ICON_PAYLOAD_SIZE 512u
#define PROGRAM_TITLE_CAPACITY 17u
#define COMPAT_LOG_PATH \
    COMPAT_FS_NATIVE_ROOT "\\9288LOG.TXT"
#define COMPAT_LOG_HEARTBEAT_TICKS 200u

#define PICV_FAULT_INVALID_ARGUMENT 1u
#define PICV_FAULT_SOURCE_STRIDE    2u
#define PICV_FAULT_SOURCE_READ      3u
#define PICV_FAULT_DESTINATION_READ 4u
#define PICV_FAULT_DESTINATION_WRITE 5u
#define PICV_FAULT_STACK_ARGUMENTS  0x100u

#define VIRTUAL_ACTION_NONE     0u
#define VIRTUAL_ACTION_UP       1u
#define VIRTUAL_ACTION_DOWN     2u
#define VIRTUAL_ACTION_LEFT     3u
#define VIRTUAL_ACTION_RIGHT    4u
#define VIRTUAL_ACTION_CONFIRM  5u
#define VIRTUAL_ACTION_BACK     6u
#define VIRTUAL_ACTION_SELECT   7u
#define VIRTUAL_ACTION_SETTINGS 8u
#define VIRTUAL_ACTION_FULLSCREEN 9u

static const char k_native_help_title[] = "9288S";

typedef struct guest_message {
    u32 hwnd;
    u32 message;
    u32 wparam;
    u32 lparam;
    u32 time;
    u32 point_x;
    u32 point_y;
} guest_message_t;

typedef struct guest_timer {
    u32 hwnd;
    u32 id;
    u32 interval;
    u32 elapsed;
    int active;
} guest_timer_t;

typedef struct compat_9588_state {
    c33_vm_t *vm;
    u16 *framebuffer;
    u16 *fullscreen_buffer;
    char selected_path[NATIVE_PATH_CAPACITY];
    char guest_cwd[GUEST_PATH_CAPACITY];
    u32 guest_window_proc;
    u32 guest_hwnd;
    u32 parent_window_proc;
    u32 parent_hwnd;
    u32 parent_pen_color;
    u32 parent_brush_color;
    u32 parent_background_color;
    u32 parent_background_mode;
    s32 parent_current_x;
    s32 parent_current_y;
    int parent_instant_paint;
    u32 guest_paint_depth;
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
    int hardware_events_ready;
    int touch_down;
    int touch_captured;
    u32 touch_x;
    u32 touch_y;
    u32 touch_region;
    int touch_escape_suppressed;
    int native_escape_pending;
    bda_handle_t native_frame;
    bda_handle_t native_draw;
    bda_handle_t native_draw_owner;
    void *native_draw_object;
    bda_gui_picture_t native_picture;
    bda_gui_message_t native_message;
    int native_frame_detached;
    int native_redraw;
    u32 raw_event_count;
    u32 raw_touch_count;
    u32 virtual_action;
    int fullscreen_mode;
    s32 fullscreen_float_x;
    s32 fullscreen_float_y;
    s32 fullscreen_touch_start_x;
    s32 fullscreen_touch_start_y;
    s32 fullscreen_float_start_x;
    s32 fullscreen_float_start_y;
    int fullscreen_dragged;
    u8 combo_keys[6];
    int controls_left;
    int request_reselect;
    guest_timer_t timers[GUEST_TIMER_COUNT];
    u32 timer_last_id;
    u32 timer_service_count;
    u32 timer_fire_count;
    u32 message_time;
    u32 pen_color;
    u32 brush_color;
    u32 background_color;
    u32 background_mode;
    u32 text_color;
    u32 legacy_font_type;
    u32 selected_log_font;
    u32 created_log_font;
    u32 created_log_font_size;
    int hzk_file;
    int hzk_attempted;
    s32 current_x;
    s32 current_y;
    int instant_paint;
    int quit;
    u32 api_call_count;
    u32 heartbeat_tick;
} compat_9588_state_t;

typedef struct compat_9588_runtime {
    d300_image_t image;
    c33_vm_t vm;
    compat_api_t api;
    compat_9588_state_t state;
} compat_9588_runtime_t;

typedef struct program_entry {
    char path[NATIVE_PATH_CAPACITY];
    char file_name[NATIVE_PATH_CAPACITY];
    char title[PROGRAM_TITLE_CAPACITY];
    u8 icon[PROGRAM_ICON_PAYLOAD_SIZE];
    int has_icon;
} program_entry_t;

/*
 * Keep this in the packaged .data section.  The minimal BDA loader copies the
 * file-backed image but does not reserve a separate trailing .bss range.
 */
static volatile u32 g_diagnostic[16] = {
    0x44473932u, 0x13579bdfu, 0x2468ace0u,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static compat_9588_state_t *g_native_input_state
    __attribute__((section(".data"))) = 0;
static bda_handle_t g_native_shared_frame
    __attribute__((section(".data"))) = 0;
static bda_handle_t g_native_shared_draw
    __attribute__((section(".data"))) = 0;
static bda_handle_t g_native_shared_draw_owner
    __attribute__((section(".data"))) = 0;
static void *g_native_shared_draw_object
    __attribute__((section(".data"))) = 0;

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

static u32 native_text_length(const char *text)
{
    u32 length = 0u;
    while (text && text[length]) {
        ++length;
    }
    return length;
}

static void compat_log_write(const char *text, int truncate)
{
    int file = bda_fs_fopen_raw(
        COMPAT_LOG_PATH, truncate ? "wb" : "rb+"
    );
    u32 length;
    if (!bda_fs_file_is_valid(file) && !truncate) {
        file = bda_fs_fopen_raw(COMPAT_LOG_PATH, "wb");
    }
    if (!bda_fs_file_is_valid(file)) {
        return;
    }
    if (!truncate) {
        (void)bda_fs_seek_raw(file, 0, BDA_SEEK_END);
    }
    length = native_text_length(text);
    if (length) {
        (void)bda_fs_fwrite_raw(text, 1u, length, file);
    }
    (void)bda_fs_close_raw(file);
}

static void compat_log_record(
    const char *event,
    const char *detail,
    u32 value0,
    u32 value1,
    u32 value2,
    u32 value3
)
{
    char line[256];
    char *out = line;
    char *end = line + sizeof(line);
    append_text(&out, end, "T=");
    append_hex(&out, end, bda_gui_tick_count_25ms());
    append_text(&out, end, " ");
    append_text(&out, end, event ? event : "EVENT");
    if (detail && *detail) {
        append_text(&out, end, " detail=");
        append_text(&out, end, detail);
    }
    append_text(&out, end, " v0=");
    append_hex(&out, end, value0);
    append_text(&out, end, " v1=");
    append_hex(&out, end, value1);
    append_text(&out, end, " v2=");
    append_hex(&out, end, value2);
    append_text(&out, end, " v3=");
    append_hex(&out, end, value3);
    append_text(&out, end, "\r\n");
    *out = 0;
    compat_log_write(line, 0);
}

static void compat_log_vm(
    const char *event,
    const c33_vm_t *vm,
    const compat_api_t *api,
    const compat_9588_state_t *state,
    c33_vm_status_t status
)
{
    char line[320];
    char *out = line;
    char *end = line + sizeof(line);
    append_text(&out, end, "T=");
    append_hex(&out, end, bda_gui_tick_count_25ms());
    append_text(&out, end, " ");
    append_text(&out, end, event ? event : "VM");
    append_text(&out, end, " status=");
    append_hex(&out, end, (u32)status);
    append_text(&out, end, " pc=");
    append_hex(&out, end, vm ? vm->pc : 0u);
    append_text(&out, end, " sp=");
    append_hex(&out, end, vm ? vm->sp : 0u);
    append_text(&out, end, " ins=");
    append_hex(
        &out, end, vm ? (u32)(vm->instructions >> 32) : 0u
    );
    append_text(&out, end, ":");
    append_hex(&out, end, vm ? (u32)vm->instructions : 0u);
    append_text(&out, end, " api=");
    append_hex(&out, end, api ? api->last_group : 0u);
    append_text(&out, end, "/");
    append_hex(&out, end, api ? api->last_slot : 0u);
    append_text(&out, end, " calls=");
    append_hex(&out, end, state ? state->api_call_count : 0u);
    append_text(&out, end, " hwnd=");
    append_hex(&out, end, state ? state->guest_hwnd : 0u);
    append_text(&out, end, " proc=");
    append_hex(&out, end, state ? state->guest_window_proc : 0u);
    append_text(&out, end, " events=");
    append_hex(&out, end, state ? state->event_count : 0u);
    append_text(&out, end, " timer=");
    append_hex(&out, end, state ? state->timer_last_id : 0u);
    append_text(&out, end, "\r\n");
    *out = 0;
    compat_log_write(line, 0);
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
    append_text(&out, end, "\nAPI: ");
    if (vm->hostcall_opaque) {
        compat_api_t *api = (compat_api_t *)vm->hostcall_opaque;
        append_hex(&out, end, api->last_group);
        append_text(&out, end, "/");
        append_hex(&out, end, api->last_slot);
    }
    append_text(&out, end, "\nAddress: ");
    append_hex(&out, end, vm->fault_address);
    append_text(&out, end, "\nSP: ");
    append_hex(&out, end, vm->sp);
    append_text(&out, end, "\nR6: ");
    append_hex(&out, end, vm->regs[6]);
    append_text(&out, end, "\nR7: ");
    append_hex(&out, end, vm->regs[7]);
    append_text(&out, end, "\nR8: ");
    append_hex(&out, end, vm->regs[8]);
    append_text(&out, end, "\nR9: ");
    append_hex(&out, end, vm->regs[9]);
    *out = 0;
    bda_msgbox("9288S compatibility", text);
}

static int guest_read_u32(c33_vm_t *vm, u32 address, u32 *value)
{
    u8 bytes[4];
    if (!c33_vm_read(vm, address, bytes, sizeof(bytes))) {
        vm->fault_address = address;
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

static int guest_write_u16(c33_vm_t *vm, u32 address, u16 value)
{
    u8 bytes[2];
    bytes[0] = (u8)value;
    bytes[1] = (u8)(value >> 8);
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

static int file_mode_may_write(const char *mode)
{
    while (mode && *mode) {
        if (*mode == 'w' || *mode == 'a' || *mode == '+') {
            return 1;
        }
        ++mode;
    }
    return 0;
}

static int guest_path_is_absolute(const char *path)
{
    if (!path) {
        return 0;
    }
    return path[0] == '\\' || path[0] == '/' ||
           (((path[0] >= 'A' && path[0] <= 'Z') ||
             (path[0] >= 'a' && path[0] <= 'z')) &&
            path[1] == ':');
}

static int map_guest_path_for_state(
    const compat_9588_state_t *state,
    const char *guest_path,
    char *native_path,
    u32 native_capacity
)
{
    char combined[NATIVE_PATH_CAPACITY];
    u32 at = 0u;
    u32 index = 0u;
    if (!state || !guest_path) {
        return 0;
    }
    if (guest_path_is_absolute(guest_path) ||
        state->guest_cwd[0] == 0 ||
        (state->guest_cwd[0] == '\\' &&
         state->guest_cwd[1] == 0)) {
        return compat_fs_map_guest_path(
            guest_path, native_path, native_capacity
        );
    }
    while (state->guest_cwd[index]) {
        if (at + 1u >= sizeof(combined)) return 0;
        combined[at++] = state->guest_cwd[index++];
    }
    if (at && combined[at - 1u] != '\\') {
        if (at + 1u >= sizeof(combined)) return 0;
        combined[at++] = '\\';
    }
    index = 0u;
    while (guest_path[index]) {
        if (at + 1u >= sizeof(combined)) return 0;
        combined[at++] = guest_path[index++];
    }
    combined[at] = 0;
    return compat_fs_map_guest_path(
        combined, native_path, native_capacity
    );
}

static int set_guest_cwd_from_native(
    compat_9588_state_t *state,
    const char *native_path
)
{
    const char *root = COMPAT_FS_NATIVE_ROOT;
    u32 root_length = 0u;
    u32 index = 0u;
    while (root[root_length]) ++root_length;
    while (index < root_length &&
           native_path[index] == root[index]) {
        ++index;
    }
    if (index != root_length) {
        return 0;
    }
    index = 0u;
    if (!native_path[root_length]) {
        state->guest_cwd[index++] = '\\';
    } else {
        while (native_path[root_length] &&
               index + 1u < sizeof(state->guest_cwd)) {
            state->guest_cwd[index++] = native_path[root_length++];
        }
        if (native_path[root_length]) {
            return 0;
        }
    }
    state->guest_cwd[index] = 0;
    return 1;
}

static int native_path_info(
    const char *native_path,
    bda_fs_find_data_t *find_data
)
{
    int result;
    bda_fs_find_data_init(find_data);
    result = bda_fs_findfirst(native_path, 0x27u, find_data);
    if (result != -1) {
        (void)bda_fs_findclose(find_data);
    }
    return result;
}

static int bda_fs_remove_compat(const char *path)
{
    return bda_sdk_internal_call1(
        bda_sdk_internal_fs(), 0x024u, (u32)path
    );
}

static int bda_fs_rename_compat(
    const char *old_path,
    const char *new_path
)
{
    return bda_sdk_internal_call2(
        bda_sdk_internal_fs(),
        0x028u,
        (u32)old_path,
        (u32)new_path
    );
}

static int bda_fs_rmdir_compat(const char *path)
{
    return bda_sdk_internal_call1(
        bda_sdk_internal_fs(), 0x034u, (u32)path
    );
}

static void copy_native_path(char *destination, const char *source)
{
    u32 index = 0u;
    if (!destination) {
        return;
    }
    if (source) {
        while (source[index] && index + 1u < NATIVE_PATH_CAPACITY) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = 0;
}

static void ensure_native_parent_directories(const char *native_path)
{
    char partial[NATIVE_PATH_CAPACITY];
    u32 length = 0u;
    u32 index;

    if (!native_path) {
        return;
    }
    while (native_path[length] &&
           length + 1u < sizeof(partial)) {
        partial[length] = native_path[length];
        ++length;
    }
    if (native_path[length]) {
        return;
    }
    partial[length] = 0;

    /*
     * Skip the drive-root separator, then create every path component up to
     * but not including the final filename. Passing a trailing separator
     * creates the complete directory itself.
     */
    for (index = 3u; index < length; ++index) {
        if (partial[index] == '\\' || partial[index] == '/') {
            char separator = partial[index];
            partial[index] = 0;
            (void)bda_fs_mkdir(partial);
            partial[index] = separator;
        }
    }
}

static void ensure_native_directory_tree(const char *native_path)
{
    ensure_native_parent_directories(native_path);
    (void)bda_fs_mkdir(native_path);
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

static u8 glyph_row(char ch, u32 row);
static u8 legacy_small_glyph_row(char ch, u32 row);
static int read_hzk_glyph(
    compat_9588_state_t *state,
    u8 high,
    u8 low,
    u8 glyph[HZK_GLYPH_SIZE]
);

static void fill_framebuffer(compat_9588_state_t *state, u16 color)
{
    u32 i;
    for (i = 0; i < SCREEN_W * SCREEN_H; ++i) {
        state->framebuffer[i] = color;
    }
}

static void put_panel_pixel(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u16 color
)
{
    if (!state->framebuffer ||
        x < 0 || y < 0 ||
        x >= SCREEN_W || y >= SCREEN_H) {
        return;
    }
    state->framebuffer[(u32)y * SCREEN_W + (u32)x] = color;
}

static void fill_panel_rect(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    u16 color
)
{
    u32 px;
    u32 py;
    for (py = 0u; py < height; ++py) {
        for (px = 0u; px < width; ++px) {
            put_panel_pixel(state, x + (s32)px, y + (s32)py, color);
        }
    }
}

static void draw_panel_line(
    compat_9588_state_t *state,
    s32 x0,
    s32 y0,
    s32 x1,
    s32 y1,
    u16 color
)
{
    s32 dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    s32 sx = x0 < x1 ? 1 : -1;
    s32 dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    s32 sy = y0 < y1 ? 1 : -1;
    s32 error = dx + dy;
    for (;;) {
        s32 twice;
        put_panel_pixel(state, x0, y0, color);
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

static void draw_panel_text(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    const char *text,
    u16 color
)
{
    u32 index;
    for (index = 0u; text[index]; ++index) {
        u32 row;
        for (row = 0u; row < 7u; ++row) {
            u8 bits = glyph_row(text[index], row);
            u32 column;
            for (column = 0u; column < 5u; ++column) {
                if (bits & (1u << (4u - column))) {
                    put_panel_pixel(
                        state,
                        x + (s32)(index * 6u + column),
                        y + (s32)row,
                        color
                    );
                }
            }
        }
    }
}

static void draw_panel_hz_bitmap(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    const u8 glyph[24],
    u16 color
)
{
    u32 row;
    for (row = 0u; row < 12u; ++row) {
        u16 bits =
            ((u16)glyph[row * 2u] << 8) |
            glyph[row * 2u + 1u];
        u32 column;
        for (column = 0u; column < 12u; ++column) {
            if (bits & (u16)(0x8000u >> column)) {
                put_panel_pixel(
                    state,
                    x + (s32)column,
                    y + (s32)row,
                    color
                );
            }
        }
    }
}

static void fill_panel_rounded_rect(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    u32 radius,
    u16 color
)
{
    u32 px;
    u32 py;
    if (!width || !height) {
        return;
    }
    if (radius * 2u > width) {
        radius = width / 2u;
    }
    if (radius * 2u > height) {
        radius = height / 2u;
    }
    for (py = 0u; py < height; ++py) {
        for (px = 0u; px < width; ++px) {
            s32 dx = 0;
            s32 dy = 0;
            if (px < radius) {
                dx = (s32)radius - (s32)px - 1;
            } else if (px >= width - radius) {
                dx = (s32)px - (s32)(width - radius);
            }
            if (py < radius) {
                dy = (s32)radius - (s32)py - 1;
            } else if (py >= height - radius) {
                dy = (s32)py - (s32)(height - radius);
            }
            if (!dx || !dy ||
                dx * dx + dy * dy <= (s32)(radius * radius)) {
                put_panel_pixel(
                    state, x + (s32)px, y + (s32)py, color
                );
            }
        }
    }
}

static void fill_panel_circle(
    compat_9588_state_t *state,
    s32 center_x,
    s32 center_y,
    s32 radius,
    u16 color
)
{
    s32 x;
    s32 y;
    for (y = -radius; y <= radius; ++y) {
        for (x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) {
                put_panel_pixel(
                    state, center_x + x, center_y + y, color
                );
            }
        }
    }
}

static int virtual_control_pressed(
    const compat_9588_state_t *state,
    u32 action
)
{
    return state->touch_down &&
           state->touch_region == 2u &&
           state->virtual_action == action;
}

static void draw_rounded_key(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    u32 radius,
    u16 face,
    u16 edge,
    int pressed
)
{
    const u16 shadow = 0x0841u;
    s32 offset = pressed ? 2 : 0;
    fill_panel_rounded_rect(
        state, x, y + 2, width, height, radius, shadow
    );
    fill_panel_rounded_rect(
        state, x, y + offset, width, height, radius, edge
    );
    if (width > 2u && height > 2u) {
        fill_panel_rounded_rect(
            state,
            x + 1,
            y + offset + 1,
            width - 2u,
            height - 2u,
            radius > 1u ? radius - 1u : 0u,
            face
        );
    }
    if (!pressed && width > radius * 2u + 2u) {
        draw_panel_line(
            state,
            x + (s32)radius + 1,
            y + 1,
            x + (s32)width - (s32)radius - 2,
            y + 1,
            0x8cd4u
        );
    }
}

static void draw_direction_arrow(
    compat_9588_state_t *state,
    s32 center_x,
    s32 center_y,
    u32 action,
    u16 color
)
{
    s32 step;
    if (action == VIRTUAL_ACTION_UP ||
        action == VIRTUAL_ACTION_DOWN) {
        s32 direction =
            action == VIRTUAL_ACTION_UP ? 1 : -1;
        s32 tip_y =
            center_y +
            (action == VIRTUAL_ACTION_UP ? -6 : 6);
        for (step = 0; step < 6; ++step) {
            draw_panel_line(
                state,
                center_x - step,
                tip_y + direction * step,
                center_x + step,
                tip_y + direction * step,
                color
            );
        }
        fill_panel_rect(
            state,
            center_x - 1,
            center_y + (action == VIRTUAL_ACTION_UP ? 0 : -5),
            3u,
            6u,
            color
        );
    } else {
        s32 direction =
            action == VIRTUAL_ACTION_LEFT ? 1 : -1;
        s32 tip_x =
            center_x +
            (action == VIRTUAL_ACTION_LEFT ? -6 : 6);
        for (step = 0; step < 6; ++step) {
            draw_panel_line(
                state,
                tip_x + direction * step,
                center_y - step,
                tip_x + direction * step,
                center_y + step,
                color
            );
        }
        fill_panel_rect(
            state,
            center_x + (action == VIRTUAL_ACTION_LEFT ? 0 : -5),
            center_y - 1,
            6u,
            3u,
            color
        );
    }
}

static void draw_text_action_button(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    u32 action,
    u16 face,
    u16 pressed_face,
    u16 edge,
    const u8 first_glyph[24],
    const u8 second_glyph[24]
)
{
    int pressed = virtual_control_pressed(state, action);
    s32 offset = pressed ? 2 : 0;
    s32 text_x = x + ((s32)width - 24) / 2;
    s32 text_y = y + ((s32)height - 12) / 2 + offset;
    draw_rounded_key(
        state,
        x,
        y,
        width,
        height,
        8u,
        pressed ? pressed_face : face,
        edge,
        pressed
    );
    draw_panel_hz_bitmap(
        state, text_x, text_y, first_glyph, 0xef9eu
    );
    draw_panel_hz_bitmap(
        state, text_x + 12, text_y, second_glyph, 0xef9eu
    );
}

static void draw_expand_icon(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u16 color
)
{
    draw_panel_line(state, x, y + 5, x, y, color);
    draw_panel_line(state, x, y, x + 5, y, color);
    draw_panel_line(state, x + 11, y, x + 16, y, color);
    draw_panel_line(state, x + 16, y, x + 16, y + 5, color);
    draw_panel_line(state, x, y + 11, x, y + 16, color);
    draw_panel_line(state, x, y + 16, x + 5, y + 16, color);
    draw_panel_line(
        state, x + 11, y + 16, x + 16, y + 16, color
    );
    draw_panel_line(
        state, x + 16, y + 11, x + 16, y + 16, color
    );
}

static void draw_virtual_controls(compat_9588_state_t *state)
{
    static const u8 cancel_first[24] = {
        0x00,0x00,0xfc,0x00,0x4b,0xc0,0x49,0x40,
        0x79,0x40,0x49,0x40,0x79,0x40,0x49,0x40,
        0x4c,0x80,0xf8,0x80,0x09,0x40,0x0a,0x20
    };
    static const u8 cancel_second[24] = {
        0x01,0x00,0x49,0x20,0x25,0x40,0x01,0x00,
        0x8f,0xe0,0x48,0x20,0x0f,0xe0,0x08,0x20,
        0x2f,0xe0,0x48,0x20,0x88,0x20,0x08,0x60
    };
    static const u8 confirm_first[24] = {
        0x02,0x00,0xf3,0xc0,0x24,0x40,0x20,0x80,
        0x47,0xe0,0x75,0x20,0xd7,0xe0,0x55,0x20,
        0x57,0xe0,0x75,0x20,0x55,0x20,0x08,0x60
    };
    static const u8 confirm_second[24] = {
        0x08,0x00,0x04,0x00,0xff,0xe0,0x80,0x20,
        0x00,0x00,0xff,0xe0,0x04,0x00,0x24,0x00,
        0x27,0xc0,0x24,0x00,0x54,0x00,0x8f,0xe0
    };
    const u16 background = 0x10a2u;
    const u16 surface = 0x2146u;
    const u16 surface_pressed = 0x31a8u;
    const u16 edge = 0x530du;
    const u16 ink = 0xef9eu;
    const u16 muted = 0xad97u;
    const s32 dpad_x = 78;
    int pressed;

    fill_panel_rect(
        state, 0, 0, GAME_VIEW_X, GAME_VIEW_H,
        background
    );
    fill_panel_rect(
        state,
        GAME_VIEW_X + GAME_VIEW_W,
        0,
        GAME_VIEW_X,
        GAME_VIEW_H,
        background
    );
    fill_panel_rect(
        state, 0, BOTTOM_BAR_TOP, SCREEN_W, SCREEN_H - BOTTOM_BAR_TOP,
        background
    );
    draw_panel_line(
        state, 0, BOTTOM_BAR_TOP, SCREEN_W - 1, BOTTOM_BAR_TOP, 0x0841u
    );
    draw_panel_line(
        state,
        GAME_VIEW_X,
        BOTTOM_BAR_TOP + 1,
        GAME_VIEW_X + GAME_VIEW_W - 1,
        BOTTOM_BAR_TOP + 1,
        edge
    );
    draw_panel_line(
        state, GAME_VIEW_X - 1, 0, GAME_VIEW_X - 1, GAME_VIEW_H - 1,
        0x0841u
    );
    draw_panel_line(
        state,
        GAME_VIEW_X + GAME_VIEW_W,
        0,
        GAME_VIEW_X + GAME_VIEW_W,
        GAME_VIEW_H - 1,
        0x0841u
    );

    /* Side function buttons leave the 160x240 guest view untouched. */
    pressed = virtual_control_pressed(
        state, VIRTUAL_ACTION_SELECT
    );
    draw_rounded_key(
        state, 4, 84, 32u, 48u, 5u,
        pressed ? surface_pressed : surface,
        edge,
        pressed
    );
    fill_panel_rounded_rect(
        state, 13, 89 + (pressed ? 2 : 0), 14u, 3u, 1u,
        0x2697u
    );
    draw_panel_text(
        state, 11, 105 + (pressed ? 2 : 0), "EXE", ink
    );

    pressed = virtual_control_pressed(
        state, VIRTUAL_ACTION_SETTINGS
    );
    draw_rounded_key(
        state, 204, 84, 32u, 48u, 5u,
        pressed ? surface_pressed : surface,
        edge,
        pressed
    );
    fill_panel_rounded_rect(
        state, 213, 89 + (pressed ? 2 : 0), 14u, 3u, 1u,
        0xe549u
    );
    draw_panel_text(
        state, 211, 105 + (pressed ? 2 : 0), "SET", ink
    );

    pressed = virtual_control_pressed(
        state, VIRTUAL_ACTION_FULLSCREEN
    );
    draw_rounded_key(
        state, 4, 150, 32u, 44u, 5u,
        pressed ? surface_pressed : surface,
        edge,
        pressed
    );
    draw_expand_icon(
        state, 12, 156 + (pressed ? 2 : 0),
        pressed ? 0x2697u : muted
    );
    draw_panel_text(
        state, 14, 179 + (pressed ? 2 : 0), "FS", ink
    );

    /* Bottom virtual controls follow the 9288S learning-device layout. */
    pressed = virtual_control_pressed(state, VIRTUAL_ACTION_UP);
    draw_rounded_key(
        state, dpad_x + 29, 243, 26u, 22u, 6u,
        pressed ? surface_pressed : surface, edge, pressed
    );
    draw_direction_arrow(
        state, dpad_x + 42, 254 + (pressed ? 2 : 0),
        VIRTUAL_ACTION_UP, pressed ? 0x2697u : muted
    );

    pressed = virtual_control_pressed(state, VIRTUAL_ACTION_LEFT);
    draw_rounded_key(
        state, dpad_x + 1, 267, 26u, 26u, 6u,
        pressed ? surface_pressed : surface, edge, pressed
    );
    draw_direction_arrow(
        state, dpad_x + 14, 280 + (pressed ? 2 : 0),
        VIRTUAL_ACTION_LEFT, pressed ? 0x2697u : muted
    );

    fill_panel_circle(
        state, dpad_x + 42, 280, 10, 0x0841u
    );
    fill_panel_circle(
        state, dpad_x + 42, 280, 7, 0x31a8u
    );

    pressed = virtual_control_pressed(state, VIRTUAL_ACTION_RIGHT);
    draw_rounded_key(
        state, dpad_x + 57, 267, 26u, 26u, 6u,
        pressed ? surface_pressed : surface, edge, pressed
    );
    draw_direction_arrow(
        state, dpad_x + 70, 280 + (pressed ? 2 : 0),
        VIRTUAL_ACTION_RIGHT, pressed ? 0x2697u : muted
    );

    pressed = virtual_control_pressed(state, VIRTUAL_ACTION_DOWN);
    draw_rounded_key(
        state, dpad_x + 29, 295, 26u, 21u, 6u,
        pressed ? surface_pressed : surface, edge, pressed
    );
    draw_direction_arrow(
        state, dpad_x + 42, 305 + (pressed ? 2 : 0),
        VIRTUAL_ACTION_DOWN, pressed ? 0x2697u : muted
    );

    /* Text actions replace game-console-style A/B buttons. */
    draw_text_action_button(
        state,
        4,
        263,
        56u,
        44u,
        VIRTUAL_ACTION_BACK,
        surface,
        surface_pressed,
        0x9987u,
        cancel_first,
        cancel_second
    );
    draw_text_action_button(
        state,
        180,
        263,
        56u,
        44u,
        VIRTUAL_ACTION_CONFIRM,
        0x0d32u,
        0x0badu,
        0x2697u,
        confirm_first,
        confirm_second
    );
}

static void put_guest_pixel(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 color
)
{
    s32 panel_x;
    s32 panel_y;
    if (!state->framebuffer ||
        x < 0 || y < 0 ||
        x >= GUEST_SCREEN_W || y >= GUEST_SCREEN_H) {
        return;
    }
    panel_x = GAME_VIEW_X + x;
    panel_y = y;
    put_panel_pixel(
        state, panel_x, panel_y, logical_color_to_rgb565(color)
    );
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

static void reverse_guest_rect(
    compat_9588_state_t *state,
    u32 x,
    u32 y,
    u32 width,
    u32 height
)
{
    u32 px;
    u32 py;
    for (py = 0u; py < height && y + py < GUEST_SCREEN_H; ++py) {
        for (px = 0u; px < width && x + px < GUEST_SCREEN_W; ++px) {
            u32 panel_x = GAME_VIEW_X + x + px;
            u32 panel_y = y + py;
            state->framebuffer[panel_y * SCREEN_W + panel_x] ^= 0xffffu;
        }
    }
}

static u32 guest_gray_from_rgb565(u16 pixel)
{
    u32 red;
    u32 green;
    u32 blue;
    u32 luminance;

    if (pixel == 0x0000u) return 0u;
    if (pixel == 0x52aau) return 1u;
    if (pixel == 0xad55u) return 2u;
    if (pixel == 0xffffu) return 3u;

    red = ((pixel >> 11) & 31u) * 255u / 31u;
    green = ((pixel >> 5) & 63u) * 255u / 63u;
    blue = (pixel & 31u) * 255u / 31u;
    luminance = (red * 30u + green * 59u + blue * 11u) / 100u;
    return (luminance * 3u + 127u) / 255u;
}

static int save_guest_box(
    compat_9588_state_t *state,
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    u32 guest_buffer
)
{
    u32 stride;
    u32 row;
    if (!guest_buffer || !width || !height ||
        x >= GUEST_SCREEN_W || y >= GUEST_SCREEN_H ||
        width > GUEST_SCREEN_W - x ||
        height > GUEST_SCREEN_H - y) {
        return 0;
    }
    stride = compat_gui_packed_2bpp_stride(width);
    for (row = 0u; row < height; ++row) {
        u32 byte_index;
        for (byte_index = 0u; byte_index < stride; ++byte_index) {
            u8 packed = 0u;
            u32 subpixel;
            for (subpixel = 0u; subpixel < 4u; ++subpixel) {
                u32 column = byte_index * 4u + subpixel;
                u32 gray = 0u;
                if (column < width) {
                    gray = guest_gray_from_rgb565(
                        state->framebuffer[
                            (y + row) * SCREEN_W +
                            GAME_VIEW_X + x + column
                        ]
                    );
                }
                packed |= (u8)(gray << (6u - subpixel * 2u));
            }
            if (!c33_vm_write(
                    state->vm,
                    guest_buffer + row * stride + byte_index,
                    &packed,
                    1u
                )) {
                state->vm->fault_address =
                    guest_buffer + row * stride + byte_index;
                return 0;
            }
        }
    }
    return 1;
}

static int restore_guest_box(
    compat_9588_state_t *state,
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    u32 guest_buffer
)
{
    u32 stride;
    u32 row;
    if (!guest_buffer || !width || !height ||
        x >= GUEST_SCREEN_W || y >= GUEST_SCREEN_H ||
        width > GUEST_SCREEN_W - x ||
        height > GUEST_SCREEN_H - y) {
        return 0;
    }
    stride = compat_gui_packed_2bpp_stride(width);
    for (row = 0u; row < height; ++row) {
        u32 byte_index;
        for (byte_index = 0u; byte_index < stride; ++byte_index) {
            u8 packed;
            u32 subpixel;
            if (!c33_vm_read(
                    state->vm,
                    guest_buffer + row * stride + byte_index,
                    &packed,
                    1u
                )) {
                state->vm->fault_address =
                    guest_buffer + row * stride + byte_index;
                return 0;
            }
            for (subpixel = 0u; subpixel < 4u; ++subpixel) {
                u32 column = byte_index * 4u + subpixel;
                if (column < width) {
                    put_guest_pixel(
                        state,
                        (s32)(x + column),
                        (s32)(y + row),
                        (packed >> (6u - subpixel * 2u)) & 3u
                    );
                }
            }
        }
    }
    return 1;
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

static void release_native_draw_context(
    compat_9588_state_t *state
)
{
    bda_handle_t draw;
    if (!state) {
        return;
    }
    draw = state->native_draw;
    state->native_draw = 0;
    state->native_draw_owner = 0;
    if (draw && (s32)draw != -1) {
        bda_gui_end_draw(draw);
    }
}

static int acquire_native_draw_context(
    compat_9588_state_t *state,
    bda_handle_t owner
)
{
    if (!state || !owner || (s32)owner == -1) {
        return 0;
    }
    if (state->native_draw &&
        state->native_draw_owner == owner) {
        return 1;
    }
    release_native_draw_context(state);
    state->native_draw = bda_gui_current_draw(owner);
    if (!state->native_draw ||
        (s32)state->native_draw == -1) {
        state->native_draw = 0;
        return 0;
    }
    state->native_draw_owner = owner;
    return 1;
}

static void put_fullscreen_pixel(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u16 color
)
{
    if (!state || !state->fullscreen_buffer ||
        x < 0 || y < 0 ||
        x >= FULLSCREEN_W || y >= FULLSCREEN_H) {
        return;
    }
    state->fullscreen_buffer[(u32)y * SCREEN_W + (u32)x] =
        color;
}

static void fill_fullscreen_rect(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    u16 color
)
{
    u32 px;
    u32 py;
    for (py = 0u; py < height; ++py) {
        for (px = 0u; px < width; ++px) {
            put_fullscreen_pixel(
                state, x + (s32)px, y + (s32)py, color
            );
        }
    }
}

static void draw_fullscreen_text(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    const char *text,
    u16 color
)
{
    u32 index;
    for (index = 0u; text[index]; ++index) {
        u32 row;
        for (row = 0u; row < 7u; ++row) {
            u8 bits = glyph_row(text[index], row);
            u32 column;
            for (column = 0u; column < 5u; ++column) {
                if (bits & (1u << (4u - column))) {
                    put_fullscreen_pixel(
                        state,
                        x + (s32)(index * 6u + column),
                        y + (s32)row,
                        color
                    );
                }
            }
        }
    }
}

static void draw_fullscreen_restore_float(
    compat_9588_state_t *state
)
{
    s32 x = state->fullscreen_float_x;
    s32 y = state->fullscreen_float_y;
    fill_fullscreen_rect(
        state,
        x + 2,
        y + 2,
        FULLSCREEN_FLOAT_W,
        FULLSCREEN_FLOAT_H,
        0x0000u
    );
    fill_fullscreen_rect(
        state,
        x,
        y,
        FULLSCREEN_FLOAT_W,
        FULLSCREEN_FLOAT_H,
        0xef9eu
    );
    fill_fullscreen_rect(
        state,
        x + 1,
        y + 1,
        FULLSCREEN_FLOAT_W - 2u,
        FULLSCREEN_FLOAT_H - 2u,
        0x10a2u
    );
    fill_fullscreen_rect(
        state,
        x + 5,
        y + 5,
        FULLSCREEN_FLOAT_W - 10u,
        3u,
        0x2697u
    );
    draw_fullscreen_text(
        state, x + 9, y + 14, "BACK", 0xef9eu
    );
}

static u16 interpolate_rgb565(
    u16 first,
    u16 second,
    u32 weight
)
{
    u32 inverse;
    u32 red;
    u32 green;
    u32 blue;
    if (weight == 0u || first == second) {
        return first;
    }
    inverse = 256u - weight;
    red =
        (((u32)(first >> 11) * inverse) +
         ((u32)(second >> 11) * weight) + 128u) >> 8;
    green =
        ((((u32)first >> 5) & 0x3fu) * inverse +
         (((u32)second >> 5) & 0x3fu) * weight +
         128u) >> 8;
    blue =
        (((u32)(first & 0x1fu) * inverse) +
         ((u32)(second & 0x1fu) * weight) + 128u) >> 8;
    return (u16)((red << 11) | (green << 5) | blue);
}

static void scale_fullscreen_row_bilinear(
    const u16 *source,
    u16 *destination
)
{
    /*
     * 160 source pixels map to 213 destination pixels with both endpoints
     * aligned. The resulting 8.8 fixed-point step is exactly 192 (3/4).
     */
    u32 source_position = 0u;
    u32 output_x;
    for (output_x = 0u;
         output_x < FULLSCREEN_VIEW_W;
         ++output_x) {
        u32 source_x = source_position >> 8;
        u32 next_x =
            source_x + 1u < GUEST_SCREEN_W
                ? source_x + 1u
                : source_x;
        destination[output_x] = interpolate_rgb565(
            source[source_x],
            source[next_x],
            source_position & 0xffu
        );
        source_position +=
            (GUEST_SCREEN_W - 1u) * 256u /
            (FULLSCREEN_VIEW_W - 1u);
    }
}

static void build_fullscreen_framebuffer(
    compat_9588_state_t *state
)
{
    u16 row_storage_a[FULLSCREEN_VIEW_W];
    u16 row_storage_b[FULLSCREEN_VIEW_W];
    u16 *upper_row = row_storage_a;
    u16 *lower_row = row_storage_b;
    u32 upper_source_y = 0u;
    u32 lower_source_y =
        GUEST_SCREEN_H > 1u ? 1u : 0u;
    u32 output_y;
    if (!state || !state->framebuffer ||
        !state->fullscreen_buffer) {
        return;
    }
    /*
     * The 9288S guest surface is 160x240 in the portrait backing store.
     * Keep its 2:3 aspect ratio and scale it to 213x320 in the centre of
     * the 240x320 panel. Two separable RGB565 interpolation passes provide
     * bilinear filtering without floating-point work on the 9588. Only two
     * horizontally scaled source rows are cached on the stack.
     */
    bda_memset(
        state->fullscreen_buffer,
        0,
        SCREEN_W * SCREEN_H * sizeof(u16)
    );
    scale_fullscreen_row_bilinear(
        state->framebuffer + GAME_VIEW_X,
        upper_row
    );
    scale_fullscreen_row_bilinear(
        state->framebuffer +
            lower_source_y * SCREEN_W + GAME_VIEW_X,
        lower_row
    );
    for (output_y = 0u;
         output_y < FULLSCREEN_VIEW_H;
         ++output_y) {
        u32 source_position =
            output_y * (GUEST_SCREEN_H - 1u) * 256u /
            (FULLSCREEN_VIEW_H - 1u);
        u32 source_y = source_position >> 8;
        u32 next_y =
            source_y + 1u < GUEST_SCREEN_H
                ? source_y + 1u
                : source_y;
        u32 vertical_weight = source_position & 0xffu;
        u16 *output =
            state->fullscreen_buffer +
            (output_y + FULLSCREEN_VIEW_Y) * SCREEN_W +
            FULLSCREEN_VIEW_X;
        u32 output_x;
        if (source_y != upper_source_y) {
            if (source_y == lower_source_y) {
                u16 *swap = upper_row;
                upper_row = lower_row;
                lower_row = swap;
            } else {
                scale_fullscreen_row_bilinear(
                    state->framebuffer +
                        source_y * SCREEN_W + GAME_VIEW_X,
                    upper_row
                );
            }
            upper_source_y = source_y;
            lower_source_y = next_y;
            if (lower_source_y != upper_source_y) {
                scale_fullscreen_row_bilinear(
                    state->framebuffer +
                        lower_source_y * SCREEN_W +
                        GAME_VIEW_X,
                    lower_row
                );
            }
        }
        for (output_x = 0u;
             output_x < FULLSCREEN_VIEW_W;
             ++output_x) {
            output[output_x] = interpolate_rgb565(
                upper_row[output_x],
                next_y == source_y
                    ? upper_row[output_x]
                    : lower_row[output_x],
                vertical_weight
            );
        }
    }
    draw_fullscreen_restore_float(state);
}

static int present_native_framebuffer(
    compat_9588_state_t *state
)
{
    u16 *present_pixels;
    void *old_object;
    int draw_result;
    if (!state || !state->framebuffer ||
        !state->native_draw ||
        !state->native_draw_object) {
        return 0;
    }
    /*
     * Match the proven gba-for9588 path: render the complete RGB565 frame
     * straight into the Frame draw context.  A compatible-context copy
     * loses rapidly changing guest regions on both the emulator and device.
     */
    present_pixels =
        state->fullscreen_mode && state->fullscreen_buffer
            ? state->fullscreen_buffer
            : state->framebuffer;
    state->native_picture.source_pixels = present_pixels;
    (void)bda_gui_draw_guard_begin();
    old_object = bda_gui_select_draw_object(
        state->native_draw, state->native_draw_object
    );
    draw_result = bda_gui_render_picture(
        state->native_draw,
        0,
        0,
        SCREEN_W,
        SCREEN_H,
        &state->native_picture
    );
    (void)bda_gui_select_draw_object(
        state->native_draw, old_object
    );
    (void)bda_gui_draw_guard_end();
    g_diagnostic[8] = (u32)draw_result;
    g_diagnostic[9] = 0u;
    g_diagnostic[10] += 1u;
    if (draw_result == 0) {
        state->native_redraw = 0;
        return 1;
    }
    return 0;
}

static void present_framebuffer(compat_9588_state_t *state)
{
    if (!state->framebuffer) {
        return;
    }
    render_listbox_selection(state);
    if (state->fullscreen_mode && state->fullscreen_buffer) {
        build_fullscreen_framebuffer(state);
    } else {
        draw_virtual_controls(state);
    }
    (void)present_native_framebuffer(state);
}

static void log_picv_fault(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    u32 picture,
    u32 virtual_screen,
    u32 mode,
    u32 reason,
    u32 return_pc
)
{
    compat_log_record(
        "PICV_FAULT_GEOMETRY",
        state->selected_path,
        (u32)x,
        (u32)y,
        width,
        height
    );
    compat_log_record(
        "PICV_FAULT_BUFFERS",
        state->selected_path,
        picture,
        virtual_screen,
        mode,
        reason
    );
    compat_log_record(
        "PICV_FAULT_CONTEXT",
        state->selected_path,
        state->vm->pc,
        state->vm->sp,
        return_pc,
        state->vm->fault_address
    );
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
    int has_header = c33_vm_read(
        vm, guest_buffer, image_header, sizeof(image_header)
    );
    int selector_frame;
    u8 packed;
    u32 loaded_byte = 0xffffffffu;

    if (has_header) {
        u32 header_width =
            (u32)image_header[8] | ((u32)image_header[9] << 8);
        u32 header_height =
            (u32)image_header[10] | ((u32)image_header[11] << 8);
        u32 header_payload =
            (u32)image_header[12] |
            ((u32)image_header[13] << 8) |
            ((u32)image_header[14] << 16) |
            ((u32)image_header[15] << 24);
        u32 required_payload =
            compat_gui_packed_2bpp_payload_size(
                header_width, header_height
            );
        /*
         * The SDK image object is authoritative. 9288S applications do not
         * consistently pass a literal pixel width/height here: Five-in-a-row
         * passes 161x241 for a 160x240 frame, negative deltas for small
         * sprites, and x=8,y=40,w=137,h=51 for a valid 144x118 options
         * panel. Using those call-site values as the packed row width shifts
         * every following scanline. The original API obtains the actual
         * 2bpp layout from the 16-byte image header.
         */
        if (header_width && header_height &&
            header_width <= GUEST_SCREEN_W &&
            header_height <= GUEST_SCREEN_H &&
            header_payload >= required_payload) {
            width = header_width;
            height = header_height;
        }
    }
    if (!width || !height ||
        width > GUEST_SCREEN_W || height > GUEST_SCREEN_H ||
        width > 0xffffffffu / height) {
        return 0;
    }
    selector_frame =
        state->parent_hwnd &&
        x == 0 && y == 0 &&
        width == GUEST_SCREEN_W && height == GUEST_SCREEN_H;
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
    if (has_header) {
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
    if (state->instant_paint) {
        present_framebuffer(state);
    }
    return 1;
}

static int legacy_blit_virtual_2bpp(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    u32 picture,
    u32 virtual_screen,
    u32 mode,
    u32 *failure_reason
)
{
    c33_vm_t *vm = state->vm;
    u32 source_stride;
    u32 source_start_x = 0u;
    u32 source_start_y = 0u;
    u32 destination_x;
    u32 destination_y;
    u32 clipped_width;
    u32 clipped_height;
    u32 row;
    u8 source[40];
    u8 destination[40];

    if (failure_reason) {
        *failure_reason = 0u;
    }
    if (!virtual_screen || (!picture && mode != 4u) ||
        !width || !height ||
        width > GUEST_SCREEN_W || height > GUEST_SCREEN_H) {
        if (failure_reason) {
            *failure_reason = PICV_FAULT_INVALID_ARGUMENT;
        }
        return 0;
    }
    /*
     * SysShowPicV accepts signed positions and clips at every virtual-screen
     * edge. 雷霆战机 keeps animating sprites after x reaches 180, and 三国霸业
     * deliberately draws a 160-pixel strip at x=2. Both are valid no-op or
     * clipped draws, not VM faults.
     */
    if (x < 0) {
        source_start_x = (u32)-x;
    }
    if (y < 0) {
        source_start_y = (u32)-y;
    }
    if (source_start_x >= width || source_start_y >= height ||
        x >= (s32)GUEST_SCREEN_W || y >= (s32)GUEST_SCREEN_H) {
        return 1;
    }
    destination_x = x < 0 ? 0u : (u32)x;
    destination_y = y < 0 ? 0u : (u32)y;
    clipped_width = width - source_start_x;
    clipped_height = height - source_start_y;
    if (clipped_width > GUEST_SCREEN_W - destination_x) {
        clipped_width = GUEST_SCREEN_W - destination_x;
    }
    if (clipped_height > GUEST_SCREEN_H - destination_y) {
        clipped_height = GUEST_SCREEN_H - destination_y;
    }
    if (!clipped_width || !clipped_height) {
        return 1;
    }
    source_stride = (width + 3u) / 4u;
    if (source_stride > sizeof(source)) {
        if (failure_reason) {
            *failure_reason = PICV_FAULT_SOURCE_STRIDE;
        }
        return 0;
    }
    for (row = 0u; row < clipped_height; ++row) {
        u32 column;
        u32 destination_address =
            virtual_screen +
            (destination_y + row) * (GUEST_SCREEN_W / 4u);
        if (mode != 4u &&
            !c33_vm_read(
                vm,
                picture + (source_start_y + row) * source_stride,
                source,
                source_stride
            )) {
            vm->fault_address =
                picture + (source_start_y + row) * source_stride;
            if (failure_reason) {
                *failure_reason = PICV_FAULT_SOURCE_READ;
            }
            return 0;
        }
        /*
         * Most 三国霸业 assets are four-pixel-aligned tiles, including its
         * 160x240 frames. Keep those operations byte-wise; doing a VM memory
         * access and branch for every individual pixel makes the intro run
         * hundreds of times slower under the 9588 CPU emulator.
         */
        if (!source_start_x &&
            (destination_x & 3u) == 0u &&
            (clipped_width & 3u) == 0u) {
            u32 first_byte = destination_x / 4u;
            u32 clipped_stride = clipped_width / 4u;
            if (mode == 0u &&
                destination_x == 0u &&
                clipped_width == GUEST_SCREEN_W) {
                if (!c33_vm_write(
                        vm,
                        destination_address,
                        source,
                        clipped_stride
                    )) {
                    vm->fault_address = destination_address;
                    if (failure_reason) {
                        *failure_reason =
                            PICV_FAULT_DESTINATION_WRITE;
                    }
                    return 0;
                }
                continue;
            }
            if (!c33_vm_read(
                    vm,
                    destination_address,
                    destination,
                    sizeof(destination)
                )) {
                vm->fault_address = destination_address;
                if (failure_reason) {
                    *failure_reason =
                        PICV_FAULT_DESTINATION_READ;
                }
                return 0;
            }
            for (column = 0u; column < clipped_stride; ++column) {
                u8 *destination_byte =
                    &destination[first_byte + column];
                /*
                 * The shipped game assets use mode 1 for the sparse sprite
                 * plane and mode 2 for its inverse mask. This is the order
                 * used by the 9288S runtime even though an old SDK comment
                 * labels the two modes the other way around.
                 */
                if (mode == 1u) {
                    *destination_byte |= source[column];
                } else if (mode == 2u) {
                    *destination_byte &= source[column];
                } else if (mode == 3u) {
                    *destination_byte ^= source[column];
                } else if (mode == 4u) {
                    *destination_byte = 0xffu;
                } else {
                    *destination_byte = source[column];
                }
            }
            if (!c33_vm_write(
                    vm,
                    destination_address,
                    destination,
                    sizeof(destination)
            )) {
                vm->fault_address = destination_address;
                if (failure_reason) {
                    *failure_reason =
                        PICV_FAULT_DESTINATION_WRITE;
                }
                return 0;
            }
            continue;
        }
        if (!c33_vm_read(
                vm,
                destination_address,
                destination,
                sizeof(destination)
        )) {
            vm->fault_address = destination_address;
            if (failure_reason) {
                *failure_reason = PICV_FAULT_DESTINATION_READ;
            }
            return 0;
        }
        for (column = 0u; column < clipped_width; ++column) {
            u32 source_x = source_start_x + column;
            u32 output_x = destination_x + column;
            u32 source_shift = 6u - ((source_x & 3u) * 2u);
            u32 destination_shift =
                6u - ((output_x & 3u) * 2u);
            u32 source_pixel =
                mode == 4u ? 3u :
                (source[source_x / 4u] >> source_shift) & 3u;
            u8 *destination_byte =
                &destination[output_x / 4u];
            u32 destination_pixel =
                (*destination_byte >> destination_shift) & 3u;
            u32 output_pixel;

            if (mode == 1u) {
                output_pixel = destination_pixel | source_pixel;
            } else if (mode == 2u) {
                output_pixel = destination_pixel & source_pixel;
            } else if (mode == 3u) {
                output_pixel = destination_pixel ^ source_pixel;
            } else {
                output_pixel = source_pixel;
            }
            *destination_byte =
                (u8)((*destination_byte &
                      (u8)~(u8)(3u << destination_shift)) |
                     (u8)(output_pixel << destination_shift));
        }
        if (!c33_vm_write(
                vm,
                destination_address,
                destination,
                sizeof(destination)
        )) {
            vm->fault_address = destination_address;
            if (failure_reason) {
                *failure_reason = PICV_FAULT_DESTINATION_WRITE;
            }
            return 0;
        }
    }
    return 1;
}

static int legacy_blit_screen_2bpp(
    compat_9588_state_t *state,
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    u32 picture,
    u32 mode
)
{
    static const u16 gray_palette[4] = {
        0x0000u, 0x52aau, 0xad55u, 0xffffu
    };
    c33_vm_t *vm = state->vm;
    u32 row_bytes;
    u32 row;
    u8 packed[40];

    if (!picture || !width || !height ||
        width > GUEST_SCREEN_W ||
        x >= GUEST_SCREEN_W || y >= GUEST_SCREEN_H ||
        width > GUEST_SCREEN_W - x ||
        height > GUEST_SCREEN_H - y) {
        return 0;
    }
    /*
     * 9288S picture and virtual-screen buffers both use four-level grayscale:
     * four pixels per byte, most-significant pair first.
     */
    row_bytes = (width + 3u) / 4u;
    for (row = 0u; row < height; ++row) {
        u32 column;
        u32 address = picture + row * row_bytes;
        if (!c33_vm_read(vm, address, packed, row_bytes)) {
            vm->fault_address = address;
            return 0;
        }
        for (column = 0u; column < width; ++column) {
            u32 shift = 6u - ((column & 3u) * 2u);
            u32 source = (packed[column / 4u] >> shift) & 3u;
            u16 *destination =
                state->framebuffer +
                (y + row) * SCREEN_W + GAME_VIEW_X + x + column;
            u32 output = source;
            if (mode == 4u) {
                output = 3u;
            }
            *destination = gray_palette[output];
        }
    }
    return 1;
}

static int legacy_get_part_picture_data_2bpp(
    compat_9588_state_t *state,
    u32 width,
    u32 height,
    u32 x1,
    u32 y1,
    u32 x2,
    u32 y2,
    u32 source_buffer,
    u32 destination_buffer
)
{
    c33_vm_t *vm = state->vm;
    u32 source_stride;
    u32 destination_width;
    u32 destination_stride;
    u32 row;
    u8 source[40];
    u8 destination[40];

    if (!width || !height ||
        width > GUEST_SCREEN_W || height > GUEST_SCREEN_H ||
        x1 > x2 || y1 > y2 ||
        x2 >= width || y2 >= height ||
        !source_buffer || !destination_buffer) {
        return 0;
    }
    source_stride = (width + 3u) / 4u;
    destination_width = x2 - x1 + 1u;
    destination_stride = (destination_width + 3u) / 4u;
    if (source_stride > sizeof(source) ||
        destination_stride > sizeof(destination)) {
        return 0;
    }
    for (row = 0u; row <= y2 - y1; ++row) {
        u32 column;
        u32 source_address =
            source_buffer + (y1 + row) * source_stride;
        u32 destination_address =
            destination_buffer + row * destination_stride;
        bda_memset(destination, 0, destination_stride);
        if (!c33_vm_read(
                vm, source_address, source, source_stride
            )) {
            vm->fault_address = source_address;
            return 0;
        }
        for (column = 0u; column < destination_width; ++column) {
            u32 source_x = x1 + column;
            u32 source_shift = 6u - ((source_x & 3u) * 2u);
            u32 destination_shift = 6u - ((column & 3u) * 2u);
            u32 pixel =
                (source[source_x / 4u] >> source_shift) & 3u;
            destination[column / 4u] |=
                (u8)(pixel << destination_shift);
        }
        if (!c33_vm_write(
                vm,
                destination_address,
                destination,
                destination_stride
            )) {
            vm->fault_address = destination_address;
            return 0;
        }
    }
    return 1;
}

static int legacy_write_virtual_pixel_2bpp(
    compat_9588_state_t *state,
    u32 virtual_screen,
    s32 x,
    s32 y,
    int set
)
{
    u32 address;
    u8 value;
    u32 shift;
    if (!virtual_screen ||
        x < 0 || y < 0 ||
        x >= GUEST_SCREEN_W || y >= GUEST_SCREEN_H) {
        return 1;
    }
    address =
        virtual_screen +
        (u32)y * (GUEST_SCREEN_W / 4u) +
        (u32)x / 4u;
    shift = 6u - (((u32)x & 3u) * 2u);
    if (!c33_vm_read(state->vm, address, &value, 1u)) {
        state->vm->fault_address = address;
        return 0;
    }
    /*
     * Game glyph helpers supply a transparent monochrome mask. Ink is the
     * inverse of the existing pixel: white on 雷霆战机's black playfield and
     * black on a light menu. Zero bits leave the background untouched.
     */
    if (set) {
        u32 current = (value >> shift) & 3u;
        value =
            (u8)((value & (u8)~(u8)(3u << shift)) |
                 (u8)((3u - current) << shift));
    }
    if (!c33_vm_write(state->vm, address, &value, 1u)) {
        state->vm->fault_address = address;
        return 0;
    }
    return 1;
}

static int legacy_draw_ascii_virtual(
    compat_9588_state_t *state,
    u32 virtual_screen,
    s32 x,
    s32 y,
    u8 character
)
{
    u32 row;
    for (row = 0u; row < 12u; ++row) {
        u8 bits = legacy_small_glyph_row((char)character, row);
        u32 column;
        for (column = 0u; column < 5u; ++column) {
            if (!legacy_write_virtual_pixel_2bpp(
                    state,
                    virtual_screen,
                    x + (s32)column,
                    y + (s32)row,
                    (bits & (1u << (4u - column))) != 0u
                )) {
                return 0;
            }
        }
    }
    return 1;
}

static int legacy_draw_hz_virtual(
    compat_9588_state_t *state,
    u32 virtual_screen,
    s32 x,
    s32 y,
    u8 high,
    u8 low
)
{
    u8 glyph[HZK_GLYPH_SIZE];
    u32 row;
    if (!read_hzk_glyph(state, high, low, glyph)) {
        return 1;
    }
    for (row = 0u; row < 12u; ++row) {
        u16 bits =
            ((u16)glyph[row * 2u] << 8) |
            glyph[row * 2u + 1u];
        u32 column;
        for (column = 0u; column < 12u; ++column) {
            if (!legacy_write_virtual_pixel_2bpp(
                    state,
                    virtual_screen,
                    x + (s32)column,
                    y + (s32)row,
                    (bits & (u16)(0x8000u >> column)) != 0u
                )) {
                return 0;
            }
        }
    }
    return 1;
}

static int legacy_print_string_virtual(
    compat_9588_state_t *state,
    u32 virtual_screen,
    s32 x,
    s32 y,
    u32 guest_string
)
{
    u32 index;
    s32 cursor_x = x;
    for (index = 0u; index < 256u;) {
        u8 ch;
        if (!c33_vm_read(
                state->vm, guest_string + index, &ch, 1u
            )) {
            return 0;
        }
        if (!ch) {
            return 1;
        }
        if (ch == '\r' || ch == '\n') {
            cursor_x = x;
            y += 12;
            ++index;
            continue;
        }
        if (ch >= 0x80u) {
            u8 low;
            if (!c33_vm_read(
                    state->vm, guest_string + index + 1u, &low, 1u
                ) ||
                !legacy_draw_hz_virtual(
                    state, virtual_screen, cursor_x, y, ch, low
                )) {
                return 0;
            }
            cursor_x += 12;
            index += 2u;
        } else {
            if (!legacy_draw_ascii_virtual(
                    state, virtual_screen, cursor_x, y, ch
                )) {
                return 0;
            }
            cursor_x += ch == ' ' ? 4 : 6;
            ++index;
        }
    }
    return 1;
}

static int legacy_present_virtual_2bpp(
    compat_9588_state_t *state,
    u32 virtual_screen
)
{
    static const u16 gray_palette[4] = {
        0x0000u, 0x52aau, 0xad55u, 0xffffu
    };
    c33_vm_t *vm = state->vm;
    u32 row;
    u8 packed[40];

    if (!virtual_screen) {
        return 0;
    }
    for (row = 0u; row < GUEST_SCREEN_H; ++row) {
        u32 source_byte;
        u32 address =
            virtual_screen + row * (GUEST_SCREEN_W / 4u);
        u16 *output =
            state->framebuffer + row * SCREEN_W + GAME_VIEW_X;
        if (!c33_vm_read(vm, address, packed, sizeof(packed))) {
            vm->fault_address = address;
            return 0;
        }
        for (source_byte = 0u;
             source_byte < sizeof(packed);
             ++source_byte) {
            u8 bits = packed[source_byte];
            u16 *pixels = output + source_byte * 4u;
            pixels[0] = gray_palette[(bits >> 6) & 3u];
            pixels[1] = gray_palette[(bits >> 4) & 3u];
            pixels[2] = gray_palette[(bits >> 2) & 3u];
            pixels[3] = gray_palette[bits & 3u];
        }
    }
    if (state->instant_paint) {
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

static void draw_3d_control_frame(
    compat_9588_state_t *state,
    s32 x0,
    s32 y0,
    s32 x1,
    s32 y1,
    u32 fill_color,
    int raised
)
{
    u32 upper_left = raised ? 15u : 7u;
    u32 lower_right = raised ? 7u : 15u;

    if (x1 < x0 || y1 < y0) {
        return;
    }
    fill_guest_rect(
        state,
        (u32)x0,
        (u32)y0,
        (u32)(x1 - x0 + 1),
        (u32)(y1 - y0 + 1),
        fill_color
    );
    draw_line(state, x0, y0, x1, y0, upper_left);
    draw_line(state, x0, y0, x0, y1, upper_left);
    draw_line(state, x0, y1, x1, y1, lower_right);
    draw_line(state, x1, y0, x1, y1, lower_right);
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
    if (ch == 'A') {
        static const u8 glyph[7] = {14,17,17,31,17,17,17};
        return glyph[row];
    }
    if (ch == 'B') {
        static const u8 glyph[7] = {30,17,17,30,17,17,30};
        return glyph[row];
    }
    if (ch == 'C') {
        static const u8 glyph[7] = {14,17,16,16,16,17,14};
        return glyph[row];
    }
    if (ch == 'E') {
        static const u8 glyph[7] = {31,16,16,30,16,16,31};
        return glyph[row];
    }
    if (ch == 'K') {
        static const u8 glyph[7] = {17,18,20,24,20,18,17};
        return glyph[row];
    }
    if (ch == 'O') {
        static const u8 glyph[7] = {14,17,17,17,17,17,14};
        return glyph[row];
    }
    if (ch == 'R') {
        static const u8 glyph[7] = {30,17,17,30,20,18,17};
        return glyph[row];
    }
    if (ch == 'S') {
        static const u8 glyph[7] = {15,16,16,14,1,1,30};
        return glyph[row];
    }
    if (ch == 'T') {
        static const u8 glyph[7] = {31,4,4,4,4,4,4};
        return glyph[row];
    }
    if (ch == 'X') {
        static const u8 glyph[7] = {17,17,10,4,10,17,17};
        return glyph[row];
    }
    return 0;
}

static u8 legacy_small_glyph_row(char ch, u32 row)
{
    /*
     * Font type 0 on the 9288S is a 6x12 ASCII cell.  These digit rows are
     * the original bitmaps from the 9288S HZK library; the visible glyph is
     * five pixels wide with one pixel of character spacing.
     */
    static const u8 digits[10][12] = {
        {0,0,14,17,17,17,17,17,17,14,0,0},
        {0,0,12,4,4,4,4,4,4,31,0,0},
        {0,0,14,17,1,2,4,8,17,31,0,0},
        {0,0,14,17,1,6,1,1,17,14,0,0},
        {0,0,6,10,10,18,18,31,2,7,0,0},
        {0,0,15,8,8,14,1,1,17,14,0,0},
        {0,0,7,8,16,30,17,17,17,14,0,0},
        {0,0,31,17,1,2,2,2,4,4,0,0},
        {0,0,14,17,17,14,17,17,17,14,0,0},
        {0,0,14,17,17,17,15,1,2,28,0,0}
    };
    if (row >= 12u) return 0u;
    if (ch >= '0' && ch <= '9') {
        return digits[ch - '0'][row];
    }
    if (row < 2u || row >= 9u) {
        return 0u;
    }
    return glyph_row(ch, row - 2u);
}

static int read_hzk_glyph(
    compat_9588_state_t *state,
    u8 high,
    u8 low,
    u8 glyph[HZK_GLYPH_SIZE]
)
{
    u32 index;
    u32 offset;
    int count;

    if (high < 0xa1u || high > 0xf7u ||
        low < 0x40u || low == 0x7fu || low == 0xffu) {
        return 0;
    }
    if (!state->hzk_attempted) {
        state->hzk_attempted = 1;
        state->hzk_file = bda_fs_fopen_raw(
            "A:\\\xcf\xb5\xcd\xb3\\\xca\xfd\xbe\xdd\\HZK_LIB.BIN",
            "rb"
        );
        if (!bda_fs_file_is_valid(state->hzk_file)) {
            state->hzk_file = bda_fs_fopen_raw(
                "/\xcf\xb5\xcd\xb3/\xca\xfd\xbe\xdd/HZK_LIB.BIN",
                "rb"
            );
        }
    }
    if (!bda_fs_file_is_valid(state->hzk_file)) {
        return 0;
    }

    index =
        (u32)high * 190u + (u32)low -
        (low < 0x80u ? 0x5ffeu : 0x5fffu);
    offset = HZK_BASE_OFFSET + index * HZK_GLYPH_SIZE;
    /*
     * Unlike ISO C fseek(), the verified 9588 SDK returns the new absolute
     * position on success.  HZK glyphs all live at non-zero offsets.
     */
    if (bda_fs_seek_raw(state->hzk_file, (s32)offset, BDA_SEEK_SET) !=
        (int)offset) {
        return 0;
    }
    count = bda_fs_fread_raw(
        glyph, 1u, HZK_GLYPH_SIZE, state->hzk_file
    );
    return count == (int)HZK_GLYPH_SIZE;
}

static void put_guest_text_pixel(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 color,
    s32 clip_left,
    s32 clip_top,
    s32 clip_right,
    s32 clip_bottom
)
{
    if (x >= clip_left && x < clip_right &&
        y >= clip_top && y < clip_bottom) {
        put_guest_pixel(state, x, y, color);
    }
}

static void fill_guest_text_cell(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    s32 clip_left,
    s32 clip_top,
    s32 clip_right,
    s32 clip_bottom
)
{
    u32 row;
    u32 column;
    if (state->background_mode != 0u) {
        return;
    }
    for (row = 0u; row < height; ++row) {
        for (column = 0u; column < width; ++column) {
            put_guest_text_pixel(
                state,
                x + (s32)column,
                y + (s32)row,
                state->background_color,
                clip_left,
                clip_top,
                clip_right,
                clip_bottom
            );
        }
    }
}

static void draw_guest_text(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    u32 guest_text,
    s32 length,
    s32 clip_left,
    s32 clip_top,
    s32 clip_right,
    s32 clip_bottom
)
{
    u32 index;
    u32 limit = length < 0 ? 512u : (u32)length;
    s32 cursor_x = x;
    s32 cursor_y = y;
    int small_font = state->legacy_font_type == 0u;
    if (limit > 512u) limit = 512u;
    for (index = 0; index < limit;) {
        u8 ch;
        if (!c33_vm_read(state->vm, guest_text + index, &ch, 1u) || !ch) {
            break;
        }
        if (ch == '\r' || ch == '\n') {
            u8 next = 0u;
            if (ch == '\r' && index + 1u < limit) {
                (void)c33_vm_read(
                    state->vm, guest_text + index + 1u, &next, 1u
                );
            }
            index += next == '\n' ? 2u : 1u;
            cursor_x = x;
            cursor_y += small_font ? 12 : 15;
            if (cursor_y >= clip_bottom) {
                break;
            }
            continue;
        }
        if (ch >= 0x80u && index + 1u < limit) {
            u8 low;
            u8 glyph[HZK_GLYPH_SIZE];
            u32 cell_width = small_font ? 12u : 16u;
            if (!c33_vm_read(
                    state->vm, guest_text + index + 1u, &low, 1u
                )) {
                break;
            }
            fill_guest_text_cell(
                state,
                cursor_x,
                cursor_y,
                cell_width,
                12u,
                clip_left,
                clip_top,
                clip_right,
                clip_bottom
            );
            if (read_hzk_glyph(state, ch, low, glyph)) {
                u32 row;
                for (row = 0u; row < 12u; ++row) {
                    u16 bits =
                        ((u16)glyph[row * 2u] << 8) |
                        glyph[row * 2u + 1u];
                    u32 column;
                    u32 width = small_font ? 12u : 16u;
                    for (column = 0u; column < width; ++column) {
                        if (bits & (u16)(0x8000u >> column)) {
                            put_guest_text_pixel(
                                state,
                                cursor_x + (s32)column,
                                cursor_y + (s32)row,
                                state->text_color,
                                clip_left,
                                clip_top,
                                clip_right,
                                clip_bottom
                            );
                        }
                    }
                }
            }
            cursor_x += small_font ? 12 : 16;
            index += 2u;
            continue;
        }
        {
            u32 row;
            u32 row_count = small_font ? 12u : 7u;
            u32 advance = small_font ? 6u : (ch == ' ' ? 6u : 12u);
            fill_guest_text_cell(
                state,
                cursor_x,
                cursor_y,
                advance,
                small_font ? 12u : 14u,
                clip_left,
                clip_top,
                clip_right,
                clip_bottom
            );
            for (row = 0; row < row_count; ++row) {
                u8 bits = small_font
                    ? legacy_small_glyph_row((char)ch, row)
                    : glyph_row((char)ch, row);
                u32 column;
                for (column = 0; column < 5u; ++column) {
                    if (bits & (1u << (4u - column))) {
                        if (small_font) {
                            put_guest_text_pixel(
                                state,
                                cursor_x + (s32)column,
                                cursor_y + (s32)row,
                                state->text_color,
                                clip_left,
                                clip_top,
                                clip_right,
                                clip_bottom
                            );
                        } else {
                            put_guest_text_pixel(
                                state,
                                cursor_x + (s32)(column * 2u),
                                cursor_y + (s32)(row * 2u),
                                state->text_color,
                                clip_left,
                                clip_top,
                                clip_right,
                                clip_bottom
                            );
                            put_guest_text_pixel(
                                state,
                                cursor_x + (s32)(column * 2u + 1u),
                                cursor_y + (s32)(row * 2u),
                                state->text_color,
                                clip_left,
                                clip_top,
                                clip_right,
                                clip_bottom
                            );
                            put_guest_text_pixel(
                                state,
                                cursor_x + (s32)(column * 2u),
                                cursor_y + (s32)(row * 2u + 1u),
                                state->text_color,
                                clip_left,
                                clip_top,
                                clip_right,
                                clip_bottom
                            );
                            put_guest_text_pixel(
                                state,
                                cursor_x + (s32)(column * 2u + 1u),
                                cursor_y + (s32)(row * 2u + 1u),
                                state->text_color,
                                clip_left,
                                clip_top,
                                clip_right,
                                clip_bottom
                            );
                        }
                    }
                }
            }
            cursor_x += (s32)advance;
            ++index;
        }
    }
    if (state->instant_paint) {
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

static int message_is_pending(
    const compat_9588_state_t *state,
    u32 hwnd,
    u32 message
)
{
    u32 index;
    u32 position;
    if (!state) {
        return 0;
    }
    position = state->event_read;
    for (index = 0u; index < state->event_count; ++index) {
        const guest_message_t *event = &state->events[position];
        if (event->hwnd == hwnd && event->message == message) {
            return 1;
        }
        position = (position + 1u) % EVENT_QUEUE_SIZE;
    }
    return 0;
}

static int panel_to_guest(
    const compat_9588_state_t *state,
    u32 panel_x,
    u32 panel_y,
    u32 *guest_x,
    u32 *guest_y
)
{
    (void)state;
    if (panel_y >= GAME_VIEW_H ||
        panel_x < GAME_VIEW_X ||
        panel_x >= GAME_VIEW_X + GAME_VIEW_W) {
        return 0;
    }
    *guest_x = panel_x - GAME_VIEW_X;
    *guest_y = panel_y;
    return 1;
}

static u32 virtual_action_at(
    const compat_9588_state_t *state,
    u32 x,
    u32 y
)
{
    const s32 dpad_x = 78;
    (void)state;

    if (x >= 2u && x < 38u && y >= 82u && y < 134u) {
        return VIRTUAL_ACTION_SELECT;
    }
    if (x >= 202u && x < 238u && y >= 82u && y < 134u) {
        return VIRTUAL_ACTION_SETTINGS;
    }
    if (x >= 2u && x < 38u && y >= 148u && y < 196u) {
        return VIRTUAL_ACTION_FULLSCREEN;
    }
    if ((s32)x >= dpad_x + 28 && (s32)x < dpad_x + 56 &&
        y >= 242u && y < 266u) {
        return VIRTUAL_ACTION_UP;
    }
    if ((s32)x >= dpad_x && (s32)x < dpad_x + 28 &&
        y >= 266u && y < 294u) {
        return VIRTUAL_ACTION_LEFT;
    }
    if ((s32)x >= dpad_x + 56 && (s32)x < dpad_x + 84 &&
        y >= 266u && y < 294u) {
        return VIRTUAL_ACTION_RIGHT;
    }
    if ((s32)x >= dpad_x + 28 && (s32)x < dpad_x + 56 &&
        y >= 294u && y < 318u) {
        return VIRTUAL_ACTION_DOWN;
    }
    if (x >= 4u && x < 60u && y >= 263u && y < 309u) {
        return VIRTUAL_ACTION_BACK;
    }
    if (x >= 180u && x < 236u &&
        y >= 263u && y < 309u) {
        return VIRTUAL_ACTION_CONFIRM;
    }
    return VIRTUAL_ACTION_NONE;
}

static u32 virtual_action_scancode(u32 action)
{
    switch (action) {
    case VIRTUAL_ACTION_UP: return COMPAT_SCANCODE_UP;
    case VIRTUAL_ACTION_DOWN: return COMPAT_SCANCODE_DOWN;
    case VIRTUAL_ACTION_LEFT: return COMPAT_SCANCODE_LEFT;
    case VIRTUAL_ACTION_RIGHT: return COMPAT_SCANCODE_RIGHT;
    case VIRTUAL_ACTION_CONFIRM: return COMPAT_SCANCODE_ENTER;
    case VIRTUAL_ACTION_BACK: return COMPAT_SCANCODE_ESCAPE;
    default: return 0u;
    }
}

static void toggle_control_layout(compat_9588_state_t *state)
{
    state->controls_left = !state->controls_left;
}

static int enter_fullscreen(compat_9588_state_t *state)
{
    if (!state) {
        return 0;
    }
    if (!state->fullscreen_buffer) {
        state->fullscreen_buffer = (u16 *)bda_alloc(
            SCREEN_W * SCREEN_H * sizeof(u16)
        );
        if (allocation_failed(state->fullscreen_buffer)) {
            compat_log_record(
                "FULLSCREEN_ALLOC_FAILED",
                state->selected_path,
                SCREEN_W * SCREEN_H * sizeof(u16),
                (u32)state->fullscreen_buffer,
                0u,
                0u
            );
            state->fullscreen_buffer = 0;
            return 0;
        }
    }
    if (state->fullscreen_float_x < 0 ||
        state->fullscreen_float_x >
            FULLSCREEN_W - FULLSCREEN_FLOAT_W ||
        state->fullscreen_float_y < 0 ||
        state->fullscreen_float_y >
            FULLSCREEN_H - FULLSCREEN_FLOAT_H) {
        state->fullscreen_float_x =
            FULLSCREEN_FLOAT_DEFAULT_X;
        state->fullscreen_float_y =
            FULLSCREEN_FLOAT_DEFAULT_Y;
    }
    state->fullscreen_mode = 1;
    state->fullscreen_dragged = 0;
    state->native_redraw = 1;
    compat_log_record(
        "FULLSCREEN_ENTER",
        state->selected_path,
        (u32)state->fullscreen_float_x,
        (u32)state->fullscreen_float_y,
        FULLSCREEN_W,
        FULLSCREEN_H
    );
    return 1;
}

static void leave_fullscreen(compat_9588_state_t *state)
{
    if (!state || !state->fullscreen_mode) {
        return;
    }
    state->fullscreen_mode = 0;
    state->fullscreen_dragged = 0;
    state->native_redraw = 1;
    compat_log_record(
        "FULLSCREEN_EXIT",
        state->selected_path,
        (u32)state->fullscreen_float_x,
        (u32)state->fullscreen_float_y,
        0u,
        0u
    );
}

static void fullscreen_logical_touch(
    u32 panel_x,
    u32 panel_y,
    s32 *logical_x,
    s32 *logical_y
)
{
    *logical_x = (s32)panel_x;
    *logical_y = (s32)panel_y;
}

static int fullscreen_float_hit(
    const compat_9588_state_t *state,
    s32 x,
    s32 y
)
{
    return x >= state->fullscreen_float_x &&
           x < state->fullscreen_float_x + FULLSCREEN_FLOAT_W &&
           y >= state->fullscreen_float_y &&
           y < state->fullscreen_float_y + FULLSCREEN_FLOAT_H;
}

static s32 clamp_s32(s32 value, s32 minimum, s32 maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void queue_guest_pointer_down(
    compat_9588_state_t *state,
    u32 guest_x,
    u32 guest_y
)
{
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
        state,
        COMPAT_MSG_LBUTTONDOWN,
        guest_x,
        guest_y,
        COMPAT_KEYSTATE_LEFT_BUTTON
    );
}

static void queue_guest_pointer_up(
    compat_9588_state_t *state,
    u32 guest_x,
    u32 guest_y
)
{
    queue_pointer_message(
        state, COMPAT_MSG_LBUTTONUP, guest_x, guest_y, 0u
    );
    if (state->parent_hwnd && guest_y >= 190u) {
        queue_message(
            state,
            state->guest_hwnd,
            COMPAT_MSG_KEYDOWN,
            guest_x < 80u
                ? COMPAT_SCANCODE_ENTER
                : COMPAT_SCANCODE_ESCAPE,
            0u
        );
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
}

static void service_fullscreen_touch_sample(
    compat_9588_state_t *state,
    int down,
    u32 panel_x,
    u32 panel_y
)
{
    s32 logical_x;
    s32 logical_y;
    s32 content_x;
    s32 content_y;
    u32 guest_x;
    u32 guest_y;
    int guest_hit;
    fullscreen_logical_touch(
        panel_x, panel_y, &logical_x, &logical_y
    );
    logical_x = clamp_s32(logical_x, 0, FULLSCREEN_W - 1);
    logical_y = clamp_s32(logical_y, 0, FULLSCREEN_H - 1);
    guest_hit =
        logical_x >= FULLSCREEN_VIEW_X &&
        logical_x < FULLSCREEN_VIEW_X + FULLSCREEN_VIEW_W &&
        logical_y >= FULLSCREEN_VIEW_Y &&
        logical_y < FULLSCREEN_VIEW_Y + FULLSCREEN_VIEW_H;
    content_x = clamp_s32(
        logical_x - FULLSCREEN_VIEW_X,
        0,
        FULLSCREEN_VIEW_W - 1
    );
    content_y = clamp_s32(
        logical_y - FULLSCREEN_VIEW_Y,
        0,
        FULLSCREEN_VIEW_H - 1
    );
    guest_x =
        (u32)content_x * GUEST_SCREEN_W /
        FULLSCREEN_VIEW_W;
    guest_y =
        (u32)content_y * GUEST_SCREEN_H /
        FULLSCREEN_VIEW_H;

    if (down && !state->touch_down) {
        if (fullscreen_float_hit(state, logical_x, logical_y)) {
            state->touch_region = 3u;
            state->fullscreen_touch_start_x = logical_x;
            state->fullscreen_touch_start_y = logical_y;
            state->fullscreen_float_start_x =
                state->fullscreen_float_x;
            state->fullscreen_float_start_y =
                state->fullscreen_float_y;
            state->fullscreen_dragged = 0;
        } else if (guest_hit) {
            queue_guest_pointer_down(state, guest_x, guest_y);
            state->touch_region = 1u;
            state->touch_captured = 1;
            g_diagnostic[14] += 1u;
        }
    } else if (down && state->touch_region == 3u) {
        s32 delta_x =
            logical_x - state->fullscreen_touch_start_x;
        s32 delta_y =
            logical_y - state->fullscreen_touch_start_y;
        if (delta_x > FULLSCREEN_DRAG_THRESHOLD ||
            delta_x < -FULLSCREEN_DRAG_THRESHOLD ||
            delta_y > FULLSCREEN_DRAG_THRESHOLD ||
            delta_y < -FULLSCREEN_DRAG_THRESHOLD) {
            state->fullscreen_dragged = 1;
        }
        state->fullscreen_float_x = clamp_s32(
            state->fullscreen_float_start_x + delta_x,
            0,
            FULLSCREEN_W - FULLSCREEN_FLOAT_W
        );
        state->fullscreen_float_y = clamp_s32(
            state->fullscreen_float_start_y + delta_y,
            0,
            FULLSCREEN_H - FULLSCREEN_FLOAT_H
        );
        state->native_redraw = 1;
    } else if (down && state->touch_region == 1u &&
               (panel_x != state->touch_x ||
                panel_y != state->touch_y)) {
        queue_pointer_message(
            state,
            COMPAT_MSG_MOUSEMOVE,
            guest_x,
            guest_y,
            COMPAT_KEYSTATE_LEFT_BUTTON
        );
    } else if (!down && state->touch_down) {
        if (state->touch_region == 3u) {
            s32 delta_x =
                logical_x - state->fullscreen_touch_start_x;
            s32 delta_y =
                logical_y - state->fullscreen_touch_start_y;
            /*
             * The 9588 can coalesce MOVE and UP into one raw-event pump.
             * Re-evaluate total displacement at release so a real drag is
             * never mistaken for the tap-to-restore gesture.
             */
            if (delta_x > FULLSCREEN_DRAG_THRESHOLD ||
                delta_x < -FULLSCREEN_DRAG_THRESHOLD ||
                delta_y > FULLSCREEN_DRAG_THRESHOLD ||
                delta_y < -FULLSCREEN_DRAG_THRESHOLD) {
                state->fullscreen_dragged = 1;
                state->fullscreen_float_x = clamp_s32(
                    state->fullscreen_float_start_x + delta_x,
                    0,
                    FULLSCREEN_W - FULLSCREEN_FLOAT_W
                );
                state->fullscreen_float_y = clamp_s32(
                    state->fullscreen_float_start_y + delta_y,
                    0,
                    FULLSCREEN_H - FULLSCREEN_FLOAT_H
                );
            }
            if (state->fullscreen_dragged) {
                compat_log_record(
                    "FULLSCREEN_FLOAT_MOVED",
                    state->selected_path,
                    (u32)state->fullscreen_float_x,
                    (u32)state->fullscreen_float_y,
                    (u32)logical_x,
                    (u32)logical_y
                );
            } else {
                leave_fullscreen(state);
            }
        } else if (state->touch_region == 1u) {
            queue_guest_pointer_up(state, guest_x, guest_y);
        }
        state->touch_region = 0u;
        state->virtual_action = VIRTUAL_ACTION_NONE;
        state->touch_captured = 0;
    }
    state->touch_down = down;
    state->touch_x = panel_x;
    state->touch_y = panel_y;
}

static void service_native_touch_sample(
    compat_9588_state_t *state,
    int down,
    u32 panel_x,
    u32 panel_y
)
{
    u32 guest_x;
    u32 guest_y;
    u32 action;

    if (state->fullscreen_mode) {
        service_fullscreen_touch_sample(
            state, down, panel_x, panel_y
        );
        return;
    }

    if (down && !state->touch_down) {
        if (panel_to_guest(
                state, panel_x, panel_y, &guest_x, &guest_y
            )) {
            queue_guest_pointer_down(state, guest_x, guest_y);
            state->touch_region = 1u;
            state->touch_captured = 1;
            g_diagnostic[14] += 1u;
        } else {
            state->virtual_action =
                virtual_action_at(state, panel_x, panel_y);
            state->touch_region = 2u;
        }
    } else if (down && state->touch_region == 1u &&
               (panel_x != state->touch_x ||
                panel_y != state->touch_y) &&
               panel_to_guest(
                   state, panel_x, panel_y, &guest_x, &guest_y
               )) {
        queue_pointer_message(
            state,
            COMPAT_MSG_MOUSEMOVE,
            guest_x,
            guest_y,
            COMPAT_KEYSTATE_LEFT_BUTTON
        );
    } else if (!down && state->touch_down) {
        if (state->touch_region == 1u) {
            if (!panel_to_guest(
                    state, panel_x, panel_y, &guest_x, &guest_y
                )) {
                (void)panel_to_guest(
                    state,
                    panel_x < GAME_VIEW_X
                        ? GAME_VIEW_X
                        : (panel_x >= GAME_VIEW_X + GAME_VIEW_W
                            ? GAME_VIEW_X + GAME_VIEW_W - 1u
                            : panel_x),
                    panel_y < GAME_VIEW_H
                        ? panel_y : GAME_VIEW_H - 1u,
                    &guest_x,
                    &guest_y
                );
            }
            queue_guest_pointer_up(state, guest_x, guest_y);
        } else if (state->touch_region == 2u) {
            action = virtual_action_at(state, panel_x, panel_y);
            if (action == state->virtual_action) {
                u32 scancode = virtual_action_scancode(action);
                if (scancode) {
                    queue_message(
                        state,
                        state->guest_hwnd,
                        COMPAT_MSG_KEYDOWN,
                        scancode,
                        0u
                    );
                } else if (action == VIRTUAL_ACTION_SELECT) {
                    state->request_reselect = 1;
                } else if (action == VIRTUAL_ACTION_SETTINGS) {
                    toggle_control_layout(state);
                } else if (action == VIRTUAL_ACTION_FULLSCREEN) {
                    (void)enter_fullscreen(state);
                }
            }
        }
        state->touch_region = 0u;
        state->virtual_action = VIRTUAL_ACTION_NONE;
        state->touch_captured = 0;
    }
    state->touch_down = down;
    state->touch_x = panel_x;
    state->touch_y = panel_y;
}

static void queue_native_key_down(
    compat_9588_state_t *state,
    u32 scancode
)
{
    int queued;
    if (!scancode) {
        return;
    }
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
    queued = queue_message(
        state,
        state->guest_hwnd,
        COMPAT_MSG_KEYDOWN,
        scancode,
        0u
    );
    g_diagnostic[10] = (u32)(queued != 0);
    g_diagnostic[11] = state->event_count;
    g_diagnostic[12] = scancode;
    g_diagnostic[13] = scancode;
    g_diagnostic[14] = scancode;
    g_diagnostic[15] += 1u;
}

static int native_input_window_proc(
    bda_handle_t handle,
    u32 message,
    u32 wparam,
    u32 lparam
)
{
    compat_9588_state_t *state = g_native_input_state;
    if (state && message == BDA_MSG_DRAW_CONTEXT_ATTACH) {
        state->native_frame = handle;
        if (acquire_native_draw_context(state, handle)) {
            g_native_shared_frame = state->native_frame;
            g_native_shared_draw = state->native_draw;
            g_native_shared_draw_owner = state->native_draw_owner;
            if (!state->native_draw_object) {
                state->native_draw_object =
                    bda_gui_draw_object_create(7u);
            }
            g_native_shared_draw_object = state->native_draw_object;
            state->native_redraw = 1;
        }
    } else if (state &&
               message == BDA_MSG_DRAW_CONTEXT_DETACH) {
        if (!state->native_draw_owner ||
            state->native_draw_owner == handle) {
            release_native_draw_context(state);
            g_native_shared_draw = 0;
            g_native_shared_draw_owner = 0;
        }
        state->native_frame_detached = 1;
    }
    return bda_gui_default_proc(handle, message, wparam, lparam);
}

static int native_input_open(compat_9588_state_t *state)
{
    bda_frame_desc_t descriptor;
    if (!state) {
        return 0;
    }
    bda_memset(&descriptor, 0, sizeof(descriptor));
    bda_memset(
        &state->native_message, 0, sizeof(state->native_message)
    );
    state->native_frame_detached = 0;
    state->native_redraw = 1;
    state->hardware_events_ready = 0;
    state->touch_down = 0;
    state->touch_escape_suppressed = 0;
    state->native_escape_pending = 0;
    bda_memset(state->combo_keys, 0, sizeof(state->combo_keys));
    bda_memset(
        &state->native_picture, 0, sizeof(state->native_picture)
    );
    state->native_picture.width = SCREEN_W;
    state->native_picture.height = SCREEN_H;
    state->native_picture.stride_bytes =
        SCREEN_W * sizeof(u16);
    state->native_picture.source_pixels = state->framebuffer;
    state->native_picture.selected_index = -1;

    if (g_native_shared_frame &&
        (s32)g_native_shared_frame != -1) {
        state->native_frame = g_native_shared_frame;
        state->native_draw = g_native_shared_draw;
        state->native_draw_owner = g_native_shared_draw_owner;
        state->native_draw_object = g_native_shared_draw_object;
        g_native_input_state = state;
        (void)bda_gui_frame_activate(state->native_frame, 0x100u);
        if (!state->native_draw &&
            !acquire_native_draw_context(
                state, state->native_frame
            )) {
            g_native_input_state = 0;
            return 0;
        }
        if (!state->native_draw_object) {
            state->native_draw_object =
                bda_gui_draw_object_create(7u);
        }
        if (!state->native_draw_object ||
            (s32)(u32)state->native_draw_object == -1) {
            g_native_input_state = 0;
            return 0;
        }
        g_native_shared_draw = state->native_draw;
        g_native_shared_draw_owner = state->native_draw_owner;
        g_native_shared_draw_object = state->native_draw_object;
        return 1;
    }

    descriptor.style = 0u;
    descriptor.title = k_native_help_title;
    descriptor.wndproc = native_input_window_proc;
    descriptor.height = SCREEN_W;
    descriptor.width = SCREEN_H;
    g_native_input_state = state;
    state->native_frame = bda_gui_register_frame_desc(&descriptor);
    if (!state->native_frame ||
        (s32)state->native_frame == -1) {
        state->native_frame = 0;
        g_native_input_state = 0;
        return 0;
    }
    (void)bda_gui_frame_activate(state->native_frame, 0x100u);
    if (!acquire_native_draw_context(
            state, state->native_frame
        )) {
        bda_gui_close_frame(state->native_frame);
        state->native_frame = 0;
        g_native_input_state = 0;
        return 0;
    }
    state->native_draw_object =
        bda_gui_draw_object_create(7u);
    if (!state->native_draw_object ||
        (s32)(u32)state->native_draw_object == -1) {
        state->native_draw_object = 0;
        release_native_draw_context(state);
        bda_gui_close_frame(state->native_frame);
        state->native_frame = 0;
        g_native_input_state = 0;
        return 0;
    }
    g_native_shared_frame = state->native_frame;
    g_native_shared_draw = state->native_draw;
    g_native_shared_draw_owner = state->native_draw_owner;
    g_native_shared_draw_object = state->native_draw_object;
    return 1;
}

static void native_input_close(compat_9588_state_t *state)
{
    if (!state) {
        return;
    }
    if (state->native_frame &&
        (s32)state->native_frame != -1) {
        g_native_shared_frame = state->native_frame;
        g_native_shared_draw = state->native_draw;
        g_native_shared_draw_owner = state->native_draw_owner;
        g_native_shared_draw_object = state->native_draw_object;
    }
    if (g_native_input_state == state) {
        g_native_input_state = 0;
    }
    state->native_frame = 0;
    state->native_draw = 0;
    state->native_draw_owner = 0;
    state->native_draw_object = 0;
}

static void native_input_shutdown(void)
{
    bda_gui_message_t message;
    u32 wait;
    if (!g_native_shared_frame ||
        (s32)g_native_shared_frame == -1) {
        return;
    }
    g_native_input_state = 0;
    (void)bda_gui_frame_stop(g_native_shared_frame);
    (void)bda_gui_frame_release(g_native_shared_frame);
    bda_memset(&message, 0, sizeof(message));
    for (wait = 0u; wait < 128u; ++wait) {
        if (!bda_gui_event_pump_frame_once(
                &message, g_native_shared_frame
            )) {
            break;
        }
        bda_sys_delay(1u);
    }
    if (g_native_shared_draw &&
        (s32)g_native_shared_draw != -1) {
        bda_gui_end_draw(g_native_shared_draw);
    }
    bda_gui_close_frame(g_native_shared_frame);
    g_native_shared_frame = 0;
    g_native_shared_draw = 0;
    g_native_shared_draw_owner = 0;
    g_native_shared_draw_object = 0;
}

static void filter_native_touch_escape(
    compat_9588_state_t *state,
    bda_gui_input_packet_t *packet
)
{
    const u32 escape_index = BDA_INPUT_PACKET_ESCAPE_INDEX;
    if (!state->touch_escape_suppressed) {
        return;
    }
    if (!state->touch_down &&
        packet->bytes[escape_index] != 1u) {
        state->touch_escape_suppressed = 0;
    }
    state->native_escape_pending = 0;
    packet->bytes[escape_index] = 0u;
    state->combo_keys[escape_index] = 0u;
}

static void service_hardware_input(compat_9588_state_t *state)
{
    static const u32 portrait_scancodes[
        BDA_GUI_INPUT_PACKET_SIZE
    ] = {
        COMPAT_SCANCODE_RIGHT,
        COMPAT_SCANCODE_LEFT,
        COMPAT_SCANCODE_DOWN,
        COMPAT_SCANCODE_UP,
        COMPAT_SCANCODE_ESCAPE,
        COMPAT_SCANCODE_ENTER
    };
    bda_gui_input_packet_t packet;
    bda_gui_raw_event_t event;
    u32 index;
    u32 drained = 0u;
    int move_pending = 0;

    while (drained < RAW_EVENT_MAX_PER_POLL &&
           bda_gui_raw_event_fetch(&event) >= 0) {
        ++drained;
        ++state->raw_event_count;
        if ((u32)event.code == BDA_INPUT_EVENT_TOUCH_DOWN) {
            u16 x = 0u;
            u16 y = 0u;
            ++state->raw_touch_count;
            state->touch_escape_suppressed = 1;
            bda_gui_touch_position(&x, &y);
            service_native_touch_sample(state, 1, x, y);
            move_pending = 0;
        } else if ((u32)event.code ==
                   BDA_INPUT_EVENT_TOUCH_MOVE) {
            ++state->raw_touch_count;
            if (state->touch_down) {
                move_pending = 1;
            }
        } else if ((u32)event.code ==
                   BDA_INPUT_EVENT_TOUCH_UP) {
            ++state->raw_touch_count;
            state->touch_escape_suppressed = 1;
            if (state->touch_down) {
                u16 x = 0u;
                u16 y = 0u;
                bda_gui_touch_position(&x, &y);
                service_native_touch_sample(state, 0, x, y);
            }
            move_pending = 0;
        }
    }
    if (move_pending && state->touch_down) {
        u16 x = 0u;
        u16 y = 0u;
        bda_gui_touch_position(&x, &y);
        service_native_touch_sample(state, 1, x, y);
    }

    (void)bda_gui_input_packet(&packet);
    filter_native_touch_escape(state, &packet);
    if (!state->hardware_events_ready) {
        /*
         * Discard the launcher navigation and the key that opened this BDA.
         * Only events arriving after the guest is ready belong to the game.
         */
        for (index = 0u;
             index < BDA_GUI_INPUT_PACKET_SIZE;
             ++index) {
            state->combo_keys[index] =
                packet.bytes[index] == 1u ? 1u : 0u;
        }
        state->hardware_events_ready = 1;
    } else {
        for (index = 0u;
             index < BDA_GUI_INPUT_PACKET_SIZE;
             ++index) {
            u8 down = packet.bytes[index] == 1u ? 1u : 0u;
            if (index == BDA_INPUT_PACKET_ESCAPE_INDEX) {
                if (down && !state->combo_keys[index]) {
                    state->native_escape_pending = 1;
                } else if (!down &&
                           state->combo_keys[index] &&
                           state->native_escape_pending) {
                    queue_native_key_down(
                        state, COMPAT_SCANCODE_ESCAPE
                    );
                    state->native_escape_pending = 0;
                }
            } else if (down && !state->combo_keys[index]) {
                queue_native_key_down(
                    state,
                    portrait_scancodes[index]
                );
            }
            state->combo_keys[index] = down;
        }
    }
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

static c33_vm_status_t call_guest_window_proc(
    compat_9588_state_t *state,
    u32 hwnd,
    u32 message,
    u32 wparam,
    u32 lparam
)
{
    c33_vm_status_t status;
    if (!state->guest_window_proc) {
        state->vm->regs[4] = 0u;
        return C33_VM_OK;
    }
    if (message == COMPAT_MSG_PAINT) {
        state->guest_paint_depth++;
    }
    status = c33_vm_call(
        state->vm,
        state->guest_window_proc,
        hwnd,
        message,
        wparam,
        lparam,
        CALLBACK_BUDGET
    );
    if (message == COMPAT_MSG_PAINT && state->guest_paint_depth) {
        state->guest_paint_depth--;
    }
    return status;
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
            char native_path[NATIVE_PATH_CAPACITY];
            char mode[8];
            int file;
            if (!guest_read_c_string(
                    vm, vm->regs[6], guest_path, sizeof(guest_path)
                ) ||
                !guest_read_c_string(
                    vm, vm->regs[7], mode, sizeof(mode)
                )) {
                return C33_VM_FAULT;
            }
            if (!map_guest_path_for_state(
                    state,
                    guest_path, native_path, sizeof(native_path)
                )) {
                vm->regs[4] = 0xffffffffu;
                return C33_VM_OK;
            }
            if (file_mode_may_write(mode)) {
                ensure_native_parent_directories(native_path);
            }
            file = bda_fs_fopen_raw(native_path, mode);
            /*
             * Some packed D300 programs reopen their own EXE to read appended
             * data. Keep the generic root mapping as the primary location,
             * then attach only the currently selected bootstrap image as a
             * read-only fallback. No application or filename is special-cased.
             */
            if (!bda_fs_file_is_valid(file) &&
                !file_mode_may_write(mode) &&
                state->selected_path[0] &&
                (byte_string_contains(guest_path, ".exe") ||
                 byte_string_contains(guest_path, ".EXE"))) {
                file = bda_fs_fopen_raw(state->selected_path, mode);
            }
            vm->regs[4] = (u32)file;
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
    case COMPAT_FS_CLEAR_ERROR:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_FS_REMOVE:
        {
            char guest_path[GUEST_PATH_CAPACITY];
            char native_path[NATIVE_PATH_CAPACITY];
            if (!guest_read_c_string(
                    vm, vm->regs[6], guest_path, sizeof(guest_path)
                )) {
                return C33_VM_FAULT;
            }
            if (!map_guest_path_for_state(
                    state, guest_path, native_path, sizeof(native_path)
                )) {
                vm->regs[4] = 0xffffffffu;
            } else {
                vm->regs[4] = (u32)bda_fs_remove_compat(native_path);
            }
            return C33_VM_OK;
        }
    case COMPAT_FS_RENAME:
        {
            char old_guest[GUEST_PATH_CAPACITY];
            char new_guest[GUEST_PATH_CAPACITY];
            char old_native[NATIVE_PATH_CAPACITY];
            char new_native[NATIVE_PATH_CAPACITY];
            if (!guest_read_c_string(
                    vm, vm->regs[6], old_guest, sizeof(old_guest)
                ) ||
                !guest_read_c_string(
                    vm, vm->regs[7], new_guest, sizeof(new_guest)
                )) {
                return C33_VM_FAULT;
            }
            if (!map_guest_path_for_state(
                    state, old_guest, old_native, sizeof(old_native)
                ) ||
                !map_guest_path_for_state(
                    state, new_guest, new_native, sizeof(new_native)
                )) {
                vm->regs[4] = 0xffffffffu;
            } else {
                ensure_native_parent_directories(new_native);
                vm->regs[4] = (u32)bda_fs_rename_compat(
                    old_native, new_native
                );
            }
            return C33_VM_OK;
        }
    case COMPAT_FS_CHDIR:
        {
            char guest_path[GUEST_PATH_CAPACITY];
            char native_path[NATIVE_PATH_CAPACITY];
            bda_fs_find_data_t find_data;
            int result;
            if (!guest_read_c_string(
                    vm, vm->regs[6], guest_path, sizeof(guest_path)
                )) {
                return C33_VM_FAULT;
            }
            if (!map_guest_path_for_state(
                    state, guest_path, native_path, sizeof(native_path)
                )) {
                vm->regs[4] = 0xffffffffu;
                return C33_VM_OK;
            }
            if (!guest_path[0] ||
                native_path[
                    sizeof(COMPAT_FS_NATIVE_ROOT) - 1u
                ] == 0) {
                result = 0;
            } else {
                result = native_path_info(native_path, &find_data);
                if (result != -1 &&
                    !(find_data.attr_or_flags & 0x10u)) {
                    result = -1;
                }
            }
            if (result != -1 &&
                !set_guest_cwd_from_native(state, native_path)) {
                result = -1;
            }
            vm->regs[4] = (u32)result;
            return C33_VM_OK;
        }
    case COMPAT_FS_MKDIR:
        {
            char guest_path[192];
            char native_path[NATIVE_PATH_CAPACITY];
            if (!guest_read_c_string(
                    vm, vm->regs[6], guest_path, sizeof(guest_path)
                )) {
                return C33_VM_FAULT;
            }
            if (!map_guest_path_for_state(
                    state,
                    guest_path, native_path, sizeof(native_path)
                )) {
                vm->regs[4] = 0xffffffffu;
                return C33_VM_OK;
            }
            ensure_native_directory_tree(native_path);
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_FS_RMDIR:
        {
            char guest_path[GUEST_PATH_CAPACITY];
            char native_path[NATIVE_PATH_CAPACITY];
            if (!guest_read_c_string(
                    vm, vm->regs[6], guest_path, sizeof(guest_path)
                )) {
                return C33_VM_FAULT;
            }
            if (!map_guest_path_for_state(
                    state, guest_path, native_path, sizeof(native_path)
                )) {
                vm->regs[4] = 0xffffffffu;
            } else {
                vm->regs[4] = (u32)bda_fs_rmdir_compat(native_path);
            }
            return C33_VM_OK;
        }
    case COMPAT_FS_GET_FILE_NUMBER:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_FS_FIND_FIRST:
        {
            char guest_path[192];
            char native_path[NATIVE_PATH_CAPACITY];
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
            if (!map_guest_path_for_state(
                    state,
                    guest_path, native_path, sizeof(native_path)
                )) {
                vm->regs[4] = 0xffffffffu;
                return C33_VM_OK;
            }
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
    case COMPAT_FS_GETCWD:
    case COMPAT_FS_GETCWD2:
        {
            u32 length = 0u;
            u32 buffer = vm->regs[6];
            u32 capacity = vm->regs[7];
            while (state->guest_cwd[length]) ++length;
            if (!capacity || length + 1u > capacity) {
                vm->regs[4] = slot == COMPAT_FS_GETCWD
                    ? 0u : 0xffffffffu;
                return C33_VM_OK;
            }
            if (!buffer && slot == COMPAT_FS_GETCWD) {
                buffer = compat_api_heap_alloc(api, length + 1u, 0);
            }
            if (!buffer ||
                !c33_vm_write(
                    vm, buffer, state->guest_cwd, length + 1u
                )) {
                vm->regs[4] = slot == COMPAT_FS_GETCWD
                    ? 0u : 0xffffffffu;
                return C33_VM_OK;
            }
            vm->regs[4] = slot == COMPAT_FS_GETCWD ? buffer : 0u;
            return C33_VM_OK;
        }
    case COMPAT_FS_STAT:
        {
            char guest_path[GUEST_PATH_CAPACITY];
            char native_path[NATIVE_PATH_CAPACITY];
            bda_fs_find_data_t find_data;
            int result;
            if (!guest_read_c_string(
                    vm, vm->regs[6], guest_path, sizeof(guest_path)
                )) {
                return C33_VM_FAULT;
            }
            if (!map_guest_path_for_state(
                    state,
                    guest_path, native_path, sizeof(native_path)
                )) {
                vm->regs[4] = 0xffffffffu;
                return C33_VM_OK;
            }
            result = native_path_info(native_path, &find_data);
            if (result != -1) {
                u16 mode = (find_data.attr_or_flags & 0x10u)
                    ? 0x4000u : 0x8000u;
                if (!guest_write_u16(vm, vm->regs[7] + 0u, 0u) ||
                    !guest_write_u16(vm, vm->regs[7] + 2u, mode) ||
                    !guest_write_u16(vm, vm->regs[7] + 4u, 0u) ||
                    !guest_write_u16(vm, vm->regs[7] + 6u, 0u) ||
                    !guest_write_u32(
                        vm, vm->regs[7] + 8u, find_data.size_or_aux
                    ) ||
                    !guest_write_u32(vm, vm->regs[7] + 12u, 0u) ||
                    !guest_write_u32(vm, vm->regs[7] + 16u, 0u) ||
                    !guest_write_u32(vm, vm->regs[7] + 20u, 0u)) {
                    return C33_VM_FAULT;
                }
            }
            vm->regs[4] = (u32)result;
            return C33_VM_OK;
        }
    case COMPAT_FS_ACCESS:
        {
            char guest_path[GUEST_PATH_CAPACITY];
            char native_path[NATIVE_PATH_CAPACITY];
            bda_fs_find_data_t find_data;
            if (!guest_read_c_string(
                    vm, vm->regs[6], guest_path, sizeof(guest_path)
                )) {
                return C33_VM_FAULT;
            }
            vm->regs[4] =
                map_guest_path_for_state(
                    state,
                    guest_path,
                    native_path,
                    sizeof(native_path)
                ) &&
                native_path_info(native_path, &find_data) != -1
                    ? 0u : 0xffffffffu;
            return C33_VM_OK;
        }
    case COMPAT_FS_INIT:
    case COMPAT_FS_REFRESH_CACHE:
    case COMPAT_FS_UPDATE:
    case COMPAT_FS_FLUSH_CACHE:
        vm->regs[4] = 0u;
        return C33_VM_OK;
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
    state->api_call_count += 1u;
    if (group == COMPAT_API_FS) {
        return dispatch_9588_fs(api, slot, state);
    }
    if (group != COMPAT_API_GUI) {
        return C33_VM_UNSUPPORTED;
    }

    switch (slot) {
    case COMPAT_GUI_DRAW_3D_CONTROL_FRAME:
        {
            u32 y1;
            u32 fill_color;
            u32 raised;
            if (!guest_read_u32(vm, vm->sp + 4u, &y1) ||
                !guest_read_u32(vm, vm->sp + 8u, &fill_color) ||
                !guest_read_u32(vm, vm->sp + 12u, &raised)) {
                return C33_VM_FAULT;
            }
            draw_3d_control_frame(
                state,
                (s32)vm->regs[7],
                (s32)vm->regs[8],
                (s32)vm->regs[9],
                (s32)y1,
                fill_color,
                raised != 0u
            );
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
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
            c33_vm_status_t status;
            guest_message_t event;
            if (!read_guest_message(vm, vm->regs[6], &event)) {
                vm->fault_address = vm->regs[6];
                return C33_VM_FAULT;
            }
            if (event.message == COMPAT_MSG_KEYDOWN) {
                g_diagnostic[10] = 2u;
                g_diagnostic[11] = event.wparam;
                g_diagnostic[12] = event.hwnd;
                g_diagnostic[13] = event.message;
                g_diagnostic[14] = state->guest_window_proc;
            }
            status = call_guest_window_proc(
                state,
                event.hwnd,
                event.message,
                event.wparam,
                event.lparam
            );
            if (event.message == COMPAT_MSG_KEYDOWN) {
                g_diagnostic[10] =
                    0x300u | ((u32)status & 0xffu);
            }
            return status;
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
            compat_log_record(
                "CREATE_MAIN_ENTER",
                state->selected_path,
                vm->regs[6],
                callback,
                state->guest_hwnd,
                state->parent_hwnd
            );
            if (state->guest_hwnd) {
                if (state->parent_hwnd) {
                    compat_log_record(
                        "CREATE_MAIN_NESTING_LIMIT",
                        state->selected_path,
                        state->guest_hwnd,
                        state->parent_hwnd,
                        callback,
                        0u
                    );
                    return C33_VM_UNSUPPORTED;
                }
                state->parent_window_proc = state->guest_window_proc;
                state->parent_hwnd = state->guest_hwnd;
                state->parent_pen_color = state->pen_color;
                state->parent_brush_color = state->brush_color;
                state->parent_background_color =
                    state->background_color;
                state->parent_background_mode =
                    state->background_mode;
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
            compat_log_vm(
                "CREATE_MAIN_AFTER_CREATE", vm, api, state, status
            );
            if (status != C33_VM_OK) return status;
            status = call_guest_window_proc(
                state, state->guest_hwnd, COMPAT_MSG_PAINT, 0u, 0u
            );
            compat_log_vm(
                "CREATE_MAIN_AFTER_PAINT", vm, api, state, status
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
                state->background_mode =
                    state->parent_background_mode;
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
    case COMPAT_GUI_UPDATE_WINDOW:
        present_framebuffer(state);
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_INVALIDATE_RECT:
        /*
         * 9288S MiniGUI invalidates a region; it does not append an unlimited
         * stream of MSG_PAINT records. Coalesce one paint per window and
         * suppress re-invalidation from inside that window's paint callback.
         * Without this, Lianliankan never empties its queue and timers/input
         * cannot be serviced.
         */
        if (state->guest_paint_depth ||
            message_is_pending(
                state, vm->regs[6], COMPAT_MSG_PAINT
            )) {
            vm->regs[4] = 1u;
        } else {
            vm->regs[4] = queue_message(
                state,
                vm->regs[6],
                COMPAT_MSG_PAINT,
                0u,
                0u
            );
        }
        return C33_VM_OK;
    case COMPAT_GUI_BEGIN_PAINT:
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_END_PAINT:
        if (!state->instant_paint) {
            present_framebuffer(state);
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_CLIENT_TO_SCREEN:
        /* The compatibility window occupies the full 160x240 guest area. */
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_SET_TIMER:
        {
            guest_timer_t *timer = 0;
            u32 index;
            for (index = 0u; index < GUEST_TIMER_COUNT; ++index) {
                if (state->timers[index].active &&
                    state->timers[index].hwnd == vm->regs[6] &&
                    state->timers[index].id == vm->regs[7]) {
                    timer = &state->timers[index];
                    break;
                }
            }
            if (!timer) {
                for (index = 0u; index < GUEST_TIMER_COUNT; ++index) {
                    if (!state->timers[index].active) {
                        timer = &state->timers[index];
                        break;
                    }
                }
            }
            if (!timer) {
                vm->regs[4] = 0u;
                return C33_VM_OK;
            }
            timer->hwnd = vm->regs[6];
            timer->id = vm->regs[7];
            /*
             * Official 9288S guiExt.h defines TIME_MS(ms)=ms*10/25:
             * one legacy speed unit is 2.5 ms. Thunder passes 20 (50 ms)
             * and Lianliankan passes 500 (1.25 s).
             */
            timer->interval =
                compat_gui_timer_interval_ms(vm->regs[8]);
            timer->elapsed = 0u;
            timer->active = 1;
            state->timer_last_id = timer->id;
            compat_log_record(
                "TIMER_SET",
                state->selected_path,
                timer->hwnd,
                timer->id,
                vm->regs[8],
                timer->interval
            );
            vm->regs[4] = 1u;
        }
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
        {
            u32 index;
            int removed = 0;
            for (index = 0u; index < GUEST_TIMER_COUNT; ++index) {
                if (state->timers[index].active &&
                    state->timers[index].hwnd == vm->regs[6] &&
                    state->timers[index].id == vm->regs[7]) {
                    state->timers[index].active = 0;
                    removed = 1;
                    break;
                }
            }
            compat_log_record(
                "TIMER_KILL",
                state->selected_path,
                vm->regs[6],
                vm->regs[7],
                (u32)removed,
                index
            );
            vm->regs[4] = (u32)removed;
        }
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
        vm->regs[4] = state->background_mode;
        return C33_VM_OK;
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
        vm->regs[4] = state->background_mode;
        state->background_mode = vm->regs[7] ? 1u : 0u;
        return C33_VM_OK;
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
            if (!guest_read_u32(vm, vm->sp + 4u, &y1)) {
                return C33_VM_FAULT;
            }
            /*
             * 9288S Rectangle draws the outline.  Games use FillBox for a
             * filled region; filling here erases the Five-in-a-row menu text.
             */
            draw_line(state, x0, y0, x1, y0, state->pen_color);
            draw_line(state, x1, y0, x1, (s32)y1, state->pen_color);
            draw_line(state, x1, (s32)y1, x0, (s32)y1, state->pen_color);
            draw_line(state, x0, (s32)y1, x0, y0, state->pen_color);
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_SAVE_SCREEN_BOX:
        {
            u32 guest_buffer;
            if (!guest_read_u32(vm, vm->sp + 4u, &guest_buffer)) {
                return C33_VM_FAULT;
            }
            vm->regs[4] = save_guest_box(
                state,
                vm->regs[6],
                vm->regs[7],
                vm->regs[8],
                vm->regs[9],
                guest_buffer
            ) ? 0u : 0xffffffffu;
            return C33_VM_OK;
        }
    case COMPAT_GUI_PUT_SAVED_BOX_ON_SCREEN:
        {
            u32 guest_buffer;
            if (!guest_read_u32(vm, vm->sp + 4u, &guest_buffer)) {
                return C33_VM_FAULT;
            }
            (void)restore_guest_box(
                state,
                vm->regs[6],
                vm->regs[7],
                vm->regs[8],
                vm->regs[9],
                guest_buffer
            );
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_FILL_BOX:
        {
            u32 height;
            if (!guest_read_u32(vm, vm->sp + 4u, &height)) {
                return C33_VM_FAULT;
            }
            fill_guest_rect(
                state,
                vm->regs[7],
                vm->regs[8],
                vm->regs[9],
                height,
                state->brush_color
            );
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
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
        {
            u32 requested_size = 12u;
            /*
             * The tenth argument is the requested pixel size.  The object is
             * opaque to applications, so a stable non-null compatibility
             * handle is sufficient; SelectFont applies its recorded metrics.
             */
            if (!guest_read_u32(vm, vm->sp + 24u, &requested_size)) {
                return C33_VM_FAULT;
            }
            state->created_log_font = 3u;
            state->created_log_font_size =
                requested_size ? requested_size : 12u;
            vm->regs[4] = state->created_log_font;
            return C33_VM_OK;
        }
    case COMPAT_GUI_DESTROY_LOG_FONT:
        if (vm->regs[6] == state->created_log_font) {
            if (state->selected_log_font == state->created_log_font) {
                state->selected_log_font = 0u;
                state->legacy_font_type = 0u;
            }
            state->created_log_font = 0u;
            state->created_log_font_size = 0u;
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_SELECT_FONT:
        vm->regs[4] = state->selected_log_font;
        state->selected_log_font = vm->regs[7];
        if (vm->regs[7] == state->created_log_font) {
            state->legacy_font_type =
                state->created_log_font_size <= 12u ? 0u : 1u;
        }
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
                (s32)length,
                0,
                0,
                GUEST_SCREEN_W,
                GUEST_SCREEN_H
            );
            vm->regs[4] = 1u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_DRAW_TEXT_EX:
        {
            u32 left;
            u32 top;
            u32 right;
            u32 bottom;
            if (!guest_read_u32(vm, vm->regs[9] + 0u, &left) ||
                !guest_read_u32(vm, vm->regs[9] + 4u, &top) ||
                !guest_read_u32(vm, vm->regs[9] + 8u, &right) ||
                !guest_read_u32(vm, vm->regs[9] + 12u, &bottom)) {
                return C33_VM_FAULT;
            }
            (void)right;
            (void)bottom;
            draw_guest_text(
                state,
                (s32)left,
                (s32)top,
                vm->regs[7],
                (s32)vm->regs[8],
                (s32)left,
                (s32)top,
                (s32)right + 1,
                (s32)bottom + 1
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
                    (GAME_VIEW_H / 2) * SCREEN_W +
                    GAME_VIEW_X +
                    (GAME_VIEW_W / 2)
                ];
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_SET_HDC_FONT:
        state->legacy_font_type = vm->regs[7] & 0xffu;
        vm->regs[4] = 3u;
        return C33_VM_OK;
    case COMPAT_GUI_GET_HDC_FONT:
        vm->regs[4] = state->legacy_font_type;
        return C33_VM_OK;
    case COMPAT_GUI_SET_SYS_FONT:
        state->legacy_font_type = vm->regs[6] & 0xffu;
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_CLEAR_SCREEN:
        fill_guest_rect(
            state, 0u, 0u, GUEST_SCREEN_W, GUEST_SCREEN_H, 15u
        );
        if (state->instant_paint) {
            present_framebuffer(state);
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_SHOW_PICTURE_VIRTUAL:
        {
            u32 picture = 0u;
            u32 virtual_screen = 0u;
            u32 mode = 0u;
            u32 return_pc = 0u;
            u32 failure_reason = 0u;
            int picture_ok =
                guest_read_u32(vm, vm->sp + 4u, &picture);
            int virtual_ok =
                guest_read_u32(vm, vm->sp + 8u, &virtual_screen);
            int mode_ok =
                guest_read_u32(vm, vm->sp + 12u, &mode);
            (void)guest_read_u32(vm, vm->sp, &return_pc);
            if (!picture_ok || !virtual_ok || !mode_ok) {
                u32 read_mask =
                    (picture_ok ? 1u : 0u) |
                    (virtual_ok ? 2u : 0u) |
                    (mode_ok ? 4u : 0u);
                if (!picture_ok) {
                    vm->fault_address = vm->sp + 4u;
                } else if (!virtual_ok) {
                    vm->fault_address = vm->sp + 8u;
                } else {
                    vm->fault_address = vm->sp + 12u;
                }
                log_picv_fault(
                    state,
                    (s16)(u16)vm->regs[6],
                    (s16)(u16)vm->regs[7],
                    vm->regs[8],
                    vm->regs[9],
                    picture,
                    virtual_screen,
                    mode,
                    PICV_FAULT_STACK_ARGUMENTS | read_mask,
                    return_pc
                );
                return C33_VM_FAULT;
            }
            vm->fault_address = 0u;
            if (!legacy_blit_virtual_2bpp(
                    state,
                    (s16)(u16)vm->regs[6],
                    (s16)(u16)vm->regs[7],
                    vm->regs[8],
                    vm->regs[9],
                    picture,
                    virtual_screen,
                    mode,
                    &failure_reason
                )) {
                log_picv_fault(
                    state,
                    (s16)(u16)vm->regs[6],
                    (s16)(u16)vm->regs[7],
                    vm->regs[8],
                    vm->regs[9],
                    picture,
                    virtual_screen,
                    mode,
                    failure_reason,
                    return_pc
                );
                return C33_VM_FAULT;
            }
            vm->regs[4] = 1u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_SHOW_PICTURE_SCREEN:
        {
            u32 height;
            u32 picture;
            u32 mode;
            if (!guest_read_u32(vm, vm->sp + 4u, &height) ||
                !guest_read_u32(vm, vm->sp + 8u, &picture) ||
                !guest_read_u32(vm, vm->sp + 12u, &mode) ||
                !legacy_blit_screen_2bpp(
                    state,
                    vm->regs[7],
                    vm->regs[8],
                    vm->regs[9],
                    height,
                    picture,
                    mode
                )) {
                return C33_VM_FAULT;
            }
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_GET_PART_PICTURE_DATA:
        {
            u32 x2;
            u32 y2;
            u32 source_buffer;
            u32 destination_buffer;
            if (!guest_read_u32(vm, vm->sp + 4u, &x2) ||
                !guest_read_u32(vm, vm->sp + 8u, &y2) ||
                !guest_read_u32(vm, vm->sp + 12u, &source_buffer) ||
                !guest_read_u32(
                    vm, vm->sp + 16u, &destination_buffer
                )) {
                return C33_VM_FAULT;
            }
            vm->regs[4] = legacy_get_part_picture_data_2bpp(
                state,
                vm->regs[6],
                vm->regs[7],
                vm->regs[8],
                vm->regs[9],
                x2,
                y2,
                source_buffer,
                destination_buffer
            ) ? 1u : 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_BLIT_FRAME:
        if (!legacy_present_virtual_2bpp(state, vm->regs[7])) {
            return C33_VM_FAULT;
        }
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_DRAW_ASCII:
        {
            u32 virtual_screen;
            vm->fault_address = vm->sp + 4u;
            if (!guest_read_u32(vm, vm->sp + 4u, &virtual_screen) ||
                (vm->fault_address = virtual_screen,
                 !legacy_draw_ascii_virtual(
                    state,
                    virtual_screen,
                    (s32)vm->regs[7],
                    (s32)vm->regs[8],
                    (u8)vm->regs[9]
                ))) {
                return C33_VM_FAULT;
            }
            vm->fault_address = 0u;
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_DRAW_HZ:
        {
            u32 virtual_screen;
            if (!guest_read_u32(vm, vm->sp + 4u, &virtual_screen) ||
                !legacy_draw_hz_virtual(
                    state,
                    virtual_screen,
                    (s32)vm->regs[7],
                    (s32)vm->regs[8],
                    (u8)(vm->regs[9] >> 8),
                    (u8)vm->regs[9]
                )) {
                return C33_VM_FAULT;
            }
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_PRINT_STRING:
        {
            u32 virtual_screen;
            if (!guest_read_u32(vm, vm->sp + 4u, &virtual_screen) ||
                !legacy_print_string_virtual(
                    state,
                    virtual_screen,
                    (s32)vm->regs[7],
                    (s32)vm->regs[8],
                    vm->regs[9]
                )) {
                return C33_VM_FAULT;
            }
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_CLEAR_RECT:
        {
            u32 bottom;
            s32 left = (s32)vm->regs[7];
            s32 top = (s32)vm->regs[8];
            s32 right = (s32)vm->regs[9];
            if (!guest_read_u32(vm, vm->sp + 4u, &bottom)) {
                return C33_VM_FAULT;
            }
            if (right >= left && (s32)bottom >= top) {
                fill_guest_rect(
                    state,
                    (u32)left,
                    (u32)top,
                    (u32)(right - left + 1),
                    (u32)((s32)bottom - top + 1),
                    state->background_color
                );
            }
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
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
    case COMPAT_GUI_RESET_AUTO_CLOSE_TIMER:
    case COMPAT_GUI_RESET_AUTO_CLOSE_LCD:
    case COMPAT_GUI_RESET_AUTO_CLOSE_LED:
    case COMPAT_GUI_TRACE_INIT:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_REVERSE_RECT:
        {
            u32 height;
            if (!guest_read_u32(vm, vm->sp + 4u, &height)) {
                return C33_VM_FAULT;
            }
            reverse_guest_rect(
                state,
                vm->regs[7],
                vm->regs[8],
                vm->regs[9],
                height
            );
            if (state->instant_paint) {
                present_framebuffer(state);
            }
            vm->regs[4] = 0u;
            return C33_VM_OK;
        }
    case COMPAT_GUI_GET_SCREEN_WIDTH:
        vm->regs[4] = GUEST_SCREEN_W;
        return C33_VM_OK;
    case COMPAT_GUI_GET_SCREEN_HEIGHT:
        vm->regs[4] = GUEST_SCREEN_H;
        return C33_VM_OK;
    case COMPAT_GUI_MUSIC_VOLUME_SET:
    case COMPAT_GUI_SPEECH_OUTPUT_SET:
    case COMPAT_GUI_MUSIC_OUTPUT_SET:
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_MUSIC_VOLUME_GET:
        vm->regs[4] = 5u;
        return C33_VM_OK;
    case COMPAT_GUI_SPEECH_OUTPUT_GET:
    case COMPAT_GUI_MUSIC_OUTPUT_GET:
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_SCAN_GAME_COMBO_KEYS:
        {
            u32 index;
            u32 pressed = 0u;
            if (!c33_vm_write(
                    vm,
                    vm->regs[6],
                    state->combo_keys,
                    sizeof(state->combo_keys)
                )) {
                vm->fault_address = vm->regs[6];
                return C33_VM_FAULT;
            }
            for (index = 0u;
                 index < sizeof(state->combo_keys);
                 ++index) {
                if (state->combo_keys[index]) {
                    pressed = 1u;
                    break;
                }
            }
            vm->regs[4] = pressed;
            return C33_VM_OK;
        }
    case COMPAT_GUI_SET_LANDSCAPE:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_GET_LANDSCAPE:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_ATTACH_ENABLE:
    case COMPAT_GUI_LED:
    case COMPAT_GUI_DISPLAY_BACKLIGHT_STATUS:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_GET_BACKLIGHT_STATUS:
        vm->regs[4] = 1u;
        return C33_VM_OK;
    case COMPAT_GUI_IS_SYSTEM_LOW_POWER:
        vm->regs[4] = 0u;
        return C33_VM_OK;
    case COMPAT_GUI_HELP2:
        {
            char *help = guest_copy_c_string(vm, vm->regs[7], 4096u);
            u32 drained = 0u;
            bda_gui_raw_event_t event;
            if (!help) {
                return C33_VM_FAULT;
            }
            release_native_draw_context(state);
            vm->regs[4] = (u32)bda_help_page(
                0, k_native_help_title, help
            );
            bda_free(help);
            /*
             * The native Help Page temporarily owns the display. Match the
             * GBA port's modal lifecycle: discard its release events, then
             * reactivate the runtime Frame and reacquire the draw context.
             */
            while (drained < 32u &&
                   bda_gui_raw_event_fetch(&event) >= 0) {
                ++drained;
            }
    state->hardware_events_ready = 0;
    state->touch_down = 0;
            state->touch_region = 0u;
            state->touch_escape_suppressed = 0;
            state->native_escape_pending = 0;
            if (state->native_frame) {
                (void)bda_gui_frame_activate(
                    state->native_frame, 0x100u
                );
                (void)acquire_native_draw_context(
                    state, state->native_frame
                );
            }
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
    u32 index;
    if (state->quit) {
        return;
    }
    state->timer_service_count++;
    if (state->timer_service_count == 1u) {
        compat_log_record(
            "TIMER_SERVICE_START",
            state->selected_path,
            elapsed,
            state->event_count,
            state->timer_last_id,
            0u
        );
    }
    for (index = 0u; index < GUEST_TIMER_COUNT; ++index) {
        guest_timer_t *timer = &state->timers[index];
        if (!timer->active) {
            continue;
        }
        timer->elapsed += elapsed;
        if (timer->elapsed >= timer->interval) {
            timer->elapsed -= timer->interval;
            state->timer_fire_count++;
            if (state->timer_fire_count <= 8u) {
                compat_log_record(
                    "TIMER_FIRE",
                    state->selected_path,
                    timer->hwnd,
                    timer->id,
                    timer->interval,
                    state->event_count
                );
            }
            (void)queue_message(
                state,
                timer->hwnd,
                COMPAT_MSG_TIMER,
                timer->id,
                0u
            );
        }
    }
}

static u32 program_read_u32le(const u8 *bytes)
{
    return (u32)bytes[0] |
           ((u32)bytes[1] << 8) |
           ((u32)bytes[2] << 16) |
           ((u32)bytes[3] << 24);
}

static u32 program_byte_string_length(const char *text, u32 capacity)
{
    u32 length = 0u;
    if (!text) {
        return 0u;
    }
    while (length < capacity && text[length]) {
        ++length;
    }
    return length;
}

static int program_has_exe_suffix(const char *name)
{
    u32 length = program_byte_string_length(name, NATIVE_PATH_CAPACITY);
    char e;
    char x;
    char last;
    if (length < 4u || length >= NATIVE_PATH_CAPACITY) {
        return 0;
    }
    e = name[length - 3u];
    x = name[length - 2u];
    last = name[length - 1u];
    if (e >= 'A' && e <= 'Z') e = (char)(e + ('a' - 'A'));
    if (x >= 'A' && x <= 'Z') x = (char)(x + ('a' - 'A'));
    if (last >= 'A' && last <= 'Z') {
        last = (char)(last + ('a' - 'A'));
    }
    return name[length - 4u] == '.' &&
           e == 'e' && x == 'x' && last == 'e';
}

static void program_copy_basename(
    char destination[NATIVE_PATH_CAPACITY],
    const char *source
)
{
    u32 length = program_byte_string_length(source, 0x20au);
    u32 start = 0u;
    u32 index;
    for (index = 0u; index < length; ++index) {
        if (source[index] == '\\' || source[index] == '/') {
            start = index + 1u;
        }
    }
    index = 0u;
    while (start + index < length &&
           index + 1u < NATIVE_PATH_CAPACITY) {
        destination[index] = source[start + index];
        ++index;
    }
    destination[index] = 0;
}

static int program_build_native_path(
    char destination[NATIVE_PATH_CAPACITY],
    const char *file_name
)
{
    const char *root = COMPAT_FS_NATIVE_PROGRAMS_DIRECTORY;
    u32 root_index = 0u;
    u32 index = 0u;
    u32 name_index = 0u;
    while (root[root_index] && index + 1u < NATIVE_PATH_CAPACITY) {
        destination[index++] = root[root_index++];
    }
    while (file_name[name_index] &&
           index + 1u < NATIVE_PATH_CAPACITY) {
        destination[index++] = file_name[name_index++];
    }
    destination[index] = 0;
    return !root[root_index] && !file_name[name_index];
}

static void program_title_from_filename(
    char title[PROGRAM_TITLE_CAPACITY],
    const char *file_name
)
{
    u32 source_length =
        program_byte_string_length(file_name, NATIVE_PATH_CAPACITY);
    u32 limit = source_length;
    u32 source = 0u;
    u32 output = 0u;
    if (source_length >= 4u &&
        program_has_exe_suffix(file_name)) {
        limit -= 4u;
    }
    while (source < limit && output + 1u < PROGRAM_TITLE_CAPACITY) {
        u8 ch = (u8)file_name[source];
        if (ch >= 0x80u) {
            if (source + 1u >= limit ||
                output + 2u >= PROGRAM_TITLE_CAPACITY) {
                break;
            }
            title[output++] = file_name[source++];
            title[output++] = file_name[source++];
        } else {
            title[output++] = file_name[source++];
        }
    }
    while (output && title[output - 1u] == ' ') {
        --output;
    }
    title[output] = 0;
}

static void program_title_from_header(
    char title[PROGRAM_TITLE_CAPACITY],
    const u8 header[D300_MIN_HEADER_SIZE],
    const char *file_name
)
{
    u32 source = 0u;
    u32 output = 0u;
    while (source < 16u && header[0xb0u + source] &&
           output + 1u < PROGRAM_TITLE_CAPACITY) {
        u8 ch = header[0xb0u + source];
        if (ch >= 0x80u) {
            if (source + 1u >= 16u ||
                !header[0xb0u + source + 1u] ||
                output + 2u >= PROGRAM_TITLE_CAPACITY) {
                break;
            }
            title[output++] = (char)ch;
            title[output++] = (char)header[0xb0u + source + 1u];
            source += 2u;
        } else {
            title[output++] = (char)ch;
            ++source;
        }
    }
    while (output && title[output - 1u] == ' ') {
        --output;
    }
    title[output] = 0;
    if (!output) {
        program_title_from_filename(title, file_name);
    }
}

static int read_program_metadata(program_entry_t *entry)
{
    int file;
    int file_size;
    int count;
    u8 header[D300_MIN_HEADER_SIZE];
    u8 icon_block[
        PROGRAM_ICON_HEADER_SIZE + PROGRAM_ICON_PAYLOAD_SIZE
    ];
    d300_image_t image;
    d300_status_t status;

    compat_log_record(
        "SELECTOR_META_OPEN_BEGIN",
        entry->file_name,
        0u,
        0u,
        0u,
        0u
    );
    file = bda_fs_fopen_raw(entry->path, "rb");
    compat_log_record(
        "SELECTOR_META_OPEN_END",
        entry->file_name,
        (u32)file,
        0u,
        0u,
        0u
    );
    if (!bda_fs_file_is_valid(file)) {
        return 0;
    }
    file_size = bda_fs_seek_raw(file, 0, BDA_SEEK_END);
    compat_log_record(
        "SELECTOR_META_SIZE",
        entry->file_name,
        (u32)file_size,
        0u,
        0u,
        0u
    );
    if (file_size < (int)D300_MIN_HEADER_SIZE ||
        (u32)file_size > MAX_D300_FILE_SIZE ||
        bda_fs_seek_raw(file, 0, BDA_SEEK_SET) != 0) {
        (void)bda_fs_close_raw(file);
        return 0;
    }
    count = bda_fs_fread_raw(
        header, 1u, D300_MIN_HEADER_SIZE, file
    );
    if (count != (int)D300_MIN_HEADER_SIZE) {
        (void)bda_fs_close_raw(file);
        return 0;
    }
    bda_memset(&image, 0, sizeof(image));
    status = d300_parse(&image, header, (u32)file_size);
    compat_log_record(
        "SELECTOR_META_PARSE",
        entry->file_name,
        (u32)status,
        image.icon_offset,
        image.icon_size,
        image.program_size
    );
    if (status != D300_OK) {
        (void)bda_fs_close_raw(file);
        return 0;
    }
    program_title_from_header(entry->title, header, entry->file_name);
    entry->has_icon = 0;
    if (image.icon_size >= sizeof(icon_block) &&
        image.icon_offset <= (u32)file_size &&
        sizeof(icon_block) <= (u32)file_size - image.icon_offset &&
        bda_fs_seek_raw(
            file, (s32)image.icon_offset, BDA_SEEK_SET
        ) == (int)image.icon_offset) {
        count = bda_fs_fread_raw(
            icon_block, 1u, sizeof(icon_block), file
        );
        if (count == (int)sizeof(icon_block)) {
            u32 icon_width =
                (u32)icon_block[8] |
                ((u32)icon_block[9] << 8);
            u32 icon_height =
                (u32)icon_block[10] |
                ((u32)icon_block[11] << 8);
            u32 payload_size = program_read_u32le(icon_block + 12u);
            if (icon_width == PROGRAM_ICON_WIDTH &&
                icon_height == PROGRAM_ICON_HEIGHT * 2u &&
                payload_size >= PROGRAM_ICON_PAYLOAD_SIZE) {
                bda_memcpy(
                    entry->icon,
                    icon_block + PROGRAM_ICON_HEADER_SIZE,
                    PROGRAM_ICON_PAYLOAD_SIZE
                );
                entry->has_icon = 1;
            }
        }
    }
    (void)bda_fs_close_raw(file);
    compat_log_record(
        "SELECTOR_META_DONE",
        entry->file_name,
        (u32)entry->has_icon,
        0u,
        0u,
        0u
    );
    return 1;
}

static u32 load_program_entries(
    program_entry_t *entries,
    u32 capacity
)
{
    bda_fs_find_data_t find_data;
    int result;
    int find_open;
    u32 count = 0u;
    bda_fs_find_data_init(&find_data);
    compat_log_record(
        "SELECTOR_FINDFIRST_BEGIN",
        COMPAT_FS_NATIVE_PROGRAMS_DIRECTORY "*.*",
        capacity,
        0x27u,
        sizeof(find_data),
        0u
    );
    result = bda_fs_findfirst(
        COMPAT_FS_NATIVE_PROGRAMS_DIRECTORY "*.*",
        0x27u,
        &find_data
    );
    compat_log_record(
        "SELECTOR_FINDFIRST_END",
        0,
        (u32)result,
        (u32)find_data.cursor,
        find_data.attr_or_flags,
        (u32)(s32)find_data.volume_index
    );
    find_open = result != -1;
    while (result != -1 && count < capacity) {
        program_entry_t *entry = &entries[count];
        int metadata_ok = 0;
        bda_memset(entry, 0, sizeof(*entry));
        program_copy_basename(
            entry->file_name, find_data.name_or_path
        );
        compat_log_record(
            "SELECTOR_SCAN_ITEM",
            entry->file_name,
            count,
            find_data.attr_or_flags,
            find_data.size_or_aux,
            (u32)(s32)find_data.volume_index
        );
        if (!(find_data.attr_or_flags & 0x10u) &&
            program_has_exe_suffix(entry->file_name) &&
            program_build_native_path(
                entry->path, entry->file_name
            )) {
            metadata_ok = read_program_metadata(entry);
        }
        if (metadata_ok) {
            ++count;
        }
        compat_log_record(
            "SELECTOR_FINDNEXT_BEGIN",
            entry->file_name,
            count,
            0u,
            0u,
            0u
        );
        result = bda_fs_findnext(&find_data);
        compat_log_record(
            "SELECTOR_FINDNEXT_END",
            0,
            (u32)result,
            count,
            (u32)find_data.cursor,
            (u32)(s32)find_data.volume_index
        );
    }
    if (find_open) {
        compat_log_record(
            "SELECTOR_FINDCLOSE_BEGIN",
            0,
            count,
            (u32)find_data.cursor,
            0u,
            0u
        );
        (void)bda_fs_findclose(&find_data);
        compat_log_record(
            "SELECTOR_FINDCLOSE_END",
            0,
            count,
            0u,
            0u,
            0u
        );
    }
    return count;
}

static s32 draw_panel_gbk_text(
    compat_9588_state_t *state,
    s32 x,
    s32 y,
    const char *text,
    s32 right,
    u16 color
)
{
    u32 index = 0u;
    s32 cursor = x;
    while (text[index] && cursor < right) {
        u8 ch = (u8)text[index];
        if (ch >= 0x80u && text[index + 1u]) {
            u8 glyph[HZK_GLYPH_SIZE];
            if (cursor + 12 > right) {
                break;
            }
            if (read_hzk_glyph(
                    state, ch, (u8)text[index + 1u], glyph
                )) {
                draw_panel_hz_bitmap(
                    state, cursor, y, glyph, color
                );
            } else {
                draw_panel_text(state, cursor, y + 2, "?", color);
            }
            cursor += 12;
            index += 2u;
        } else {
            char character[2];
            if (cursor + 6 > right) {
                break;
            }
            character[0] = (char)ch;
            character[1] = 0;
            draw_panel_text(state, cursor, y + 2, character, color);
            cursor += 6;
            ++index;
        }
    }
    return cursor;
}

static void draw_program_icon(
    compat_9588_state_t *state,
    const program_entry_t *entry,
    s32 x,
    s32 y,
    int selected
)
{
    static const u16 colors[4] = {
        0x0000u, 0x52aau, 0xad55u, 0xffffu
    };
    u32 row;
    u32 frame_offset =
        selected ? PROGRAM_ICON_FRAME_SIZE : 0u;
    if (!entry->has_icon) {
        fill_panel_rounded_rect(
            state, x, y, PROGRAM_ICON_WIDTH,
            PROGRAM_ICON_HEIGHT, 4u, 0x4208u
        );
        draw_panel_text(state, x + 7, y + 13, "EXE", 0xffffu);
        return;
    }
    for (row = 0u; row < PROGRAM_ICON_HEIGHT; ++row) {
        u32 column;
        for (column = 0u; column < PROGRAM_ICON_WIDTH; ++column) {
            u32 byte_index =
                frame_offset + row * (PROGRAM_ICON_WIDTH / 4u) +
                column / 4u;
            u8 packed = entry->icon[byte_index];
            u32 value =
                (packed >> compat_gui_packed_2bpp_shift(column)) & 3u;
            put_panel_pixel(
                state,
                x + (s32)column,
                y + (s32)row,
                colors[value]
            );
        }
    }
}

static void program_page_text(
    char text[8],
    u32 page,
    u32 page_count
)
{
    u32 output = 0u;
    if (page >= 9u) {
        text[output++] = (char)('0' + ((page + 1u) / 10u) % 10u);
    }
    text[output++] = (char)('0' + (page + 1u) % 10u);
    text[output++] = '/';
    if (page_count >= 10u) {
        text[output++] = (char)('0' + (page_count / 10u) % 10u);
    }
    text[output++] = (char)('0' + page_count % 10u);
    text[output] = 0;
}

static void draw_program_browser(
    compat_9588_state_t *state,
    const program_entry_t *entries,
    u32 count,
    u32 selected
)
{
    u32 page = count ? selected / PROGRAMS_PER_PAGE : 0u;
    u32 page_count =
        count ? (count + PROGRAMS_PER_PAGE - 1u) /
            PROGRAMS_PER_PAGE : 1u;
    u32 first = page * PROGRAMS_PER_PAGE;
    u32 row;
    char page_label[8];

    fill_framebuffer(state, 0xef7du);
    fill_panel_rect(state, 0, 0, SCREEN_W, 34u, 0x18c3u);
    draw_panel_gbk_text(
        state, 10, 10,
        "9288S \xb3\xcc\xd0\xf2",
        180, 0xffffu
    );
    program_page_text(page_label, page, page_count);
    draw_panel_text(state, 205, 13, page_label, 0xffffu);

    for (row = 0u; row < PROGRAMS_PER_PAGE; ++row) {
        u32 index = first + row;
        s32 y = (s32)(PROGRAM_ROW_TOP + row * PROGRAM_ROW_HEIGHT);
        int is_selected = count && index == selected;
        u16 background = is_selected ? 0x2a69u : 0xffffu;
        u16 title_color = is_selected ? 0xffffu : 0x1082u;
        fill_panel_rect(
            state, 6, y, SCREEN_W - 12u,
            PROGRAM_ROW_HEIGHT - 3u, background
        );
        if (index < count) {
            draw_program_icon(
                state, &entries[index], 13, y + 5, is_selected
            );
            draw_panel_gbk_text(
                state, 56, y + 15,
                entries[index].title, 230, title_color
            );
        }
    }

    if (!count) {
        draw_panel_gbk_text(
            state, 30, 115,
            "\xce\xb4\xd5\xd2\xb5\xbd 9288S \xb3\xcc\xd0\xf2",
            220, 0x4208u
        );
        draw_panel_gbk_text(
            state, 42, 143,
            "\xc7\xeb\xbd\xab EXE \xb7\xc5\xc8\xeb",
            215, 0x7befu
        );
        draw_panel_gbk_text(
            state, 18, 165,
            "\xcf\xb5\xcd\xb3\\\xb3\xcc\xd0\xf2",
            220, 0x7befu
        );
    }

    fill_panel_rounded_rect(state, 8, 284, 70u, 28u, 6u, 0x632cu);
    draw_panel_gbk_text(
        state, 31, 291, "\xcd\xcb\xb3\xf6", 70, 0xffffu
    );
    fill_panel_rounded_rect(state, 162, 284, 70u, 28u, 6u, 0x2a69u);
    draw_panel_gbk_text(
        state, 185, 291, "\xcb\xa2\xd0\xc2", 225, 0xffffu
    );
    if (count) {
        draw_panel_gbk_text(
            state, 89, 292, "\xc8\xb7\xc8\xcf\xb4\xf2\xbf\xaa",
            155, 0x4208u
        );
    }
}

static u32 poll_program_browser_key(compat_9588_state_t *state)
{
    static const u32 scancodes[BDA_GUI_INPUT_PACKET_SIZE] = {
        COMPAT_SCANCODE_RIGHT,
        COMPAT_SCANCODE_LEFT,
        COMPAT_SCANCODE_DOWN,
        COMPAT_SCANCODE_UP,
        COMPAT_SCANCODE_ESCAPE,
        COMPAT_SCANCODE_ENTER
    };
    bda_gui_input_packet_t packet;
    u32 index;
    u32 pressed = 0u;

    (void)bda_gui_input_packet(&packet);
    filter_native_touch_escape(state, &packet);
    if (!state->hardware_events_ready) {
        for (index = 0u;
             index < BDA_GUI_INPUT_PACKET_SIZE;
             ++index) {
            state->combo_keys[index] =
                packet.bytes[index] == 1u ? 1u : 0u;
        }
        state->hardware_events_ready = 1;
        return 0u;
    }
    for (index = 0u;
         index < BDA_GUI_INPUT_PACKET_SIZE;
         ++index) {
        u8 down = packet.bytes[index] == 1u ? 1u : 0u;
        if (index == BDA_INPUT_PACKET_ESCAPE_INDEX) {
            if (down && !state->combo_keys[index]) {
                state->native_escape_pending = 1;
            } else if (!down &&
                       state->combo_keys[index] &&
                       state->native_escape_pending) {
                pressed = COMPAT_SCANCODE_ESCAPE;
                state->native_escape_pending = 0;
            }
        } else if (!pressed &&
                   down && !state->combo_keys[index]) {
            pressed = scancodes[index];
        }
        state->combo_keys[index] = down;
    }
    return pressed;
}

static int poll_program_browser_touch(
    compat_9588_state_t *state,
    u32 *x_out,
    u32 *y_out
)
{
    bda_gui_raw_event_t event;
    u32 drained = 0u;
    int move_pending = 0;
    while (drained < RAW_EVENT_MAX_PER_POLL &&
           bda_gui_raw_event_fetch(&event) >= 0) {
        ++drained;
        ++state->raw_event_count;
        if ((u32)event.code == BDA_INPUT_EVENT_TOUCH_DOWN) {
            u16 x = 0u;
            u16 y = 0u;
            ++state->raw_touch_count;
            state->touch_escape_suppressed = 1;
            bda_gui_touch_position(&x, &y);
            state->touch_down = 1;
            state->touch_x = x;
            state->touch_y = y;
            move_pending = 0;
        } else if ((u32)event.code ==
                   BDA_INPUT_EVENT_TOUCH_MOVE) {
            ++state->raw_touch_count;
            if (state->touch_down) {
                move_pending = 1;
            }
        } else if ((u32)event.code ==
                   BDA_INPUT_EVENT_TOUCH_UP) {
            ++state->raw_touch_count;
            state->touch_escape_suppressed = 1;
            if (state->touch_down) {
                u16 x = 0u;
                u16 y = 0u;
                bda_gui_touch_position(&x, &y);
                state->touch_down = 0;
                state->touch_x = x;
                state->touch_y = y;
                *x_out = x;
                *y_out = y;
                return 1;
            }
            move_pending = 0;
        }
    }
    if (move_pending && state->touch_down) {
        u16 x = 0u;
        u16 y = 0u;
        bda_gui_touch_position(&x, &y);
        state->touch_x = x;
        state->touch_y = y;
    }
    return 0;
}

static int select_9288s_program(
    char selected_path[NATIVE_PATH_CAPACITY]
)
{
    compat_9588_state_t *state;
    program_entry_t *entries;
    u16 *framebuffer;
    u32 count;
    u32 selected = 0u;
    u32 heartbeat_tick;
    u32 selector_open_tick;
    int input_armed = 0;
    int dirty = 1;
    int result = 0;

    state = (compat_9588_state_t *)bda_alloc(sizeof(*state));
    entries = (program_entry_t *)bda_alloc(
        sizeof(*entries) * PROGRAM_MAX_ENTRIES
    );
    framebuffer = (u16 *)bda_alloc(SCREEN_W * SCREEN_H * sizeof(u16));
    compat_log_record(
        "SELECTOR_ALLOC",
        0,
        (u32)state,
        (u32)entries,
        (u32)framebuffer,
        sizeof(*entries) * PROGRAM_MAX_ENTRIES
    );
    if (allocation_failed(state) ||
        allocation_failed(entries) ||
        allocation_failed(framebuffer)) {
        if (!allocation_failed(framebuffer)) bda_free(framebuffer);
        if (!allocation_failed(entries)) bda_free(entries);
        if (!allocation_failed(state)) bda_free(state);
        bda_msgbox("9288S", "Not enough memory for program list");
        return -1;
    }
    bda_memset(state, 0, sizeof(*state));
    state->framebuffer = framebuffer;
    state->hzk_file = -1;
    compat_log_record(
        "SELECTOR_SCAN_BEGIN",
        0,
        sizeof(*state),
        SCREEN_W * SCREEN_H * sizeof(u16),
        PROGRAM_MAX_ENTRIES,
        0u
    );
    count = load_program_entries(entries, PROGRAM_MAX_ENTRIES);
    compat_log_record(
        "SELECTOR_SCAN_END",
        0,
        count,
        0u,
        0u,
        0u
    );
    if (!native_input_open(state)) {
        compat_log_record(
            "SELECTOR_FRAME_FAILED",
            0,
            (u32)state->native_frame,
            0u,
            0u,
            0u
        );
        if (state->hzk_attempted &&
            bda_fs_file_is_valid(state->hzk_file)) {
            (void)bda_fs_close_raw(state->hzk_file);
        }
        bda_free(framebuffer);
        bda_free(entries);
        bda_free(state);
        bda_msgbox("9288S", "Could not open input window");
        return -1;
    }
    compat_log_record(
        "SELECTOR_FRAME_OPEN",
        0,
        (u32)state->native_frame,
        (u32)state->native_draw,
        0u,
        (u32)state->native_draw_object
    );
    heartbeat_tick = bda_gui_tick_count_25ms();
    selector_open_tick = heartbeat_tick;

    for (;;) {
        u32 key;
        u32 touch_x = 0u;
        u32 touch_y = 0u;
        int touched;
        int refresh = 0;
        if (dirty) {
            compat_log_record(
                "SELECTOR_DRAW_BEGIN",
                0,
                count,
                selected,
                0u,
                0u
            );
            draw_program_browser(state, entries, count, selected);
            compat_log_record(
                "SELECTOR_PRESENT_BEGIN",
                0,
                count,
                selected,
                0u,
                0u
            );
            (void)present_native_framebuffer(state);
            compat_log_record(
                "SELECTOR_PRESENT_END",
                0,
                g_diagnostic[8],
                g_diagnostic[9],
                g_diagnostic[10],
                (u32)state->native_redraw
            );
            compat_log_record(
                "SELECTOR_READY",
                0,
                count,
                selected,
                (u32)state->framebuffer,
                0u
            );
            dirty = 0;
        }
        if (state->native_redraw) {
            dirty = 1;
        }
        touched = poll_program_browser_touch(
            state, &touch_x, &touch_y
        );
        key = poll_program_browser_key(state);
        /*
         * The touch/key event that launches the BDA may still be queued when
         * the selector frame opens.  Drain it, then require a short released
         * interval before accepting selector input; otherwise that launch
         * touch can silently choose the second row.
         */
        if (!input_armed) {
            u32 now = bda_gui_tick_count_25ms();
            touched = 0;
            key = 0u;
            if (now - selector_open_tick >= 8u &&
                !state->touch_down) {
                input_armed = 1;
                compat_log_record(
                    "SELECTOR_INPUT_ARMED",
                    0,
                    state->raw_event_count,
                    state->raw_touch_count,
                    now - selector_open_tick,
                    0u
                );
            }
        }
        if (key) {
            compat_log_record(
                "SELECTOR_KEY",
                0,
                key,
                selected,
                count,
                0u
            );
        }
        if (touched) {
            compat_log_record(
                "SELECTOR_TOUCH",
                0,
                touch_x,
                touch_y,
                selected,
                count
            );
        }
        if (key == COMPAT_SCANCODE_ESCAPE) {
            result = 0;
            break;
        } else if (key == COMPAT_SCANCODE_ENTER) {
            if (count) {
                copy_native_path(selected_path, entries[selected].path);
                compat_log_record(
                    "SELECTOR_CHOOSE",
                    selected_path,
                    selected,
                    count,
                    key,
                    0u
                );
                result = 1;
                break;
            }
            refresh = 1;
        } else if (count && key == COMPAT_SCANCODE_UP) {
            selected = selected ? selected - 1u : count - 1u;
            dirty = 1;
        } else if (count && key == COMPAT_SCANCODE_DOWN) {
            selected = selected + 1u < count ? selected + 1u : 0u;
            dirty = 1;
        } else if (count && key == COMPAT_SCANCODE_LEFT) {
            if (selected >= PROGRAMS_PER_PAGE) {
                selected -= PROGRAMS_PER_PAGE;
            } else {
                selected = 0u;
            }
            dirty = 1;
        } else if (count && key == COMPAT_SCANCODE_RIGHT) {
            if (selected + PROGRAMS_PER_PAGE < count) {
                selected += PROGRAMS_PER_PAGE;
            } else {
                selected = count - 1u;
            }
            dirty = 1;
        }

        if (touched) {
            if (touch_y >= PROGRAM_ROW_TOP &&
                touch_y <
                    PROGRAM_ROW_TOP +
                    PROGRAMS_PER_PAGE * PROGRAM_ROW_HEIGHT &&
                touch_x >= 6u && touch_x < SCREEN_W - 6u) {
                u32 page = count
                    ? selected / PROGRAMS_PER_PAGE : 0u;
                u32 row =
                    (touch_y - PROGRAM_ROW_TOP) / PROGRAM_ROW_HEIGHT;
                u32 index = page * PROGRAMS_PER_PAGE + row;
                if (index < count) {
                    copy_native_path(
                        selected_path, entries[index].path
                    );
                    compat_log_record(
                        "SELECTOR_CHOOSE",
                        selected_path,
                        index,
                        count,
                        touch_x,
                        touch_y
                    );
                    result = 1;
                    break;
                }
            } else if (touch_y >= 280u &&
                       touch_x >= 8u && touch_x < 78u) {
                result = 0;
                break;
            } else if (touch_y >= 280u &&
                       touch_x >= 162u && touch_x < 232u) {
                refresh = 1;
            }
        }
        if (refresh) {
            compat_log_record(
                "SELECTOR_REFRESH",
                0,
                count,
                selected,
                0u,
                0u
            );
            bda_memset(
                entries, 0,
                sizeof(*entries) * PROGRAM_MAX_ENTRIES
            );
            count = load_program_entries(
                entries, PROGRAM_MAX_ENTRIES
            );
            selected = 0u;
            dirty = 1;
        }
        {
            u32 now = bda_gui_tick_count_25ms();
            if (now - heartbeat_tick >=
                COMPAT_LOG_HEARTBEAT_TICKS) {
                compat_log_record(
                    "SELECTOR_HEARTBEAT",
                    0,
                    count,
                    selected,
                    (u32)state->touch_down,
                    ((state->raw_touch_count & 0xffffu) << 16) |
                        (state->raw_event_count & 0xffffu)
                );
                heartbeat_tick = now;
            }
        }
        bda_sys_delay(1u);
    }

    compat_log_record(
        "SELECTOR_LEAVE",
        result == 1 ? selected_path : 0,
        (u32)result,
        count,
        selected,
        ((state->raw_touch_count & 0xffffu) << 16) |
            (state->raw_event_count & 0xffffu)
    );
    native_input_close(state);
    if (state->hzk_attempted &&
        bda_fs_file_is_valid(state->hzk_file)) {
        (void)bda_fs_close_raw(state->hzk_file);
    }
    bda_free(framebuffer);
    bda_free(entries);
    bda_free(state);
    return result;
}

static u8 *load_d300_file(
    const char *path,
    u32 *size_out,
    u32 *stage_out,
    s32 *detail_out
)
{
    int file;
    int file_size;
    int items_read;
    u8 *bytes;

    *size_out = 0u;
    *stage_out = 1u;
    *detail_out = 0;
    file = bda_fs_fopen_raw(path, "rb");
    *detail_out = file;
    if (!bda_fs_file_is_valid(file)) {
        return 0;
    }
    *stage_out = 2u;
    if (bda_fs_seek_raw(file, 0, BDA_SEEK_END) < 0) {
        (void)bda_fs_close_raw(file);
        return 0;
    }
    *stage_out = 3u;
    file_size = bda_fs_tell_raw(file);
    *detail_out = file_size;
    if (file_size < (int)D300_MIN_HEADER_SIZE ||
        (u32)file_size > MAX_D300_FILE_SIZE) {
        (void)bda_fs_close_raw(file);
        return 0;
    }
    *stage_out = 4u;
    if (bda_fs_seek_raw(file, 0, BDA_SEEK_SET) < 0) {
        (void)bda_fs_close_raw(file);
        return 0;
    }
    *stage_out = 5u;
    bytes = (u8 *)bda_alloc((u32)file_size);
    *detail_out = (s32)(u32)bytes;
    if (allocation_failed(bytes)) {
        (void)bda_fs_close_raw(file);
        return 0;
    }
    *stage_out = 6u;
    items_read = bda_fs_fread_raw(
        bytes, 1u, (u32)file_size, file
    );
    *detail_out = items_read;
    (void)bda_fs_close_raw(file);
    if (items_read != file_size) {
        bda_free(bytes);
        return 0;
    }
    *size_out = (u32)file_size;
    *stage_out = 0u;
    return bytes;
}

static int run_selected_game(
    const char *selected_path,
    int *reselect_out
)
{
    u8 *file_bytes;
    u32 file_size;
    u32 load_stage;
    s32 load_detail;
    u8 *iram = 0;
    u8 *api_ram = 0;
    u8 *heap_ram = 0;
    u8 *code_ram = 0;
    u8 *jit_code = 0;
    /*
     * D300 images only report the initialized program length. 9288S
     * executables also address zero-initialized globals above that range,
     * so keep the entire application window mapped just like the original
     * 9288S SDRAM layout.
     */
    u32 code_size = GUEST_CODE_MAX_SIZE;
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

    *reselect_out = 0;
    compat_log_record(
        "RUN_BEGIN", selected_path, 0u, 0u, 0u, 0u
    );
    file_bytes = load_d300_file(
        selected_path, &file_size, &load_stage, &load_detail
    );
    if (!file_bytes) {
        char text[192];
        char *out = text;
        char *end = text + sizeof(text);
        append_text(&out, end, "Could not load selected EXE\nStage: ");
        append_hex(&out, end, load_stage);
        append_text(&out, end, "\nDetail: ");
        append_hex(&out, end, (u32)load_detail);
        append_text(&out, end, "\nPath: ");
        append_text(&out, end, selected_path);
        *out = 0;
        compat_log_record(
            "LOAD_FAILED",
            selected_path,
            load_stage,
            (u32)load_detail,
            0u,
            0u
        );
        bda_msgbox("9288S", text);
        *reselect_out = 1;
        return 3;
    }
    compat_log_record(
        "LOAD_OK", selected_path, file_size, 0u, 0u, 0u
    );

    g_diagnostic[1] = 1u;
    runtime = (compat_9588_runtime_t *)bda_alloc(sizeof(*runtime));
    if (allocation_failed(runtime)) {
        compat_log_record(
            "ALLOC_RUNTIME_FAILED",
            selected_path,
            sizeof(*runtime),
            (u32)runtime,
            0u,
            0u
        );
        bda_msgbox("9288S compatibility", "Not enough runtime memory");
        bda_free(file_bytes);
        return 1;
    }
    g_diagnostic[1] = 2u;
    bda_memset(runtime, 0, sizeof(*runtime));
    image = &runtime->image;
    vm = &runtime->vm;
    api = &runtime->api;
    state = &runtime->state;
    state->vm = vm;
    copy_native_path(state->selected_path, selected_path);
    state->guest_cwd[0] = '\\';
    state->guest_cwd[1] = 0;
    state->pen_color = 16u;
    state->brush_color = 15u;
    state->background_color = 15u;
    state->background_mode = 0u;
    state->text_color = 16u;
    state->controls_left = 1;
    state->fullscreen_float_x = FULLSCREEN_FLOAT_DEFAULT_X;
    state->fullscreen_float_y = FULLSCREEN_FLOAT_DEFAULT_Y;
    framebuffer = (u16 *)bda_alloc(SCREEN_W * SCREEN_H * 2u);
    if (allocation_failed(framebuffer)) {
        compat_log_record(
            "ALLOC_FRAMEBUFFER_FAILED",
            selected_path,
            SCREEN_W * SCREEN_H * 2u,
            (u32)framebuffer,
            (u32)runtime,
            0u
        );
        bda_msgbox("9288S compatibility", "Not enough display memory");
        bda_free(file_bytes);
        bda_free(runtime);
        return 2;
    }
    state->framebuffer = framebuffer;
    g_diagnostic[1] = 3u;
    fill_framebuffer(state, 0x0000u);
    present_framebuffer(state);

    image_status = d300_parse(image, file_bytes, file_size);
    compat_log_record(
        "D300_PARSED",
        selected_path,
        (u32)image_status,
        image->program_size,
        image->resource_offset,
        image->resource_size
    );
    if (image_status != D300_OK) {
        bda_msgbox("9288S compatibility", d300_status_string(image_status));
        bda_free(file_bytes);
        bda_free(framebuffer);
        bda_free(runtime);
        *reselect_out = 1;
        return 4;
    }
    g_diagnostic[1] = 4u;
    if (image->program_size > GUEST_CODE_MAX_SIZE) {
        bda_msgbox("9288S compatibility", "D300 program is too large");
        bda_free(file_bytes);
        bda_free(framebuffer);
        bda_free(runtime);
        *reselect_out = 1;
        return 5;
    }
    iram = (u8 *)bda_alloc(GUEST_IRAM_SIZE);
    api_ram = (u8 *)bda_alloc(GUEST_API_SIZE);
    heap_ram = (u8 *)bda_alloc(GUEST_HEAP_SIZE);
    code_ram = (u8 *)bda_alloc(code_size);
    if (allocation_failed(iram) ||
        allocation_failed(api_ram) ||
        allocation_failed(heap_ram) ||
        allocation_failed(code_ram)) {
        if (!allocation_failed(iram)) bda_free(iram);
        if (!allocation_failed(api_ram)) bda_free(api_ram);
        if (!allocation_failed(heap_ram)) bda_free(heap_ram);
        if (!allocation_failed(code_ram)) bda_free(code_ram);
        bda_free(file_bytes);
        bda_free(framebuffer);
        bda_free(runtime);
        compat_log_record(
            "ALLOC_GUEST_FAILED",
            selected_path,
            (u32)iram,
            (u32)api_ram,
            (u32)heap_ram,
            (u32)code_ram
        );
        bda_msgbox("9288S compatibility", "Not enough memory for guest RAM");
        return 6;
    }
    compat_log_record(
        "ALLOC_GUEST_OK",
        selected_path,
        (u32)iram,
        (u32)api_ram,
        (u32)heap_ram,
        (u32)code_ram
    );
    g_diagnostic[1] = 5u;
    bda_memset(iram, 0, GUEST_IRAM_SIZE);
    bda_memset(api_ram, 0, GUEST_API_SIZE);
    bda_memset(heap_ram, 0, GUEST_HEAP_SIZE);
    bda_memset(code_ram, 0, code_size);
    bda_memcpy(
        code_ram,
        d300_program(image),
        image->program_size
    );
    bda_free(file_bytes);
    file_bytes = 0;

    c33_vm_init(vm);
    c33_vm_map(vm, 0, iram, GUEST_IRAM_SIZE, 1);
    c33_vm_map(vm, GUEST_API_BASE, api_ram, GUEST_API_SIZE, 1);
    c33_vm_map(vm, GUEST_HEAP_BASE, heap_ram, GUEST_HEAP_SIZE, 1);
    c33_vm_map(vm, GUEST_CODE_BASE, code_ram, code_size, 1);
    compat_api_init(api, vm, GUEST_HEAP_BASE, GUEST_HEAP_END);
    api->dispatch = dispatch_9588;
    api->dispatch_opaque = state;
    if (!compat_api_install(api)) {
        compat_log_record(
            "API_INSTALL_FAILED",
            selected_path,
            api->heap_next,
            api->heap_end,
            0u,
            0u
        );
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

    jit_code = (u8 *)bda_alloc(C33_JIT_CODE_CACHE_SIZE);
    if (!allocation_failed(jit_code)) {
        int jit_enabled = c33_jit_init(
            vm, jit_code, C33_JIT_CODE_CACHE_SIZE
        );
        compat_log_record(
            "JIT_INIT",
            selected_path,
            (u32)jit_enabled,
            (u32)jit_code,
            C33_JIT_CODE_CACHE_SIZE,
            0u
        );
    } else {
        jit_code = 0;
        (void)c33_jit_init(vm, 0, 0u);
        compat_log_record(
            "JIT_ALLOC_FAILED",
            selected_path,
            C33_JIT_CODE_CACHE_SIZE,
            0u,
            0u,
            0u
        );
    }

    if (!native_input_open(state)) {
        compat_log_record(
            "RUN_FRAME_FAILED",
            selected_path,
            (u32)state->native_frame,
            0u,
            0u,
            0u
        );
        bda_msgbox("9288S", "Could not open input window");
        (void)c33_jit_init(0, 0, 0u);
        if (jit_code) bda_free(jit_code);
        bda_free(code_ram);
        bda_free(heap_ram);
        bda_free(api_ram);
        bda_free(iram);
        bda_free(framebuffer);
        bda_free(runtime);
        return 9;
    }
    compat_log_record(
        "RUN_FRAME_OPEN",
        selected_path,
        (u32)state->native_frame,
        0u,
        0u,
        0u
    );
    c33_vm_reset(vm, D300_GUEST_LOAD_BASE, GUEST_STACK_TOP, 0);
    c33_jit_reset(vm);
    vm->sp -= 4u;
    c33_vm_write(vm, vm->sp, exit_pc, sizeof(exit_pc));
    host_tick = bda_gui_tick_count_25ms();
    state->heartbeat_tick = bda_gui_tick_count_25ms();
    g_diagnostic[1] = 7u;
    compat_log_vm("VM_START", vm, api, state, C33_VM_OK);

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
            compat_log_vm("VM_STOP", vm, api, state, vm_status);
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
            bda_sys_delay(1u);
            service_hardware_input(state);
            if (state->request_reselect) {
                vm_status = C33_VM_DONE;
                break;
            }
            current_tick = bda_gui_tick_count_25ms();
            if (current_tick != host_tick) {
                service_timer(
                    state, (current_tick - host_tick) * 25u
                );
                host_tick = current_tick;
            }
            if (current_tick - state->heartbeat_tick >=
                COMPAT_LOG_HEARTBEAT_TICKS) {
                c33_jit_stats_t jit_stats;
                compat_log_vm(
                    "VM_HEARTBEAT", vm, api, state, vm_status
                );
                c33_jit_get_stats(vm, &jit_stats);
                compat_log_record(
                    "JIT_HEARTBEAT",
                    selected_path,
                    jit_stats.blocks_compiled,
                    jit_stats.blocks_verified,
                    jit_stats.verification_failures,
                    jit_stats.last_failed_pc
                );
                compat_log_record(
                    "JIT_HEARTBEAT2",
                    selected_path,
                    jit_stats.cache_hits,
                    jit_stats.cache_misses,
                    jit_stats.fallback_steps,
                    (u32)jit_stats.native_instructions
                );
                compat_log_record(
                    "JIT_HEARTBEAT3",
                    selected_path,
                    jit_stats.negative_hits,
                    jit_stats.compile_failures,
                    (u32)jit_stats.lookup_probes,
                    jit_stats.max_lookup_probes
                );
                state->heartbeat_tick = current_tick;
            }
        }
        present_framebuffer(state);
    }

    native_input_close(state);
    {
        c33_jit_stats_t jit_stats;
        c33_jit_get_stats(vm, &jit_stats);
        compat_log_record(
            "JIT_STATS_A",
            selected_path,
            jit_stats.enabled,
            jit_stats.blocks_compiled,
            jit_stats.cache_hits,
            jit_stats.cache_misses
        );
        compat_log_record(
            "JIT_STATS_B",
            selected_path,
            jit_stats.cache_used,
            jit_stats.cache_flushes,
            jit_stats.blocks_verified,
            jit_stats.verification_failures
        );
        compat_log_record(
            "JIT_STATS_C",
            selected_path,
            jit_stats.last_failed_pc,
            jit_stats.fallback_steps,
            (u32)jit_stats.native_instructions,
            (u32)(jit_stats.native_instructions >> 32)
        );
        compat_log_record(
            "JIT_STATS_D",
            selected_path,
            (u32)vm->instructions,
            (u32)(vm->instructions >> 32),
            jit_stats.cache_size,
            0u
        );
        compat_log_record(
            "JIT_STATS_E",
            selected_path,
            jit_stats.negative_hits,
            jit_stats.compile_failures,
            (u32)jit_stats.lookup_probes,
            jit_stats.max_lookup_probes
        );
    }
    if (vm_status == C33_VM_DONE &&
        !state->quit &&
        !state->request_reselect) {
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
    if (state->hzk_attempted &&
        bda_fs_file_is_valid(state->hzk_file)) {
        (void)bda_fs_close_raw(state->hzk_file);
        state->hzk_file = -1;
    }
    (void)c33_jit_init(0, 0, 0u);
    if (jit_code) bda_free(jit_code);
    bda_free(code_ram);
    bda_free(heap_ram);
    bda_free(api_ram);
    bda_free(iram);
    if (state->fullscreen_buffer) {
        bda_free(state->fullscreen_buffer);
        state->fullscreen_buffer = 0;
    }
    bda_free(framebuffer);
    state->framebuffer = 0;
    *reselect_out = state->request_reselect;
    compat_log_record(
        "INPUT_STATS",
        selected_path,
        state->raw_event_count,
        state->raw_touch_count,
        (u32)state->hardware_events_ready,
        (u32)state->touch_down
    );
    compat_log_record(
        "RUN_END",
        selected_path,
        (u32)vm_status,
        (u32)state->request_reselect,
        (u32)state->quit,
        state->api_call_count
    );
    bda_free(runtime);
    return vm_status == C33_VM_DONE ? 0 : 8;
}

__attribute__((section(".text.bda_main")))
int bda_main(void)
{
    char *selected_path;
    int selector_result;
    int run_result = 0;
    int reselect = 0;

    selected_path = (char *)bda_alloc(NATIVE_PATH_CAPACITY);
    if (allocation_failed(selected_path)) {
        bda_msgbox("9288S", "Not enough memory for program path");
        return 1;
    }
    ensure_native_directory_tree(COMPAT_FS_NATIVE_PROGRAMS_ROOT);
    compat_log_write(
        "9288S compatibility real-device log v10\r\n", 1
    );
    compat_log_record(
        "BDA_START",
        COMPAT_FS_NATIVE_ROOT,
        (u32)selected_path,
        sizeof(compat_9588_runtime_t),
        VM_SLICE,
        CALLBACK_BUDGET
    );
    compat_log_record(
        "INPUT_ARCH",
        "GBA raw touch + input packet",
        RAW_EVENT_MAX_PER_POLL,
        0u,
        0u,
        0u
    );
    for (;;) {
        compat_log_record(
            "SELECTOR_ENTER", 0, 0u, 0u, 0u, 0u
        );
        selector_result = select_9288s_program(selected_path);
        compat_log_record(
            "SELECTOR_RESULT",
            selector_result == 1 ? selected_path : 0,
            (u32)selector_result,
            0u,
            0u,
            0u
        );
        if (selector_result != 1) {
            if (selector_result < 0) run_result = 2;
            break;
        }
        run_result = run_selected_game(selected_path, &reselect);
        if (!reselect) {
            break;
        }
    }
    compat_log_record(
        "BDA_END", 0, (u32)run_result, (u32)reselect, 0u, 0u
    );
    native_input_shutdown();
    bda_free(selected_path);
    return run_result;
}

/* The existing one-source BDA builder compiles a single translation unit. */
#include "../../runtime/src/d300.c"
#include "../../runtime/src/c33vm.c"
#include "../../runtime/src/c33jit.c"
#include "../../runtime/src/compat_api.c"
#include "../../runtime/src/compat_fs.c"
