#include "ftpsrv.h"
#include <ftpsrv_vfs.h>
#include "utils.h"
#include "log/log.h"
#include "custom_commands.h"
#include "vfs_nx.h"
#include "vfs/vfs_nx_save.h"

#include <string.h>
#include <switch.h>
#include <switch/services/bsd.h>
#include <minIni.h>

// 添加清理文件名的函数，移除或替换非法字符
static void sanitize_filename(char* filename) {
    if (filename == NULL) return;
    
    char* src = filename;
    char* dst = filename;
    
    while (*src) {
        // 替换非法字符为下划线
        switch (*src) {
            case '<':
            case '>':
            case ':':
            case '"':
            case '/':
            case '\\':
            case '|':
            case '?':
            case '*':
                *dst = '_';
                break;
            default:
                *dst = *src;
                break;
        }
        
        // 避免连续的下划线
        if (dst > filename && *(dst-1) == '_' && *dst == '_') {
            src++;
            continue;
        }
        
        src++;
        dst++;
    }
    
    // 移除末尾的空格和点
    while (dst > filename && (*(dst-1) == ' ' || *(dst-1) == '.')) {
        dst--;
    }
    
    *dst = '\0';
    
    // 确保文件名不为空
    if (strlen(filename) == 0) {
        strcpy(filename, "unknown_game");
    }
}

static const char* INI_PATH = "/config/ftpsrv/config.ini";
static const char* LOG_PATH = "/config/ftpsrv/log.txt";
static struct FtpSrvConfig g_ftpsrv_config = {0};
static bool g_led_enabled = false;

// 存储当前运行游戏的用户信息
static AccountUid g_current_game_user_uid = {0};
static char g_current_game_user_name[33] = {0};

// 全局变量用于create_game_folder函数
static char sanitized_name[0x200] = {0};
static char tid_str[17] = {0};
static char* folder_name = NULL;

static void ftp_log_callback(enum FTP_API_LOG_TYPE type, const char* msg) {
    log_file_write(msg);
    if (g_led_enabled) {
        led_flash();
    }
}

static void ftp_progress_callback(void) {
    if (g_led_enabled) {
        led_flash();
    }
}

// 添加LED控制函数
void enableBreathingEffect(HidsysUniquePadId unique_pad_id) {
    HidsysNotificationLedPattern pattern;
    memset(&pattern, 0, sizeof(pattern));

    // Setup Breathing effect pattern data.
    pattern.baseMiniCycleDuration = 0x8;             // 100ms.
    pattern.totalMiniCycles = 0x2;                   // 3 mini cycles. Last one 12.5ms.
    pattern.totalFullCycles = 0x0;                   // Repeat forever.
    pattern.startIntensity = 0x2;                    // 13%.

    pattern.miniCycles[0].ledIntensity = 0xF;        // 100%.
    pattern.miniCycles[0].transitionSteps = 0xF;     // 15 steps. Transition time 1.5s.
    pattern.miniCycles[0].finalStepDuration = 0x0;   // Forced 12.5ms.
    pattern.miniCycles[1].ledIntensity = 0x2;        // 13%.
    pattern.miniCycles[1].transitionSteps = 0xF;     // 15 steps. Transition time 1.5s.
    pattern.miniCycles[1].finalStepDuration = 0x0;   // Forced 12.5ms.

    hidsysSetNotificationLedPattern(&pattern, unique_pad_id);
}

void disableBreathingEffect(HidsysUniquePadId unique_pad_id) {
    HidsysNotificationLedPattern pattern;
    memset(&pattern, 0, sizeof(pattern));
    hidsysSetNotificationLedPattern(&pattern, unique_pad_id);
}

void enableHeartbeatEffect(HidsysUniquePadId unique_pad_id) {
    HidsysNotificationLedPattern pattern;
    memset(&pattern, 0, sizeof(pattern));

    // Setup Heartbeat effect pattern data.
    pattern.baseMiniCycleDuration = 0x1;             // 12.5ms.
    pattern.totalMiniCycles = 0xF;                   // 16 mini cycles.
    pattern.totalFullCycles = 0x0;                   // Repeat forever.
    pattern.startIntensity = 0x0;                    // 0%.

    // First beat.
    pattern.miniCycles[0].ledIntensity = 0xF;        // 100%.
    pattern.miniCycles[0].transitionSteps = 0xF;     // 15 steps. Total 187.5ms.
    pattern.miniCycles[0].finalStepDuration = 0x0;   // Forced 12.5ms.
    pattern.miniCycles[1].ledIntensity = 0x0;        // 0%.
    pattern.miniCycles[1].transitionSteps = 0xF;     // 15 steps. Total 187.5ms.
    pattern.miniCycles[1].finalStepDuration = 0x0;   // Forced 12.5ms.

    // Second beat.
    pattern.miniCycles[2].ledIntensity = 0xF;
    pattern.miniCycles[2].transitionSteps = 0xF;
    pattern.miniCycles[2].finalStepDuration = 0x0;
    pattern.miniCycles[3].ledIntensity = 0x0;
    pattern.miniCycles[3].transitionSteps = 0xF;
    pattern.miniCycles[3].finalStepDuration = 0x0;

    // Led off wait time.
    for(int i=2; i<15; i++) {
        pattern.miniCycles[i].ledIntensity = 0x0;        // 0%.
        pattern.miniCycles[i].transitionSteps = 0xF;     // 15 steps. Total 187.5ms.
        pattern.miniCycles[i].finalStepDuration = 0xF;   // 187.5ms.
    }

    hidsysSetNotificationLedPattern(&pattern, unique_pad_id);
}

