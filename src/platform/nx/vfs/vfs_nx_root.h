// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

struct VfsDeviceEntry {
    char name[32];
};

struct VfsRootFile {
    bool padding;
};

struct VfsRootDir {
    size_t index;
    bool is_valid;
};

struct VfsRootDirEntry {
    char buf[64];
};

struct FtpVfs;
extern const struct FtpVfs g_vfs_root;

void vfs_root_init(const struct VfsDeviceEntry* entries, const u32* count);
void vfs_root_exit(void);

#ifdef __cplusplus
}
#endif
