#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include <signal.h>
#include <sys/wait.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "firmware.h"
#include "install.h"
#include "make_ext4fs.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"

#include "extendedcommands.h"
#include "nandroid.h"
#include "mounts.h"
#include "flashutils/flashutils.h"
#include "edify/expr.h"
#include <libgen.h>
#include "mtdutils/mtdutils.h"
#include "bmlutils/bmlutils.h"
#include "cutils/android_reboot.h"
#include "mmcutils/mmcutils.h"
#include "voldclient/voldclient.h"

#include "adb_install.h"

#ifdef ENABLE_LOKI
#include "compact_loki.h"
#endif

int signature_check_enabled = 1;
#ifdef ENABLE_LOKI
int loki_support_enabled = 1;
#endif
int script_assert_enabled = 1;
static const char *SDCARD_UPDATE_FILE = "update.zip";

int
get_filtered_menu_selection(const char** headers, char** items, int menu_only, int initial_selection, int items_count) {
    int index;
    int offset = 0;
    int* translate_table = (int*)malloc(sizeof(int) * items_count);
    for (index = 0; index < items_count; index++) {
        if (items[index] == NULL)
            continue;
        char *item = items[index];
        items[index] = NULL;
        items[offset] = item;
        translate_table[offset] = index;
        offset++;
    }
    items[offset] = NULL;

    initial_selection = translate_table[initial_selection];
    int ret = get_menu_selection(headers, items, menu_only, initial_selection);
    if (ret < 0 || ret >= offset) {
        free(translate_table);
        return ret;
    }

    ret = translate_table[ret];
    free(translate_table);
    return ret;
}

void write_string_to_file(const char* filename, const char* string) {
    ensure_path_mounted(filename);
    char tmp[PATH_MAX];
    sprintf(tmp, "mkdir -p $(dirname %s)", filename);
    __system(tmp);
    FILE *file = fopen(filename, "w");
    if( file != NULL) {
        fprintf(file, "%s", string);
        fclose(file);
    }
}

void write_recovery_version() {
    char path[PATH_MAX];
    sprintf(path, "%s%sclockworkmod/.recovery_version", get_primary_storage_path(), (is_data_media() ? "/0/" : "/"));
    write_string_to_file(path,EXPAND(RECOVERY_VERSION) "\n" EXPAND(TARGET_DEVICE));
}

void
toggle_signature_check()
{
    signature_check_enabled = !signature_check_enabled;
#ifndef USE_CHINESE_FONT
    ui_print("Signature Check: %s\n", signature_check_enabled ? "Enabled" : "Disabled");
#else
    ui_print("签名校验: %s\n", signature_check_enabled ? "已启用" : "已禁用");
#endif
}

#ifdef ENABLE_LOKI
void
toggle_loki_support()
{
    loki_support_enabled = !loki_support_enabled;
#ifndef USE_CHINESE_FONT
    ui_print("Loki Support: %s\n", loki_support_enabled ? "Enabled" : "Disabled");
#else
    ui_print("Loki 支持: %s\n", loki_support_enabled ? "已启用" : "已禁用");
#endif
}
#endif

int install_zip(const char* packagefilepath)
{
#ifndef USE_CHINESE_FONT
    ui_print("\n-- Installing: %s\n", packagefilepath);
#else
    ui_print("\n-- 正在刷机: %s\n", packagefilepath);
#endif
    if (device_flash_type() == MTD) {
        set_sdcard_update_bootloader_message();
    }
    int status = install_package(packagefilepath);
    ui_reset_progress();
    if (status != INSTALL_SUCCESS) {
        ui_set_background(BACKGROUND_ICON_ERROR);
#ifndef USE_CHINESE_FONT
        ui_print("Installation aborted.\n");
#else
        ui_print("刷机已中止。\n");
#endif
        return 1;
    }
#ifdef ENABLE_LOKI
    if(loki_support_enabled) {
#ifndef USE_CHINESE_FONT
       ui_print("Checking if loki-fying is needed");
#else
       ui_print("正在检查是否需要 loki-fying");
#endif
       int result;
       if(result = loki_check()) {
           return result;
       }
    }
#endif
    ui_set_background(BACKGROUND_ICON_NONE);
#ifndef USE_CHINESE_FONT
    ui_print("\nInstall from sdcard complete.\n");
#else
    ui_print("\n已完成 SD 卡中刷机包的刷入。\n");
#endif
    return 0;
}

#define ITEM_CHOOSE_ZIP       0
#define ITEM_APPLY_SIDELOAD   1
#define ITEM_SIG_CHECK        2
#define ITEM_CHOOSE_ZIP_INT   3