void disableHeartbeatEffect(HidsysUniquePadId unique_pad_id) {
    HidsysNotificationLedPattern pattern;
    memset(&pattern, 0, sizeof(pattern));
    hidsysSetNotificationLedPattern(&pattern, unique_pad_id);
}

// 添加获取当前运行游戏TID的函数
static Result get_current_tid(u64* tid) {
    Result rc;
    
    if (R_FAILED(rc = pmdmntInitialize())) {
        return rc;
    }
    
    if (R_FAILED(rc = pminfoInitialize())) {
        pmdmntExit();
        return rc;
    }
    
    u64 pid;
    if (R_SUCCEEDED(rc = pmdmntGetApplicationProcessId(&pid))) {
        Result pid_rc = pminfoGetProgramId(tid, pid);
        if (0x20f == pid_rc) {
            *tid = 0x0100000000001000ULL; // QLAUNCH_TID
            rc = 0; // 设置为成功状态，确保日志能被记录
        } else {
            rc = pid_rc;
        }
    } else if (rc == 0x20f) {
        *tid = 0x0100000000001000ULL; // QLAUNCH_TID
        rc = 0; // 设置为成功状态，确保日志能被记录
    } else {
        *tid = 0;
    }
    
    pminfoExit();
    pmdmntExit();
    return rc;
}

// 添加创建游戏名称文件夹的函数
static void create_game_folder(u64 tid) {
    if (tid == 0) return;
    
    // 获取游戏名称
    NcmContentId content_id = {0};
    struct AppName app_name = {0};
    get_app_name(tid, &content_id, &app_name);
    
    // 清理游戏名称中的非法字符
    memset(sanitized_name, 0, sizeof(sanitized_name));
    if (strlen(app_name.str) > 0) {
        strncpy(sanitized_name, app_name.str, sizeof(sanitized_name) - 1);
        sanitize_filename(sanitized_name);
    }
    
    // 如果无法获取游戏名称，则使用TID作为后备
    folder_name = sanitized_name;
    memset(tid_str, 0, sizeof(tid_str));
    if (strlen(sanitized_name) == 0) {
        snprintf(tid_str, sizeof(tid_str), "%016lX", tid);
        folder_name = tid_str;
    }
    
    FsFileSystem* sdmc_fs = fsdev_wrapGetDeviceFileSystem("sdmc");
    if (sdmc_fs == NULL) {
        log_file_write("failed to get SD card filesystem for game folder creation");
        return;
    }
    
    // 遍历全部用户名，在autoback文件夹内生成游戏名称文件夹
    AccountUid user_ids[ACC_USER_LIST_SIZE] = {0};
    s32 total_users = 0;
    Result account_rc = accountGetUserCount(&total_users);
    if (R_SUCCEEDED(account_rc) && total_users > 0) {
        account_rc = accountListAllUsers(user_ids, ACC_USER_LIST_SIZE, &total_users);
        if (R_SUCCEEDED(account_rc)) {
            for (s32 i = 0; i < total_users; i++) {
                AccountProfile profile = {0};
                AccountUserData user_data = {0};
                AccountProfileBase profile_base = {0};
                
                account_rc = accountGetProfile(&profile, user_ids[i]);
                if (R_SUCCEEDED(account_rc)) {
                    account_rc = accountProfileGet(&profile, &user_data, &profile_base);
                    if (R_SUCCEEDED(account_rc)) {
                        char username[33] = {0}; // AccountProfileBase nickname is 32 chars + null terminator
                        strncpy(username, profile_base.nickname, sizeof(username) - 1);
                        
                        // 创建游戏名称文件夹路径
                        char game_folder_path[256] = {0};
                        snprintf(game_folder_path, sizeof(game_folder_path), "/autoback/%s/%s", username, folder_name);
                        
                        // 创建游戏名称文件夹
                        Result mkdir_rc = fsFsCreateDirectory(sdmc_fs, game_folder_path);
                        if (R_SUCCEEDED(mkdir_rc)) {
                            char log_buf[512] = {0};
                            snprintf(log_buf, sizeof(log_buf), "created game folder: %s", game_folder_path);
                            log_file_write(log_buf);
                        } else if (mkdir_rc == 0x402) { // FSERROR_PATH_ALREADY_EXISTS
                            // 文件夹已存在，不需要处理
                        } else {
                            char log_buf[512] = {0};
                            snprintf(log_buf, sizeof(log_buf), "failed to create game folder %s: 0x%x", game_folder_path, mkdir_rc);
                            log_file_write(log_buf);
                        }
                    }
                    accountProfileClose(&profile);
                }
            }
        }
    }
}

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/iosupport.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "ftpsrv.h"
#include "ftpsrv_vfs.h"
#include "vfs_nx.h"
#include "vfs/vfs_nx_save.h"

