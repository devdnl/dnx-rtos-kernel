/*==============================================================================
@file    hostfs.c

@author  Daniel Zorychta

@brief   Host file system (POSIX simulator). File system gives access to the
         host directory selected by the simulator --hostfs option.

@note    Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/*
 * Mount example:
 *      mount("hostfs", "", "/host", "");          - whole --hostfs directory
 *      mount("hostfs", "/sub/dir", "/mnt", "ro");  - subdirectory, read only
 *
 * Source path selects subdirectory of the host directory. Option "ro" mounts
 * file system in read only mode.
 */

/*==============================================================================
  Include files
==============================================================================*/
#include "fs/fs.h"
#include "posix/host/host.h"

/*==============================================================================
  Local macros
==============================================================================*/
/* maximal size of single host operation (task switching latency) */
#define MAX_CHUNK_SIZE (64 * 1024)

/* maximal length of source path (subdirectory) */
#define PREFIX_MAX 256

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        bool read_only;
        u32_t opened;
        char prefix[PREFIX_MAX];
} hostfs_t;

typedef struct {
        int fd;
} hostfile_t;

typedef struct {
        void *dir;
        size_t seek;
        char name[_HOST_FS_NAME_MAX + 1];
} hostdir_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int host_errno(int err);
static int make_path(hostfs_t *hdl, const char *path, char **hpath);
static mode_t host_mode(const struct _host_stat *hstat);
static void fill_stat(const struct _host_stat *hstat, struct stat *stat);
static void opened_inc(hostfs_t *hdl);
static void opened_dec(hostfs_t *hdl);

