/*==============================================================================
File    ext4fs.c

Author  Daniel Zorychta

Brief   EXT4 file system

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/*==============================================================================
  Include files
==============================================================================*/
#include "fs/fs.h"
#include "ext4.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define SECTOR_SIZE  512
#define LOCK_TIMEOUT _WAIT_FOREVER_MS
#define SYNC_TIMEOUT 2000
#define NAME_MAX_LEN 255

/* open flags handled by lwext4 (values in vfs.h are equal to lwext4 ones) */
#define EXT4_OPEN_FLAGS_MASK (O_ACCMODE | O_CREAT | O_TRUNC | O_APPEND)

/* mode bits accepted by chmod and mkdir */
#define MODE_PERM_MASK (S_IPMT | S_ISUID | S_ISGID | S_ISVTX)

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        struct ext4_mountpoint *mp;
        struct ext4_blockdev_iface bdif;
        struct ext4_blockdev bd;
        kfile_t *dev;
        kmtx_t *fs_mutex;
        u8_t buf[SECTOR_SIZE];
        u32_t open_files;
        struct ext4fs_file *files; /* list of opened files */
} ext4fs_t;

typedef struct ext4fs_file {
        ext4_file file;
        struct ext4fs_file *next; /* next opened file */
        bool modified;            /* mtime must be updated */
} ext4fs_file_t;

typedef struct {
        ext4_dir dir;
        size_t seek;                 /* position of the next entry */
        char *path;                  /* directory path + space for entry name */
        size_t path_len;             /* length of directory path with '/' */
        char name[NAME_MAX_LEN + 1]; /* name of current entry */
} ext4fs_dir_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int fs_lock(ext4fs_t *hdl);
static void fs_unlock(ext4fs_t *hdl);
static int file_opened(ext4fs_t *hdl);
static void file_closed(ext4fs_t *hdl);
static bool get_time(time_t *t);
static bool is_dir(u32_t mode);
static int dir_is_empty(ext4fs_t *hdl, const char *path);
static int fill_stat(ext4fs_t *hdl, const char *path, ext4_file *file, struct stat *stat);
static int zero_fill(ext4_file *file, u64_t from, u64_t to);
static void apply_mtime(ext4fs_t *hdl, ext4fs_file_t *fh);
static void share_fsize(ext4fs_t *hdl, ext4fs_file_t *fh);
static int bopen(struct ext4_blockdev *bdev);
static int bclose(struct ext4_blockdev *bdev);
static int bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id, uint32_t blk_cnt);
static int bwrite(struct ext4_blockdev *bdev, const void *buf, uint64_t blk_id, uint32_t blk_cnt);
static int bdev_lock(struct ext4_blockdev *bdev);
static int bdev_unlock(struct ext4_blockdev *bdev);
static void mp_lock(void *p_user);
static void mp_unlock(void *p_user);
static mode_t ext4ftype2vfs(u32_t mode);