// 添加流式传输ZIP到SD卡的函数
static Result stream_zip_to_sdcard(struct mmz_Data* mz, FsFileSystem* sdmc_fs, const char* output_path) {
    Result rc;
    FsFile output_file;
    
    // 添加调试日志：开始流式传输
    char debug_buf[256] = {0};
    snprintf(debug_buf, sizeof(debug_buf), "Starting streaming to %s", output_path);
    log_file_write(debug_buf);
    
    // 创建输出文件
    rc = fsFsCreateFile(sdmc_fs, output_path, 0, 0);
    if (R_FAILED(rc) && rc != 0x2EE202) { // 忽略已存在的错误
        snprintf(debug_buf, sizeof(debug_buf), "Failed to create file %s: 0x%x", output_path, rc);
        log_file_write(debug_buf);
        return rc;
    } else if (rc == 0x2EE202) {
        snprintf(debug_buf, sizeof(debug_buf), "File %s already exists, will overwrite", output_path);
        log_file_write(debug_buf);
    } else {
        snprintf(debug_buf, sizeof(debug_buf), "Successfully created file %s", output_path);
        log_file_write(debug_buf);
    }
    
    // 打开文件
    rc = fsFsOpenFile(sdmc_fs, output_path, FsOpenMode_Read|FsOpenMode_Write|FsOpenMode_Append, &output_file);
    if (R_FAILED(rc)) {
        snprintf(debug_buf, sizeof(debug_buf), "Failed to open file %s: 0x%x", output_path, rc);
        log_file_write(debug_buf);
        return rc;
    }
    
    snprintf(debug_buf, sizeof(debug_buf), "Successfully opened file %s for writing", output_path);
    log_file_write(debug_buf);
    
    // 流式传输ZIP数据
    u64 total_written = 0;
    u8 buffer[8192]; // 8KB缓冲区
    
    while (1) {
        int bytes_read = mmz_read(mz, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            // 错误处理
            snprintf(debug_buf, sizeof(debug_buf), "Error reading ZIP data: %d", bytes_read);
            log_file_write(debug_buf);
            fsFileClose(&output_file);
            return -1;
        }
        
        if (bytes_read == 0) {
            // 传输完成
            snprintf(debug_buf, sizeof(debug_buf), "Finished reading ZIP data, total bytes: %lu", total_written);
            log_file_write(debug_buf);
            break;
        }
            // 添加调试日志：准备写入数据
        snprintf(debug_buf, sizeof(debug_buf), "Writing %d bytes at offset %lu to %s", bytes_read, total_written, output_path);
        log_file_write(debug_buf);
        
        // 写入SD卡 - 修复偏移量和bytes_written的使用
        rc = fsFileWrite(&output_file, total_written, buffer, bytes_read, FsWriteOption_None);
        if (R_FAILED(rc)) {
            snprintf(debug_buf, sizeof(debug_buf), "Failed to write to file %s: 0x%x (tried to write %d bytes at offset %lu)", output_path, rc, bytes_read, total_written);
            log_file_write(debug_buf);
            fsFileClose(&output_file);
            return rc;
        } else {
            // 添加调试日志：成功写入数据
            snprintf(debug_buf, sizeof(debug_buf), "Successfully wrote %d bytes at offset %lu to %s", bytes_read, total_written, output_path);
            log_file_write(debug_buf);
        }
        
        total_written += bytes_read;
        
        // 每写入1MB数据记录一次日志，避免日志过于频繁
        if (total_written % (1024 * 1024) == 0) {
            snprintf(debug_buf, sizeof(debug_buf), "Written %lu MB to %s", total_written / (1024 * 1024), output_path);
            log_file_write(debug_buf);
        }
    }
    
    // 刷新文件缓冲区确保数据写入
    rc = fsFileFlush(&output_file);
    if (R_FAILED(rc)) {
        snprintf(debug_buf, sizeof(debug_buf), "Failed to flush file %s: 0x%x, total bytes written: %lu", output_path, rc, total_written);
        log_file_write(debug_buf);
        fsFileClose(&output_file);
        return rc;
    } else {
        // 添加调试日志：完成文件写入
        snprintf(debug_buf, sizeof(debug_buf), "Successfully flushed file %s, total bytes written: %lu", output_path, total_written);
        log_file_write(debug_buf);
    }
    
    // 关闭文件
    fsFileClose(&output_file);
    return 0;
}