/*==============================================================================
  Local objects
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  External objects
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize file system
 *
 * @param[out]          **fs_handle             file system allocated memory
 * @param[in ]           *src_path              subdirectory of host directory
 * @param[in ]           *opts                  file system options (can be NULL)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_INIT(hostfs, void **fs_handle, const char *src_path, const char *opts)
{
        if (!_host_fs_is_available()) {
                printk("HOSTFS: host directory not selected (use --hostfs <dir>)");
                return ENODEV;
        }

        if (src_path && (strlen(src_path) >= PREFIX_MAX)) {
                return ENAMETOOLONG;
        }

        int err = sys_zalloc(sizeof(hostfs_t), fs_handle);
        if (!err) {
                hostfs_t *hdl = *fs_handle;

                hdl->read_only = sys_stropt_is_flag(opts, "ro");

                if (src_path && (src_path[0] != '\0')) {
                        if (src_path[0] != '/') {
                                hdl->prefix[0] = '/';
                        }
                        sys_strlcat(hdl->prefix, src_path, sizeof(hdl->prefix));
                }

                /* source directory must exist */
                struct _host_stat hstat;
                char *hpath = NULL;
                err = make_path(hdl, "/", &hpath);
                if (!err) {
                        err = host_errno(_host_fs_stat(hpath, &hstat));
                        if (!err && (hstat.type != _HOST_FTYPE_DIR)) {
                                err = ENOTDIR;
                        }
                        sys_free(cast(void *, &hpath));
                }

                if (err) {
                        sys_free(fs_handle);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Release file system
 *
 * @param[in ]          *fs_handle              file system allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_RELEASE(hostfs, void *fs_handle)
{
        hostfs_t *hdl = fs_handle;

        if (hdl->opened > 0) {
                return EBUSY;
        }

        return sys_free(&fs_handle);
}

//==============================================================================
/**
 * @brief Open file
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *fhdl                   file handler
 * @param[out]          *fpos                   file position
 * @param[in]           *path                   file path
 * @param[in]            flags                  file open flags
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_OPEN(hostfs, void *fs_handle, void **fhdl, fpos_t *fpos, const char *path, u32_t flags)
{
        hostfs_t *hdl = fs_handle;

        const u32_t acc = flags & O_ACCMODE;
        const bool wr = (acc == O_WRONLY) || (acc == O_RDWR);

        if (hdl->read_only && (wr || (flags & (O_CREAT | O_TRUNC)))) {
                return EROFS;
        }

        u32_t hflags = (acc == O_WRONLY) ? _HOST_FS_O_WRONLY : (acc == O_RDWR) ? _HOST_FS_O_RDWR : _HOST_FS_O_RDONLY;

        hflags |= (flags & O_CREAT) ? _HOST_FS_O_CREAT : 0;
        hflags |= (flags & O_EXCL) ? _HOST_FS_O_EXCL : 0;
        hflags |= (flags & O_TRUNC) ? _HOST_FS_O_TRUNC : 0;

        char *hpath = NULL;
        int err = make_path(hdl, path, &hpath);
        if (err) {
                return err;
        }

        hostfile_t *file = NULL;
        err = sys_zalloc(sizeof(hostfile_t), cast(void *, &file));
        if (!err) {
                err = host_errno(_host_fs_open(hpath, hflags, 0666, &file->fd));

                if (!err && (flags & O_APPEND)) {
                        struct _host_stat hstat;
                        err = host_errno(_host_fs_fstat(file->fd, &hstat));
                        if (!err) {
                                *fpos = hstat.size;
                        } else {
                                _host_fs_close(file->fd);
                        }
                } else if (!err) {
                        *fpos = 0;
                }

                if (!err) {
                        *fhdl = file;
                        opened_inc(hdl);
                } else {
                        sys_free(cast(void *, &file));
                }
        }

        sys_free(cast(void *, &hpath));

        return err;
}

//==============================================================================
/**
 * @brief Close file
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CLOSE(hostfs, void *fs_handle, void *fhdl)
{
        hostfs_t *hdl = fs_handle;
        hostfile_t *file = fhdl;

        int err = host_errno(_host_fs_close(file->fd));

        sys_free(cast(void *, &file));
        opened_dec(hdl);

        return err;
}

//==============================================================================
/**
 * @brief Write data to the file
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ]          *fpos                   position in file
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_WRITE(hostfs, void *fs_handle, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        hostfs_t *hdl = fs_handle;
        hostfile_t *file = fhdl;

        if (hdl->read_only) {
                return EROFS;
        }

        int err = ESUCC;
        *wrcnt = 0;

        while (!err && (*wrcnt < count)) {
                size_t len = min(count - *wrcnt, cast(size_t, MAX_CHUNK_SIZE));
                size_t n = 0;

                err = host_errno(_host_fs_write(file->fd, *fpos + *wrcnt, &src[*wrcnt], len, &n));
                *wrcnt += n;
        }

        return err;
}

//==============================================================================
/**
 * @brief Read data from file
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ]          *fpos                   position in file
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_READ(hostfs, void *fs_handle, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG2(fs_handle, fattr);

        hostfile_t *file = fhdl;

        int err = ESUCC;
        *rdcnt = 0;

        while (!err && (*rdcnt < count)) {
                size_t len = min(count - *rdcnt, cast(size_t, MAX_CHUNK_SIZE));
                size_t n = 0;

                err = host_errno(_host_fs_read(file->fd, *fpos + *rdcnt, &dst[*rdcnt], len, &n));
                *rdcnt += n;

                if (n < len) {
                        break; // end of file
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief IO operations on files
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_IOCTL(hostfs, void *fs_handle, void *fhdl, int request, void *arg)
{
        UNUSED_ARG4(fs_handle, fhdl, request, arg);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Flush file data
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_FLUSH(hostfs, void *fs_handle, void *fhdl)
{
        UNUSED_ARG2(fs_handle, fhdl);

        /* data is written directly to the host (host cache) */
        return ESUCC;
}