/*==============================================================================
  Local objects
==============================================================================*/
static const struct ext4_lock EXT4_LOCK = {
        .lock = mp_lock,
        .unlock = mp_unlock,
};

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
 * @brief Initialize file system.
 *
 * @param[out]          **fs_handle             file system allocated memory
 * @param[in ]           *src_path              file source path
 * @param[in ]           *opts                  file system options (can be NULL)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_INIT(ext4fs, void **fs_handle, const char *src_path, const char *opts)
{
        int err = sys_zalloc(sizeof(ext4fs_t), fs_handle);
        if (err) {
                return err;
        }

        ext4fs_t *hdl = *fs_handle;
        bool mounted = false;
        bool read_only = sys_stropt_is_flag(opts, "ro");

        err = sys_fopen(src_path, read_only ? O_RDONLY : O_RDWR, 0, cast(kfile_t **, &hdl->dev));
        if (err) {
                goto finish;
        }

        struct stat st;
        err = sys_fstat(hdl->dev, &st);
        if (err) {
                goto finish;
        }

        err = sys_mutex_create(KMTX_TYPE_RECURSIVE, cast(kmtx_t **, &hdl->fs_mutex));
        if (err) {
                goto finish;
        }

        hdl->bdif.open = bopen;
        hdl->bdif.bread = bread;
        hdl->bdif.bwrite = bwrite;
        hdl->bdif.close = bclose;
        hdl->bdif.lock = bdev_lock;
        hdl->bdif.unlock = bdev_unlock;
        hdl->bdif.ph_bsize = SECTOR_SIZE;
        hdl->bdif.ph_bbuf = hdl->buf;
        hdl->bdif.ph_bcnt = st.st_size / SECTOR_SIZE;
        hdl->bdif.p_user = hdl;

        hdl->bd.part_size = st.st_size;
        hdl->bd.bdif = &hdl->bdif;

        err = ext4_mount(&hdl->bd, &hdl->mp, read_only, hdl);
        if (err) {
                goto finish;
        }

        mounted = true;
        ext4_mount_setup_locks(hdl->mp, &EXT4_LOCK);

        int rerr = ext4_recover(hdl->mp);
        if (rerr) {
                if (!read_only) {
                        printk("EXT4FS: %s journal recovery failed (%d)", src_path, rerr);
                        err = rerr;
                        goto finish;
                }

                printk("EXT4FS: %s journal not recovered, data can be inconsistent", src_path);
        }

        rerr = ext4_journal_start(hdl->mp);
        if (rerr && !read_only) {
                printk("EXT4FS: %s journal start failed (%d)", src_path, rerr);
                err = rerr;
                goto finish;
        }

        ext4_cache_write_back(__EXT4FS_CFG_WR_BUF_STRATEGY__, hdl->mp);

        if (read_only) {
                printk("EXT4FS: %s mounted read only", src_path);
        }

finish:
        if (err) {
                if (mounted) {
                        ext4_umount(hdl->mp);
                }

                if (hdl->fs_mutex) {
                        sys_mutex_destroy(hdl->fs_mutex);
                }

                if (hdl->dev) {
                        sys_fclose(cast(kfile_t *, hdl->dev));
                }

                sys_free(fs_handle);
        }

        return err;
}

//==============================================================================
/**
 * @brief Release file system.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_RELEASE(ext4fs, void *fs_handle)
{
        ext4fs_t *hdl = fs_handle;

        int err = fs_lock(hdl);
        if (err) {
                return err;
        }

        if (hdl->open_files > 0) {
                fs_unlock(hdl);
                return EBUSY;
        }

        err = ext4_cache_write_back(false, hdl->mp);
        if (!err) {
                err = ext4_cache_flush(hdl->mp);
        }

        if (err) {
                ext4_cache_write_back(__EXT4FS_CFG_WR_BUF_STRATEGY__, hdl->mp);
                fs_unlock(hdl);
                return err;
        }

        ext4_journal_stop(hdl->mp);

        err = ext4_umount(hdl->mp);
        if (err) {
                /* restore working state */
                ext4_journal_start(hdl->mp);
                ext4_cache_write_back(__EXT4FS_CFG_WR_BUF_STRATEGY__, hdl->mp);
                fs_unlock(hdl);
                return err;
        }

        fs_unlock(hdl);
        sys_mutex_destroy(hdl->fs_mutex);
        sys_fclose(cast(kfile_t *, hdl->dev));
        sys_free(&fs_handle);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Open file.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *fhdl                   file extra data
 * @param[out]          *fpos                   file position
 * @param[in]           *path                   file path
 * @param[in]            flags                  file open flags
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_OPEN(ext4fs, void *fs_handle, void **fhdl, fpos_t *fpos, const char *path, u32_t flags)
{
        ext4fs_t *hdl = fs_handle;
        ext4fs_file_t *fh = NULL;

        int err = sys_zalloc(sizeof(ext4fs_file_t), cast(void **, &fh));
        if (err) {
                return err;
        }

        err = fs_lock(hdl);
        if (err) {
                sys_free(cast(void **, &fh));
                return err;
        }

        u32_t mode = 0;
        bool exists = (ext4_mode_get(path, NULL, &mode, hdl->mp) == ESUCC);

        if (exists && is_dir(mode)) {
                err = EISDIR;

        } else if (exists && (flags & O_CREAT) && (flags & O_EXCL)) {
                err = EEXIST;

        } else {
                err = ext4_fopen2(&fh->file, path, flags & EXT4_OPEN_FLAGS_MASK, hdl->mp);
        }

        if (!err) {
                if (!exists) {
                        time_t t;
                        if (get_time(&t)) {
                                ext4_ctime_set(path, t, hdl->mp);
                                ext4_mtime_set(path, &fh->file, t, hdl->mp);
                                ext4_atime_set(path, t, hdl->mp);
                        }

                } else if ((flags & O_TRUNC) && ((flags & O_ACCMODE) != O_RDONLY)) {
                        fh->modified = true;
                }

                err = file_opened(hdl);
                if (err) {
                        ext4_fclose(&fh->file);
                }
        }

        if (!err) {
                fh->next = hdl->files;
                hdl->files = fh;

                /* O_TRUNC: other handles of this file must see new size */
                if (fh->modified) {
                        share_fsize(hdl, fh);
                }

                *fpos = ext4_ftell(&fh->file);
                *fhdl = fh;
        }

        fs_unlock(hdl);

        if (err) {
                sys_free(cast(void **, &fh));
        }

        return err;
}