// 修改生成存档的函数，使用流式传输
static void generate_save_archive(u64 tid) {
    if (tid == 0) return;
    
    // 添加调试日志：开始生成存档
    char debug_buf[256] = {0};
    snprintf(debug_buf, sizeof(debug_buf), "Starting save archive generation for TID: %016lX", tid);
    log_file_write(debug_buf);
    
    // 创建游戏文件夹
    create_game_folder(tid);
    
    // 只处理当前运行游戏的用户
    if (g_current_game_user_uid.uid[0] == 0 && g_current_game_user_uid.uid[1] == 0) {
        snprintf(debug_buf, sizeof(debug_buf), "No current game user detected, skipping save archive generation");
        log_file_write(debug_buf);
        return;
    }
    
    // 使用全局变量中的用户信息
    AccountUid target_user = g_current_game_user_uid;
    const char* username = g_current_game_user_name[0] ? g_current_game_user_name : "Unknown";
    
    // 添加调试日志：处理当前用户
    snprintf(debug_buf, sizeof(debug_buf), "Processing save archive for user: %s (%016lX%016lX)", 
             username, target_user.uid[0], target_user.uid[1]);
    log_file_write(debug_buf);
    
    FsFileSystem save_fs;
    FsSaveDataAttribute attr = {0};
    attr.application_id = tid;
    attr.uid = target_user;
    attr.save_data_type = FsSaveDataType_Account;
    
    // 使用fsOpenReadOnlySaveDataFileSystem挂载存档文件系统
    Result rc = fsOpenReadOnlySaveDataFileSystem(&save_fs, FsSaveDataSpaceId_User, &attr);
    if (R_SUCCEEDED(rc)) {
        // 添加调试日志：成功挂载存档文件系统
        snprintf(debug_buf, sizeof(debug_buf), "Successfully mounted save filesystem for user %s", username);
        log_file_write(debug_buf);
        
        // 检查存档是否存在 - 尝试读取存档根目录
        FsDir dir;
        Result dir_rc = fsFsOpenDirectory(&save_fs, "/", FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir);
        
        if (R_SUCCEEDED(dir_rc)) {
            // 存档存在，继续生成ZIP文件
            s64 entry_count = 0;
            fsDirGetEntryCount(&dir, &entry_count);
            fsDirClose(&dir);
            
            // 添加调试日志：存档条目数量
            snprintf(debug_buf, sizeof(debug_buf), "Save archive entry count: %ld for user %s", entry_count, username);
            log_file_write(debug_buf);
            
            if (entry_count > 0) {
                // 为当前用户创建存档ZIP文件
                struct mmz_Data mz = {0};
                
                // 调用mmz_build_zip生成存档元数据
                Result rc = mmz_build_zip(&mz, &save_fs, tid, target_user, FsSaveDataSpaceId_User);
                if (R_SUCCEEDED(rc)) {
                    // 添加调试日志：成功生成存档元数据
                    snprintf(debug_buf, sizeof(debug_buf), "Successfully generated save archive metadata for user %s", username);
                    log_file_write(debug_buf);
                    
                    // 确保元数据写入磁盘
                    fsFileFlush(&mz.fbuf_out);
                    
                    // 创建最终路径：/autoback/用户名/folder_name/游戏名_时间戳.zip
                    time_t now = time(NULL);
                    struct tm *tm_info = localtime(&now);
                    char timestamp[64];
                    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);
                    
                    char final_path[FS_MAX_PATH];
                    snprintf(final_path, sizeof(final_path), "/autoback/%s/%s/%016lX_%s.zip", 
                             username, folder_name, tid, timestamp);
                    
                    FsFileSystem* sdmc_fs = fsdev_wrapGetDeviceFileSystem("sdmc");
                    if (sdmc_fs != NULL) {
                        // 添加调试日志：获取SD卡文件系统成功
                        snprintf(debug_buf, sizeof(debug_buf), "Successfully obtained SD card filesystem");
                        log_file_write(debug_buf);
                        
                        // 确保用户目录存在
                        char user_dir[256];
                        snprintf(user_dir, sizeof(user_dir), "/autoback/%s", username);
                        fsFsCreateDirectory(sdmc_fs, user_dir);
                        
                        // 确保游戏文件夹存在
                        char game_dir[256];
                        snprintf(game_dir, sizeof(game_dir), "/autoback/%s/%s", username, folder_name);
                        fsFsCreateDirectory(sdmc_fs, game_dir);
                        
                        // 添加调试日志：开始流式传输ZIP到SD卡
                        snprintf(debug_buf, sizeof(debug_buf), "Starting streaming ZIP to SD card for user %s", username);
                        log_file_write(debug_buf);
                        
                        // 流式传输ZIP到SD卡
                        rc = stream_zip_to_sdcard(&mz, sdmc_fs, final_path);
                        
                        // 关闭临时文件句柄
                        fsFileClose(&mz.fbuf_out);
                        
                        // 传输完成后清理临时文件
                        char temp_path[FS_MAX_PATH];
                        mzz_build_temp_path(temp_path, tid, target_user, FsSaveDataSpaceId_User);
                        if (sdmc_fs != NULL) {
                            fsFsDeleteFile(sdmc_fs, temp_path);
                        }
                        
                        if (R_SUCCEEDED(rc)) {
                            char log_buf[512] = {0};
                            snprintf(log_buf, sizeof(log_buf), "Successfully created save archive: %s", final_path);
                            log_file_write(log_buf);
                        } else {
                            char log_buf[512] = {0};
                            snprintf(log_buf, sizeof(log_buf), "Failed to stream archive to SD card: 0x%x", rc);
                            log_file_write(log_buf);
                        }
                    }
                    
                    // 注意：不在这里删除临时文件，因为mmz_read还需要读取它
                    // 清理工作将在stream_zip_to_sdcard完成后进行
                } else {
                    char log_buf[512] = {0};
                    snprintf(log_buf, sizeof(log_buf), "Failed to generate save archive for TID %016lX and user %s: 0x%x", tid, username, rc);
                    log_file_write(log_buf);
                    
                    // 注意：不在这里删除临时文件，因为mmz_read还需要读取它
                    // 清理工作将在stream_zip_to_sdcard完成后进行
                }
            } else {
                // 存档目录存在但为空，跳过
                char log_buf[512] = {0};
                snprintf(log_buf, sizeof(log_buf), "Skipping empty save archive for TID %016lX and user %s", tid, username);
                log_file_write(log_buf);
            }
        } else if (dir_rc == 0x7D402) { // FSERROR_PATH_NOT_FOUND
            // 存档不存在，跳过该用户
            char log_buf[512] = {0};
            snprintf(log_buf, sizeof(log_buf), "No save data found for TID %016lX and user %s, skipping", tid, username);
            log_file_write(log_buf);
        } else {
            // 其他错误，记录日志但继续
            char log_buf[512] = {0};
            snprintf(log_buf, sizeof(log_buf), "Error checking save data for TID %016lX and user %s: 0x%x", tid, username, dir_rc);
            log_file_write(log_buf);
        }
        
        // 关闭存档文件系统
        fsFsClose(&save_fs);
    } else {
        char log_buf[512] = {0};
        snprintf(log_buf, sizeof(log_buf), "Failed to open save data file system for TID %016lX and user %s: 0x%x", tid, username, rc);
        log_file_write(log_buf);
    }
    
    // 添加调试日志：完成生成存档
    snprintf(debug_buf, sizeof(debug_buf), "Finished save archive generation for TID: %016lX", tid);
    log_file_write(debug_buf);
}

