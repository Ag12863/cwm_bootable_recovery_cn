/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "install.h"
#include "mincrypt/rsa.h"
#include "minui/minui.h"
#include "minzip/SysUtil.h"
#include "minzip/Zip.h"
#include "mounts.h"
#include "mtdutils/mtdutils.h"
#include "roots.h"
#include "verifier.h"

#include "firmware.h"

#include "extendedcommands.h"


#define ASSUMED_UPDATE_BINARY_NAME  "META-INF/com/google/android/update-binary"
#define ASSUMED_UPDATE_SCRIPT_NAME  "META-INF/com/google/android/update-script"
#define PUBLIC_KEYS_FILE "/res/keys"

// The update binary ask us to install a firmware file on reboot.  Set
// that up.  Takes ownership of type and filename.
static int
handle_firmware_update(char* type, char* filename, ZipArchive* zip) {
    unsigned int data_size;
    const ZipEntry* entry = NULL;

    if (strncmp(filename, "PACKAGE:", 8) == 0) {
        entry = mzFindZipEntry(zip, filename+8);
        if (entry == NULL) {
#ifndef USE_CHINESE_FONT
            LOGE("Failed to find \"%s\" in package", filename+8);
#else
            LOGE("无法在刷机包中找到 \"%s\"", filename+8);
#endif
            return INSTALL_ERROR;
        }
        data_size = entry->uncompLen;
    } else {
        struct stat st_data;
        if (stat(filename, &st_data) < 0) {
#ifndef USE_CHINESE_FONT
            LOGE("Error stat'ing %s: %s\n", filename, strerror(errno));
#else
            LOGE("统计时出错 %s: %s\n", filename, strerror(errno));
#endif
            return INSTALL_ERROR;
        }
        data_size = st_data.st_size;
    }

#ifndef USE_CHINESE_FONT
    LOGI("type is %s; size is %d; file is %s\n",
#else
    LOGI("类型为 %s; 大小为 %d; 文件为 %s\n",
#endif
         type, data_size, filename);

    char* data = malloc(data_size);
    if (data == NULL) {
#ifndef USE_CHINESE_FONT
        LOGI("Can't allocate %d bytes for firmware data\n", data_size);
#else
        LOGI("无法为固件数据分配 %d 字节的空间\n", data_size);
#endif
        return INSTALL_ERROR;
    }

    if (entry) {
        if (mzReadZipEntry(zip, entry, data, data_size) == false) {
#ifndef USE_CHINESE_FONT
            LOGE("Failed to read \"%s\" from package", filename+8);
#else
            LOGE("无法读取刷机包中的 \"%s\"", filename+8);
#endif
            return INSTALL_ERROR;
        }
    } else {
        FILE* f = fopen(filename, "rb");
        if (f == NULL) {
#ifndef USE_CHINESE_FONT
            LOGE("Failed to open %s: %s\n", filename, strerror(errno));
#else
            LOGE("无法打开 %s: %s\n", filename, strerror(errno));
#endif
            return INSTALL_ERROR;
        }
        if (fread(data, 1, data_size, f) != data_size) {
#ifndef USE_CHINESE_FONT
            LOGE("Failed to read firmware data: %s\n", strerror(errno));
#else
            LOGE("无法读取固件数据: %s\n", strerror(errno));
#endif
            return INSTALL_ERROR;
        }
        fclose(f);
    }

    if (remember_firmware_update(type, data, data_size)) {
#ifndef USE_CHINESE_FONT
        LOGE("Can't store %s image\n", type);
#else
        LOGE("无法保存 %s 镜像\n", type);
#endif
        free(data);
        return INSTALL_ERROR;
    }

    free(filename);

    return INSTALL_SUCCESS;
}

static const char *LAST_INSTALL_FILE = "/cache/recovery/last_install";

// If the package contains an update binary, extract it and run it.
static int
try_update_binary(const char *path, ZipArchive *zip) {
    const ZipEntry* binary_entry =
            mzFindZipEntry(zip, ASSUMED_UPDATE_BINARY_NAME);
    if (binary_entry == NULL) {
        const ZipEntry* update_script_entry =
                mzFindZipEntry(zip, ASSUMED_UPDATE_SCRIPT_NAME);
        if (update_script_entry != NULL) {
#ifndef USE_CHINESE_FONT
            ui_print("Amend scripting (update-script) is no longer supported.\n");
            ui_print("Amend scripting was deprecated by Google in Android 1.5.\n");
            ui_print("It was necessary to remove it when upgrading to the ClockworkMod 3.0 Gingerbread based recovery.\n");
            ui_print("Please switch to Edify scripting (updater-script and update-binary) to create working update zip packages.\n");
#else
            ui_print("Amend 格式的脚本(update-script)现在已不被支持。\n");
            ui_print("Amend 格式的脚本自 Android 1.5 起已被谷歌取消支持。\n");
            ui_print("当使用高于 3.0 版本的 ClockworkMod 时，需要移除 Amend 格式的脚本。\n");
            ui_print("请将脚本转换为 Edify 格式的脚本(updater-script 加 update-binary)以便做出可以使用的刷机包。\n");
#endif
            return INSTALL_UPDATE_BINARY_MISSING;
        }

        mzCloseZipArchive(zip);
        return INSTALL_UPDATE_BINARY_MISSING;
    }

    char* binary = "/tmp/update_binary";
    unlink(binary);
    int fd = creat(binary, 0755);
    if (fd < 0) {
        mzCloseZipArchive(zip);
#ifndef USE_CHINESE_FONT
        LOGE("Can't make %s\n", binary);
#else
        LOGE("无法创建 %s\n", binary);
#endif
        return 1;
    }
    bool ok = mzExtractZipEntryToFile(zip, binary_entry, fd);
    close(fd);

    if (!ok) {
#ifndef USE_CHINESE_FONT
        LOGE("Can't copy %s\n", ASSUMED_UPDATE_BINARY_NAME);
#else
        LOGE("无法复制 %s\n", ASSUMED_UPDATE_BINARY_NAME);
#endif
        mzCloseZipArchive(zip);
        return 1;
    }

    int pipefd[2];
    pipe(pipefd);

    // When executing the update binary contained in the package, the
    // arguments passed are:
    //
    //   - the version number for this interface
    //
    //   - an fd to which the program can write in order to update the
    //     progress bar.  The program can write single-line commands:
    //
    //        progress <frac> <secs>
    //            fill up the next <frac> part of of the progress bar
    //            over <secs> seconds.  If <secs> is zero, use
    //            set_progress commands to manually control the
    //            progress of this segment of the bar
    //
    //        set_progress <frac>
    //            <frac> should be between 0.0 and 1.0; sets the
    //            progress bar within the segment defined by the most
    //            recent progress command.
    //
    //        firmware <"hboot"|"radio"> <filename>
    //            arrange to install the contents of <filename> in the
    //            given partition on reboot.
    //
    //            (API v2: <filename> may start with "PACKAGE:" to
    //            indicate taking a file from the OTA package.)
    //
    //            (API v3: this command no longer exists.)
    //
    //        ui_print <string>
    //            display <string> on the screen.
    //
    //   - the name of the package zip file.
    //

    char** args = malloc(sizeof(char*) * 5);
    args[0] = binary;
    args[1] = EXPAND(RECOVERY_API_VERSION);   // defined in Android.mk
    args[2] = malloc(10);
    sprintf(args[2], "%d", pipefd[1]);
    args[3] = (char*)path;
    args[4] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        setenv("UPDATE_PACKAGE", path, 1);
        close(pipefd[0]);
        execv(binary, args);
        fprintf(stdout, "E:Can't run %s (%s)\n", binary, strerror(errno));
        _exit(-1);
    }
    close(pipefd[1]);

    char* firmware_type = NULL;
    char* firmware_filename = NULL;

    char buffer[1024];
    FILE* from_child = fdopen(pipefd[0], "r");
    while (fgets(buffer, sizeof(buffer), from_child) != NULL) {
        char* command = strtok(buffer, " \n");
        if (command == NULL) {
            continue;
        } else if (strcmp(command, "progress") == 0) {
            char* fraction_s = strtok(NULL, " \n");
            char* seconds_s = strtok(NULL, " \n");

            float fraction = strtof(fraction_s, NULL);
            int seconds = strtol(seconds_s, NULL, 10);

            ui_show_progress(fraction * (1-VERIFICATION_PROGRESS_FRACTION),
                             seconds);
        } else if (strcmp(command, "set_progress") == 0) {
            char* fraction_s = strtok(NULL, " \n");
            float fraction = strtof(fraction_s, NULL);
            ui_set_progress(fraction);
        } else if (strcmp(command, "firmware") == 0) {
            char* type = strtok(NULL, " \n");
            char* filename = strtok(NULL, " \n");

            if (type != NULL && filename != NULL) {
                if (firmware_type != NULL) {
#ifndef USE_CHINESE_FONT
                    LOGE("ignoring attempt to do multiple firmware updates");
#else
                    LOGE("忽略多个固件更新的操作");
#endif
                } else {
                    firmware_type = strdup(type);
                    firmware_filename = strdup(filename);
                }
            }
        } else if (strcmp(command, "ui_print") == 0) {
            char* str = strtok(NULL, "\n");
            if (str) {
                ui_print("%s", str);
            } else {
                ui_print("\n");
            }
        } else {
#ifndef USE_CHINESE_FONT
            LOGE("unknown command [%s]\n", command);
#else
            LOGE("未知命令 [%s]\n", command);
#endif
        }
    }
    fclose(from_child);

    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
#ifndef USE_CHINESE_FONT
        LOGE("Error in %s\n(Status %d)\n", path, WEXITSTATUS(status));
#else
        LOGE("%s 中出错\n(状态 %d)\n", path, WEXITSTATUS(status));
#endif
        mzCloseZipArchive(zip);
        return INSTALL_ERROR;
    }

    if (firmware_type != NULL) {
        int ret = handle_firmware_update(firmware_type, firmware_filename, zip);
        mzCloseZipArchive(zip);
        return ret;
    }
    mzCloseZipArchive(zip);
    return INSTALL_SUCCESS;
}

