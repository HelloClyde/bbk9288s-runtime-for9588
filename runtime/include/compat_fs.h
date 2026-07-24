#ifndef BBK9288S_COMPAT_FS_H
#define BBK9288S_COMPAT_FS_H

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

#endif