int show_install_update_menu()
{
    char buf[100];
    int i = 0, chosen_item = 0;
    static char* install_menu_items[MAX_NUM_MANAGED_VOLUMES + 3];

    char* primary_path = get_primary_storage_path();
    char** extra_paths = get_extra_storage_paths();
    int num_extra_volumes = get_num_extra_volumes();

    memset(install_menu_items, 0, MAX_NUM_MANAGED_VOLUMES + 3);

#ifndef USE_CHINESE_FONT
    static const char* headers[] = {  "Install update from zip file",
#else
    static const char* headers[] = {  "刷入 zip 刷机包",
#endif
                                "",
                                NULL
    };

#ifndef USE_CHINESE_FONT
    sprintf(buf, "choose zip from %s", primary_path);
#else
    sprintf(buf, "从 %s 中选择刷机包", primary_path);
#endif
    install_menu_items[0] = strdup(buf);

#ifndef USE_CHINESE_FONT
    install_menu_items[1] = "install zip from sideload";
#else
    install_menu_items[1] = "使用 sideload 方式刷机";
#endif

#ifndef USE_CHINESE_FONT
    install_menu_items[2] = "toggle signature verification";
#else
    install_menu_items[2] = "切换签名校验";
#endif

    install_menu_items[3 + num_extra_volumes] = NULL;

    for (i = 0; i < num_extra_volumes; i++) {
#ifndef USE_CHINESE_FONT
        sprintf(buf, "choose zip from %s", extra_paths[i]);
#else
        sprintf(buf, "从 %s 中选择刷机包", extra_paths[i]);
#endif
        install_menu_items[3 + i] = strdup(buf);
    }

    for (;;)
    {
        chosen_item = get_menu_selection(headers, install_menu_items, 0, 0);
        switch (chosen_item)
        {
            case ITEM_SIG_CHECK:
                toggle_signature_check();
                break;
            case ITEM_CHOOSE_ZIP:
                show_choose_zip_menu(primary_path);
                write_recovery_version();
                break;
            case ITEM_APPLY_SIDELOAD:
                apply_from_adb();
                break;
            default:
                if (chosen_item >= ITEM_CHOOSE_ZIP_INT) {
                    show_choose_zip_menu(extra_paths[chosen_item - 3]);
                } else {
                    goto out;
                }
        }
    }
out:
    // free all the dynamic items
    free(install_menu_items[0]);
    if (extra_paths != NULL) {
        for (i = 0; i < num_extra_volumes; i++)
            free(install_menu_items[3 + i]);
    }
    return chosen_item;
}

void free_string_array(char** array)
{
    if (array == NULL)
        return;
    char* cursor = array[0];
    int i = 0;
    while (cursor != NULL)
    {
        free(cursor);
        cursor = array[++i];
    }
    free(array);
}

char** gather_files(const char* directory, const char* fileExtensionOrDirectory, int* numFiles)
{
    char path[PATH_MAX] = "";
    DIR *dir;
    struct dirent *de;
    int total = 0;
    int i;
    char** files = NULL;
    int pass;
    *numFiles = 0;
    int dirLen = strlen(directory);

    dir = opendir(directory);
    if (dir == NULL) {
#ifndef USE_CHINESE_FONT
        ui_print("Couldn't open directory.\n");
#else
        ui_print("无法打开文件夹。\n");
#endif
        return NULL;
    }

    unsigned int extension_length = 0;
    if (fileExtensionOrDirectory != NULL)
        extension_length = strlen(fileExtensionOrDirectory);

    int isCounting = 1;
    i = 0;
    for (pass = 0; pass < 2; pass++) {
        while ((de=readdir(dir)) != NULL) {
            // skip hidden files
            if (de->d_name[0] == '.')
                continue;

            // NULL means that we are gathering directories, so skip this
            if (fileExtensionOrDirectory != NULL)
            {
                // make sure that we can have the desired extension (prevent seg fault)
                if (strlen(de->d_name) < extension_length)
                    continue;
                // compare the extension
                if (strcmp(de->d_name + strlen(de->d_name) - extension_length, fileExtensionOrDirectory) != 0)
                    continue;
            }
            else
            {
                struct stat info;
                char fullFileName[PATH_MAX];
                strcpy(fullFileName, directory);
                strcat(fullFileName, de->d_name);
                lstat(fullFileName, &info);
                // make sure it is a directory
                if (!(S_ISDIR(info.st_mode)))
                    continue;
            }

            if (pass == 0)
            {
                total++;
                continue;
            }

            files[i] = (char*) malloc(dirLen + strlen(de->d_name) + 2);
            strcpy(files[i], directory);
            strcat(files[i], de->d_name);
            if (fileExtensionOrDirectory == NULL)
                strcat(files[i], "/");
            i++;
        }
        if (pass == 1)
            break;
        if (total == 0)
            break;
        rewinddir(dir);
        *numFiles = total;
        files = (char**) malloc((total+1)*sizeof(char*));
        files[total]=NULL;
    }

    if(closedir(dir) < 0) {
#ifndef USE_CHINESE_FONT
        LOGE("Failed to close directory.");
#else
        LOGE("无法关闭文件夹。");
#endif
    }

    if (total==0) {
        return NULL;
    }

    // sort the result
    if (files != NULL) {
        for (i = 0; i < total; i++) {
            int curMax = -1;
            int j;
            for (j = 0; j < total - i; j++) {
                if (curMax == -1 || strcmp(files[curMax], files[j]) < 0)
                    curMax = j;
            }
            char* temp = files[curMax];
            files[curMax] = files[total - i - 1];
            files[total - i - 1] = temp;
        }
    }

    return files;
}

// pass in NULL for fileExtensionOrDirectory and you will get a directory chooser
char* choose_file_menu(const char* basedir, const char* fileExtensionOrDirectory, const char* headers[])
{
    static const char* fixed_headers[20];
    char path[PATH_MAX] = "";
    DIR *dir;
    struct dirent *de;
    int numFiles = 0;
    int numDirs = 0;
    int i;
    char* return_value = NULL;
    char directory[PATH_MAX];
    int dir_len = strlen(basedir);

    strcpy(directory, basedir);

    // Append a traiing slash if necessary
    if (directory[dir_len - 1] != '/') {
        strcat(directory, "/");
        dir_len++;
    }

    i = 0;
    while (headers[i]) {
        i++;
    }
    i = 0;
    while (headers[i]) {
        fixed_headers[i] = headers[i];
        i++;
    }
    fixed_headers[i] = directory;
    fixed_headers[i + 1] = "";
    fixed_headers[i + 2 ] = NULL;

    char** files = gather_files(directory, fileExtensionOrDirectory, &numFiles);
    char** dirs = NULL;
    if (fileExtensionOrDirectory != NULL)
        dirs = gather_files(directory, NULL, &numDirs);
    int total = numDirs + numFiles;
    if (total == 0)
    {
#ifndef USE_CHINESE_FONT
        ui_print("No files found.\n");
#else
        ui_print("未找到文件。\n");
#endif
    }
    else
    {
        char** list = (char**) malloc((total + 1) * sizeof(char*));
        list[total] = NULL;


        for (i = 0 ; i < numDirs; i++)
        {
            list[i] = strdup(dirs[i] + dir_len);
        }

        for (i = 0 ; i < numFiles; i++)
        {
            list[numDirs + i] = strdup(files[i] + dir_len);
        }

        for (;;)
        {
            int chosen_item = get_menu_selection(fixed_headers, list, 0, 0);
            if (chosen_item == GO_BACK || chosen_item == REFRESH)
                break;
            static char ret[PATH_MAX];
            if (chosen_item < numDirs)
            {
                char* subret = choose_file_menu(dirs[chosen_item], fileExtensionOrDirectory, headers);
                if (subret != NULL)
                {
                    strcpy(ret, subret);
                    return_value = ret;
                    break;
                }
                continue;
            }
            strcpy(ret, files[chosen_item - numDirs]);
            return_value = ret;
            break;
        }
        free_string_array(list);
    }

    free_string_array(files);
    free_string_array(dirs);
    return return_value;
}

void show_choose_zip_menu(const char *mount_point)
{
    if (ensure_path_mounted(mount_point) != 0) {
#ifndef USE_CHINESE_FONT
        LOGE ("Can't mount %s\n", mount_point);
#else
        LOGE ("无法挂载 %s\n", mount_point);
#endif
        return;
    }

#ifndef USE_CHINESE_FONT
    static const char* headers[] = {  "Choose a zip to apply",
#else
    static const char* headers[] = {  "选择要刷入的刷机包以进行刷机",
#endif
                                "",
                                NULL
    };

    char* file = choose_file_menu(mount_point, ".zip", headers);
    if (file == NULL)
        return;
#ifndef USE_CHINESE_FONT
    static char* confirm_install  = "Confirm install?";
#else
    static char* confirm_install  = "确认刷机？";
#endif
    static char confirm[PATH_MAX];
#ifndef USE_CHINESE_FONT
    sprintf(confirm, "Yes - Install %s", basename(file));
#else
    sprintf(confirm, "是 - 刷入 %s", basename(file));
#endif
    if (confirm_selection(confirm_install, confirm))
        install_zip(file);
}

void show_nandroid_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
#ifndef USE_CHINESE_FONT
        LOGE("Can't mount %s\n", path);
#else
        LOGE("无法挂载 %s\n", path);
#endif
        return;
    }

#ifndef USE_CHINESE_FONT
    static const char* headers[] = {  "Choose an image to restore",
#else
    static const char* headers[] = {  "选择要还原的镜像",
#endif
                                "",
                                NULL
    };

    char tmp[PATH_MAX];
    sprintf(tmp, "%s/clockworkmod/backup/", path);
    char* file = choose_file_menu(tmp, NULL, headers);
    if (file == NULL)
        return;

#ifndef USE_CHINESE_FONT
    if (confirm_selection("Confirm restore?", "Yes - Restore"))
#else
    if (confirm_selection("确认还原？", "是 - 还原"))
#endif
        nandroid_restore(file, 1, 1, 1, 1, 1, 0);
}

void show_nandroid_delete_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
#ifndef USE_CHINESE_FONT
        LOGE("Can't mount %s\n", path);
#else
        LOGE("无法挂载 %s\n", path);
#endif
        return;
    }