//==============================================================================
/**
 * @brief Create directory
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created directory
 * @param[in ]           mode                   dir mode
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_MKDIR(hostfs, void *fs_handle, const char *path, mode_t mode)
{
        hostfs_t *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        char *hpath = NULL;
        int err = make_path(hdl, path, &hpath);
        if (!err) {
                err = host_errno(_host_fs_mkdir(hpath, mode & S_IPMT));
                sys_free(cast(void *, &hpath));
        }

        return err;
}

//==============================================================================
/**
 * @brief Create pipe
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created pipe
 * @param[in ]           mode                   pipe mode
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_MKFIFO(hostfs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Create node for driver file
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created node
 * @param[in ]           dev                    driver number
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_MKNOD(hostfs, void *fs_handle, const char *path, const dev_t dev)
{
        UNUSED_ARG3(fs_handle, path, dev);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Open directory
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of opened directory
 * @param[in ]          *dir                    directory object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_OPENDIR(hostfs, void *fs_handle, const char *path, kdir_t *dir)
{
        hostfs_t *hdl = fs_handle;

        char *hpath = NULL;
        int err = make_path(hdl, path, &hpath);
        if (err) {
                return err;
        }

        err = sys_zalloc(sizeof(hostdir_t), &dir->d_hdl);
        if (!err) {
                hostdir_t *hdir = dir->d_hdl;

                dir->d_seek = 0;
                dir->d_items = 0;

                err = host_errno(_host_fs_opendir(hpath, &hdir->dir));
                if (!err) {
                        opened_inc(hdl);
                } else {
                        sys_free(&dir->d_hdl);
                }
        }

        sys_free(cast(void *, &hpath));

        return err;
}

//==============================================================================
/**
 * @brief Function close dir
 *
 * @param[in] *fs_handle        FS handle
 * @param[in] *dir              directory info
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CLOSEDIR(hostfs, void *fs_handle, kdir_t *dir)
{
        hostfs_t *hdl = fs_handle;

        int err = ESUCC;

        if (dir->d_hdl) {
                hostdir_t *hdir = dir->d_hdl;

                err = host_errno(_host_fs_closedir(hdir->dir));

                sys_free(&dir->d_hdl);
                opened_dec(hdl);
        }

        return err;
}

//==============================================================================
/**
 * @brief Read directory
 *
 * @note  rewinddir()/seekdir() are supported: when VFS changed d_seek the
 *        directory is rewound and entries are skipped to requested position.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in,out]       *dir                    directory object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_READDIR(hostfs, void *fs_handle, kdir_t *dir)
{
        UNUSED_ARG1(fs_handle);

        hostdir_t *hdir = dir->d_hdl;
        struct _host_stat hstat;
        int err = ESUCC;

        if (dir->d_seek != hdir->seek) {
                _host_fs_rewinddir(hdir->dir);
                hdir->seek = 0;

                while (!err && (hdir->seek < dir->d_seek)) {
                        err = host_errno(_host_fs_readdir(hdir->dir, hdir->name, sizeof(hdir->name), &hstat));
                        if (!err) {
                                hdir->seek++;
                        }
                }
        }

        if (!err) {
                err = host_errno(_host_fs_readdir(hdir->dir, hdir->name, sizeof(hdir->name), &hstat));
        }

        if (!err) {
                hdir->seek++;

                dir->dirent.dev = 0;
                dir->dirent.d_name = hdir->name;
                dir->dirent.size = (hstat.type == _HOST_FTYPE_DIR) ? 0 : hstat.size;
                dir->dirent.mode = host_mode(&hstat);
                dir->d_seek = hdir->seek;
        }

        return err;
}

//==============================================================================
/**
 * @brief Remove file/directory
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of removed file/directory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_REMOVE(hostfs, void *fs_handle, const char *path)
{
        hostfs_t *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        char *hpath = NULL;
        int err = make_path(hdl, path, &hpath);
        if (!err) {
                err = host_errno(_host_fs_remove(hpath));
                sys_free(cast(void *, &hpath));
        }

        return err;
}

//==============================================================================
/**
 * @brief Rename file/directory
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *old_name               old object name
 * @param[in ]          *new_name               new object name
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_RENAME(hostfs, void *fs_handle, const char *old_name, const char *new_name)
{
        hostfs_t *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        char *hold = NULL;
        char *hnew = NULL;

        int err = make_path(hdl, old_name, &hold);
        if (!err) {
                err = make_path(hdl, new_name, &hnew);
        }

        if (!err) {
                err = host_errno(_host_fs_rename(hold, hnew));
        }

        if (hold) {
                sys_free(cast(void *, &hold));
        }

        if (hnew) {
                sys_free(cast(void *, &hnew));
        }

        return err;
}

//==============================================================================
/**
 * @brief Change file's mode
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[in ]           mode                   new file mode
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CHMOD(hostfs, void *fs_handle, const char *path, mode_t mode)
{
        hostfs_t *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        char *hpath = NULL;
        int err = make_path(hdl, path, &hpath);
        if (!err) {
                err = host_errno(_host_fs_chmod(hpath, mode & S_IPMT));
                sys_free(cast(void *, &hpath));
        }

        return err;
}

//==============================================================================
/**
 * @brief Change file's owner and group
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[in ]           owner                  new file owner
 * @param[in ]           group                  new file group
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CHOWN(hostfs, void *fs_handle, const char *path, uid_t owner, gid_t group)
{
        UNUSED_ARG4(fs_handle, path, owner, group);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Return file status
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[out]          *stat                   file status
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_FSTAT(hostfs, void *fs_handle, void *fhdl, struct stat *stat)
{
        UNUSED_ARG1(fs_handle);

        hostfile_t *file = fhdl;

        struct _host_stat hstat;
        int err = host_errno(_host_fs_fstat(file->fd, &hstat));
        if (!err) {
                fill_stat(&hstat, stat);
        }

        return err;
}

//==============================================================================
/**
 * @brief Return file/dir status
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[out]          *stat                   file status
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_STAT(hostfs, void *fs_handle, const char *path, struct stat *stat)
{
        hostfs_t *hdl = fs_handle;

        char *hpath = NULL;
        int err = make_path(hdl, path, &hpath);
        if (!err) {
                struct _host_stat hstat;
                err = host_errno(_host_fs_stat(hpath, &hstat));
                if (!err) {
                        fill_stat(&hstat, stat);

                        if (hdl->read_only) {
                                stat->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
                        }
                }

                sys_free(cast(void *, &hpath));
        }

        return err;
}

//==============================================================================
/**
 * @brief Return file system status
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *statfs                 file system status
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_STATFS(hostfs, void *fs_handle, struct statfs *statfs)
{
        UNUSED_ARG1(fs_handle);

        struct _host_statfs hstatfs;
        int err = host_errno(_host_fs_statfs(&hstatfs));
        if (!err) {
                u64_t bsize = hstatfs.bsize ? hstatfs.bsize : 512;
                u64_t blocks = hstatfs.blocks;
                u64_t bfree = hstatfs.bfree;

                /* values are scaled to fit in 32-bit fields */
                while ((blocks > UINT32_MAX) && (bsize < (UINT32_MAX / 2))) {
                        blocks /= 2;
                        bfree /= 2;
                        bsize *= 2;
                }

                statfs->f_type = SYS_FS_TYPE__NET;
                statfs->f_fsname = "hostfs";
                statfs->f_bsize = cast(u32_t, bsize);
                statfs->f_blocks = cast(u32_t, min(blocks, cast(u64_t, UINT32_MAX)));
                statfs->f_bfree = cast(u32_t, min(bfree, cast(u64_t, UINT32_MAX)));
                statfs->f_files = cast(u32_t, min(hstatfs.files, cast(u64_t, UINT32_MAX)));
                statfs->f_ffree = cast(u32_t, min(hstatfs.ffree, cast(u64_t, UINT32_MAX)));
        }

        return err;
}

