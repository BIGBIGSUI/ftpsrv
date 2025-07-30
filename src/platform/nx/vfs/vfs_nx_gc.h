// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <switch.h>
#include "vfs_nx_fs.h"

enum GcDirType {
    GcDirType_Invalid,
    GcDirType_Root,
    GcDirType_App,
    GcDirType_Cert,
};

struct VfsGcFileRaw {
    const u8* ptr;
    u32 size;
    u32 offset;
};

struct VfsGcFile {
    enum GcDirType type;
    union {
        struct VfsFsFile fs_file;
        struct VfsGcFileRaw raw;
    };
    bool is_valid;
};

struct VfsGcDir {
    enum GcDirType type;
    union {
        struct VfsFsDir fs_dir;
    };
    size_t index;
    bool is_valid;
};

struct VfsGcDirEntry {
    union {
        struct VfsFsDirEntry fs_entry;
        char name[512 + 128];
    };
};

struct FtpVfs;
extern const struct FtpVfs g_vfs_gc;

Result vfs_gc_init(void);
void vfs_gc_exit(void);

#ifdef __cplusplus
}
#endif