#ifndef USE_CHINESE_FONT
    static const char* headers[] = {  "Choose an image to delete",
#else
    static const char* headers[] = {  "选择要删除的镜像",
#endif
                                "",
                                NULL
    };

    char tmp[PATH_MAX];
    sprintf(tmp, "%s/clockworkmod/backup/", path);
    char* file = choose_file_menu(tmp, NULL, headers);
    if (file == NULL)
        return;

#ifndef USE_CHINESE_FONT
    if (confirm_selection("Confirm delete?", "Yes - Delete")) {
#else
    if (confirm_selection("确认删除？", "是 - 删除")) {
#endif
        // nandroid_restore(file, 1, 1, 1, 1, 1, 0);
        sprintf(tmp, "rm -rf %s", file);
        __system(tmp);
    }
}

static int control_usb_storage(bool on)
{
    int i = 0;
    int num = 0;

    for (i = 0; i < get_num_volumes(); i++) {
        Volume *v = get_device_volumes() + i;
        if (fs_mgr_is_voldmanaged(v) && vold_is_volume_available(v->mount_point)) {
            if (on) {
                vold_share_volume(v->mount_point);
            } else {
                vold_unshare_volume(v->mount_point, 1);
            }
            property_set("sys.storage.ums_enabled", on ? "1" : "0");
            num++;
        }
    }
    return num;
}

void show_mount_usb_storage_menu()
{
    // Enable USB storage using vold
    if (!control_usb_storage(true))
        return;

#ifndef USE_CHINESE_FONT
    static const char* headers[] = {  "USB Mass Storage device",
                                "Leaving this menu unmounts",
                                "your SD card from your PC.",
#else
    static const char* headers[] = {  "U 盘模式",
                                "离开当前菜单即会停",
                                "用 U 盘模式。",
#endif
                                "",
                                NULL
    };

#ifndef USE_CHINESE_FONT
    static char* list[] = { "Unmount", NULL };
#else
    static char* list[] = { "停用", NULL };
#endif

    for (;;)
    {
        int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK || chosen_item == 0)
            break;
    }

    // Disable USB storage
    control_usb_storage(false);
}

int confirm_selection(const char* title, const char* confirm)
{
    struct stat info;
    int ret = 0;

    if (0 == stat("/sdcard/clockworkmod/.no_confirm", &info))
        return 1;

    char* confirm_str = strdup(confirm);
#ifndef USE_CHINESE_FONT
    const char* confirm_headers[]  = {  title, "  THIS CAN NOT BE UNDONE.", "", NULL };
#else
    const char* confirm_headers[]  = {  title, "  本操作是不可逆的。", "", NULL };
#endif
    int one_confirm = 0 == stat("/sdcard/clockworkmod/.one_confirm", &info);
#ifdef BOARD_TOUCH_RECOVERY
    one_confirm = 1;
#endif
    if (one_confirm) {
#ifndef USE_CHINESE_FONT
        char* items[] = { "No",
#else
        char* items[] = { "否",
#endif
                        confirm_str, //" Yes -- wipe partition",   // [1]
                        NULL };
        int chosen_item = get_menu_selection(confirm_headers, items, 0, 0);
        ret = (chosen_item == 1);
    }
    else {
#ifndef USE_CHINESE_FONT
        char* items[] = { "No",
                        "No",
                        "No",
                        "No",
                        "No",
                        "No",
                        "No",
                        confirm_str, //" Yes -- wipe partition",   // [7]
                        "No",
                        "No",
                        "No",
#else
        char* items[] = { "否",
                        "否",
                        "否",
                        "否",
                        "否",
                        "否",
                        "否",
                        confirm_str, //" Yes -- wipe partition",   // [7]
                        "否",
                        "否",
                        "否",
#endif
                        NULL };
        int chosen_item = get_menu_selection(confirm_headers, items, 0, 0);
        ret = (chosen_item == 7);
    }
    free(confirm_str);
    return ret;
}

#define MKE2FS_BIN      "/sbin/mke2fs"
#define TUNE2FS_BIN     "/sbin/tune2fs"
#define E2FSCK_BIN      "/sbin/e2fsck"
extern void reset_ext4fs_info();

extern struct selabel_handle *sehandle;
int format_device(const char *device, const char *path, const char *fs_type) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        // silent failure for sd-ext
        if (strcmp(path, "/sd-ext") == 0)
            return -1;
#ifndef USE_CHINESE_FONT
        LOGE("unknown volume \"%s\"\n", path);
#else
        LOGE("未知卷 \"%s\"\n", path);
#endif
        return -1;
    }
    if (is_data_media_volume_path(path)) {
        return format_unknown_device(NULL, path, NULL);
    }
    if (strstr(path, "/data") == path && is_data_media()) {
        return format_unknown_device(NULL, path, NULL);
    }
    if (strcmp(fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
#ifndef USE_CHINESE_FONT
        LOGE("can't format_volume \"%s\"", path);
#else
        LOGE("无法格式化卷 \"%s\"", path);
#endif
        return -1;
    }

    if (strcmp(fs_type, "rfs") == 0) {
        if (ensure_path_unmounted(path) != 0) {
#ifndef USE_CHINESE_FONT
            LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
#else
            LOGE("format_volume 卸载 \"%s\" 时出错\n", v->mount_point);
#endif
            return -1;
        }
        if (0 != format_rfs_device(device, path)) {
#ifndef USE_CHINESE_FONT
            LOGE("format_volume: format_rfs_device failed on %s\n", device);
#else
            LOGE("format_volume: 使用 format_rfs_device 格式化 %s 时出错\n", device);
#endif
            return -1;
        }
        return 0;
    }
 
    if (strcmp(v->mount_point, path) != 0) {
        return format_unknown_device(v->blk_device, path, NULL);
    }

    if (ensure_path_unmounted(path) != 0) {
#ifndef USE_CHINESE_FONT
        LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
#else
        LOGE("format_volume 卸载 \"%s\" 时出错\n", v->mount_point);
#endif
        return -1;
    }

    if (strcmp(fs_type, "yaffs2") == 0 || strcmp(fs_type, "mtd") == 0) {
        mtd_scan_partitions();
        const MtdPartition* partition = mtd_find_partition_by_name(device);
        if (partition == NULL) {
#ifndef USE_CHINESE_FONT
            LOGE("format_volume: no MTD partition \"%s\"\n", device);
#else
            LOGE("format_volume: 无 MTD 分区 \"%s\"\n", device);
#endif
            return -1;
        }

        MtdWriteContext *write = mtd_write_partition(partition);
        if (write == NULL) {
#ifndef USE_CHINESE_FONT
            LOGW("format_volume: can't open MTD \"%s\"\n", device);
#else
            LOGW("format_volume: 无法打开 MTD 设备 \"%s\"\n", device);
#endif
            return -1;
        } else if (mtd_erase_blocks(write, -1) == (off_t) -1) {
#ifndef USE_CHINESE_FONT
            LOGW("format_volume: can't erase MTD \"%s\"\n", device);
#else
            LOGW("format_volume: 无法擦除 MTD 设备 \"%s\"\n", device);
#endif
            mtd_write_close(write);
            return -1;
        } else if (mtd_write_close(write)) {
#ifndef USE_CHINESE_FONT
            LOGW("format_volume: can't close MTD \"%s\"\n",device);
#else
            LOGW("format_volume: 无法关闭 MTD 设备 \"%s\"\n",device);
#endif
            return -1;
        }
        return 0;
    }

    if (strcmp(fs_type, "ext4") == 0) {
        int length = 0;
        if (strcmp(v->fs_type, "ext4") == 0) {
            // Our desired filesystem matches the one in fstab, respect v->length
            length = v->length;
        }
        reset_ext4fs_info();
        int result = make_ext4fs(device, length, v->mount_point, sehandle);
        if (result != 0) {
#ifndef USE_CHINESE_FONT
            LOGE("format_volume: make_extf4fs failed on %s\n", device);
#else
            LOGE("format_volume: 在设备 %s 上执行 make_extf4fs 时出错\n", device);
#endif
            return -1;
        }
        return 0;
    }

    return format_unknown_device(device, path, fs_type);
}

int format_unknown_device(const char *device, const char* path, const char *fs_type)
{
#ifndef USE_CHINESE_FONT
    LOGI("Formatting unknown device.\n");
#else
    LOGI("正在格式化未知设备。\n");
#endif

    if (fs_type != NULL && get_flash_type(fs_type) != UNSUPPORTED)
        return erase_raw_partition(fs_type, device);

    // if this is SDEXT:, don't worry about it if it does not exist.
    if (0 == strcmp(path, "/sd-ext"))
    {
        struct stat st;
        Volume *vol = volume_for_path("/sd-ext");
        if (vol == NULL || 0 != stat(vol->blk_device, &st))
        {
#ifndef USE_CHINESE_FONT
            LOGI("No app2sd partition found. Skipping format of /sd-ext.\n");
#else
            LOGI("未找到 app2sd 分区。跳过对 /sd-ext 的格式化。\n");
#endif
            return 0;
        }
    }

    if (NULL != fs_type) {
        if (strcmp("ext3", fs_type) == 0) {
#ifndef USE_CHINESE_FONT
            LOGI("Formatting ext3 device.\n");
#else
            LOGI("格式化 ext3 设备。\n");
#endif
            if (0 != ensure_path_unmounted(path)) {
#ifndef USE_CHINESE_FONT
                LOGE("Error while unmounting %s.\n", path);
#else
                LOGE("卸载 %s 时出错。\n", path);
#endif
                return -12;
            }
            return format_ext3_device(device);
        }

        if (strcmp("ext2", fs_type) == 0) {
#ifndef USE_CHINESE_FONT
            LOGI("Formatting ext2 device.\n");
#else
            LOGI("格式化 ext2 设备。\n");
#endif
            if (0 != ensure_path_unmounted(path)) {
#ifndef USE_CHINESE_FONT
                LOGE("Error while unmounting %s.\n", path);
#else
                LOGE("卸载 %s 时出错。\n", path);
#endif
                return -12;
            }
            return format_ext2_device(device);
        }
    }

    if (0 != ensure_path_mounted(path))
    {
#ifndef USE_CHINESE_FONT
        ui_print("Error mounting %s!\n", path);
        ui_print("Skipping format...\n");
#else
        ui_print("挂载 %s 时出错！\n", path);
        ui_print("跳过格式化...\n");
#endif
        return 0;
    }

    static char tmp[PATH_MAX];
    if (strcmp(path, "/data") == 0) {
        sprintf(tmp, "cd /data ; for f in $(ls -a | grep -v ^media$); do rm -rf $f; done");
        __system(tmp);
        // if the /data/media sdcard has already been migrated for android 4.2,
        // prevent the migration from happening again by writing the .layout_version
        struct stat st;
        if (0 == lstat("/data/media/0", &st)) {
            char* layout_version = "2";
            FILE* f = fopen("/data/.layout_version", "wb");
            if (NULL != f) {
                fwrite(layout_version, 1, 2, f);
                fclose(f);
            }
            else {
#ifndef USE_CHINESE_FONT
                LOGI("error opening /data/.layout_version for write.\n");
#else
                LOGI("打开 /data/.layout_version 进行写入时出错。\n");
#endif
            }
        }
        else {
#ifndef USE_CHINESE_FONT
            LOGI("/data/media/0 not found. migration may occur.\n");
#else
            LOGI("未找到 /data/media/0。可能已被移动到其他位置。\n");
#endif
        }
    }
    else {
        sprintf(tmp, "rm -rf %s/*", path);
        __system(tmp);
        sprintf(tmp, "rm -rf %s/.*", path);
        __system(tmp);
    }

    ensure_path_unmounted(path);
    return 0;
}

//#define MOUNTABLE_COUNT 5
//#define DEVICE_COUNT 4
//#define MMC_COUNT 2

typedef struct {
    char mount[255];
    char unmount[255];
    char path[PATH_MAX];
} MountMenuEntry;

typedef struct {
    char txt[255];
    char path[PATH_MAX];
} FormatMenuEntry;

int is_safe_to_format(char* name)
{
    char str[255];
    char* partition;
    property_get("ro.cwm.forbid_format", str, "/misc,/radio,/bootloader,/recovery,/efs,/wimax");

    partition = strtok(str, ", ");
    while (partition != NULL) {
        if (strcmp(name, partition) == 0) {
            return 0;
        }
        partition = strtok(NULL, ", ");
    }

    return 1;
}

int show_partition_menu()
{
#ifndef USE_CHINESE_FONT
    static const char* headers[] = {  "Mounts and Storage Menu",
#else
    static const char* headers[] = {  "挂载及 U 盘模式",
#endif
                                "",
                                NULL
    };

#ifndef USE_CHINESE_FONT
    static char* confirm_format  = "Confirm format?";
    static char* confirm = "Yes - Format";
#else
    static char* confirm_format  = "确认格式化？";
    static char* confirm = "是 - 格式化";
#endif
    char confirm_string[255];

    static MountMenuEntry* mount_menu = NULL;
    static FormatMenuEntry* format_menu = NULL;
    static char* list[256];

    int i, mountable_volumes, formatable_volumes;
    int num_volumes;
    int chosen_item = 0;

    num_volumes = get_num_volumes();

    if(!num_volumes)
        return 0;

    mountable_volumes = 0;
    formatable_volumes = 0;

    mount_menu = malloc(num_volumes * sizeof(MountMenuEntry));
    format_menu = malloc(num_volumes * sizeof(FormatMenuEntry));

    for (i = 0; i < num_volumes; i++) {
        Volume* v = get_device_volumes() + i;

        if (fs_mgr_is_voldmanaged(v) && !vold_is_volume_available(v->mount_point)) {
            continue;
        }

        if(strcmp("ramdisk", v->fs_type) != 0 && strcmp("mtd", v->fs_type) != 0 && strcmp("emmc", v->fs_type) != 0 && strcmp("bml", v->fs_type) != 0) {
            if (strcmp("datamedia", v->fs_type) != 0) {
#ifndef USE_CHINESE_FONT
                sprintf(mount_menu[mountable_volumes].mount, "mount %s", v->mount_point);
                sprintf(mount_menu[mountable_volumes].unmount, "unmount %s", v->mount_point);
#else
                sprintf(mount_menu[mountable_volumes].mount, "挂载 %s", v->mount_point);
                sprintf(mount_menu[mountable_volumes].unmount, "卸载 %s", v->mount_point);
#endif
                sprintf(mount_menu[mountable_volumes].path, "%s", v->mount_point);
                ++mountable_volumes;
            }
            if (is_safe_to_format(v->mount_point)) {
#ifndef USE_CHINESE_FONT
                sprintf(format_menu[formatable_volumes].txt, "format %s", v->mount_point);
#else
                sprintf(format_menu[formatable_volumes].txt, "格式化 %s", v->mount_point);
#endif
                sprintf(format_menu[formatable_volumes].path, "%s", v->mount_point);
                ++formatable_volumes;
            }
        }
        else if (strcmp("ramdisk", v->fs_type) != 0 && strcmp("mtd", v->fs_type) == 0 && is_safe_to_format(v->mount_point))
        {
#ifndef USE_CHINESE_FONT
            sprintf(format_menu[formatable_volumes].txt, "format %s", v->mount_point);
#else
            sprintf(format_menu[formatable_volumes].txt, "格式化 %s", v->mount_point);
#endif
            sprintf(format_menu[formatable_volumes].path, "%s", v->mount_point);
            ++formatable_volumes;
        }
    }

    for (;;)
    {
        for (i = 0; i < mountable_volumes; i++)
        {
            MountMenuEntry* e = &mount_menu[i];
            if(is_path_mounted(e->path))
                list[i] = e->unmount;
            else
                list[i] = e->mount;
        }

        for (i = 0; i < formatable_volumes; i++)
        {
            FormatMenuEntry* e = &format_menu[i];
            list[mountable_volumes+i] = e->txt;
        }

        if (!is_data_media()) {
#ifndef USE_CHINESE_FONT
            list[mountable_volumes + formatable_volumes] = "mount USB storage";
#else
            list[mountable_volumes + formatable_volumes] = "开启 U 盘模式";
#endif
            list[mountable_volumes + formatable_volumes + 1] = '\0';
        } else {
#ifndef USE_CHINESE_FONT
            list[mountable_volumes + formatable_volumes] = "format /data and /data/media (/sdcard)";
            list[mountable_volumes + formatable_volumes + 1] = "mount USB storage";
#else
            list[mountable_volumes + formatable_volumes] = "格式化 /data 和 /data/media (/sdcard)";
            list[mountable_volumes + formatable_volumes + 1] = "开启 U 盘模式";
#endif
            list[mountable_volumes + formatable_volumes + 2] = '\0';
        }

        chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK || chosen_item == REFRESH)
            break;
        if (chosen_item == (mountable_volumes+formatable_volumes)) {
            if (!is_data_media()) {
                show_mount_usb_storage_menu();
            }
            else {
#ifndef USE_CHINESE_FONT
                if (!confirm_selection("format /data and /data/media (/sdcard)", confirm))
#else
                if (!confirm_selection("格式化 /data 和 /data/media (/sdcard)", confirm))
#endif
                    continue;
                ignore_data_media_workaround(1);
#ifndef USE_CHINESE_FONT
                ui_print("Formatting /data...\n");
#else
                ui_print("正在格式化 /data...\n");
#endif
                if (0 != format_volume("/data"))
#ifndef USE_CHINESE_FONT
                    ui_print("Error formatting /data!\n");
#else
                    ui_print("格式化 /data 时出错！\n");
#endif
                else
#ifndef USE_CHINESE_FONT
                    ui_print("Done.\n");
#else
                    ui_print("完成。\n");
#endif
                ignore_data_media_workaround(0);
            }
        }
        else if (is_data_media() && chosen_item == (mountable_volumes+formatable_volumes+1)) {
            show_mount_usb_storage_menu();
        }
        else if (chosen_item < mountable_volumes) {
            MountMenuEntry* e = &mount_menu[chosen_item];

            if (is_path_mounted(e->path))
            {
                if (0 != ensure_path_unmounted(e->path))
#ifndef USE_CHINESE_FONT
                    ui_print("Error unmounting %s!\n", e->path);
#else
                    ui_print("卸载 %s 时出错！\n", e->path);
#endif
            }
            else
            {
                if (0 != ensure_path_mounted(e->path))
#ifndef USE_CHINESE_FONT
                    ui_print("Error mounting %s!\n",  e->path);
#else
                    ui_print("挂载 %s 时出错！\n",  e->path);
#endif
            }
        }
        else if (chosen_item < (mountable_volumes + formatable_volumes))
        {
            chosen_item = chosen_item - mountable_volumes;
            FormatMenuEntry* e = &format_menu[chosen_item];

            sprintf(confirm_string, "%s - %s", e->path, confirm_format);

            if (!confirm_selection(confirm_string, confirm))
                continue;
#ifndef USE_CHINESE_FONT
            ui_print("Formatting %s...\n", e->path);
#else
            ui_print("正在格式化 %s...\n", e->path);
#endif
            if (0 != format_volume(e->path))
#ifndef USE_CHINESE_FONT
                ui_print("Error formatting %s!\n", e->path);
#else
                ui_print("格式化 %s 时出错！\n", e->path);
#endif
            else
#ifndef USE_CHINESE_FONT
                ui_print("Done.\n");
#else
                ui_print("完成。\n");
#endif
        }
    }

    free(mount_menu);
    free(format_menu);
    return chosen_item;
}

void show_nandroid_advanced_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
#ifndef USE_CHINESE_FONT
        LOGE ("Can't mount sdcard\n");
#else
        LOGE ("无法挂载 SD 卡\n");
#endif
        return;
    }

#ifndef USE_CHINESE_FONT
    static const char* advancedheaders[] = {  "Choose an image to restore",
                                "",
                                "Choose an image to restore",
                                "first. The next menu will",
                                "show you more options.",
#else
    static const char* advancedheaders[] = {  "选择一个要还原的镜像",
                                "",
                                "首先选择一个要还原的镜",
                                "像。接下来的菜单会有更",
                                "多的选项以供选择。",
#endif
                                "",
                                NULL
    };

    char tmp[PATH_MAX];
    sprintf(tmp, "%s/clockworkmod/backup/", path);
    char* file = choose_file_menu(tmp, NULL, advancedheaders);
    if (file == NULL)
        return;

#ifndef USE_CHINESE_FONT
    static const char* headers[] = {  "Advanced Restore",
#else
    static const char* headers[] = {  "高级还原",
#endif
                                "",
                                NULL
    };

#ifndef USE_CHINESE_FONT
    static char* list[] = { "Restore boot",
                            "Restore system",
                            "Restore data",
                            "Restore cache",
                            "Restore sd-ext",
                            "Restore wimax",
#else
    static char* list[] = { "还原 boot",
                            "还原 system",
                            "还原 data",
                            "还原 cache",
                            "还原 sd-ext",
                            "还原 wimax",
#endif
                            NULL
    };
    
    if (0 != get_partition_device("wimax", tmp)) {
        // disable wimax restore option
        list[5] = NULL;
    }

#ifndef USE_CHINESE_FONT
    static char* confirm_restore  = "Confirm restore?";
#else
    static char* confirm_restore  = "确认还原？";
#endif

    int chosen_item = get_menu_selection(headers, list, 0, 0);
    switch (chosen_item)
    {
        case 0:
#ifndef USE_CHINESE_FONT
            if (confirm_selection(confirm_restore, "Yes - Restore boot"))
#else
            if (confirm_selection(confirm_restore, "是 - 还原 boot"))
#endif
                nandroid_restore(file, 1, 0, 0, 0, 0, 0);
            break;
        case 1:
#ifndef USE_CHINESE_FONT
            if (confirm_selection(confirm_restore, "Yes - Restore system"))
#else
            if (confirm_selection(confirm_restore, "是 - 还原 system"))
#endif
                nandroid_restore(file, 0, 1, 0, 0, 0, 0);
            break;
        case 2:
#ifndef USE_CHINESE_FONT
            if (confirm_selection(confirm_restore, "Yes - Restore data"))
#else
            if (confirm_selection(confirm_restore, "是 - 还原 data"))
#endif
                nandroid_restore(file, 0, 0, 1, 0, 0, 0);
            break;
        case 3:
#ifndef USE_CHINESE_FONT
            if (confirm_selection(confirm_restore, "Yes - Restore cache"))
#else
            if (confirm_selection(confirm_restore, "是 - 还原 cache"))
#endif
                nandroid_restore(file, 0, 0, 0, 1, 0, 0);
            break;
        case 4:
#ifndef USE_CHINESE_FONT
            if (confirm_selection(confirm_restore, "Yes - Restore sd-ext"))
#else
            if (confirm_selection(confirm_restore, "是 - 还原 sd-ext"))
#endif
                nandroid_restore(file, 0, 0, 0, 0, 1, 0);
            break;
        case 5:
#ifndef USE_CHINESE_FONT
            if (confirm_selection(confirm_restore, "Yes - Restore wimax"))
#else
            if (confirm_selection(confirm_restore, "是 - 还原 wimax"))
#endif
                nandroid_restore(file, 0, 0, 0, 0, 0, 1);
            break;
    }
}

static void run_dedupe_gc() {
    char path[PATH_MAX];
    char* fmt = "%s/clockworkmod/blobs";
    char* primary_path = get_primary_storage_path();
    char** extra_paths = get_extra_storage_paths();
    int i = 0;

    sprintf(path, fmt, primary_path); 
    ensure_path_mounted(primary_path);
    nandroid_dedupe_gc(path);

    if (extra_paths != NULL) {
        for (i = 0; i < get_num_extra_volumes(); i++) {
            ensure_path_mounted(extra_paths[i]);
            sprintf(path, fmt, extra_paths[i]);
            nandroid_dedupe_gc(path);
        }
    }
}

static void choose_default_backup_format() {
#ifndef USE_CHINESE_FONT
    static const char* headers[] = {  "Default Backup Format",
#else
    static const char* headers[] = {  "默认备份格式",
#endif
                                "",
                                NULL
    };

    int fmt = nandroid_get_default_backup_format();

    char **list;
#ifndef USE_CHINESE_FONT
    char* list_tar_default[] = { "tar (default)",
        "dup",
#else
    char* list_tar_default[] = { "tar (默认)",
        "增量备份",
#endif
        "tar + gzip",
        NULL
    };
    char* list_dup_default[] = { "tar",
#ifndef USE_CHINESE_FONT
        "dup (default)",
#else
        "增量备份 (默认)",
#endif
        "tar + gzip",
        NULL
    };
    char* list_tgz_default[] = { "tar",
#ifndef USE_CHINESE_FONT
        "dup",
        "tar + gzip (default)",
#else
        "增量备份",
        "tar + gzip (默认)",
#endif
        NULL
    };
    if (fmt == NANDROID_BACKUP_FORMAT_DUP) {
        list = list_dup_default;
    } else if (fmt == NANDROID_BACKUP_FORMAT_TGZ) {
        list = list_tgz_default;
    } else {
        list = list_tar_default;
    }

    int chosen_item = get_menu_selection(headers, list, 0, 0);
    switch (chosen_item) {
        case 0:
            write_string_to_file(NANDROID_BACKUP_FORMAT_FILE, "tar");
#ifndef USE_CHINESE_FONT
            ui_print("Default backup format set to tar.\n");
#else
            ui_print("默认的备份格式已设置为 tar。\n");
#endif
            break;
        case 1:
            write_string_to_file(NANDROID_BACKUP_FORMAT_FILE, "dup");
#ifndef USE_CHINESE_FONT
            ui_print("Default backup format set to dedupe.\n");
#else
            ui_print("默认的备份格式已设置为增量备份。\n");
#endif
            break;
        case 2:
            write_string_to_file(NANDROID_BACKUP_FORMAT_FILE, "tgz");
#ifndef USE_CHINESE_FONT
            ui_print("Default backup format set to tar + gzip.\n");
#else
            ui_print("默认的备份格式已设置为 tar + gzip。\n");
#endif
            break;
    }
}

static void add_nandroid_options_for_volume(char** menu, char* path, int offset)
{
    char buf[100];

#ifndef USE_CHINESE_FONT
    sprintf(buf, "backup to %s", path);
#else
    sprintf(buf, "备份到 %s", path);
#endif
    menu[offset] = strdup(buf);

#ifndef USE_CHINESE_FONT
    sprintf(buf, "restore from %s", path);
#else
    sprintf(buf, "从 %s 中选择备份文件进行还原", path);
#endif
    menu[offset + 1] = strdup(buf);

#ifndef USE_CHINESE_FONT
    sprintf(buf, "delete from %s", path);
#else
    sprintf(buf, "删除 %s 中的备份文件", path);
#endif
    menu[offset + 2] = strdup(buf);

#ifndef USE_CHINESE_FONT
    sprintf(buf, "advanced restore from %s", path);
#else
    sprintf(buf, "从 %s 中选择备份文件进行高级还原", path);
#endif
    menu[offset + 3] = strdup(buf);
}

int show_nandroid_menu()
{
    char* primary_path = get_primary_storage_path();
    char** extra_paths = get_extra_storage_paths();
    int num_extra_volumes = get_num_extra_volumes();
    int i = 0, offset = 0, chosen_item = 0;
    char* chosen_path = NULL;
    int max_backup_index = (num_extra_volumes + 1) * 4;

#ifndef USE_CHINESE_FONT
    static const char* headers[] = {  "Backup and Restore",
#else
    static const char* headers[] = {  "备份和还原",
#endif
                                      "",
                                      NULL
    };

    static char* list[((MAX_NUM_MANAGED_VOLUMES + 1) * 4) + 2];

    add_nandroid_options_for_volume(list, primary_path, offset);
    offset += 4;

    if (extra_paths != NULL) {
        for (i = 0; i < num_extra_volumes; i++) {
            add_nandroid_options_for_volume(list, extra_paths[i], offset);
            offset += 4;
        }
    }

#ifndef USE_CHINESE_FONT
    list[offset] = "free unused backup data";
#else
    list[offset] = "释放未使用的备份数据";
#endif
    offset++;
#ifndef USE_CHINESE_FONT
    list[offset] = "choose default backup format";
#else
    list[offset] = "选择默认的备份格式";
#endif
    offset++;

#ifdef RECOVERY_EXTEND_NANDROID_MENU
    extend_nandroid_menu(list, offset, sizeof(list) / sizeof(char*));
    offset++;
#endif

    list[offset] = NULL;

    for (;;) {
        chosen_item = get_filtered_menu_selection(headers, list, 0, 0, offset);
        if (chosen_item == GO_BACK || chosen_item == REFRESH)
            break;
        int chosen_subitem = chosen_item % 4;
        if (chosen_item == max_backup_index) {
            run_dedupe_gc();
        } else if (chosen_item == (max_backup_index + 1)) {
            choose_default_backup_format();
        } else if (chosen_item < max_backup_index){
            if (chosen_item < 4) {
                chosen_path = primary_path;
            } else if (extra_paths != NULL) {
                chosen_path = extra_paths[(chosen_item / 4) -1];
            }
            switch (chosen_subitem) {
            case 0:
                {
                    char backup_path[PATH_MAX];
                    time_t t = time(NULL);
                    struct tm *tmp = localtime(&t);
                    if (tmp == NULL)
                    {
                        struct timeval tp;
                        gettimeofday(&tp, NULL);
                        sprintf(backup_path, "%s/clockworkmod/backup/%ld", chosen_path, tp.tv_sec);
                    }
                    else
                    {
                        char path_fmt[PATH_MAX];
                        strftime(path_fmt, sizeof(path_fmt), "clockworkmod/backup/%F.%H.%M.%S", tmp);
                        // this sprintf results in:
                        // /emmc/clockworkmod/backup/%F.%H.%M.%S (time values are populated too)
                        sprintf(backup_path, "%s/%s", chosen_path, path_fmt);
                    }
                    nandroid_backup(backup_path);
                    write_recovery_version();
                }
                break;
            case 1:
                show_nandroid_restore_menu(chosen_path);
                write_recovery_version();
                break;
            case 2:
                show_nandroid_delete_menu(chosen_path);
                write_recovery_version();
                break;
            case 3:
                show_nandroid_advanced_restore_menu(chosen_path);
                write_recovery_version();
                break;
            default:
                break;
            }
        } else {
#ifdef RECOVERY_EXTEND_NANDROID_MENU
                handle_nandroid_menu(10, chosen_item);
#endif
            goto out;
        }
    }
out:
    for (i = 0; i < max_backup_index; i++)
        free(list[i]);
    return chosen_item;
}

static void partition_sdcard(const char* volume) {
    if (!can_partition(volume)) {
#ifndef USE_CHINESE_FONT
        ui_print("Can't partition device: %s\n", volume);
#else
        ui_print("无法对设备进行分区: %s\n", volume);
#endif
        return;
    }

    static char* ext_sizes[] = { "128M",
                                 "256M",
                                 "512M",
                                 "1024M",
                                 "2048M",
                                 "4096M",
                                 NULL };

    static char* swap_sizes[] = { "0M",
                                  "32M",
                                  "64M",
                                  "128M",
                                  "256M",
                                  NULL };

    static char* partition_types[] = { "ext3",
                                       "ext4",
                                       NULL
    };

#ifndef USE_CHINESE_FONT
    static const char* ext_headers[] = { "Ext Size", "", NULL };
    static const char* swap_headers[] = { "Swap Size", "", NULL };
    static const char* fstype_headers[] = {"Partition Type", "", NULL };
#else
    static const char* ext_headers[] = { "Ext 大小", "", NULL };
    static const char* swap_headers[] = { "Swap 大小", "", NULL };
    static const char* fstype_headers[] = {"分区类型", "", NULL };
#endif

    int ext_size = get_menu_selection(ext_headers, ext_sizes, 0, 0);
    if (ext_size == GO_BACK)
        return;

    int swap_size = get_menu_selection(swap_headers, swap_sizes, 0, 0);
    if (swap_size == GO_BACK)
        return;

    int partition_type = get_menu_selection(fstype_headers, partition_types, 0, 0);
    if (partition_type == GO_BACK)
        return;

    char sddevice[256];
    Volume *vol = volume_for_path(volume);
    strcpy(sddevice, vol->blk_device);
    // we only want the mmcblk, not the partition
    sddevice[strlen("/dev/block/mmcblkX")] = '\0';
    char cmd[PATH_MAX];
    setenv("SDPATH", sddevice, 1);
    sprintf(cmd, "sdparted -es %s -ss %s -efs %s -s", ext_sizes[ext_size], swap_sizes[swap_size], partition_types[partition_type]);
#ifndef USE_CHINESE_FONT
    ui_print("Partitioning SD Card... please wait...\n");
#else
    ui_print("正在对 SD 卡进行分区... 请稍等...\n");
#endif
    if (0 == __system(cmd))
#ifndef USE_CHINESE_FONT
        ui_print("Done!\n");
#else
        ui_print("完成！\n");
#endif
    else
#ifndef USE_CHINESE_FONT
        ui_print("An error occured while partitioning your SD Card. Please see /tmp/recovery.log for more details.\n");
#else
        ui_print("对 SD 卡进行分区出现了一个错误。请查看 /tmp/recovery.log 以了解更多细节。\n");
#endif
}

int can_partition(const char* volume) {
    if (is_data_media_volume_path(volume))
        return 0;

    Volume *vol = volume_for_path(volume);
    if (vol == NULL) {
#ifndef USE_CHINESE_FONT
        LOGI("Can't format unknown volume: %s\n", volume);
#else
        LOGI("无法格式化未知卷: %s\n", volume);
#endif
        return 0;
    }

    int vol_len = strlen(vol->blk_device);
    // do not allow partitioning of a device that isn't mmcblkX or mmcblkXp1
    if (vol->blk_device[vol_len - 2] == 'p' && vol->blk_device[vol_len - 1] != '1') {
#ifndef USE_CHINESE_FONT
        LOGI("Can't partition unsafe device: %s\n", vol->blk_device);
#else
        LOGI("无法为不安全的设备分区: %s\n", vol->blk_device);
#endif
        return 0;
    }
    
    if (strcmp(vol->fs_type, "vfat") != 0) {
#ifndef USE_CHINESE_FONT
        LOGI("Can't partition non-vfat: %s\n", vol->fs_type);
#else
        LOGI("无法对非 vfat 进行分区: %s\n", vol->fs_type);
#endif
        return 0;
    }

    return 1;
}


#ifdef ENABLE_LOKI
    #define FIXED_ADVANCED_ENTRIES 8
#else
    #define FIXED_ADVANCED_ENTRIES 7
#endif

int show_advanced_menu()
{
    char buf[80];
    int i = 0, j = 0, chosen_item = 0;
    /* Default number of entries if no compile-time extras are added */
    static char* list[MAX_NUM_MANAGED_VOLUMES + FIXED_ADVANCED_ENTRIES + 2];

    char* primary_path = get_primary_storage_path();
    char** extra_paths = get_extra_storage_paths();
    int num_extra_volumes = get_num_extra_volumes();

#ifndef USE_CHINESE_FONT
    static const char* headers[] = {  "Advanced Menu",
#else
    static const char* headers[] = {  "高级功能菜单",
#endif
                                "",
                                NULL
    };

    memset(list, 0, MAX_NUM_MANAGED_VOLUMES + FIXED_ADVANCED_ENTRIES + 2);

#ifndef USE_CHINESE_FONT
    list[0] = "reboot recovery";
#else
    list[0] = "重启 recovery";
#endif

    char bootloader_mode[PROPERTY_VALUE_MAX];
    property_get("ro.bootloader.mode", bootloader_mode, "");
    if (!strcmp(bootloader_mode, "download")) {
#ifndef USE_CHINESE_FONT
        list[1] = "reboot to download mode";
#else
        list[1] = "重启到下载模式";
#endif
    } else {
#ifndef USE_CHINESE_FONT
        list[1] = "reboot to bootloader";
#else
        list[1] = "重启到 bootloader";
#endif
    }

#ifndef USE_CHINESE_FONT
    list[2] = "power off";
    list[3] = "wipe dalvik cache";
    list[4] = "report error";
    list[5] = "key test";
    list[6] = "show log";
#else
    list[2] = "关机";
    list[3] = "清除 dalvik 缓存";
    list[4] = "报告错误";
    list[5] = "键值测试";
    list[6] = "显示日志";
#endif
#ifdef ENABLE_LOKI
#ifndef USE_CHINESE_FONT
    list[7] = "toggle loki support";
#else
    list[7] = "开关 loki 支持";
#endif
#endif

    if (can_partition(primary_path)) {
#ifndef USE_CHINESE_FONT
        sprintf(buf, "partition %s", primary_path);
#else
        sprintf(buf, "为 %s 分区", primary_path);
#endif
        list[FIXED_ADVANCED_ENTRIES] = strdup(buf);
        j++;
    }

    if (extra_paths != NULL) {
        for (i = 0; i < num_extra_volumes; i++) {
            if (can_partition(extra_paths[i])) {
#ifndef USE_CHINESE_FONT
                sprintf(buf, "partition %s", extra_paths[i]);
#else
                sprintf(buf, "为 %s 分区", extra_paths[i]);
#endif
                list[FIXED_ADVANCED_ENTRIES + j] = strdup(buf);
                j++;
            }
        }
    }

    for (;;)
    {
        chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));
        if (chosen_item == GO_BACK || chosen_item == REFRESH)
            break;
        switch (chosen_item)
        {
            case 0:
            {
#ifndef USE_CHINESE_FONT
                ui_print("Rebooting recovery...\n");
#else
                ui_print("正在重启 recovery...\n");
#endif
                reboot_main_system(ANDROID_RB_RESTART2, 0, "recovery");
                break;
            }
            case 1:
            {
                if (!strcmp(bootloader_mode, "download")) {
#ifndef USE_CHINESE_FONT
                    ui_print("Rebooting to download mode...\n");
#else
                    ui_print("正在重启到下载模式...\n");
#endif
                    reboot_main_system(ANDROID_RB_RESTART2, 0, "download");
                } else {
#ifndef USE_CHINESE_FONT
                    ui_print("Rebooting to bootloader...\n");
#else
                    ui_print("正在重启到 bootloader...\n");
#endif
                    reboot_main_system(ANDROID_RB_RESTART2, 0, "bootloader");
                }
                break;
            }
            case 2:
            {
#ifndef USE_CHINESE_FONT
                ui_print("Shutting down...\n");
#else
                ui_print("关机中...\n");
#endif
                reboot_main_system(ANDROID_RB_POWEROFF, 0, 0);
                break;
            }
            case 3:
                if (0 != ensure_path_mounted("/data"))
                    break;
                ensure_path_mounted("/sd-ext");
                ensure_path_mounted("/cache");
#ifndef USE_CHINESE_FONT
                if (confirm_selection( "Confirm wipe?", "Yes - Wipe Dalvik Cache")) {
#else
                if (confirm_selection( "确认清除？", "是 - 清除 Dalvik 缓存")) {
#endif
                    __system("rm -r /data/dalvik-cache");
                    __system("rm -r /cache/dalvik-cache");
                    __system("rm -r /sd-ext/dalvik-cache");
#ifndef USE_CHINESE_FONT
                    ui_print("Dalvik Cache wiped.\n");
#else
                    ui_print("已清除 Dalvik 缓存。\n");
#endif
                }
                ensure_path_unmounted("/data");
                break;
            case 4:
                handle_failure(1);
                break;
            case 5:
            {
#ifndef USE_CHINESE_FONT
                ui_print("Outputting key codes.\n");
                ui_print("Go back to end debugging.\n");
#else
                ui_print("正在监测键值。\n");
                ui_print("按返回键来结束调试。\n");
#endif
                int key;
                int action;
                do
                {
                    key = ui_wait_key();
                    action = device_handle_key(key, 1);
#ifndef USE_CHINESE_FONT
                    ui_print("Key: %d\n", key);
#else
                    ui_print("键值: %d\n", key);
#endif
                }
                while (action != GO_BACK);
                break;
            }
            case 6:
                ui_printlogtail(12);
                break;
#ifdef ENABLE_LOKI
            case 7:
                toggle_loki_support();
                break;
#endif
            case FIXED_ADVANCED_ENTRIES:
                partition_sdcard(primary_path);
                break;
            default:
                if (chosen_item >= (FIXED_ADVANCED_ENTRIES+1)) {
                    partition_sdcard(list[chosen_item] + 10);
                }
                break;
        }
    }
    free(list[FIXED_ADVANCED_ENTRIES]);
    if (extra_paths != NULL) {
        for (; j >= 0; --j)
            free(list[FIXED_ADVANCED_ENTRIES + 1 + j]);
    }
    return chosen_item;
}

void write_fstab_root(char *path, FILE *file)
{
    Volume *vol = volume_for_path(path);
    if (vol == NULL) {
#ifndef USE_CHINESE_FONT
        LOGW("Unable to get recovery.fstab info for %s during fstab generation!\n", path);
#else
        LOGW("无法在 fstab 生成过程中获取 recovery.fstab 中 %s 的信息！\n", path);
#endif
        return;
    }

    char device[200];
    if (vol->blk_device[0] != '/')
        get_partition_device(vol->blk_device, device);
    else
        strcpy(device, vol->blk_device);

    fprintf(file, "%s ", device);
    fprintf(file, "%s ", path);
    // special case rfs cause auto will mount it as vfat on samsung.
    fprintf(file, "%s rw\n", vol->fs_type2 != NULL && strcmp(vol->fs_type, "rfs") != 0 ? "auto" : vol->fs_type);
}

void create_fstab()
{
    struct stat info;
    __system("touch /etc/mtab");
    FILE *file = fopen("/etc/fstab", "w");
    if (file == NULL) {
#ifndef USE_CHINESE_FONT
        LOGW("Unable to create /etc/fstab!\n");
#else
        LOGW("无法创建 /etc/fstab!\n");
#endif
        return;
    }
    Volume *vol = volume_for_path("/boot");
    if (NULL != vol && strcmp(vol->fs_type, "mtd") != 0 && strcmp(vol->fs_type, "emmc") != 0 && strcmp(vol->fs_type, "bml") != 0)
         write_fstab_root("/boot", file);
    write_fstab_root("/cache", file);
    write_fstab_root("/data", file);
    write_fstab_root("/datadata", file);
    write_fstab_root("/emmc", file);
    write_fstab_root("/system", file);
    write_fstab_root("/sdcard", file);
    write_fstab_root("/sd-ext", file);
    write_fstab_root("/external_sd", file);
    fclose(file);
#ifndef USE_CHINESE_FONT
    LOGI("Completed outputting fstab.\n");
#else
    LOGI("fstab 输出已完成。\n");
#endif
}

int bml_check_volume(const char *path) {
#ifndef USE_CHINESE_FONT
    ui_print("Checking %s...\n", path);
#else
    ui_print("正在检查 %s...\n", path);
#endif
    ensure_path_unmounted(path);
    if (0 == ensure_path_mounted(path)) {
        ensure_path_unmounted(path);
        return 0;
    }
    
    Volume *vol = volume_for_path(path);
    if (vol == NULL) {
#ifndef USE_CHINESE_FONT
        LOGE("Unable process volume! Skipping...\n");
#else
        LOGE("无法处理卷！跳过...\n");
#endif
        return 0;
    }
    
#ifndef USE_CHINESE_FONT
    ui_print("%s may be rfs. Checking...\n", path);
#else
    ui_print("%s 的格式可能为 rfs。检查中...\n", path);
#endif
    char tmp[PATH_MAX];
    sprintf(tmp, "mount -t rfs %s %s", vol->blk_device, path);
    int ret = __system(tmp);
    printf("%d\n", ret);
    return ret == 0 ? 1 : 0;
}

void process_volumes() {
    create_fstab();

    if (is_data_media()) {
        setup_data_media();
    }

    return;

    // dead code.
    if (device_flash_type() != BML)
        return;

#ifndef USE_CHINESE_FONT
    ui_print("Checking for ext4 partitions...\n");
#else
    ui_print("正在检查 ext4 分区...\n");
#endif
    int ret = 0;
    ret = bml_check_volume("/system");
    ret |= bml_check_volume("/data");
    if (has_datadata())
        ret |= bml_check_volume("/datadata");
    ret |= bml_check_volume("/cache");
    
    if (ret == 0) {
#ifndef USE_CHINESE_FONT
        ui_print("Done!\n");
#else
        ui_print("完成！\n");
#endif
        return;
    }
    
    char backup_path[PATH_MAX];
    time_t t = time(NULL);
    char backup_name[PATH_MAX];
    struct timeval tp;
    gettimeofday(&tp, NULL);
    sprintf(backup_name, "before-ext4-convert-%ld", tp.tv_sec);
    sprintf(backup_path, "%s/clockworkmod/backup/%s", get_primary_storage_path(), backup_name);

    ui_set_show_text(1);
#ifndef USE_CHINESE_FONT
    ui_print("Filesystems need to be converted to ext4.\n");
    ui_print("A backup and restore will now take place.\n");
    ui_print("If anything goes wrong, your backup will be\n");
    ui_print("named %s. Try restoring it\n", backup_name);
    ui_print("in case of error.\n");
#else
    ui_print("文件系统需要转换为 ext4。\n");
    ui_print("现在将要依次执行备份和还原操作。\n");
    ui_print("如果转换中途出现了错误，请记住备份\n");
    ui_print("文件会被命名为 %s。如有问题，请\n", backup_name);
    ui_print("手动进行还原。\n");
#endif

    nandroid_backup(backup_path);
    nandroid_restore(backup_path, 1, 1, 1, 1, 1, 0);
    ui_set_show_text(0);
}

void handle_failure(int ret)
{
    if (ret == 0)
        return;
    if (0 != ensure_path_mounted(get_primary_storage_path()))
        return;
    mkdir("/sdcard/clockworkmod", S_IRWXU | S_IRWXG | S_IRWXO);
    __system("cp /tmp/recovery.log /sdcard/clockworkmod/recovery.log");
#ifndef USE_CHINESE_FONT
    ui_print("/tmp/recovery.log was copied to /sdcard/clockworkmod/recovery.log. Please open ROM Manager to report the issue.\n");
#else
    ui_print("已将 /tmp/recovery.log 复制到 /sdcard/clockworkmod/recovery.log。\n");
#endif
}

static int is_path_mounted(const char* path) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        return 0;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted.
        return 1;
    }

    if (scan_mounted_volumes() < 0)
        return 0;

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(v->mount_point);
    if (mv) {
        // volume is already mounted
        return 1;
    }
    return 0;
}

int has_datadata() {
    Volume *vol = volume_for_path("/datadata");
    return vol != NULL;
}

int volume_main(int argc, char **argv) {
    load_volume_table();
    return 0;
}

int verify_root_and_recovery() {
    if (ensure_path_mounted("/system") != 0)
        return 0;

    int ret = 0;
    struct stat st;
    // check to see if install-recovery.sh is going to clobber recovery
    // install-recovery.sh is also used to run the su daemon on stock rom for 4.3+
    // so verify that doesn't exist...
    if (0 != lstat("/system/etc/.installed_su_daemon", &st)) {
        // check install-recovery.sh exists and is executable
        if (0 == lstat("/system/etc/install-recovery.sh", &st)) {
            if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
                ui_show_text(1);
                ret = 1;
#ifndef USE_CHINESE_FONT
                if (confirm_selection("ROM may flash stock recovery on boot. Fix?", "Yes - Disable recovery flash")) {
#else
                if (confirm_selection("ROM 可能会在开机时将 recovery 还原为官方版本。是否修正？", "是 - 禁用自动还原")) {
#endif
                    __system("chmod -x /system/etc/install-recovery.sh");
                }
            }
        }
    }


    int exists = 0;
    if (0 == lstat("/system/bin/su", &st)) {
        exists = 1;
        if (S_ISREG(st.st_mode)) {
            if ((st.st_mode & (S_ISUID | S_ISGID)) != (S_ISUID | S_ISGID)) {
                ui_show_text(1);
                ret = 1;
#ifndef USE_CHINESE_FONT
                if (confirm_selection("Root access possibly lost. Fix?", "Yes - Fix root (/system/bin/su)")) {
#else
                if (confirm_selection("root 访问权限设置不当。是否修正？", "是 - 修正 root (/system/bin/su)")) {
#endif
                    __system("chmod 6755 /system/bin/su");
                }
            }
        }
    }

    if (0 == lstat("/system/xbin/su", &st)) {
        exists = 1;
        if (S_ISREG(st.st_mode)) {
            if ((st.st_mode & (S_ISUID | S_ISGID)) != (S_ISUID | S_ISGID)) {
                ui_show_text(1);
                ret = 1;
#ifndef USE_CHINESE_FONT
                if (confirm_selection("Root access possibly lost. Fix?", "Yes - Fix root (/system/xbin/su)")) {
#else
                if (confirm_selection("root 访问权限设置不当。是否修正？", "是 - 修正 root (/system/xbin/su)")) {
#endif
                    __system("chmod 6755 /system/xbin/su");
                }
            }
        }
    }

    if (!exists) {
        ui_show_text(1);
        ret = 1;

#ifndef USE_CHINESE_FONT
        if (confirm_selection("Root access is missing. Root device?", "Yes - Root device (/system/xbin/su)")) {
#else
        if (confirm_selection("未添加 root 访问所需文件。是否添加？", "是 - 确认添加 (/system/xbin/su)")) {
#endif
            __system("/sbin/install-su.sh");
        }
    }

    ensure_path_unmounted("/system");
    return ret;
}