//==============================================================================
/**
 * @brief Close file.
 *
 * Note: the file should always be closed (resources should be released) even
 *       function return error.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_CLOSE(ext4fs, void *fs_handle, void *fhdl)
{
        ext4fs_t *hdl = fs_handle;
        ext4fs_file_t *fh = fhdl;

        bool locked = (fs_lock(hdl) == ESUCC);

        for (ext4fs_file_t **pp = &hdl->files; *pp; pp = &(*pp)->next) {
                if (*pp == fh) {
                        *pp = fh->next;
                        break;
                }
        }

        apply_mtime(hdl, fh);
        ext4_fclose(&fh->file);

        if (locked) {
                fs_unlock(hdl);
        }

        sys_free(cast(void **, &fh));
        file_closed(hdl);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to the file.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ]          *fpos                   position in file
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_WRITE(ext4fs, void *fs_handle, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        ext4fs_t *hdl = fs_handle;
        ext4fs_file_t *fh = fhdl;

        *wrcnt = 0;

        int err = fs_lock(hdl);
        if (err) {
                return err;
        }

        /* lwext4 does not allow to seek beyond EOF: fill the gap by zeros */
        u64_t fsize = ext4_fsize(&fh->file);
        if (*fpos > fsize) {
                err = zero_fill(&fh->file, fsize, *fpos);
                fh->modified = true;
        }

        if (!err) {
                err = ext4_fseek(&fh->file, (i64_t)*fpos, SEEK_SET);
        }

        if (!err) {
                err = ext4_fwrite(&fh->file, src, count, wrcnt);
                if (*wrcnt > 0) {
                        fh->modified = true;
                }
        }

        /* other handles of this file must see new size */
        if (ext4_fsize(&fh->file) != fsize) {
                share_fsize(hdl, fh);
        }

        fs_unlock(hdl);

        return err;
}

//==============================================================================
/**
 * @brief Read data from file.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ]          *fpos                   position in file
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_READ(ext4fs, void *fs_handle, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        ext4fs_t *hdl = fs_handle;
        ext4fs_file_t *fh = fhdl;

        *rdcnt = 0;

        int err = fs_lock(hdl);
        if (err) {
                return err;
        }

        /* reading at or beyond EOF is not an error, just 0 bytes */
        if (*fpos < ext4_fsize(&fh->file)) {
                err = ext4_fseek(&fh->file, (i64_t)*fpos, SEEK_SET);
                if (!err) {
                        err = ext4_fread(&fh->file, dst, count, rdcnt);
                }
        }

        fs_unlock(hdl);

        return err;
}

//==============================================================================
/**
 * @brief IO operations on files.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_IOCTL(ext4fs, void *fs_handle, void *fhdl, int request, void *arg)
{
        UNUSED_ARG4(fs_handle, fhdl, request, arg);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Flush file data.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_FLUSH(ext4fs, void *fs_handle, void *fhdl)
{
        ext4fs_t *hdl = fs_handle;
        ext4fs_file_t *fh = fhdl;

        int err = fs_lock(hdl);
        if (!err) {
                apply_mtime(hdl, fh);
                err = ext4_cache_flush(hdl->mp);
                fs_unlock(hdl);
        }

        return err;
}

//==============================================================================
/**
 * @brief Return file status.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[out]          *stat                   file status
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_FSTAT(ext4fs, void *fs_handle, void *fhdl, struct stat *stat)
{
        ext4fs_t *hdl = fs_handle;
        ext4fs_file_t *fh = fhdl;

        return fill_stat(hdl, NULL, &fh->file, stat);
}

//==============================================================================
/**
 * @brief Return file/dir status.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[out]          *stat                   file status
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_STAT(ext4fs, void *fs_handle, const char *path, struct stat *stat)
{
        ext4fs_t *hdl = fs_handle;

        int err = fs_lock(hdl);
        if (err) {
                return err;
        }

        /* regular file: one path lookup, then everything from the inode */
        ext4_file file;
        memset(&file, 0, sizeof(file));

        if (ext4_fopen2(&file, path, O_RDONLY, hdl->mp) == ESUCC) {
                err = fill_stat(hdl, NULL, &file, stat);
                ext4_fclose(&file);
        } else {
                err = fill_stat(hdl, path, NULL, stat);
        }

        fs_unlock(hdl);

        return err;
}