static int
really_install_package(const char *path)
{
    ui_set_background(BACKGROUND_ICON_INSTALLING);
#ifndef USE_CHINESE_FONT
    ui_print("Finding update package...\n");
#else
    ui_print("正在查找刷机包...\n");
#endif
    ui_show_indeterminate_progress();

    // Resolve symlink in case legacy /sdcard path is used
    // Requires: symlink uses absolute path
    char new_path[PATH_MAX];
    if (strlen(path) > 1) {
        char *rest = strchr(path + 1, '/');
        if (rest != NULL) {
            int readlink_length;
            int root_length = rest - path;
            char *root = malloc(root_length + 1);
            strncpy(root, path, root_length);
            root[root_length] = 0;
            readlink_length = readlink(root, new_path, PATH_MAX);
            if (readlink_length > 0) {
                strncpy(new_path + readlink_length, rest, PATH_MAX - readlink_length);
                path = new_path;
            }
            free(root);
        }
    }

#ifndef USE_CHINESE_FONT
    LOGI("Update location: %s\n", path);
#else
    LOGI("刷机包位置: %s\n", path);
#endif

    if (ensure_path_mounted(path) != 0) {
#ifndef USE_CHINESE_FONT
        LOGE("Can't mount %s\n", path);
#else
        LOGE("无法挂载 %s\n", path);
#endif
        return INSTALL_CORRUPT;
    }

#ifndef USE_CHINESE_FONT
    ui_print("Opening update package...\n");
#else
    ui_print("正在打开刷机包...\n");
#endif

    int err;

    if (signature_check_enabled) {
        int numKeys;
        RSAPublicKey* loadedKeys = load_keys(PUBLIC_KEYS_FILE, &numKeys);
        if (loadedKeys == NULL) {
#ifndef USE_CHINESE_FONT
            LOGE("Failed to load keys\n");
#else
            LOGE("无法载入密钥\n");
#endif
            return INSTALL_CORRUPT;
        }
#ifndef USE_CHINESE_FONT
        LOGI("%d key(s) loaded from %s\n", numKeys, PUBLIC_KEYS_FILE);
#else
        LOGI("%d 个密钥已从 %s 中载入\n", numKeys, PUBLIC_KEYS_FILE);
#endif

        // Give verification half the progress bar...
#ifndef USE_CHINESE_FONT
        ui_print("Verifying update package...\n");
#else
        ui_print("正在校验刷机包...\n");
#endif
        ui_show_progress(
                VERIFICATION_PROGRESS_FRACTION,
                VERIFICATION_PROGRESS_TIME);

        err = verify_file(path, loadedKeys, numKeys);
        free(loadedKeys);
#ifndef USE_CHINESE_FONT
        LOGI("verify_file returned %d\n", err);
#else
        LOGI("verify_file 返回 %d\n", err);
#endif
        if (err != VERIFY_SUCCESS) {
#ifndef USE_CHINESE_FONT
            LOGE("signature verification failed\n");
#else
            LOGE("签名校验失败\n");
#endif
            ui_show_text(1);
#ifndef USE_CHINESE_FONT
            if (!confirm_selection("Install Untrusted Package?", "Yes - Install untrusted zip"))
#else
            if (!confirm_selection("刷入不信任的刷机包？", "是 - 刷入不信任的刷机包"))
#endif
                return INSTALL_CORRUPT;
        }
    }

    /* Try to open the package.
     */
    ZipArchive zip;
    err = mzOpenZipArchive(path, &zip);
    if (err != 0) {
#ifndef USE_CHINESE_FONT
        LOGE("Can't open %s\n(%s)\n", path, err != -1 ? strerror(err) : "bad");
#else
        LOGE("无法打开 %s\n(%s)\n", path, err != -1 ? strerror(err) : "已损坏");
#endif
        return INSTALL_CORRUPT;
    }

    /* Verify and install the contents of the package.
     */
#ifndef USE_CHINESE_FONT
    ui_print("Installing update...\n");
#else
    ui_print("正在刷入刷机包...\n");
#endif
    return try_update_binary(path, &zip);
}

int
install_package(const char* path)
{
    FILE* install_log = fopen_path(LAST_INSTALL_FILE, "w");
    if (install_log) {
        fputs(path, install_log);
        fputc('\n', install_log);
    } else {
#ifndef USE_CHINESE_FONT
        LOGE("failed to open last_install: %s\n", strerror(errno));
#else
        LOGE("无法打开 last_install: %s\n", strerror(errno));
#endif
    }
    int result = really_install_package(path);
    if (install_log) {
        fputc(result == INSTALL_SUCCESS ? '1' : '0', install_log);
        fputc('\n', install_log);
        fclose(install_log);
        chmod(LAST_INSTALL_FILE, 0644);
    }
    return result;
}
