#include "log.h"
#include "ftpsrv_vfs.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifdef __SWITCH__
#include <switch.h>
static FsFileSystem g_fs = {0};
static FsFile g_log_file = {0};
static int g_has_log_file = 0;
static s64 g_file_off = 0;

void log_file_write(const char* msg) {
    if (g_has_log_file) {
        size_t len = strlen(msg);

        if (len) {
            char buf[256];
            if (msg[len - 1] != '\n') {
                snprintf(buf, sizeof(buf), "%s\n", msg);
                msg = buf;
                len = strlen(msg);
            }

            if (R_SUCCEEDED(fsFileWrite(&g_log_file, g_file_off, msg, len, 0))) {
                g_file_off += len;
            }
        }
    }
}

void log_file_fwrite(const char* fmt, ...) {
    if (g_has_log_file) {
        char buf[256];
        va_list va;
        va_start(va, fmt);
        vsnprintf(buf, sizeof(buf), fmt, va);
        va_end(va);
        log_file_write(buf);
    }
}

void log_file_init(const char* path, const char* init_msg) {
    char safe_buf[FS_MAX_PATH];
    sniprintf(safe_buf, sizeof(safe_buf), "%s", path);

    if (!g_has_log_file) {
        if (R_SUCCEEDED(fsOpenSdCardFileSystem(&g_fs))) {
            fsFsDeleteFile(&g_fs, safe_buf);
            if (R_SUCCEEDED(fsFsCreateFile(&g_fs, safe_buf, 0, 0))) {
                if (R_SUCCEEDED(fsFsOpenFile(&g_fs, safe_buf, FsOpenMode_Write | FsOpenMode_Append, &g_log_file))) {
                    g_file_off = 0;
                    g_has_log_file = 1;
                    log_file_write(init_msg);
                    return;
                }
                fsFsDeleteFile(&g_fs, safe_buf);
            }
            fsFsClose(&g_fs);
        }
    }
}

void log_file_exit(void) {
    if (g_has_log_file) {
        log_file_write("goodbye :)");
        fsFileClose(&g_log_file);
        fsFsCommit(&g_fs);
        fsFsClose(&g_fs);
        g_has_log_file = 0;
    }
}
#else
static struct FtpVfsFile g_log_file = {0};
static int g_has_log_file = 0;

void log_file_write(const char* msg) {
    if (g_has_log_file) {
        const size_t len = strlen(msg);
        if (len) {
            ftp_vfs_write(&g_log_file, msg, len);
            if (msg[len - 1] != '\n') {
                ftp_vfs_write(&g_log_file, "\n", 1);
            }
        }
    }
}

void log_file_fwrite(const char* fmt, ...) {
    if (g_has_log_file) {
        char buf[256];
        va_list va;
        va_start(va, fmt);
        vsnprintf(buf, sizeof(buf), fmt, va);
        va_end(va);
        log_file_write(buf);
    }
}

void log_file_init(const char* path, const char* init_msg) {
    if (!g_has_log_file) {
        ftp_vfs_unlink(path);
        g_has_log_file = !ftp_vfs_open(&g_log_file, path, FtpVfsOpenMode_APPEND);
        log_file_write(init_msg);
    }
}

void log_file_exit(void) {
    if (g_has_log_file) {
        log_file_write("goodbye :)");
        ftp_vfs_close(&g_log_file);
        g_has_log_file = 0;
    }
}
#endif