//==============================================================================
/**
 * @brief Return file system status.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *statfs                 file system status
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_STATFS(ext4fs, void *fs_handle, struct statfs *statfs)
{
        ext4fs_t *hdl = fs_handle;

        statfs->f_type = SYS_FS_TYPE__SOLID;
        statfs->f_fsname = "ext4fs";

        struct ext4_mount_stats stfs;
        int err = ext4_mount_point_stats(hdl->mp, &stfs);
        if (!err) {
                statfs->f_bsize = stfs.block_size;
                statfs->f_blocks = stfs.blocks_count;
                statfs->f_bfree = stfs.free_blocks_count;
                statfs->f_ffree = stfs.free_inodes_count;
                statfs->f_files = stfs.inodes_count;
        }

        return err;
}

//==============================================================================
/**
 * @brief Create directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created directory
 * @param[in ]           mode                   dir mode
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_MKDIR(ext4fs, void *fs_handle, const char *path, mode_t mode)
{
        ext4fs_t *hdl = fs_handle;

        int err = fs_lock(hdl);
        if (err) {
                return err;
        }

        /* lwext4 returns success when object already exists */
        u32_t cur_mode;
        if (ext4_mode_get(path, NULL, &cur_mode, hdl->mp) == ESUCC) {
                err = EEXIST;
                goto finish;
        }

        err = ext4_dir_mk(path, hdl->mp);
        if (err) {
                goto finish;
        }

        err = ext4_mode_set(path, mode & MODE_PERM_MASK, hdl->mp);
        if (err) {
                ext4_dir_rm(path, hdl->mp); /* new directory is empty */
                goto finish;
        }

        time_t t;
        if (get_time(&t)) {
                ext4_ctime_set(path, t, hdl->mp);
                ext4_mtime_set(path, NULL, t, hdl->mp);
                ext4_atime_set(path, t, hdl->mp);
        }

finish:
        fs_unlock(hdl);
        return err;
}

