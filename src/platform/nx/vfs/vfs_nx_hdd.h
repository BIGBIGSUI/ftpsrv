// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "vfs_nx_stdio.h"
#include <stddef.h>
#include <dirent.h>
#include <switch.h>

struct VfsHddFile {
    struct VfsStdioFile stdio_file;
};

struct VfsHddDir {
    struct VfsStdioDir stdio_dir;
    size_t index;
    bool is_valid;
};

struct VfsHddDirEntry {
    struct VfsStdioDirEntry stdio_dir;
};

struct FtpVfs;
extern const struct FtpVfs g_vfs_hdd;

Result vfs_hdd_init(void);
void vfs_hdd_exit(void);

#ifdef __cplusplus
}
#endif
