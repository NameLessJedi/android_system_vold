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
#include <cutils/properties.h>

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

int Ext::doMount(const char *fsPath, const char *mountPoint,
                 bool ro, bool remount, int ownerUid, int ownerGid,
                 int permMask, bool createLost) {
    int rc;
    unsigned long flags;
    char mountData[255];

    flags = MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_DIRSYNC;

    flags |= (ro ? MS_RDONLY : 0);
    flags |= (remount ? MS_REMOUNT : 0);

    sprintf(mountData,
            "utf8,uid=%d,gid=%d,fmask=%o,dmask=%o,shortname=mixed",
            ownerUid, ownerGid, permMask, permMask);

    rc = mount(fsPath, mountPoint, "auto", flags, mountData);

    if (rc && errno == EROFS) {
        SLOGE("%s appears to be a read only filesystem - retrying mount RO", fsPath);
        flags |= MS_RDONLY;
        rc = mount(fsPath, mountPoint, "auto", flags, mountData);
    }

    return rc;
}

int Ext::format(const char *fsPath, unsigned int numSectors) {
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