//==============================================================================
/**
 * @brief Create pipe.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created pipe
 * @param[in ]           mode                   pipe mode
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_MKFIFO(ext4fs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Create node for driver file.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created node
 * @param[in ]           dev                    driver number
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_MKNOD(ext4fs, void *fs_handle, const char *path, const dev_t dev)
{
        UNUSED_ARG3(fs_handle, path, dev);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Open directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of opened directory
 * @param[in ]          *dir                    directory object
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_OPENDIR(ext4fs, void *fs_handle, const char *path, kdir_t *dir)
{
        ext4fs_t *hdl = fs_handle;
        ext4fs_dir_t *d = NULL;

        int err = sys_zalloc(sizeof(ext4fs_dir_t), cast(void **, &d));
        if (err) {
                return err;
        }

        size_t len = strlen(path);

        err = sys_malloc(len + 1 + NAME_MAX_LEN + 1, cast(void **, &d->path));
        if (!err) {
                memcpy(d->path, path, len);
                if ((len == 0) || (d->path[len - 1] != '/')) {
                        d->path[len++] = '/';
                }
                d->path[len] = '\0';
                d->path_len = len;

                err = ext4_dir_open(&d->dir, path, hdl->mp);
                if (!err) {
                        err = file_opened(hdl);
                        if (err) {
                                ext4_dir_close(&d->dir);
                        }
                }
        }

        if (!err) {
                memset(&dir->dirent, 0, sizeof(dir->dirent));
                dir->d_hdl = d;
                dir->d_items = 0;
                dir->d_seek = 0;
        } else {
                if (d->path) {
                        sys_free(cast(void **, &d->path));
                }
                sys_free(cast(void **, &d));
        }

        return err;
}

//==============================================================================
/**
 * @brief Close directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *dir                    directory object
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_CLOSEDIR(ext4fs, void *fs_handle, kdir_t *dir)
{
        ext4fs_t *hdl = fs_handle;
        ext4fs_dir_t *d = dir->d_hdl;

        /* resources are always released, so error is not reported */
        ext4_dir_close(&d->dir);
        sys_free(cast(void **, &d->path));
        sys_free(&dir->d_hdl);
        file_closed(hdl);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Read directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in,out]       *dir                    directory object
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_READDIR(ext4fs, void *fs_handle, kdir_t *dir)
{
        ext4fs_t *hdl = fs_handle;
        ext4fs_dir_t *d = dir->d_hdl;

        int err = fs_lock(hdl);
        if (err) {
                return err;
        }

        /* rewinddir() or seekdir(): find requested position */
        if (dir->d_seek != d->seek) {
                ext4_dir_entry_rewind(&d->dir);
                d->seek = 0;

                while (d->seek < dir->d_seek) {
                        if (ext4_dir_entry_next(&d->dir) == NULL) {
                                break;
                        }
                        d->seek = (size_t)d->dir.next_off;
                }
        }

        const ext4_direntry *de = ext4_dir_entry_next(&d->dir);
        if (de == NULL) {
                err = ENOENT;
                goto finish;
        }

        d->seek = (size_t)d->dir.next_off;
        dir->d_seek = d->seek; /* advance even if entry is unreadable */

        size_t len = de->name_length;
        memcpy(d->name, de->name, len);
        d->name[len] = '\0';

        ext4_file f;
        memset(&f, 0, sizeof(f));
        f.inode = de->inode;
        f.mp = hdl->mp;

        u32_t mode = 0;
        err = ext4_mode_get(NULL, &f, &mode, hdl->mp);
        if (err) {
                goto finish;
        }

        u64_t size = 0;
        if ((mode & EXT4_INODE_MODE_TYPE_MASK) == EXT4_INODE_MODE_FILE) {
                memcpy(d->path + d->path_len, d->name, len + 1);

                ext4_file ff;
                memset(&ff, 0, sizeof(ff));
                if (ext4_fopen2(&ff, d->path, O_RDONLY, hdl->mp) == ESUCC) {
                        size = ext4_fsize(&ff);
                        ext4_fclose(&ff);
                }
        }

        dir->dirent.dev = 0;
        dir->dirent.mode = (mode & ~EXT4_INODE_MODE_TYPE_MASK) | ext4ftype2vfs(mode);
        dir->dirent.d_name = d->name;
        dir->dirent.size = size;

finish:
        fs_unlock(hdl);
        return err;
}

//==============================================================================
/**
 * @brief Remove file/directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of removed file/directory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_REMOVE(ext4fs, void *fs_handle, const char *path)
{
        ext4fs_t *hdl = fs_handle;

        int err = fs_lock(hdl);
        if (err) {
                return err;
        }

        u32_t mode;
        err = ext4_mode_get(path, NULL, &mode, hdl->mp);
        if (!err) {
                if (is_dir(mode)) {
                        /* ext4_dir_rm() is recursive, allow only empty dirs */
                        err = dir_is_empty(hdl, path);
                        if (!err) {
                                err = ext4_dir_rm(path, hdl->mp);
                        }
                } else {
                        err = ext4_fremove(path, hdl->mp);
                }
        }

        fs_unlock(hdl);

        return err;
}

//==============================================================================
/**
 * @brief Rename file/directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *old_name               old object name
 * @param[in ]          *new_name               new object name
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_RENAME(ext4fs, void *fs_handle, const char *old_name, const char *new_name)
{
        ext4fs_t *hdl = fs_handle;

        if (strcmp(old_name, new_name) == 0) {
                return ESUCC;
        }

        /* directory can not be moved into itself */
        size_t olen = strlen(old_name);
        if ((strncmp(new_name, old_name, olen) == 0) && (new_name[olen] == '/')) {
                return EINVAL;
        }

        int err = fs_lock(hdl);
        if (err) {
                return err;
        }

        u32_t old_mode;
        err = ext4_mode_get(old_name, NULL, &old_mode, hdl->mp);
        if (err) {
                goto finish;
        }

        /* POSIX: existing target is replaced */
        u32_t new_mode;
        if (ext4_mode_get(new_name, NULL, &new_mode, hdl->mp) == ESUCC) {
                bool old_dir = is_dir(old_mode);
                bool new_dir = is_dir(new_mode);

                if (old_dir && !new_dir) {
                        err = ENOTDIR;
                } else if (!old_dir && new_dir) {
                        err = EISDIR;
                } else if (new_dir) {
                        err = dir_is_empty(hdl, new_name);
                        if (!err) {
                                err = ext4_dir_rm(new_name, hdl->mp);
                        }
                } else {
                        err = ext4_fremove(new_name, hdl->mp);
                }
        }

        if (!err) {
                err = ext4_frename(old_name, new_name, hdl->mp);
        }

