/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/mount.h>

#include <linux/kdev_t.h>

#define LOG_TAG "Vold"

#include <cutils/log.h>

#include "Ext.h"

static char FSCK_EXT_PATH[] = "/system/bin/e2fsck";
static char MKEXTFS_PATH[] = "/system/bin/mke2fs";
extern "C" int logwrap(int argc, const char **argv, int background);
extern "C" int mount(const char *, const char *, const char *, unsigned long, const void *);

int Ext::check(const char *fsPath) {
    bool rw = true;
    if (access(FSCK_EXT_PATH, X_OK)) {
        SLOGW("Skipping fs checks\n");
        return 0;
    }

    int pass = 1;
    int rc = 0;
    do {
        const char *args[5];
        args[0] = FSCK_EXT_PATH;
        args[1] = "-p";
        args[2] = "-f";
        args[3] = fsPath;
        args[4] = NULL;

        rc = logwrap(4, args, 1);

        switch(rc) {
        case 0:
        case 1:
            SLOGI("Filesystem check completed OK");
            return 0;

        case 2:
            SLOGE("Filesystem check completed, system should be rebooted");
            errno = ENODATA;
            return -1;

        case 4:
            SLOGE("Filesystem errors left uncorrected");
            errno = EIO;
            return -1;

        default:
            SLOGE("Filesystem check failed (exit code %d)", rc);
            errno = EIO;
            return -1;
        }
    } while (0);

    return 0;
}

bool Ext::doDir(const char * mountpoint, const char * path) {
    char *full_path;
    struct stat buffer;
    int rc;
    mode_t mode = S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IWGRP | S_IXGRP | S_IXOTH;

    asprintf(&full_path, "%s/%s", mountpoint, path);

    rc = stat(full_path, &buffer);
    if (rc == 0) {
        if (S_ISDIR(buffer.st_mode)) {
            /* check owner & permissions */
            if (buffer.st_uid != 1000 || buffer.st_gid != 1000)
                if(chown(full_path, 1000, 1000) != 0) {
                    SLOGE("Cannot set UID/GID for %s - %s", full_path, strerror(errno));
                    free(full_path);
                    return false;
                }
            if (buffer.st_mode != mode)
                if (chmod(full_path, mode) != 0) {
                    SLOGE("Cannot set permissions on %s - %s", full_path, strerror(errno));
                    free(full_path);
                    return false;
                }
        } else {
            SLOGE("%s is not a dir. Retreat!", full_path);
            free(full_path);
            return false;
        }
    } else {
        /* This assumes that stat will fail only when full_path
         * doesn't exists
         */
        SLOGI("Creating %s", full_path);
        if (mkdir(full_path, mode) == 0) {
            if (chown(full_path, 1000, 1000) != 0) {
                SLOGE("Cannot set UID/GID for %s - %s", full_path, strerror(errno));
                free(full_path);
                return false;
            }
        } else {
            SLOGE("Cannot create dir %s - %s", full_path, strerror(errno));
            free(full_path);
            return false;
        }
    }
    free(full_path);
    return true;
}

int Ext::doMount(const char *fsPath, const char *mountPoint,
                 bool chkDirs, bool remount) {
    int rc;
    unsigned long flags;
    char mountData[255];

    flags = MS_NODEV | MS_NOATIME | MS_NODIRATIME;
    flags |= (remount ? MS_REMOUNT : 0);

    sprintf(mountData,"barrier=1,data=ordered,noauto_da_alloc");

    rc = mount(fsPath, mountPoint, "ext4", flags, mountData);

    if (rc && errno == EROFS) {
        SLOGE("%s appears to be a read only filesystem - retrying mount RO", fsPath);
        flags |= MS_RDONLY;
        rc = mount(fsPath, mountPoint, "ext4", flags, mountData);
    }

    if (rc == 0 && chkDirs) {
        if (!doDir(mountPoint, "app") || !doDir(mountPoint, "app-private") ||
            !doDir(mountPoint, "dalvik-cache") || !doDir(mountPoint, "data") ||
            !doDir(mountPoint, "system") || !doDir("/data", "system")) {
            (void)umount(mountPoint);
            return -1;
        }
    }
    return rc;
}

int Ext::format(const char *fsPath) {
    int fd;
    const char *args[3];
    int rc;

    args[0] = MKEXTFS_PATH;
    args[1] = fsPath;
    args[2] = NULL;
    rc = logwrap(3, args, 1);

    if (rc == 0) {
        SLOGI("Filesystem formatted OK");
        return 0;
    } else {
        SLOGE("Format failed (unknown exit code %d)", rc);
        errno = EIO;
        return -1;
    }
    return 0;
}