int main(void) {
    g_ftpsrv_config.custom_command = CUSTOM_COMMANDS;
    g_ftpsrv_config.custom_command_count = CUSTOM_COMMANDS_SIZE;
    g_ftpsrv_config.log_callback = ftp_log_callback;
    g_ftpsrv_config.progress_callback = ftp_progress_callback;
    g_ftpsrv_config.anon = ini_getbool("Login", "anon", 0, INI_PATH);
    int user_len = ini_gets("Login", "user", "", g_ftpsrv_config.user, sizeof(g_ftpsrv_config.user), INI_PATH);
    int pass_len = ini_gets("Login", "pass", "", g_ftpsrv_config.pass, sizeof(g_ftpsrv_config.pass), INI_PATH);
    g_ftpsrv_config.port = ini_getl("Network", "port", 21, INI_PATH);
    g_ftpsrv_config.timeout = ini_getl("Network", "timeout", 0, INI_PATH);
    g_ftpsrv_config.use_localtime = ini_getbool("Misc", "use_localtime", 0, INI_PATH);
    bool log_enabled = ini_getbool("Log", "log", 0, INI_PATH);

    // get nx config
    bool mount_devices = ini_getbool("Nx", "mount_devices", 1, INI_PATH);
    bool mount_bis = ini_getbool("Nx", "mount_bis", 0, INI_PATH);
    bool save_writable = ini_getbool("Nx", "save_writable", 0, INI_PATH);
    g_led_enabled = ini_getbool("Nx", "led", 1, INI_PATH);
    bool skip_ascii_convert = ini_getbool("Nx", "skip_ascii_convert", 0, INI_PATH);
    g_ftpsrv_config.port = ini_getl("Nx", "sys_port", g_ftpsrv_config.port, INI_PATH); // compat

    // get Nx-Sys overrides
    g_ftpsrv_config.anon = ini_getbool("Nx-Sys", "anon", g_ftpsrv_config.anon, INI_PATH);
    user_len = ini_gets("Nx-Sys", "user", g_ftpsrv_config.user, g_ftpsrv_config.user, sizeof(g_ftpsrv_config.user), INI_PATH);
    pass_len = ini_gets("Nx-Sys", "pass", g_ftpsrv_config.pass, g_ftpsrv_config.pass, sizeof(g_ftpsrv_config.pass), INI_PATH);
    g_ftpsrv_config.port = ini_getl("Nx-Sys", "port", g_ftpsrv_config.port, INI_PATH);
    g_ftpsrv_config.timeout = ini_getl("Nx-Sys", "timeout", g_ftpsrv_config.timeout, INI_PATH);
    g_ftpsrv_config.use_localtime = ini_getbool("Nx-Sys", "use_localtime", g_ftpsrv_config.use_localtime, INI_PATH);
    log_enabled = ini_getbool("Nx-Sys", "log", log_enabled, INI_PATH);
    mount_devices = ini_getbool("Nx-Sys", "mount_devices", mount_devices, INI_PATH);
    mount_bis = ini_getbool("Nx-Sys", "mount_bis", mount_bis, INI_PATH);
    save_writable = ini_getbool("Nx-Sys", "save_writable", save_writable, INI_PATH);
    g_led_enabled = ini_getbool("Nx-Sys", "led", g_led_enabled, INI_PATH);

    if (log_enabled) {
        log_file_init(LOG_PATH, "ftpsrv - " FTPSRV_VERSION_HASH " - NX-sys");
    }

    // exit early as this is a security risk due to ldn-mitm.
    if (!user_len && !pass_len && !g_ftpsrv_config.anon) {
        log_file_write("User / Pass / Anon not set in config!");
        return EXIT_FAILURE;
    }

    vfs_nx_init(NULL, mount_devices, save_writable, mount_bis, skip_ascii_convert);

    // 创建autoback文件夹
    FsFileSystem* sdmc_fs = fsdev_wrapGetDeviceFileSystem("sdmc");
    if (sdmc_fs != NULL) {
        Result rc = fsFsCreateDirectory(sdmc_fs, "/autoback");
        if (R_SUCCEEDED(rc)) {
            log_file_write("autoback folder created successfully");
        } else if (rc == 0x402) { // FSERROR_PATH_ALREADY_EXISTS
            log_file_write("autoback folder already exists");
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "failed to create autoback folder: 0x%x", rc);
            log_file_write(buf);
        }

        // 遍历全部用户名，在autoback文件夹内生成用户名文件夹
        AccountUid user_ids[ACC_USER_LIST_SIZE] = {0};
        s32 total_users = 0;
        Result account_rc = accountGetUserCount(&total_users);
        if (R_SUCCEEDED(account_rc) && total_users > 0) {
            account_rc = accountListAllUsers(user_ids, ACC_USER_LIST_SIZE, &total_users);
            if (R_SUCCEEDED(account_rc)) {
                for (s32 i = 0; i < total_users; i++) {
                    AccountProfile profile = {0};
                    AccountUserData user_data = {0};
                    AccountProfileBase profile_base = {0};
                    
                    account_rc = accountGetProfile(&profile, user_ids[i]);
                    if (R_SUCCEEDED(account_rc)) {
                        account_rc = accountProfileGet(&profile, &user_data, &profile_base);
                        if (R_SUCCEEDED(account_rc)) {
                            char username[33] = {0}; // AccountProfileBase nickname is 32 chars + null terminator
                            strncpy(username, profile_base.nickname, sizeof(username) - 1);
                            
                            // 创建用户名文件夹路径
                            char user_folder_path[256] = {0};
                            snprintf(user_folder_path, sizeof(user_folder_path), "/autoback/%s", username);
                            
                            // 创建用户名文件夹
                            Result mkdir_rc = fsFsCreateDirectory(sdmc_fs, user_folder_path);
                            if (R_SUCCEEDED(mkdir_rc)) {
                                char log_buf[512] = {0};
                                snprintf(log_buf, sizeof(log_buf), "created user folder: %s", user_folder_path);
                                log_file_write(log_buf);
                            } else if (mkdir_rc == 0x402) { // FSERROR_PATH_ALREADY_EXISTS
                                char log_buf[512] = {0};
                                snprintf(log_buf, sizeof(log_buf), "user folder already exists: %s", user_folder_path);
                                log_file_write(log_buf);
                            } else {
                                char log_buf[512] = {0};
                                snprintf(log_buf, sizeof(log_buf), "failed to create user folder %s: 0x%x", user_folder_path, mkdir_rc);
                                log_file_write(log_buf);
                            }
                        }
                        accountProfileClose(&profile);
                    }
                }
            } else {
                char buf[128];
                snprintf(buf, sizeof(buf), "failed to list users: 0x%x", account_rc);
                log_file_write(buf);
            }
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "failed to get user count: 0x%x", account_rc);
            log_file_write(buf);
        }
    } else {
        log_file_write("failed to get SD card filesystem");
    }

    // 添加TID检测相关变量
    u64 current_tid = 0;
    u64 previous_tid = 0;
    
    // 初始化获取一次当前TID
    get_current_tid(&current_tid);
    previous_tid = current_tid;
    
    // 移除未使用的timeout变量
    // int timeout = -1;
    // if (g_ftpsrv_config.timeout) {
    //     timeout = 1000 * g_ftpsrv_config.timeout;
    // }

    while (1) {
        // 注释掉FTP服务相关代码
        // ftpsrv_init(&g_ftpsrv_config);
        // while (1) {
        //     if (ftpsrv_loop(timeout) != FTP_API_LOOP_ERROR_OK) {
        //         svcSleepThread(1000000000);
        //         break;
        //     }
            
            // 每隔一段时间检测TID变化
            static int tid_check_counter = 0;
            tid_check_counter++;
            if (tid_check_counter >= 1) { // 每次循环检测一次（大约1s检测一次）
                tid_check_counter = 0;
                
                // 获取当前运行的游戏TID
                if (R_SUCCEEDED(get_current_tid(&current_tid))) {
                    // 添加调试日志，记录每次检测到的当前TID
                    char debug_buf[256] = {0};
                    snprintf(debug_buf, sizeof(debug_buf), "Current TID detected: %016lX", current_tid);
                    log_file_write(debug_buf);
                    if (current_tid != 0x0100000000001000ULL) {
                            // 获取运行该TID游戏的用户UID和名称
                            memset(&g_current_game_user_uid, 0, sizeof(g_current_game_user_uid));
                            memset(g_current_game_user_name, 0, sizeof(g_current_game_user_name));
                            
                            Result user_rc = accountGetLastOpenedUser(&g_current_game_user_uid);
                            if (R_SUCCEEDED(user_rc)) {
                                AccountProfile profile;
                                user_rc = accountGetProfile(&profile, g_current_game_user_uid);
                                if (R_SUCCEEDED(user_rc)) {
                                    AccountProfileBase profilebase;
                                    user_rc = accountProfileGet(&profile, NULL, &profilebase);
                                    if (R_SUCCEEDED(user_rc)) {
                                        strncpy(g_current_game_user_name, profilebase.nickname, sizeof(g_current_game_user_name) - 1);
                                        g_current_game_user_name[sizeof(g_current_game_user_name) - 1] = '\0';
                                    }
                                    accountProfileClose(&profile);
                                }
                            }
                            
                            // 输出用户信息
                            char user_info_buf[256] = {0};
                            snprintf(user_info_buf, sizeof(user_info_buf), 
                                     "User running TID %016lX - UID: %016lX%016lX, Name: %s", 
                                     current_tid, 
                                     g_current_game_user_uid.uid[0], 
                                     g_current_game_user_uid.uid[1], 
                                     g_current_game_user_name[0] ? g_current_game_user_name : "Unknown");
                            log_file_write(user_info_buf);
                        }
                    // 检查TID是否发生变化（包括游戏关闭的情况）
                    if (current_tid != previous_tid) {
                        // 如果当前游戏TID不是0x0100000000001000且不是previous_tid
                        // 如果之前有游戏在运行（previous_tid != 0），则创建以前一个TID为名的文件夹并生成存档
                        if (previous_tid != 0x0100000000001000ULL) {
                            // 开启LED呼吸灯效果作为开始提示
                            HidsysUniquePadId unique_pad_ids[2] = {0};
                            s32 total_entries = 0;
                            Result rc = hidsysGetUniquePadsFromNpad(HidNpadIdType_No1, unique_pad_ids, 2, &total_entries);
                            
                            if (R_FAILED(rc) || total_entries == 0) {
                                // 尝试手持模式
                                rc = hidsysGetUniquePadsFromNpad(HidNpadIdType_Handheld, unique_pad_ids, 2, &total_entries);
                            }
                            
                            if (R_SUCCEEDED(rc) && total_entries > 0) {
                                for(s32 i = 0; i < total_entries; i++) {
                                    enableBreathingEffect(unique_pad_ids[i]);
                                }
                            }
                            
                            generate_save_archive(previous_tid);
                            
                            // 关闭LED效果作为完成提示
                            if (R_SUCCEEDED(rc) && total_entries > 0) {
                                for(s32 i = 0; i < total_entries; i++) {
                                    disableBreathingEffect(unique_pad_ids[i]);
                                }
                            }
                            
                            // 记录日志
                            char log_buf[256] = {0};
                            snprintf(log_buf, sizeof(log_buf), "Game TID changed from %016lX to %016lX", previous_tid, current_tid);
                            log_file_write(log_buf);
                        }
                        // 更新previous_tid
                        previous_tid = current_tid;
                    }
                }
            }
            
            // 添加延迟避免CPU占用过高
            svcSleepThread(1000000000); // 1秒延迟
        // }
        // ftpsrv_exit();
    }
}

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