finish:
        fs_unlock(hdl);
        return err;
}

//==============================================================================
/**
 * @brief Change file's mode.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[in ]           mode                   new file mode
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_CHMOD(ext4fs, void *fs_handle, const char *path, mode_t mode)
{
        ext4fs_t *hdl = fs_handle;

        /* VFS file type bits (S_IFMT) have different values than EXT4 ones */
        return ext4_mode_set(path, mode & MODE_PERM_MASK, hdl->mp);
}

//==============================================================================
/**
 * @brief Change file's owner and group.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[in ]           owner                  new file owner
 * @param[in ]           group                  new file group
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_CHOWN(ext4fs, void *fs_handle, const char *path, uid_t owner, gid_t group)
{
        ext4fs_t *hdl = fs_handle;
        return ext4_owner_set(path, owner, group, hdl->mp);
}

//==============================================================================
/**
 * @brief Synchronize all buffers to a medium.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_SYNC(ext4fs, void *fs_handle)
{
        ext4fs_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->fs_mutex, SYNC_TIMEOUT);
        if (!err) {
                err = ext4_cache_write_back(false, hdl->mp);
                if (!err) {
                        err = ext4_cache_flush(hdl->mp);
                        ext4_cache_write_back(__EXT4FS_CFG_WR_BUF_STRATEGY__, hdl->mp);
                }

                sys_mutex_unlock(hdl->fs_mutex);
        } else {
                printk("EXT4FS: sync timeout, buffers not flushed");
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function lock file system.
 *
 * @param  hdl          file system handle.
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int fs_lock(ext4fs_t *hdl)
{
        return sys_mutex_lock(hdl->fs_mutex, LOCK_TIMEOUT);
}

//==============================================================================
/**
 * @brief  Function unlock file system.
 *
 * @param  hdl          file system handle.
 */
//==============================================================================
static void fs_unlock(ext4fs_t *hdl)
{
        sys_mutex_unlock(hdl->fs_mutex);
}

//==============================================================================
/**
 * @brief  Function increase open files counter.
 *
 * @param  hdl          file system handle.
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int file_opened(ext4fs_t *hdl)
{
        int err = fs_lock(hdl);
        if (!err) {
                hdl->open_files++;
                fs_unlock(hdl);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function decrease open files counter.
 *
 * @param  hdl          file system handle.
 */
//==============================================================================
static void file_closed(ext4fs_t *hdl)
{
        if (fs_lock(hdl) == ESUCC) {
                if (hdl->open_files > 0) {
                        hdl->open_files--;
                }
                fs_unlock(hdl);
        }
}

//==============================================================================
/**
 * @brief  Function get current time.
 *
 * @param  t            time destination.
 *
 * @return true if time is valid.
 */
//==============================================================================
static bool get_time(time_t *t)
{
        *t = 0;
        return sys_gettime(t) == ESUCC;
}

//==============================================================================
/**
 * @brief  Function check if EXT4 mode describes directory.
 *
 * @param  mode         EXT4 inode mode.
 *
 * @return true if directory.
 */
//==============================================================================
static bool is_dir(u32_t mode)
{
        return (mode & EXT4_INODE_MODE_TYPE_MASK) == EXT4_INODE_MODE_DIRECTORY;
}

//==============================================================================
/**
 * @brief  Function check if directory is empty (contains only . and ..).
 *
 * @param  hdl          file system handle.
 * @param  path         directory path.
 *
 * @return ESUCC if empty, ENOTEMPTY if not empty, other errno on error.
 */
//==============================================================================
static int dir_is_empty(ext4fs_t *hdl, const char *path)
{
        ext4_dir *d = NULL;

        int err = sys_zalloc(sizeof(ext4_dir), cast(void **, &d));
        if (err) {
                return err;
        }

        err = ext4_dir_open(d, path, hdl->mp);
        if (!err) {
                const ext4_direntry *de;

                while ((de = ext4_dir_entry_next(d)) != NULL) {
                        bool dot = (de->name_length == 1) && (de->name[0] == '.');
                        bool dotdot = (de->name_length == 2) && (de->name[0] == '.') && (de->name[1] == '.');
                        if (!dot && !dotdot) {
                                err = ENOTEMPTY;
                                break;
                        }
                }

                ext4_dir_close(d);
        }

        sys_free(cast(void **, &d));

        return err;
}

