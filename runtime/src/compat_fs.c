#include "../include/compat_fs.h"

static int compat_fs_is_separator(char value)
{
    return value == '\\' || value == '/';
}

static int compat_fs_is_drive_letter(char value)
{
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

static size_t compat_fs_string_length(const char *text)
{
    size_t length = 0u;
    while (text && text[length]) {
        ++length;
    }
    return length;
}

static int compat_fs_copy_root(
    char *native_path,
    size_t native_capacity,
    size_t *length_out
)
{
    const char *root = COMPAT_FS_NATIVE_ROOT;
    size_t length = compat_fs_string_length(root);
    size_t index;

    if (!native_path || !length_out || native_capacity <= length) {
        return 0;
    }
    for (index = 0u; index < length; ++index) {
        native_path[index] = root[index];
    }
    native_path[length] = 0;
    *length_out = length;
    return 1;
}

int compat_fs_map_guest_path(
    const char *guest_path,
    char *native_path,
    size_t native_capacity
)
{
    const char *source;
    size_t root_length;
    size_t native_length;

    if (!guest_path ||
        !compat_fs_copy_root(
            native_path, native_capacity, &root_length
        )) {
        return 0;
    }
    native_length = root_length;
    source = guest_path;

    if (compat_fs_is_drive_letter(source[0]) && source[1] == ':') {
        source += 2;
    }
    while (compat_fs_is_separator(*source)) {
        ++source;
    }

    while (*source) {
        const char *component = source;
        size_t component_length = 0u;
        size_t index;

        while (source[component_length] &&
               !compat_fs_is_separator(source[component_length])) {
            ++component_length;
        }
        source += component_length;
        while (compat_fs_is_separator(*source)) {
            ++source;
        }

        if (component_length == 0u ||
            (component_length == 1u && component[0] == '.')) {
            continue;
        }
        if (component_length == 2u &&
            component[0] == '.' &&
            component[1] == '.') {
            if (native_length <= root_length) {
                native_path[0] = 0;
                return 0;
            }
            while (native_length > root_length &&
                   native_path[native_length - 1u] != '\\') {
                --native_length;
            }
            if (native_length > root_length) {
                --native_length;
            }
            native_path[native_length] = 0;
            continue;
        }
        for (index = 0u; index < component_length; ++index) {
            if (component[index] == ':') {
                native_path[0] = 0;
                return 0;
            }
        }
        if (native_length + 1u + component_length >= native_capacity) {
            native_path[0] = 0;
            return 0;
        }
        native_path[native_length++] = '\\';
        for (index = 0u; index < component_length; ++index) {
            native_path[native_length++] = component[index];
        }
        native_path[native_length] = 0;
    }
    return 1;
}