#define TCP_TX_BUF_SIZE (1024 * 4)
#define TCP_RX_BUF_SIZE (1024 * 4)
#define TCP_TX_BUF_SIZE_MAX (1024 * 64)
#define TCP_RX_BUF_SIZE_MAX (1024 * 64)
#define UDP_TX_BUF_SIZE (0)
#define UDP_RX_BUF_SIZE (0)
#define SB_EFFICIENCY (1)

#define ALIGN_MSS(v) ((((v) + 1500 - 1) / 1500) * 1500)

#define SOCKET_TMEM_SIZE \
    ((((( \
      ALIGN_MSS(TCP_TX_BUF_SIZE_MAX ? TCP_TX_BUF_SIZE_MAX : TCP_TX_BUF_SIZE) \
    + ALIGN_MSS(TCP_RX_BUF_SIZE_MAX ? TCP_RX_BUF_SIZE_MAX : TCP_RX_BUF_SIZE)) \
    + (UDP_TX_BUF_SIZE ? ALIGN_MSS(UDP_TX_BUF_SIZE) : 0)) \
    + (UDP_RX_BUF_SIZE ? ALIGN_MSS(UDP_RX_BUF_SIZE) : 0)) \
    + 0xFFF) &~ 0xFFF) \
    * SB_EFFICIENCY