//==============================================================================
/**
 * @brief  Function fill stat structure. Object selected by path or file.
 *
 * @param  hdl          file system handle.
 * @param  path         object path (NULL if file is used).
 * @param  file         opened file (NULL if path is used).
 * @param  stat         destination.
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int fill_stat(ext4fs_t *hdl, const char *path, ext4_file *file, struct stat *stat)
{
        u32_t ctime = 0, mtime = 0, uid = 0, gid = 0, mode = 0;

        int err = ext4_ctime_get(path, file, &ctime, hdl->mp);
        if (!err) {
                err = ext4_mtime_get(path, file, &mtime, hdl->mp);
        }

        if (!err) {
                err = ext4_owner_get(path, file, &uid, &gid, hdl->mp);
        }

        if (!err) {
                err = ext4_mode_get(path, file, &mode, hdl->mp);
        }

        if (!err) {
                memset(stat, 0, sizeof(*stat));
                stat->st_ctime = ctime;
                stat->st_mtime = mtime;
                stat->st_dev = 0;
                stat->st_uid = uid;
                stat->st_gid = gid;
                stat->st_mode = (mode & ~EXT4_INODE_MODE_TYPE_MASK) | ext4ftype2vfs(mode);
                stat->st_size = file ? ext4_fsize(file) : 0;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function fill file by zeros from one position to another.
 *
 * @param  file         file.
 * @param  from         start position (current file size).
 * @param  to           end position.
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int zero_fill(ext4_file *file, u64_t from, u64_t to)
{
        static const u8_t ZEROS[SECTOR_SIZE];

        int err = ext4_fseek(file, (i64_t)from, SEEK_SET);

        while (!err && (from < to)) {
                size_t n = ((to - from) > sizeof(ZEROS)) ? sizeof(ZEROS) : (size_t)(to - from);
                size_t wr = 0;

                err = ext4_fwrite(file, ZEROS, n, &wr);
                if (!err && (wr == 0)) {
                        err = ENOSPC;
                }

                from += wr;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function update mtime of modified file.
 *
 * @param  hdl          file system handle.
 * @param  fh           file handle.
 */
//==============================================================================
static void apply_mtime(ext4fs_t *hdl, ext4fs_file_t *fh)
{
        if (fh->modified) {
                time_t t;
                if (get_time(&t)) {
                        ext4_mtime_set(NULL, &fh->file, t, hdl->mp);
                }

                fh->modified = false;
        }
}

//==============================================================================
/**
 * @brief  Function copy file size to all handles of the same inode.
 *
 * lwext4 keeps file size in each ext4_file object separately (set at open),
 * so size changed by one handle is not visible by other ones.
 * Function must be called with file system locked.
 *
 * @param  hdl          file system handle.
 * @param  fh           file handle that changed file size.
 */
//==============================================================================
static void share_fsize(ext4fs_t *hdl, ext4fs_file_t *fh)
{
        for (ext4fs_file_t *f = hdl->files; f; f = f->next) {
                if ((f != fh) && (f->file.inode == fh->file.inode)) {
                        f->file.fsize = fh->file.fsize;

                        if (f->file.fpos > f->file.fsize) {
                                f->file.fpos = f->file.fsize;
                        }
                }
        }
}

