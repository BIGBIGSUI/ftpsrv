// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <dirent.h>

struct VfsStdioFile {
    int fd;
    int valid;
};

struct VfsStdioDir {
    DIR* fd;
    bool is_valid;
};

struct VfsStdioDirEntry {
    struct dirent* d;
};

int vfs_stdio_internal_open(struct VfsStdioFile* f, const char* path, enum FtpVfsOpenMode mode);
int vfs_stdio_internal_read(struct VfsStdioFile* f, void* buf, size_t size);
int vfs_stdio_internal_write(struct VfsStdioFile* f, const void* buf, size_t size);
int vfs_stdio_internal_seek(struct VfsStdioFile* f, size_t off);
int vfs_stdio_internal_close(struct VfsStdioFile* f);
int vfs_stdio_internal_isfile_open(struct VfsStdioFile* f);

int vfs_stdio_internal_opendir(struct VfsStdioDir* f, const char* path);
const char* vfs_stdio_internal_readdir(struct VfsStdioDir* f, struct VfsStdioDirEntry* entry);
int vfs_stdio_internal_dirlstat(struct VfsStdioDir* f, const struct VfsStdioDirEntry* entry, const char* path, struct stat* st);
int vfs_stdio_internal_closedir(struct VfsStdioDir* f);
int vfs_stdio_internal_isdir_open(struct VfsStdioDir* f);

int vfs_stdio_internal_stat(const char* path, struct stat* st);
int vfs_stdio_internal_lstat(const char* path, struct stat* st);
int vfs_stdio_internal_mkdir(const char* path);
int vfs_stdio_internal_unlink(const char* path);
int vfs_stdio_internal_rmdir(const char* path);
int vfs_stdio_internal_rename(const char* path_src, const char* path_dst);

struct FtpVfs;
extern const struct FtpVfs g_vfs_stdio;

#ifdef __cplusplus
}
#endif
