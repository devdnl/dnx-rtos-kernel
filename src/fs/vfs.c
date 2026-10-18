/*==============================================================================
@file    vfs.c

@author  Daniel Zorychta

@brief   This file support virtual file system

@note    Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

         This program is free software; you can redistribute it and/or modify
         it under the terms of the GNU General Public License as published by
         the Free Software Foundation and modified by the dnx RTOS exception.

         NOTE: The modification  to the GPL is  included to allow you to
               distribute a combined work that includes dnx RTOS without
               being obliged to provide the source  code for proprietary
               components outside of the dnx RTOS.

         The dnx RTOS  is  distributed  in the hope  that  it will be useful,
         but WITHOUT  ANY  WARRANTY;  without  even  the implied  warranty of
         MERCHANTABILITY  or  FITNESS  FOR  A  PARTICULAR  PURPOSE.  See  the
         GNU General Public License for more details.

         Full license text is available on the following file: doc/license.txt.


==============================================================================*/

#pragma GCC optimize("O3")

/*==============================================================================
  Include files
==============================================================================*/
#include "lib/misc.h"
#include "kernel/errno.h"
#include <string.h>
#include <stdint.h>
#include "fs/vfs.h"
#include "lib/llist.h"
#include "kernel/kwrapper.h"
#include "kernel/process.h"
#include "kernel/sysfunc.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#undef errno
#define PATH_MAX_LEN 256

/* maximum value of device major/minor number (see struct vfs_dev_stat) */
#define DEV_NUM_MAX 0xFF

#ifndef ENAMETOOLONG
#define ENAMETOOLONG EINVAL
#endif

#ifndef EXDEV
#define EXDEV ENOTSUP
#endif

#ifndef EOVERFLOW
#define EOVERFLOW EINVAL
#endif

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
typedef struct FS_entry {
        const char *source_path;
        const char *mount_point;
        struct FS_entry *parent;
        void *handle;
        const vfs_FS_itf_t *interface;
        u32_t children_cnt; //!< number of file systems mounted in this file system
        u32_t refs;         //!< number of users: opened files/dirs and operations in progress
} FS_entry_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static bool is_first_fs(const char *mount_point);
static int new_FS_entry(FS_entry_t *parent_FS, const char *fs_mount_point, const char *fs_src_file,
        const vfs_FS_itf_t *fs_interface, const char *opts, FS_entry_t **fs_entry);
static int delete_FS_entry(FS_entry_t *this);
static void put_FS(FS_entry_t *fs);
static int check_dir_exists(FS_entry_t *fs, const char *path);
static bool is_file_valid(kfile_t *file);
static bool is_dir_valid(kdir_t *dir);
static bool is_mount_point_at_or_under(const char *path);
static int get_path_FS(const char *path, size_t len, int *position, FS_entry_t **fs_entry);
static int get_mount_root_FS(const char *path, size_t len, FS_entry_t **fs_entry);
static int get_path_base_FS(const char *path, bool mnt_root, const char **ext_path, FS_entry_t **fs_entry);
static int new_absolute_path(const struct vfs_path *path, enum path_correction corr, char **new_path);
static int open_device_file(const char *path, int flags, kfile_t *file_obj);
static int direct_devfs_close(void *fshdl, void *fhdl);
static int direct_devfs_write(void *fshdl, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr attr);
static int direct_devfs_read(void *fshdl, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr attr);
static int direct_devfs_ioctl(void *fshdl, void *fhdl, int iroq, void *arg);
static int direct_devfs_fstat(void *fshdl, void *fhdl, struct stat *stat);
static int direct_devfs_flush(void *fshdl, void *fhdl);

/*==============================================================================
  Local object definitions
==============================================================================*/
static const char dev_path_protocol[] = "dev://";
static const char root_path[] = "/";

static struct {
        llist_t *mnt_list;
        kmtx_t *resource_mtx;
} VFS;