//==============================================================================
/**
 * @brief  Function open block device. Not used, block opened at FS init.
 *
 * @param  bdev         block device.
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int bopen(struct ext4_blockdev *bdev)
{
        UNUSED_ARG1(bdev);
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function close block device. Not used, block closed at FS release.
 *
 * @param  bdev         block device.
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int bclose(struct ext4_blockdev *bdev)
{
        UNUSED_ARG1(bdev);
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function read data from selected block (by cache).
 *
 * @param  bdev         block device.
 * @param  buf          buffer to read.
 * @param  blk_id       block ID (start block).
 * @param  blk_cnt      number of blocks to read.
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id, uint32_t blk_cnt)
{
        ext4fs_t *hdl = bdev->bdif->p_user;
        size_t size = (size_t)bdev->bdif->ph_bsize * blk_cnt;
        size_t rdcnt = 0;

        int err = sys_fseek(hdl->dev, (i64_t)(blk_id * bdev->bdif->ph_bsize), SEEK_SET);
        if (!err) {
                err = sys_fread(buf, size, &rdcnt, hdl->dev);
                if (!err && (rdcnt != size)) {
                        err = EIO;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function write data to selected block (by cache).
 *
 * @param  bdev         block device.
 * @param  buf          buffer to write.
 * @param  blk_id       block ID (start block).
 * @param  blk_cnt      number of blocks to read.
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int bwrite(struct ext4_blockdev *bdev, const void *buf, uint64_t blk_id, uint32_t blk_cnt)
{
        ext4fs_t *hdl = bdev->bdif->p_user;
        size_t size = (size_t)bdev->bdif->ph_bsize * blk_cnt;
        size_t wrcnt = 0;

        int err = sys_fseek(hdl->dev, (i64_t)(blk_id * bdev->bdif->ph_bsize), SEEK_SET);
        if (!err) {
                err = sys_fwrite(buf, size, &wrcnt, hdl->dev);
                if (!err && (wrcnt != size)) {
                        err = EIO;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function lock access to disc.
 *
 * @param  bdev         block device.
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int bdev_lock(struct ext4_blockdev *bdev)
{
        ext4fs_t *hdl = bdev->bdif->p_user;
        return sys_mutex_lock(hdl->fs_mutex, LOCK_TIMEOUT);
}

//==============================================================================
/**
 * @brief  Function unlock access to disc.
 *
 * @param  bdev         block device.
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int bdev_unlock(struct ext4_blockdev *bdev)
{
        ext4fs_t *hdl = bdev->bdif->p_user;
        return sys_mutex_unlock(hdl->fs_mutex);
}

//==============================================================================
/**
 * @brief  Function lock access to mount point.
 *
 * @param  p_user       user object pointer
 */
//==============================================================================
static void mp_lock(void *p_user)
{
        ext4fs_t *hdl = p_user;
        sys_mutex_lock(hdl->fs_mutex, LOCK_TIMEOUT);
}

//==============================================================================
/**
 * @brief  Function unlock access to mount point.
 *
 * @param  p_user       user object pointer
 */
//==============================================================================
static void mp_unlock(void *p_user)
{
        ext4fs_t *hdl = p_user;
        sys_mutex_unlock(hdl->fs_mutex);
}

//==============================================================================
/**
 * @brief  Function convert EXT4 file type to VFS.
 *
 * @param  mode         file type coded in mode.
 *
 * @return VFS file type.
 */
//==============================================================================
static mode_t ext4ftype2vfs(u32_t mode)
{
        switch (mode & EXT4_INODE_MODE_TYPE_MASK) {
        case EXT4_INODE_MODE_FIFO:
                return S_IFIFO;
        case EXT4_INODE_MODE_CHARDEV:
                return S_IFDEV;
        case EXT4_INODE_MODE_DIRECTORY:
                return S_IFDIR;
        case EXT4_INODE_MODE_BLOCKDEV:
                return S_IFDEV;
        case EXT4_INODE_MODE_FILE:
                return S_IFREG;
        case EXT4_INODE_MODE_SOFTLINK:
                return S_IFLNK;
        case EXT4_INODE_MODE_SOCKET:
                return S_IFIFO;
        default:
                return 0;
        }
}

//==============================================================================
/**
 * @brief  Function allocate memory for EXT4 library.
 *
 * @param  size         size to allocate.
 *
 * @return Pointer to allocated memory or NULL if error.
 */
//==============================================================================
void *ext4_user_malloc(size_t size)
{
        void *mem = NULL;
        return sys_malloc(size, &mem) == ESUCC ? mem : NULL;
}

//==============================================================================
/**
 * @brief  Function allocate memory for EXT4 library.
 *
 * @param  num          number of elements to allocate.
 * @param  size         element size.
 *
 * @return Pointer to allocated memory or NULL if error.
 */
//==============================================================================
void *ext4_user_calloc(size_t num, size_t size)
{
        if ((size != 0) && (num > ((size_t)-1) / size)) {
                return NULL;
        }

        void *mem = NULL;
        return sys_zalloc(num * size, &mem) == ESUCC ? mem : NULL;
}

//==============================================================================
/**
 * @brief  Function free allocated memory.
 *
 * @param  mem          memory to free.
 */
//==============================================================================
void ext4_user_free(void *mem)
{
        sys_free(&mem);
}

/*==============================================================================
  End of file
==============================================================================*/