#define NUMBER_OF_SOCKETS (2)

static alignas(0x1000) u8 SOCKET_TRANSFER_MEM[SOCKET_TMEM_SIZE * NUMBER_OF_SOCKETS];

static u32 socketSelectVersion(void) {
    if (hosversionBefore(3,0,0)) {
        return 1;
    } else if (hosversionBefore(4,0,0)) {
        return 2;
    } else if (hosversionBefore(5,0,0)) {
        return 3;
    } else if (hosversionBefore(6,0,0)) {
        return 4;
    } else if (hosversionBefore(8,0,0)) {
        return 5;
    } else if (hosversionBefore(9,0,0)) {
        return 6;
    } else if (hosversionBefore(13,0,0)) {
        return 7;
    } else if (hosversionBefore(16,0,0)) {
        return 8;
    } else /* latest known version */ {
        return 9;
    }
}

void __libnx_init_time(void);

// Newlib heap configuration function (makes malloc/free work).
void __libnx_initheap(void) {
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    // Configure the newlib heap.
    fake_heap_start = NULL;
    fake_heap_end   = NULL;
}

void __appInit(void) {
    Result rc;

    if (R_FAILED(rc = smInitialize()))
        diagAbortWithResult(rc);

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    const SocketInitConfig socket_config = {
        .tcp_tx_buf_size     = TCP_TX_BUF_SIZE,
        .tcp_rx_buf_size     = TCP_RX_BUF_SIZE,
        .tcp_tx_buf_max_size = TCP_TX_BUF_SIZE_MAX,
        .tcp_rx_buf_max_size = TCP_RX_BUF_SIZE_MAX,
        .udp_tx_buf_size     = UDP_TX_BUF_SIZE,
        .udp_rx_buf_size     = UDP_RX_BUF_SIZE,
        .sb_efficiency       = SB_EFFICIENCY,
        .num_bsd_sessions    = 1,
        .bsd_service_type    = BsdServiceType_Auto,
    };

    const BsdInitConfig bsd_config = {
        .version             = socketSelectVersion(),

        .tmem_buffer         = SOCKET_TRANSFER_MEM,
        .tmem_buffer_size    = sizeof(SOCKET_TRANSFER_MEM),

        .tcp_tx_buf_size     = socket_config.tcp_tx_buf_size,
        .tcp_rx_buf_size     = socket_config.tcp_rx_buf_size,
        .tcp_tx_buf_max_size = socket_config.tcp_tx_buf_max_size,
        .tcp_rx_buf_max_size = socket_config.tcp_rx_buf_max_size,

        .udp_tx_buf_size     = socket_config.udp_tx_buf_size,
        .udp_rx_buf_size     = socket_config.udp_rx_buf_size,

        .sb_efficiency       = socket_config.sb_efficiency,
    };

    if (R_FAILED(rc = timeInitialize()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = fsInitialize()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = fsdev_wrapMountSdmc()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = bsdInitialize(&bsd_config, socket_config.num_bsd_sessions, socket_config.bsd_service_type)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = accountInitialize(AccountServiceType_System)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = ncmInitialize()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = setInitialize()))
        diagAbortWithResult(rc);

    hidsysInitialize();
    __libnx_init_time();
}

// Service deinitialization.
void __appExit(void) {
    vfs_nx_exit();
    log_file_exit();
    hidsysExit();
    setExit();
    ncmExit();
    accountExit();
    bsdExit();
    fsdev_wrapUnmountAll();
    fsExit();
    timeExit();
    smExit();
}