static const struct vfs_FS_itf direct_devfs = {
        .fs_close = direct_devfs_close,
        .fs_write = direct_devfs_write,
        .fs_read = direct_devfs_read,
        .fs_ioctl = direct_devfs_ioctl,
        .fs_fstat = direct_devfs_fstat,
        .fs_flush = direct_devfs_flush,
        .fs_magic = _VFS_FILE_SYSTEM_MAGIC_NO,
};

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize VFS module
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_init(void)
{
        int err = _llist_create_krn(_MM_KRN, NULL, NULL, &VFS.mnt_list);
        if (!err) {
                err = _mutex_create(KMTX_TYPE_RECURSIVE, &VFS.resource_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function mount file system in VFS.
 *
 * @param[in] *src_path         path to source file when file system load data
 * @param[in] *mount_point      path when dir shall be mounted
 * @param[in] *fsif             pointer to file system interface
 * @param[in] *opts             options
 *
 * @return One of errno values.
 */
//==============================================================================
int _vfs_mount(const struct vfs_path *src_path, const struct vfs_path *mount_point, const struct vfs_FS_itf *fsif,
        const char *opts)
{
        if (!mount_point || !mount_point->PATH || !src_path || !src_path->PATH || !fsif || !fsif->fs_init ||
                !fsif->fs_release || !fsif->fs_open || !fsif->fs_close || !fsif->fs_write || !fsif->fs_read ||
                !fsif->fs_ioctl || !fsif->fs_fstat || !fsif->fs_flush || !fsif->fs_mknod || !fsif->fs_sync ||
                !fsif->fs_opendir || !fsif->fs_closedir || !fsif->fs_readdir
#if __OS_ENABLE_FSTAT__ == _YES_
                || !fsif->fs_stat
#endif
#if __OS_ENABLE_MKDIR__ == _YES_
                || !fsif->fs_mkdir
#endif
#if __OS_ENABLE_MKFIFO__ == _YES_
                || !fsif->fs_mkfifo
#endif
#if __OS_ENABLE_REMOVE__ == _YES_
                || !fsif->fs_remove
#endif
#if __OS_ENABLE_RENAME__ == _YES_
                || !fsif->fs_rename
#endif
#if __OS_ENABLE_CHMOD__ == _YES_
                || !fsif->fs_chmod
#endif
#if __OS_ENABLE_CHOWN__ == _YES_
                || !fsif->fs_chown
#endif
#if __OS_ENABLE_STATFS__ == _YES_
                || !fsif->fs_statfs
#endif
                || fsif->fs_magic != _VFS_FILE_SYSTEM_MAGIC_NO) {

                return EINVAL;
        }

        // create new paths that are corrected by CWD
        char *cwd_mount_point = NULL;
        char *cwd_src_path = NULL;

        int err = new_absolute_path(mount_point, ADD_SLASH, &cwd_mount_point);
        if (!err) {
                err = new_absolute_path(src_path, SUB_SLASH, &cwd_src_path);
        }

        if (!err) {
                err = _mutex_lock(VFS.resource_mtx, _WAIT_FOREVER_MS);
        }

        if (!err) {
                FS_entry_t *new_fs = NULL;
                FS_entry_t *base_fs = NULL;

                /*
                 * create new FS in existing DIR and FS, otherwise create new FS if
                 * first mount
                 */
                if (is_first_fs(cwd_mount_point)) {
                        err = new_FS_entry(NULL, cwd_mount_point, cwd_src_path, fsif, opts, &new_fs);

                } else {
                        const char *ext_path = NULL;

                        err = get_path_base_FS(cwd_mount_point, false, &ext_path, &base_fs);
                        if (!err) {
                                FS_entry_t *mounted_fs;
                                if (get_path_FS(cwd_mount_point, strlen(cwd_mount_point), NULL, &mounted_fs) ==
                                        ESUCC) {
                                        err = EADDRINUSE;
                                } else {
                                        err = check_dir_exists(base_fs, ext_path);
                                }
                        }

                        if (!err) {
                                err = new_FS_entry(base_fs, cwd_mount_point, cwd_src_path, fsif, opts, &new_fs);
                        }
                }

                /*
                 * mount FS if created
                 */
                if (!err) {
                        // source "/" is presented as "-" (the same buffer size)
                        if (isstreq(cwd_src_path, "/")) {
                                cwd_src_path[0] = '-';
                        }

                        if (sys_llist_push_back(VFS.mnt_list, new_fs)) {
                                if (base_fs) {
                                        base_fs->children_cnt++;
                                }

                                // paths are owned by the new entry now
                                cwd_mount_point = NULL;
                                cwd_src_path = NULL;

                        } else {
                                // roll back: release FS only, paths are freed below
                                int rel_err = new_fs->interface->fs_release(new_fs->handle);
                                if (rel_err) {
                                        printk("VFS: unable to release FS mounted at '%s' (%d)", cwd_mount_point,
                                                rel_err);
                                }

                                _kfree(_MM_KRN, cast(void **, &new_fs));
                                err = ENOMEM;
                        }
                }

                if (base_fs) {
                        put_FS(base_fs);
                }

                _mutex_unlock(VFS.resource_mtx);
        }

        if (cwd_mount_point) {
                _kfree(_MM_KRN, cast(void **, &cwd_mount_point));
        }

        if (cwd_src_path) {
                _kfree(_MM_KRN, cast(void **, &cwd_src_path));
        }

        return err;
}

//==============================================================================
/**
 * @brief Function unmount file system
 *
 * File system cannot be unmounted when other file system is mounted in it or
 * when it is in use (opened files or directories, operations in progress).
 *
 * @param[in] *path             dir path
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_umount(const struct vfs_path *path)
{
        if (!path || !path->PATH) {
                return EINVAL;
        }

        char *cwd_path;
        int err = new_absolute_path(path, ADD_SLASH, &cwd_path);
        if (not err) {
                err = _mutex_lock(VFS.resource_mtx, _WAIT_FOREVER_MS);
                if (not err) {

                        int position;
                        FS_entry_t *mount_fs;
                        err = get_path_FS(cwd_path, strlen(cwd_path), &position, &mount_fs);

                        if (not err) {
                                if ((mount_fs->children_cnt == 0) && (mount_fs->refs == 0)) {
                                        err = delete_FS_entry(mount_fs);
                                        if (not err) {
                                                sys_llist_take(VFS.mnt_list, position);
                                        }
                                } else {
                                        err = EBUSY;
                                }
                        }

                        _mutex_unlock(VFS.resource_mtx);
                }

                _kfree(_MM_KRN, cast(void **, &cwd_path));
        }

        return err;
}

#if __OS_ENABLE_STATFS__ == _YES_
//==============================================================================
/**
 * @brief Function return file system describe object
 *
 * @note  Strings referenced by mntent (mnt_dir, mnt_src) are owned by VFS and
 *        are valid only as long as the file system stays mounted.
 *
 * @param [in]  seek          item to read
 * @param [out] mntent        pointer to mntent object
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_getmntentry(int seek, struct mntent *mntent)
{
        if (!mntent) {
                return EINVAL;
        }

        FS_entry_t *fs = NULL;
        int err = _mutex_lock(VFS.resource_mtx, _WAIT_FOREVER_MS);
        if (!err) {
                fs = sys_llist_at(VFS.mnt_list, seek);
                if (fs) {
                        fs->refs++; // FS cannot be unmounted during statfs
                        mntent->mnt_dir = fs->mount_point;
                        mntent->mnt_src = fs->source_path;
                        err = ESUCC;
                } else {
                        err = ENOENT;
                }
                _mutex_unlock(VFS.resource_mtx);
        }

        if (!err) {
                mntent->mnt_free = 0;
                mntent->mnt_total = 0;

                struct statfs stat_fs;
                memset(&stat_fs, 0, sizeof(stat_fs));

                err = fs->interface->fs_statfs(fs->handle, &stat_fs);
                if (!err) {
                        mntent->mnt_fsname = stat_fs.f_fsname;
                        mntent->mnt_free = (u64_t)stat_fs.f_bfree * stat_fs.f_bsize;
                        mntent->mnt_total = (u64_t)stat_fs.f_blocks * stat_fs.f_bsize;
                } else {
                        mntent->mnt_fsname = stat_fs.f_fsname ? stat_fs.f_fsname : "unknown";
                }

                put_FS(fs);
        }

        return err;
}
#endif

//==============================================================================
/**
 * @brief Function create node for driver file
 *
 * @param[in] path              path when driver-file shall be created
 * @param[in] dev               device number
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_mknod(const struct vfs_path *path, dev_t dev)
{
        if (!path || !path->PATH) {
                return EINVAL;
        }

        char *cwd_path;
        int err = new_absolute_path(path, NO_SLASH_ACTION, &cwd_path);
        if (!err) {

                const char *external_path;
                FS_entry_t *fs;
                err = get_path_base_FS(cwd_path, false, &external_path, &fs);
                if (!err) {
                        err = fs->interface->fs_mknod(fs->handle, external_path, dev);
                        put_FS(fs);
                }

                _kfree(_MM_KRN, cast(void **, &cwd_path));
        }

        return err;
}

#if __OS_ENABLE_MKDIR__ == _YES_
//==============================================================================
/**
 * @brief Create directory
 *
 * @param[in] path              path to new directory
 * @param[in] mode              directory mode
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_mkdir(const struct vfs_path *path, mode_t mode)
{
        if (!path || !path->PATH) {
                return EINVAL;
        }

        char *cwd_path;
        int err = new_absolute_path(path, SUB_SLASH, &cwd_path);
        if (!err) {

                const char *external_path;
                FS_entry_t *fs;
                err = get_path_base_FS(cwd_path, false, &external_path, &fs);
                if (!err) {
                        err = fs->interface->fs_mkdir(fs->handle, external_path, mode & S_IPMT);
                        put_FS(fs);
                }

                _kfree(_MM_KRN, cast(void **, &cwd_path));
        }

        return err;
}
#endif

#if __OS_ENABLE_MKFIFO__ == _YES_
//==============================================================================
/**
 * @brief Create pipe
 *
 * @param[in] path              path to pipe
 * @param[in] mode              directory mode
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_mkfifo(const struct vfs_path *path, mode_t mode)
{
        if (!path || !path->PATH) {
                return EINVAL;
        }

        char *cwd_path;
        int err = new_absolute_path(path, NO_SLASH_ACTION, &cwd_path);
        if (!err) {

                const char *external_path;
                FS_entry_t *fs;
                err = get_path_base_FS(cwd_path, false, &external_path, &fs);
                if (!err) {
                        err = fs->interface->fs_mkfifo(fs->handle, external_path, mode & S_IPMT);
                        put_FS(fs);
                }

                _kfree(_MM_KRN, cast(void **, &cwd_path));
        }

        return err;
}
#endif

//==============================================================================
/**
 * @brief Function open directory
 *
 * @param[in]  path                 directory path
 * @param[out] dir                  pointer to dir pointer
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_opendir(const struct vfs_path *path, kdir_t **dir)
{
        if (!path || !path->PATH || !dir) {
                return EINVAL;
        }

        int err = _kzalloc(_MM_KRN, sizeof(kdir_t), NULL, 0, 0, cast(void **, dir));
        if (!err) {
                char *cwd_path;
                err = new_absolute_path(path, ADD_SLASH, &cwd_path);
                if (!err) {

                        const char *external_path;
                        FS_entry_t *fs;
                        err = get_path_base_FS(cwd_path, false, &external_path, &fs);

                        if (!err) {
                                (*dir)->FS_hdl = fs->handle;
                                (*dir)->FS_if = fs->interface;
                                (*dir)->FS_entry = fs;

                                err = fs->interface->fs_opendir(fs->handle, external_path, *dir);
                                if (err) {
                                        (*dir)->FS_entry = NULL;
                                        put_FS(fs);
                                }
                        }

                        _kfree(_MM_KRN, cast(void **, &cwd_path));
                }

                if (!err) {
                        (*dir)->header.self = *dir;
                        (*dir)->header.type = RES_TYPE_DIR;
                } else {
                        _kfree(_MM_KRN, cast(void **, dir));
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function close opened directory
 *
 * @note  Directory object is released even if file system returns an error
 *        (the same behavior as _vfs_fclose()).
 *
 * @param[in] *dir                  directory object
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_closedir(kdir_t *dir)
{
        int err = EINVAL;

        if (is_dir_valid(dir)) {
                err = dir->FS_if->fs_closedir(dir->FS_hdl, dir);

                FS_entry_t *fs = dir->FS_entry;

                dir->header.self = NULL;
                dir->header.type = RES_TYPE_UNKNOWN;
                dir->FS_entry = NULL;
                _kfree(_MM_KRN, cast(void **, &dir));

                put_FS(fs);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function read next item of opened directory
 *
 * @param[in]  *dir                  directory object
 * @param[out] **dirent              pointer to direntry pointer
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_readdir(kdir_t *dir, dirent_t **dirent)
{
        int err = EINVAL;

        if (is_dir_valid(dir) && dirent) {
                err = dir->FS_if->fs_readdir(dir->FS_hdl, dir);
                if (!err) {
                        *dirent = &dir->dirent;
                } else {
                        *dirent = NULL;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function set position of read index.
 *
 * @param dir           directory object
 * @param seek          file position
 *
 * @return One of errno values.
 */
//==============================================================================
int _vfs_seekdir(kdir_t *dir, u32_t seek)
{
        if (is_dir_valid(dir)) {
                dir->d_seek = seek;
                return ESUCC;
        } else {
                return EINVAL;
        }
}

//==============================================================================
/**
 * @brief Function get position of read index.
 *
 * @param dir           directory object
 * @param seek          file position pointer
 *
 * @return One of errno values.
 */
//==============================================================================
int _vfs_telldir(kdir_t *dir, u32_t *seek)
{
        if (is_dir_valid(dir) && seek) {
                *seek = dir->d_seek;
                return ESUCC;
        } else {
                return EINVAL;
        }
}

#if __OS_ENABLE_REMOVE__ == _YES_
//==============================================================================
/**
 * @brief Remove file
 * Removes file or directory. Mount points and directories that contain mount
 * points cannot be removed.
 *
 * @param[in] *path                localization of file/directory
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_remove(const struct vfs_path *path)
{
        if (!path || !path->PATH) {
                return EINVAL;
        }

        char *cwd_path;
        int err = new_absolute_path(path, SUB_SLASH, &cwd_path);
        if (!err) {

                const char *external_path = NULL;
                FS_entry_t *base_fs = NULL;

                err = _mutex_lock(VFS.resource_mtx, _WAIT_FOREVER_MS);
                if (!err) {
                        if (is_mount_point_at_or_under(cwd_path)) {
                                err = EBUSY;
                        } else {
                                err = get_path_base_FS(cwd_path, false, &external_path, &base_fs);
                        }

                        _mutex_unlock(VFS.resource_mtx);
                }

                if (!err) {
                        err = base_fs->interface->fs_remove(base_fs->handle, external_path);
                        put_FS(base_fs);
                }

                _kfree(_MM_KRN, cast(void **, &cwd_path));
        }

        return err;
}
#endif

#if __OS_ENABLE_RENAME__ == _YES_
//==============================================================================
/**
 * @brief Rename file name
 * The implementation of rename can move files only if external FS provide
 * functionality. Local VFS cannot do this. Cross FS move is also not possible.
 * Mount points and directories that contain mount points cannot be renamed.
 *
 * @param[in] *old_name                  old file name
 * @param[in] *new_name                  new file name
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_rename(const struct vfs_path *old_name, const struct vfs_path *new_name)
{
        if (!old_name || !old_name->PATH || !new_name || !new_name->PATH) {
                return EINVAL;
        }

        char *cwd_old_name = NULL;
        char *cwd_new_name = NULL;

        int err = new_absolute_path(old_name, NO_SLASH_ACTION, &cwd_old_name);
        if (!err) {
                err = new_absolute_path(new_name, NO_SLASH_ACTION, &cwd_new_name);
        }

        if (!err) {
                FS_entry_t *old_fs = NULL;
                FS_entry_t *new_fs = NULL;
                const char *old_extern_path;
                const char *new_extern_path;

                err = _mutex_lock(VFS.resource_mtx, _WAIT_FOREVER_MS);
                if (!err) {
                        if (is_mount_point_at_or_under(cwd_old_name) || is_mount_point_at_or_under(cwd_new_name)) {
                                err = EBUSY;
                        }

                        if (!err) {
                                err = get_path_base_FS(cwd_old_name, false, &old_extern_path, &old_fs);
                        }

                        if (!err) {
                                err = get_path_base_FS(cwd_new_name, false, &new_extern_path, &new_fs);
                                if (err) {
                                        put_FS(old_fs);
                                        old_fs = NULL;
                                }
                        }

                        _mutex_unlock(VFS.resource_mtx);
                }

                if (!err) {
                        if (old_fs == new_fs) {
                                err = old_fs->interface->fs_rename(old_fs->handle, old_extern_path, new_extern_path);
                        } else {
                                err = EXDEV;
                        }

                        put_FS(old_fs);
                        put_FS(new_fs);
                }
        }

        if (cwd_old_name) {
                _kfree(_MM_KRN, cast(void **, &cwd_old_name));
        }

        if (cwd_new_name) {
                _kfree(_MM_KRN, cast(void **, &cwd_new_name));
        }

        return err;
}
#endif

#if __OS_ENABLE_CHMOD__ == _YES_
//==============================================================================
/**
 * @brief Function change file mode
 *
 * @param[in] *path         file path
 * @param[in]  mode         file mode
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_chmod(const struct vfs_path *path, mode_t mode)
{
        if (!path || !path->PATH) {
                return EINVAL;
        }

        char *cwd_path;
        int err = new_absolute_path(path, NO_SLASH_ACTION, &cwd_path);
        if (!err) {

                const char *external_path;
                FS_entry_t *fs;
                err = get_path_base_FS(cwd_path, true, &external_path, &fs);
                if (!err) {
                        err = fs->interface->fs_chmod(fs->handle, external_path, mode & S_IPMT);
                        put_FS(fs);
                }

                _kfree(_MM_KRN, cast(void **, &cwd_path));
        }

        return err;
}
#endif

#if __OS_ENABLE_CHOWN__ == _YES_
//==============================================================================
/**
 * @brief Function change file owner and group
 *
 * @param[in] *path         file path
 * @param[in]  owner        file owner
 * @param[in]  group        file group
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_chown(const struct vfs_path *path, uid_t owner, gid_t group)
{
        if (!path || !path->PATH) {
                return EINVAL;
        }

        char *cwd_path;
        int err = new_absolute_path(path, NO_SLASH_ACTION, &cwd_path);
        if (!err) {

                const char *external_path;
                FS_entry_t *fs;
                err = get_path_base_FS(cwd_path, true, &external_path, &fs);
                if (!err) {
                        err = fs->interface->fs_chown(fs->handle, external_path, owner, group);
                        put_FS(fs);
                }

                _kfree(_MM_KRN, cast(void **, &cwd_path));
        }

        return err;
}
#endif

#if __OS_ENABLE_FSTAT__ == _YES_
//==============================================================================
/**
 * @brief Function returns file/dir status
 *
 * @param[in]  *path            file/dir path
 * @param[out] *stat            pointer to structure
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_stat(const struct vfs_path *path, struct stat *stat)
{
        if (!path || !path->PATH || !stat) {
                return EINVAL;
        }

        char *cwd_path;
        int err = new_absolute_path(path, NO_SLASH_ACTION, &cwd_path);
        if (!err) {

                const char *external_path;
                FS_entry_t *fs;
                err = get_path_base_FS(cwd_path, true, &external_path, &fs);
                if (!err) {
                        err = fs->interface->fs_stat(fs->handle, external_path, stat);
                        put_FS(fs);
                }

                _kfree(_MM_KRN, cast(void **, &cwd_path));
        }

        return err;
}
#endif

#if __OS_ENABLE_STATFS__ == _YES_
//==============================================================================
/**
 * @brief Function returns file system status
 *
 * @param[in]  *path            fs path
 * @param[out] *statfs          pointer to FS status structure
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_statfs(const struct vfs_path *path, struct statfs *statfs)
{
        if (!path || !path->PATH || !statfs) {
                return EINVAL;
        }

        char *cwd_path;
        int err = new_absolute_path(path, ADD_SLASH, &cwd_path);
        if (!err) {

                FS_entry_t *fs;
                err = get_path_base_FS(cwd_path, false, NULL, &fs);
                if (!err) {
                        err = fs->interface->fs_statfs(fs->handle, statfs);
                        put_FS(fs);
                }

                _kfree(_MM_KRN, cast(void **, &cwd_path));
        }

        return err;
}
#endif

//==============================================================================
/**
 * @brief Function open selected file
 *
 * @param[in]  *name             file path
 * @param[in]  flags             file flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_APPEND,
 * O_NONBLOCK, O_EXCL)
 * @param[in]  mode              mode (used only when a new file is created)
 * @param[out] **file            pointer to file pointer
 *
 * @return One of errno values
 */
//==============================================================================
int _vfs_fopen(const struct vfs_path *path, int flags, mode_t mode, kfile_t **file)
{
        if (!path || !path->PATH || !file || (path->PATH[0] == '\0')) {
                return EINVAL;
        }

        if (LAST_CHARACTER(path->PATH) == '/') { /* path is a directory */
                return EISDIR;
        }

        int acc_mode = flags & O_ACCMODE;
        if (acc_mode == O_ACCMODE) { /* O_WRONLY | O_RDWR is not a valid access mode */
                return EINVAL;
        }

#if __OS_ENABLE_CHMOD__ != _YES_
        UNUSED_ARG1(mode);
#endif

        vfs_file_flags_t f_flags;

        memset(&f_flags, 0, sizeof(vfs_file_flags_t));
        f_flags.rd = (acc_mode == O_RDONLY) || (acc_mode == O_RDWR);
        f_flags.wr = (acc_mode == O_WRONLY) || (acc_mode == O_RDWR);
        f_flags.append = (flags & O_APPEND) != 0;

        kfile_t *file_obj = NULL;
        char *cwd_path = NULL;
        bool opened = false;

        int err = _kzalloc(_MM_KRN, sizeof(kfile_t), NULL, 0, 0, cast(void **, &file_obj));
        if (err) {
                return err;
        }

        if (strncmp(path->PATH, dev_path_protocol, sizeof(dev_path_protocol) - 1) == 0) {
                /*
                 * Open driver directly (driver only)
                 */
                err = open_device_file(path->PATH, flags, file_obj);
                opened = (err == ESUCC);

        } else {
                const char *external_path = NULL;
                FS_entry_t *fs = NULL;

                err = new_absolute_path(path, NO_SLASH_ACTION, &cwd_path);
                if (!err) {
                        err = get_path_base_FS(cwd_path, false, &external_path, &fs);
                }

                if (!err) {
                        // reference is released together with file object
                        file_obj->FS_entry = fs;

#if __OS_ENABLE_CHMOD__ == _YES_
                        bool existed = false;
#if __OS_ENABLE_FSTAT__ == _YES_
                        if (flags & O_CREAT) {
                                struct stat st;
                                memset(&st, 0, sizeof(st));
                                existed = (fs->interface->fs_stat(fs->handle, external_path, &st) == ESUCC);
                        }
#endif
#endif
                        err = fs->interface->fs_open(fs->handle, &file_obj->f_hdl, &file_obj->f_lseek, external_path,
                                flags);

                        if (!err) {
                                opened = true;
                                file_obj->FS_hdl = fs->handle;
                                file_obj->FS_if = fs->interface;
                        }

#if __OS_ENABLE_CHMOD__ == _YES_
                        /*
                         * Permissions are applied only to a newly created file.
                         * Without FSTAT support existence cannot be checked, so
                         * permissions are applied at each open with O_CREAT.
                         */
                        if (!err && (flags & O_CREAT) && !existed) {
                                struct stat st;
                                memset(&st, 0, sizeof(st));
                                if (fs->interface->fs_fstat(fs->handle, file_obj->f_hdl, &st) != ESUCC) {
                                        memset(&st, 0, sizeof(st));
                                }

                                mode_t cur_mode = st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
                                err = fs->interface->fs_chmod(fs->handle, external_path, cur_mode | (mode & S_IPMT));
                        }
#endif
                }
        }

        struct stat stat;
        memset(&stat, 0, sizeof(stat));

        if (!err) {
                err = file_obj->FS_if->fs_fstat(file_obj->FS_hdl, file_obj->f_hdl, &stat);
        }

        if (!err) {
                if ((stat.st_mode & S_IRUSR) == 0) {
                        f_flags.rd = false;
                }

                if ((stat.st_mode & S_IWUSR) == 0) {
                        f_flags.wr = false;
                }

                if (f_flags.append) {
                        file_obj->f_lseek = stat.st_size;
                }

                file_obj->f_flag = f_flags;
                file_obj->references = 1;
                file_obj->header.self = file_obj;
                file_obj->header.type = RES_TYPE_FILE;
                *file = file_obj;

        } else {
                if (opened) {
                        // do not leak FS/driver file handle
                        file_obj->FS_if->fs_close(file_obj->FS_hdl, file_obj->f_hdl);
                }

                put_FS(file_obj->FS_entry);
                _kfree(_MM_KRN, cast(void **, &file_obj));
        }

        if (cwd_path) {
                _kfree(_MM_KRN, cast(void **, &cwd_path));
        }

        return err;
}

//==============================================================================
/**
 * @brief Function increase open reference.
 *
 * @param[in] file              file
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_fopen_ref(kfile_t *file)
{
        int err = EINVAL;

        if (is_file_valid(file)) {
                err = _mutex_lock(VFS.resource_mtx, _WAIT_FOREVER_MS);
                if (!err) {
                        if (file->references > 0) {
                                file->references++;
                        } else {
                                err = EINVAL; // file is being closed
                        }

                        _mutex_unlock(VFS.resource_mtx);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function close opened file
 *
 * @param[in] file              pinter to file
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_fclose(kfile_t *file)
{
        if (!is_file_valid(file)) {
                return EINVAL;
        }

        bool last = false;

        int err = _mutex_lock(VFS.resource_mtx, _WAIT_FOREVER_MS);
        if (!err) {
                if (file->references <= 0) {
                        err = EINVAL; // already closed by another thread
                } else {
                        file->references--;
                        last = (file->references == 0);
                }

                _mutex_unlock(VFS.resource_mtx);
        }

        if (!err && last) {
                err = file->FS_if->fs_close(file->FS_hdl, file->f_hdl);

                // note: free resource even on error
                FS_entry_t *fs = file->FS_entry;

                file->header.self = NULL;
                file->header.type = RES_TYPE_UNKNOWN;
                file->FS_hdl = NULL;
                file->FS_entry = NULL;
                _kfree(_MM_KRN, cast(void **, &file));

                put_FS(fs);
        }

        return err;
}

//==============================================================================
/**
 * @brief Update file position after read/write operation.
 *
 * If file system modified position itself then its value is used, otherwise
 * position is advanced by number of transferred bytes.
 *
 * @param file          file object
 * @param fs_pos        position returned by file system
 * @param count         number of transferred bytes
 */
//==============================================================================
static inline void update_position(kfile_t *file, fpos_t fs_pos, size_t count)
{
        if (fs_pos != file->f_lseek) {
                file->f_lseek = fs_pos;
        } else {
                file->f_lseek += count;
        }
}

//==============================================================================
/**
 * @brief Function write data to file
 *
 * @param[in]  ptr              address to data (src)
 * @param[in]  count            number of items
 * @param[out] wrcnt            number of written elements
 * @param[in]  file             pointer to file object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_fwrite(const void *ptr, size_t size, size_t *wrcnt, kfile_t *file)
{
        int err = EINVAL;

        if (ptr && size && wrcnt && is_file_valid(file)) {
                *wrcnt = 0;

                if (file->f_flag.wr) {
                        err = ESUCC;

                        // in append mode each write is done at the end of file
                        if (file->f_flag.append) {
                                struct stat stat;
                                memset(&stat, 0, sizeof(stat));

                                err = file->FS_if->fs_fstat(file->FS_hdl, file->f_hdl, &stat);
                                if (!err) {
                                        file->f_lseek = stat.st_size;
                                        file->f_flag.seekmod = false;
                                }
                        }

                        if (!err) {
                                fpos_t fpos = file->f_lseek;

                                err = file->FS_if->fs_write(file->FS_hdl, file->f_hdl, ptr, size, &fpos, wrcnt,
                                        file->f_flag.fattr);

                                if (!err) {
                                        if ((*wrcnt < size) && !file->f_flag.fattr.non_blocking_wr) {
                                                file->f_flag.eof = true;
                                        }

                                        update_position(file, fpos, *wrcnt);
                                }
                        }

                        if (err) {
                                file->f_flag.error = true;
                        }
                } else {
                        file->f_flag.error = true;
                        err = EBADF;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function read data from file
 *
 * @param[out] ptr              address to data (dst)
 * @param[in]  size             item size
 * @param[out] rdcnt            number of read bytes
 * @param[in]  file             pointer to file object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_fread(void *ptr, size_t size, size_t *rdcnt, kfile_t *file)
{
        int err = EINVAL;

        if (ptr && size && rdcnt && is_file_valid(file)) {
                *rdcnt = 0;

                if (file->f_flag.rd) {
                        fpos_t fpos = file->f_lseek;

                        err = file->FS_if->fs_read(file->FS_hdl, file->f_hdl, ptr, size, &fpos, rdcnt,
                                file->f_flag.fattr);

                        if (!err) {
                                if ((*rdcnt < size) &&
                                        !(file->f_flag.fattr.non_blocking_rd or file->f_flag.fattr.non_strict_rd)) {

                                        file->f_flag.eof = true;
                                }

                                update_position(file, fpos, *rdcnt);
                        } else {
                                file->f_flag.error = true;
                        }
                } else {
                        file->f_flag.error = true;
                        err = EBADF;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function set seek value
 *
 * @note  In append mode position can be changed freely (e.g. for reading),
 *        writes are always done at the end of file.
 *
 * @param[in] *file             file object
 * @param[in]  offset           seek value
 * @param[in]  mode             seek mode
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_fseek(kfile_t *file, i64_t offset, int mode)
{
        if (!is_file_valid(file)) {
                return EINVAL;
        }

        i64_t base;

        switch (mode) {
        case VFS_SEEK_SET:
                base = 0;
                break;

        case VFS_SEEK_CUR:
                base = cast(i64_t, file->f_lseek);
                break;

        case VFS_SEEK_END: {
                struct stat stat;
                memset(&stat, 0, sizeof(stat));

                int err = _vfs_fstat(file, &stat);
                if (err) {
                        return err;
                }

                base = cast(i64_t, stat.st_size);
                break;
        }

        default:
                return EINVAL;
        }

        if ((offset > 0) && (base > (INT64_MAX - offset))) {
                return EOVERFLOW;
        }

        i64_t new_pos = base + offset;
        if (new_pos < 0) {
                return EINVAL;
        }

        file->f_lseek = new_pos;
        file->f_flag.eof = false;
        file->f_flag.seekmod = true;

        if ((mode == VFS_SEEK_SET) && (offset == 0)) {
                file->f_flag.error = false;
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief Function returns seek value
 *
 * @param[in]  file             file object
 * @param[out] lseek            file seek
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_ftell(kfile_t *file, i64_t *lseek)
{
        if (is_file_valid(file) && lseek) {
                *lseek = file->f_lseek;
                return ESUCC;
        } else {
                return EINVAL;
        }
}

//==============================================================================
/**
 * @brief Function support not standard operations on devices
 *
 * @note  Requests not handled by VFS are passed to the file system together
 *        with one argument fetched from @p arg. Caller must always provide
 *        this argument (NULL if request does not use it), otherwise behavior
 *        is undefined (va_arg on missing argument).
 *
 * @param[in]     *file         file
 * @param[in]      rq           request
 * @param[in,out]  arg          argument
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_vfioctl(kfile_t *file, int rq, va_list arg)
{
        if (is_file_valid(file)) {
                switch (rq) {
                case IOCTL_VFS__NON_BLOCKING_RD_MODE:
                        file->f_flag.fattr.non_blocking_rd = true;
                        file->f_flag.fattr.non_strict_rd = false;
                        return ESUCC;

                case IOCTL_VFS__NON_STRICT_RD_MODE:
                        file->f_flag.fattr.non_blocking_rd = false;
                        file->f_flag.fattr.non_strict_rd = true;
                        return ESUCC;

                case IOCTL_VFS__DEFAULT_RD_MODE:
                        file->f_flag.fattr.non_blocking_rd = false;
                        file->f_flag.fattr.non_strict_rd = false;
                        return ESUCC;

                case IOCTL_VFS__NON_BLOCKING_WR_MODE:
                        file->f_flag.fattr.non_blocking_wr = true;
                        return ESUCC;

                case IOCTL_VFS__DEFAULT_WR_MODE:
                        file->f_flag.fattr.non_blocking_wr = false;
                        return ESUCC;

                case IOCTL_VFS__IS_NON_BLOCKING_RD_MODE: {
                        bool *val = va_arg(arg, bool *);
                        if (!val) {
                                return EINVAL;
                        }
                        *val = file->f_flag.fattr.non_blocking_rd;
                        return ESUCC;
                }

                case IOCTL_VFS__IS_NON_STRICT_RD_MODE: {
                        bool *val = va_arg(arg, bool *);
                        if (!val) {
                                return EINVAL;
                        }
                        *val = file->f_flag.fattr.non_strict_rd;
                        return ESUCC;
                }

                case IOCTL_VFS__IS_NON_BLOCKING_WR_MODE: {
                        bool *val = va_arg(arg, bool *);
                        if (!val) {
                                return EINVAL;
                        }
                        *val = file->f_flag.fattr.non_blocking_wr;
                        return ESUCC;
                }
                }

                return file->FS_if->fs_ioctl(file->FS_hdl, file->f_hdl, rq, va_arg(arg, void *));
        } else {
                return EINVAL;
        }
}

//==============================================================================
/**
 * @brief Function returns file/dir status
 *
 * @param[in]  *file            file object
 * @param[out] *stat            pointer to stat structure
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_fstat(kfile_t *file, struct stat *stat)
{
        int err = EINVAL;

        if (is_file_valid(file) && stat) {
                err = file->FS_if->fs_fstat(file->FS_hdl, file->f_hdl, stat);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function flush file data
 *
 * @param[in] *file     file to flush
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_fflush(kfile_t *file)
{
        int err = EINVAL;

        if (is_file_valid(file)) {
                err = file->FS_if->fs_flush(file->FS_hdl, file->f_hdl);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function check end of file
 *
 * @param[in]  *file    file
 * @param[out] *eof     EOF indicator
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_feof(kfile_t *file, int *eof)
{
        if (is_file_valid(file) && eof) {
                *eof = file->f_flag.eof ? EOF : 0;
                return ESUCC;
        } else {
                return EINVAL;
        }
}

//==============================================================================
/**
 * @brief Function clear file's error
 *
 * @param[in] *file     file
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_clearerr(kfile_t *file)
{
        if (is_file_valid(file)) {
                file->f_flag.eof = false;
                file->f_flag.error = false;
                return ESUCC;
        } else {
                return EINVAL;
        }
}

//==============================================================================
/**
 * @brief Function check that file has no errors
 *
 * @param[in]  file     file
 * @param[out] error    error indicator (1 for error, 0 no error)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_ferror(kfile_t *file, int *error)
{
        if (is_file_valid(file) && error) {
                *error = file->f_flag.error ? 1 : 0;
                return ESUCC;
        } else {
                return EINVAL;
        }
}

//==============================================================================
/**
 * @brief Synchronize internal buffers of mounted file systems
 *
 * @param None
 *
 * @return None
 */
//==============================================================================
void _vfs_sync(void)
{
        if (_mutex_lock(VFS.resource_mtx, _WAIT_FOREVER_MS) == ESUCC) {

                _llist_foreach(FS_entry_t *, fs, VFS.mnt_list)
                {
                        fs->interface->fs_sync(fs->handle);
                }

                _mutex_unlock(VFS.resource_mtx);
        }
}

//==============================================================================
/**
 * @brief  Function check if selected mount point and current mount path
 *         allow to mount this file system as root.
 *
 * @param  mount_point          mount point path
 *
 * @return If file system can be mount as root file system then true is returned,
 *         otherwise false.
 */
//==============================================================================
static bool is_first_fs(const char *mount_point)
{
        return sys_llist_size(VFS.mnt_list) == 0 && strcmp(mount_point, "/") == 0;
}

//==============================================================================
/**
 * @brief  Create a new file system entry. Function initialize selected file system.
 *
 * @param[in ] parent_FS        parent file system (can be NULL if none)
 * @param[in ] fs_mount_point   file system mount point. This object must be permanent
 * @param[in ] fs_src_file      file system source file. This object must be permanent
 * @param[in ] fs_interface     file system interface (is copied to the object)
 * @param[in ] opts             file system options
 * @param[out] entry            entry
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int new_FS_entry(FS_entry_t *parent_FS, const char *fs_mount_point, const char *fs_src_file,
        const vfs_FS_itf_t *fs_interface, const char *opts, FS_entry_t **fs_entry)
{
        FS_entry_t *new_FS = NULL;
        int err = _kzalloc(_MM_KRN, sizeof(FS_entry_t), NULL, 0, 0, cast(void **, &new_FS));
        if (!err) {
                err = fs_interface->fs_init(&new_FS->handle, fs_src_file, opts);
                if (!err) {
                        new_FS->interface = fs_interface;
                        new_FS->mount_point = fs_mount_point;
                        new_FS->source_path = fs_src_file;
                        new_FS->parent = parent_FS;
                        new_FS->children_cnt = 0;
                        new_FS->refs = 0;
                        *fs_entry = new_FS;
                } else {
                        _kfree(_MM_KRN, cast(void **, &new_FS));
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Delete created file system entry. Function try release mounted file system.
 *
 * @param  this         file system entry object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int delete_FS_entry(FS_entry_t *this)
{
        int err = EINVAL;

        if (this) {
                err = this->interface->fs_sync(this->handle);
                if (err) {
                        printk("VFS: unable to sync '%s' (%d)", this->mount_point, err);
                }

                err = this->interface->fs_release(this->handle);
                if (!err) {
                        if (this->parent && this->parent->children_cnt) {
                                this->parent->children_cnt--;
                        }

                        if (this->mount_point) {
                                _kfree(_MM_KRN, cast(void **, &this->mount_point));
                        }

                        if (this->source_path) {
                                _kfree(_MM_KRN, cast(void **, &this->source_path));
                        }

                        _kfree(_MM_KRN, cast(void **, &this));
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Release file system reference taken by get_path_base_FS().
 *
 * @param  fs           file system entry (can be NULL)
 */
//==============================================================================
static void put_FS(FS_entry_t *fs)
{
        if (fs && (_mutex_lock(VFS.resource_mtx, _WAIT_FOREVER_MS) == ESUCC)) {
                if (fs->refs > 0) {
                        fs->refs--;
                }

                _mutex_unlock(VFS.resource_mtx);
        }
}

//==============================================================================
/**
 * @brief  Check if directory exists in selected file system.
 *
 * @param  fs           file system entry
 * @param  path         directory path (FS internal)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int check_dir_exists(FS_entry_t *fs, const char *path)
{
        kdir_t *dir = NULL;

        int err = _kzalloc(_MM_KRN, sizeof(kdir_t), NULL, 0, 0, cast(void **, &dir));
        if (!err) {
                dir->FS_hdl = fs->handle;
                dir->FS_if = fs->interface;

                err = fs->interface->fs_opendir(fs->handle, path, dir);
                if (!err) {
                        err = fs->interface->fs_closedir(fs->handle, dir);
                }

                _kfree(_MM_KRN, cast(void **, &dir));
        }

        return err;
}

//==============================================================================
/**
 * @brief Check if file object is valid
 *
 * @param file  file object to examine
 *
 * @return true if object is valid, otherwise false
 */
//==============================================================================
static bool is_file_valid(kfile_t *file)
{
        return (_mm_is_object_in_heap(file) && file->FS_hdl != NULL && file->FS_if != NULL &&
                file->header.type == RES_TYPE_FILE && file->FS_if->fs_magic == _VFS_FILE_SYSTEM_MAGIC_NO);
}

//==============================================================================
/**
 * @brief Check if dir object is valid
 *
 * @param file  file object to examine
 *
 * @return true if object is valid, otherwise false
 */
//==============================================================================
static bool is_dir_valid(kdir_t *dir)
{
        return (_mm_is_object_in_heap(dir) && dir->FS_if != NULL && dir->header.type == RES_TYPE_DIR);
}

//==============================================================================
/**
 * @brief Check if selected path is a mount point or contains any mount point.
 *        Function shall be called with VFS mutex locked.
 *
 * @param path          absolute path (trailing slash is ignored)
 *
 * @return true if mount point is at or under path, otherwise false.
 */
//==============================================================================
static bool is_mount_point_at_or_under(const char *path)
{
        size_t len = strlen(path);

        if ((len > 0) && (path[len - 1] == '/')) {
                len--;
        }

        if (len == 0) { // root directory
                return true;
        }

        _llist_foreach(FS_entry_t *, entry, VFS.mnt_list)
        {
                // strncmp() fails if mount point is shorter than len, then mount_point[len] is valid
                if ((strncmp(entry->mount_point, path, len) == 0) && (entry->mount_point[len] == '/')) {
                        return true;
                }
        }

        return false;
}

//==============================================================================
/**
 * @brief Function return file system entry mounted exactly at selected path.
 *
 * @param[in]  path             path to FS
 * @param[in]  len              number of path characters to compare
 * @param[out] position         object position in list (can be NULL)
 * @param[out] fs_entry         found entry
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int get_path_FS(const char *path, size_t len, int *position, FS_entry_t **fs_entry)
{
        int pos = 0;

        _llist_foreach(FS_entry_t *, entry, VFS.mnt_list)
        {
                if ((strlen(entry->mount_point) == len) && (strncmp(path, entry->mount_point, len) == 0)) {
                        if (position) {
                                *position = pos;
                        }

                        *fs_entry = entry;
                        return ESUCC;
                }

                pos++;
        }

        return ENOENT;
}

//==============================================================================
/**
 * @brief Function return file system mounted at path given without trailing
 *        slash (e.g. "/mnt" for mount point "/mnt/").
 *
 * @param[in]  path             path to FS (without trailing slash)
 * @param[in]  len              path length
 * @param[out] fs_entry         found entry
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int get_mount_root_FS(const char *path, size_t len, FS_entry_t **fs_entry)
{
        _llist_foreach(FS_entry_t *, entry, VFS.mnt_list)
        {
                // mount points always end with slash
                if ((strlen(entry->mount_point) == len + 1) && (strncmp(path, entry->mount_point, len) == 0)) {
                        *fs_entry = entry;
                        return ESUCC;
                }
        }

        return ENOENT;
}

//==============================================================================
/**
 * @brief Function returned the base file system of selected path. The external
 *        path is passed by pointer ext_path. On success, a reference of the
 *        file system is taken and shall be released by put_FS().
 *        Function is thread safe.
 *
 * @param[in]  path           absolute path
 * @param[in]  mnt_root       if true then path without trailing slash that
 *                            points to mount point is resolved to the root of
 *                            mounted file system (otherwise to parent FS)
 * @param[out] ext_path       pointer to external part of path (can be NULL)
 * @param[out] fs_entry       file system entry
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int get_path_base_FS(const char *path, bool mnt_root, const char **ext_path, FS_entry_t **fs_entry)
{
        size_t len = path ? strlen(path) : 0;

        if ((len == 0) || (path[0] != '/') || !fs_entry) {
                return EINVAL;
        }

        int err = _mutex_lock(VFS.resource_mtx, _WAIT_FOREVER_MS);
        if (!err) {
                const char *ext = NULL;

                err = ENOENT;

                if (mnt_root && (path[len - 1] != '/')) {
                        if (get_mount_root_FS(path, len, fs_entry) == ESUCC) {
                                ext = root_path;
                                err = ESUCC;
                        }
                }

                if (err) {
                        // position of the last slash of tested path prefix
                        size_t pos = len - 1;
                        while ((pos > 0) && (path[pos] != '/')) {
                                pos--;
                        }

                        for (;;) {
                                if (get_path_FS(path, pos + 1, NULL, fs_entry) == ESUCC) {
                                        ext = path + pos;
                                        err = ESUCC;
                                        break;
                                }

                                if (pos == 0) {
                                        break;
                                }

                                do {
                                        pos--;
                                } while ((pos > 0) && (path[pos] != '/'));
                        }
                }

                if (!err) {
                        (*fs_entry)->refs++;

                        if (ext_path) {
                                *ext_path = ext;
                        }
                }

                _mutex_unlock(VFS.resource_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function create new path with slash and CWD correction.
 *        Function set errno: ENOMEM. Path should be destroyed by free() family
 *        functions.
 *
 * @param[in]  path             path to correct
 * @param[in]  corr             path correction kind
 * @param[out] new_path         new path
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int new_absolute_path(const struct vfs_path *path, enum path_correction corr, char **new_path)
{
        if (!path || !path->PATH || !new_path) {
                return EINVAL;
        }

        size_t abslen = 3; // additional 2 slashes (if needed) and nul

        if (path->PATH[0] == '/') {
                abslen += strlen(path->PATH);
        } else {
                if (!path->CWD || (path->CWD[0] != '/')) {
                        return EINVAL;
                }

                abslen += strlen(path->CWD) + strlen(path->PATH);
        }

        if ((abslen - 3) > PATH_MAX_LEN) {
                return ENAMETOOLONG;
        }

        // memory allocation
        char *abspath;
        int err = _kzalloc(_MM_KRN, abslen, _cpuctl_fast_mem, 0, 0, cast(void *, &abspath));
        if (!err) {
                if (path->PATH[0] == '/') {
                        strcpy(abspath, path->PATH);
                } else {
                        strcpy(abspath, path->CWD);

                        if (LAST_CHARACTER(path->CWD) != '/') {
                                strcat(abspath, "/");
                        }

                        strcat(abspath, path->PATH);
                }

                err = _vfs_realpath(abspath, corr);
                if (err) {
                        _kfree(_MM_KRN, cast(void *, &abspath));
                        return err;
                }

                size_t plen = strsize(abspath);

                if (_mm_align(plen) < _mm_align(abslen)) {
                        if (_kzalloc(_MM_KRN, plen, _cpuctl_fast_mem, 0, 0, cast(void **, new_path)) == 0) {
                                strcpy(*new_path, abspath);
                                _kfree(_MM_KRN, cast(void *, &abspath));
                                abspath = *new_path;
                        }
                }

                *new_path = abspath;
        }

        return err;
}

//==============================================================================
/**
 * @brief Function reduce relative path elements from absolute path
 *
 * Path is processed element by element: "." elements are removed, ".."
 * elements remove the previous element (root is never left), multiple slashes
 * are reduced to single one. Elements are recognized only between slashes, so
 * names like "file." or "a..b" are preserved. Path that ends with "." or ".."
 * is treated as directory (trailing slash is kept).
 *
 * @param[in, out] path        reduce absolute path buffer (./, ../)
 * @param[in]      corr        path correction kind
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _vfs_realpath(char *path, enum path_correction corr)
{
        if (!path || (path[0] != '/')) {
                return EINVAL;
        }

        const char *rdptr = path + 1;
        char *wrptr = path + 1; // root slash is always kept

        while (*rdptr != '\0') {
                // skip slashes
                while (*rdptr == '/') {
                        rdptr++;
                }

                if (*rdptr == '\0') {
                        break;
                }

                const char *element = rdptr;
                while ((*rdptr != '\0') && (*rdptr != '/')) {
                        rdptr++;
                }

                size_t element_len = rdptr - element;
                bool slash_follows = (*rdptr == '/');

                if ((element_len == 1) && (element[0] == '.')) {
                        // current directory: nothing to do

                } else if ((element_len == 2) && (element[0] == '.') && (element[1] == '.')) {
                        // parent directory: remove last written element ("name/")
                        if (wrptr > path + 1) {
                                wrptr--; // slash of last element
                                while ((wrptr > path + 1) && (*(wrptr - 1) != '/')) {
                                        wrptr--;
                                }
                        }

                } else {
                        // write position is never ahead of read position
                        memmove(wrptr, element, element_len);
                        wrptr += element_len;

                        if (slash_follows) {
                                *wrptr++ = '/';
                        }
                }
        }

        char last = *(wrptr - 1);

        switch (corr) {
        case ADD_SLASH:
                if (last != '/') {
                        *wrptr++ = '/';
                }
                break;
        case SUB_SLASH:
                if ((last == '/') && (wrptr - path > 1)) {
                        wrptr--;
                }
                break;
        default:
                break;
        }

        *wrptr = '\0';

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Open driver directly by path in format "dev://<module>:<major>:<minor>".
 *
 * @param  path         device path
 * @param  flags        open flags
 * @param  file_obj     file object to fill
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int open_device_file(const char *path, int flags, kfile_t *file_obj)
{
        const char *spec = path + sizeof(dev_path_protocol) - 1;

        const char *colon = strchr(spec, ':');
        if (!colon) {
                return ENODEV;
        }

        char module_name[64];
        size_t name_len = colon - spec;
        if ((name_len == 0) || (name_len >= sizeof(module_name))) {
                return ENODEV;
        }

        memcpy(module_name, spec, name_len);
        module_name[name_len] = '\0';

        uint major = 0;
        uint minor = 0;
        if (sys_sscanf(colon + 1, "%u:%u", &major, &minor) != 2) {
                return ENODEV;
        }

        if ((major > DEV_NUM_MAX) || (minor > DEV_NUM_MAX)) {
                return ENODEV;
        }

        int modid = _module_get_ID(module_name);
        if (modid < 0) {
                return ENODEV;
        }

        dev_t dev = _dev_t__create(modid, major, minor);

        int err = _driver_open(dev, vfs_filter_flags_for_device(flags));
        if (!err) {
                file_obj->f_hdl = cast(void *, dev);
                file_obj->FS_hdl = const_cast(&direct_devfs);
                file_obj->FS_if = &direct_devfs;
                file_obj->FS_entry = NULL;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Close driver file.
 *
 * @param  fshdl        FS handle
 * @param  fhdl         file handle
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int direct_devfs_close(void *fshdl, void *fhdl)
{
        UNUSED_ARG1(fshdl);
        return _driver_close(cast(dev_t, fhdl), true);
}

//==============================================================================
/**
 * @brief  Write data to driver file.
 *
 * @param  fshdl        FS handle
 * @param  fhdl         file handle
 * @param  src          data source
 * @param  count        data count
 * @param  fpos         file position
 * @param  wrcnt        write count
 * @param  attr         file attributes
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int direct_devfs_write(void *fshdl, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr attr)
{
        UNUSED_ARG1(fshdl);
        return _driver_write(cast(dev_t, fhdl), src, count, fpos, wrcnt, attr);
}

//==============================================================================
/**
 * @brief  Read data from driver file.
 *
 * @param  fshdl        FS handle
 * @param  fhdl         file handle
 * @param  dst          data destination
 * @param  count        data count
 * @param  fpos         file position
 * @param  rdcnt        read count
 * @param  attr         file attributes
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int direct_devfs_read(void *fshdl, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr attr)
{
        UNUSED_ARG1(fshdl);
        return _driver_read(cast(dev_t, fhdl), dst, count, fpos, rdcnt, attr);
}

//==============================================================================
/**
 * @brief  IO request.
 *
 * @param  fshdl        FS handle
 * @param  fhdl         file handle
 * @param  iorq         IO request
 * @param  arg          IO request argument
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int direct_devfs_ioctl(void *fshdl, void *fhdl, int iorq, void *arg)
{
        UNUSED_ARG1(fshdl);
        return _driver_ioctl(cast(dev_t, fhdl), iorq, arg);
}

//==============================================================================
/**
 * @brief  Driver file statistics.
 *
 * @param  fshdl        FS handle
 * @param  fhdl         file handle
 * @param  stat         statistics
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int direct_devfs_fstat(void *fshdl, void *fhdl, struct stat *stat)
{
        UNUSED_ARG1(fshdl);
        struct vfs_dev_stat dev_stat;

        int err = _driver_stat(cast(dev_t, fhdl), &dev_stat);
        if (!err) {
                memset(stat, 0, sizeof(*stat));
                stat->st_dev = cast(dev_t, fhdl);
                stat->st_mode = 0666 | S_IFDEV;
                stat->st_size = dev_stat.st_size;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Flush driver file buffer.
 *
 * @param  fshdl        FS handle
 * @param  fhdl         file handle
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int direct_devfs_flush(void *fshdl, void *fhdl)
{
        UNUSED_ARG1(fshdl);
        return _driver_flush(cast(dev_t, fhdl));
}

/*==============================================================================
  End of file
==============================================================================*/
