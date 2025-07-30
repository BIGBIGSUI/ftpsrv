// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/stat.h>
#include <switch.h>

struct VfsStorageFile {
    FsStorage s;
    s64 off;
    s64 size;
    FsBisPartitionId id;
    bool is_valid;
};

struct VfsStorageDir {
    size_t index;
    bool is_valid;
};

struct VfsStorageDirEntry {
    FsBisPartitionId id;
};

int vfs_storage_set_errno(Result rc);
int vfs_storage_internal_open(FsStorage* s, struct VfsStorageFile* f, const char* path, enum FtpVfsOpenMode mode);
int vfs_storage_internal_read(struct VfsStorageFile* f, void* buf, size_t size);
int vfs_storage_internal_seek(struct VfsStorageFile* f, size_t off);
int vfs_storage_internal_close(struct VfsStorageFile* f);
int vfs_storage_internal_isfile_open(struct VfsStorageFile* f);

int vfs_storage_internal_stat(FsStorage* s, const char* path, struct stat* st);
int vfs_storage_internal_lstat(FsStorage* s, const char* path, struct stat* st);

void vfs_storage_init(void);
void vfs_storage_exit(void);

struct FtpVfs;
extern const struct FtpVfs g_vfs_storage;

#ifdef __cplusplus
}
#endif
