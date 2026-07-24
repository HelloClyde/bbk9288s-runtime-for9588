#ifndef BBK9288S_COMPAT_FS_H
#define BBK9288S_COMPAT_FS_H

#include <stddef.h>

/*
 * The 9588 directory below is the complete virtual 9288S filesystem root.
 * Paths remain GBK byte strings so original Chinese directory and file names
 * are preserved without conversion.
 */
#define COMPAT_FS_NATIVE_APPLICATIONS_ROOT \
    "A:\\\xd3\xa6\xd3\xc3"
#define COMPAT_FS_NATIVE_DATA_ROOT \
    COMPAT_FS_NATIVE_APPLICATIONS_ROOT "\\\xca\xfd\xbe\xdd"
#define COMPAT_FS_NATIVE_ROOT \
    COMPAT_FS_NATIVE_DATA_ROOT "\\9288s"
#define COMPAT_FS_NATIVE_ROOT_DIRECTORY \
    COMPAT_FS_NATIVE_ROOT "\\"

/*
 * The 9288S and 9588 firmware file tables use the same stdio-like ordering
 * for their core entries.  Buffers still need copying because a 9288S guest
 * address is not a native BDA pointer.
 */
enum compat_fs_slot {
    COMPAT_FS_OPEN = 0,
    COMPAT_FS_CLOSE = 1,
    COMPAT_FS_READ = 2,
    COMPAT_FS_WRITE = 3,
    COMPAT_FS_SEEK = 4,
    COMPAT_FS_TELL = 5,
    COMPAT_FS_EOF = 6,
    COMPAT_FS_ERROR = 7,
    COMPAT_FS_FIND_FIRST = 15,
    COMPAT_FS_FIND_NEXT = 16,
    COMPAT_FS_FIND_CLOSE = 17,
    COMPAT_FS_DISK_INFO = 18,
    COMPAT_FS_STAT = 27
};

/*
 * Map an absolute or relative 9288S path into the private 9588 root. Both
 * slash forms are accepted, "." is removed, and ".." may never escape root.
 * Returns non-zero on success and zero for an invalid or truncated path.
 */
int compat_fs_map_guest_path(
    const char *guest_path,
    char *native_path,
    size_t native_capacity
);

#endif
