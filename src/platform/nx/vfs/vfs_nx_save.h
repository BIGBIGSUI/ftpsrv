// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <switch.h>
#include "vfs_nx_fs.h"

enum SaveDirType {
    SaveDirType_Invalid,
    SaveDirType_Root,
    SaveDirType_User1,
    SaveDirType_File,
    SaveDirType_Zip,
    SaveDirType_FileApp,
    SaveDirType_ZipApp,
};

struct SavePathData {
    enum SaveDirType type;
    AccountUid uid;
    FsSaveDataType data_type;
    FsSaveDataSpaceId space_id;
    u64 app_id;
    size_t path_off;
};

enum mmz_State {
    mmz_State_Local,
    mmz_State_Data,
    mmz_State_Descriptor,
    mmz_State_File,
    mmz_State_End,
};

struct mmz_FileInfoMeta {
    u32 crc32;
    u32 size;
    u32 string_len;
};

struct mmz_Data {
    FsFileSystem* fs;
    FsFile fbuf_out;
    u32 fbuf_off;
    time_t time;

    // meta for file_hdr and end_record.
    u32 file_count;
    u32 local_hdr_off;
    u32 central_directory_size;

    // meta for current file.
    // crc32 is filled out in mmz_State_Data, and then
    // written to file when completed.
    struct mmz_FileInfoMeta meta;

    enum mmz_State state; // current transfer state.
    FsFile fin; // file input, used in mmz_State_Data.
    u32 new_crc32;
    u32 index; // file index.
    u32 off; // relative offset for transfer state.
    u32 zip_off; // output offset.
    bool pending; // pending write completion to change state.
};

struct VfsSaveFile {
    struct SavePathData data;

    union {
        struct mmz_Data mz;
        struct VfsFsFile fs_file;
    };

    FsFileSystem fs;
    bool is_valid;
};

struct VfsSaveDir {
    struct SavePathData data;
    FsSaveDataInfoReader r;
    FsFileSystem fs;
    struct VfsFsDir fs_dir;
    s32 index;
    bool is_valid;
};

struct VfsSaveDirEntry {
    union {
        struct {
            FsSaveDataInfo info;
            char name[512 + 128];
        };
        struct VfsFsDirEntry fs_buf;
    };
};

void vfs_save_init(bool save_writable);
void vfs_save_exit(void);

struct FtpVfs;
extern const struct FtpVfs g_vfs_save;

#ifdef __cplusplus
}
#endif