//==============================================================================
/**
 * @brief Synchronize all buffers to a medium
 *
 * @param[in ]          *fs_handle              file system allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_SYNC(hostfs, void *fs_handle)
{
        UNUSED_ARG1(fs_handle);

        /* data is written directly to the host (host cache) */
        return ESUCC;
}

//==============================================================================
/**
 * @brief Function translate host error code to errno value.
 *
 * @param err           host error code
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int host_errno(int err)
{
        switch (err) {
        case _HOST_ESUCC:
                return ESUCC;
        case _HOST_EPERM:
                return EPERM;
        case _HOST_ENOENT:
                return ENOENT;
        case _HOST_EIO:
                return EIO;
        case _HOST_EBADF:
                return EBADF;
        case _HOST_ENOMEM:
                return ENOMEM;
        case _HOST_EACCES:
                return EACCES;
        case _HOST_EBUSY:
                return EBUSY;
        case _HOST_EEXIST:
                return EEXIST;
        case _HOST_EXDEV:
                return ENOTSUP; // rename between host file systems
        case _HOST_ENOTDIR:
                return ENOTDIR;
        case _HOST_EISDIR:
                return EISDIR;
        case _HOST_EINVAL:
                return EINVAL;
        case _HOST_EMFILE:
                return EMFILE;
        case _HOST_EFBIG:
                return EFBIG;
        case _HOST_ENOSPC:
                return ENOSPC;
        case _HOST_EROFS:
                return EROFS;
        case _HOST_ENAMETOOLONG:
                return ENAMETOOLONG;
        case _HOST_ENOTEMPTY:
                return ENOTEMPTY;
        case _HOST_ENOTSUP:
                return ENOTSUP;
        default:
                return EIO;
        }
}

//==============================================================================
/**
 * @brief Function create path in host file system (source path + path).
 *
 * @param hdl           file system handle
 * @param path          path relative to mount point
 * @param hpath         created path (must be freed)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int make_path(hostfs_t *hdl, const char *path, char **hpath)
{
        size_t len = strlen(hdl->prefix) + strlen(path) + 2;
        if (len > _HOST_FS_PATH_MAX) {
                return ENAMETOOLONG;
        }

        int err = sys_malloc(len, cast(void *, hpath));
        if (!err) {
                char *p = *hpath;
                p[0] = '\0';
                sys_strlcat(p, hdl->prefix, len);

                if (path[0] != '/') {
                        sys_strlcat(p, "/", len);
                }

                sys_strlcat(p, path, len);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function convert host file information to file mode.
 *
 * @param hstat         host file information
 *
 * @return File mode.
 */
//==============================================================================
static mode_t host_mode(const struct _host_stat *hstat)
{
        mode_t mode = hstat->perm & S_IPMT;

        switch (hstat->type) {
        case _HOST_FTYPE_DIR:
                mode |= S_IFDIR;
                break;
        default:
                mode |= S_IFREG;
                break;
        }

        return mode;
}

//==============================================================================
/**
 * @brief Function convert host file information to file status.
 *
 * @param hstat         host file information
 * @param stat          file status
 */
//==============================================================================
static void fill_stat(const struct _host_stat *hstat, struct stat *stat)
{
        memset(stat, 0, sizeof(*stat));

        stat->st_mode = host_mode(hstat);
        stat->st_size = (hstat->type == _HOST_FTYPE_DIR) ? 0 : hstat->size;
        stat->st_mtime = hstat->mtime;
        stat->st_ctime = hstat->ctime;
}

//==============================================================================
/**
 * @brief Function increase number of opened objects.
 *
 * @param hdl           file system handle
 */
//==============================================================================
static void opened_inc(hostfs_t *hdl)
{
        sys_critical_section_begin();
        hdl->opened++;
        sys_critical_section_end();
}

//==============================================================================
/**
 * @brief Function decrease number of opened objects.
 *
 * @param hdl           file system handle
 */
//==============================================================================
static void opened_dec(hostfs_t *hdl)
{
        sys_critical_section_begin();
        if (hdl->opened > 0) {
                hdl->opened--;
        }
        sys_critical_section_end();
}

/*==============================================================================
  End of file
==============================================================================*/
