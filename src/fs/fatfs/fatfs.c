/*==============================================================================
@file    fatfs.c

@author  Daniel Zorychta

@brief   FAT File system support.

@note    Copyright (C) 2020 Daniel Zorychta <daniel.zorychta@gmail.com>

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
#include "ffs/ff.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define MUTEX_TIMEOUT           5000
#define ZERO_FILL_CHUNK         128

#define PERM_R                  (S_IRUSR | S_IRGRP | S_IROTH)
#define PERM_W                  (S_IWUSR | S_IWGRP | S_IWOTH)
#define PERM_X                  (S_IXUSR | S_IXGRP | S_IXOTH)

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
/*
 * One FatFs file object is shared by all handles opened to the same file.
 * FatFs (file lock) does not allow to open the same file twice for writing,
 * and separate objects would not see each other's size and cached data.
 * Each handle has own position kept by VFS, the object is positioned
 * before each operation under the FS mutex.
 */
struct fatobj {
        FIL   fil;              /* must be first member */
        DWORD id_clu;           /* object identity: containing directory cluster */
        DWORD id_ofs;           /* object identity: entry offset in directory */
        int   refs;             /* number of handles */
        bool  writable;         /* FIL opened with FA_WRITE */
};

struct fatdir {
        FDIR    dir;
        FILINFO fno;            /* kept on heap: large with LFN/UTF-8 buffers */
        u32_t   seek;           /* d_seek value matching current FatFs position */
};

struct fatfs {
        kfile_t *fsfile;
        FATFS fatfs;
        llist_t *file_list;     /* list of struct fatobj */
        kmtx_t *mutex;
        int opened_dirs;
        bool read_only;
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int faterr_2_errno(FRESULT fresult);
static time_t time_fat2unix(WORD fdate, WORD ftime);
static mode_t fattrib_2_mode(BYTE fattrib);
static bool is_root_path(const char *path);
static int fat_stat(struct fatfs *hdl, const char *path, FILINFO **fno);
static struct fatobj *find_obj(struct fatfs *hdl, DWORD clu, DWORD ofs);
static FRESULT obj_open(struct fatfs *hdl, struct fatobj *obj, const char *path, BYTE method, bool need_wr);
static int is_inside(struct fatfs *hdl, const char *path, DWORD clu, DWORD ofs, bool *inside);
static int fill_zeros(FIL *fil, FSIZE_t pos);
static int cmp_ptr(const void *a, const void *b);
static void dummy_free(void *obj);

/*==============================================================================
  Local object definitions
==============================================================================*/

/*==============================================================================
  Exported object definitions
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize file system
 *
 * @param[out]          **fs_handle             file system allocated memory
 * @param[in ]           *src_path              file source path
 * @param[in ]           *opts                  file system options (can be NULL)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_INIT(fatfs, void **fs_handle, const char *src_path, const char *opts)
{
        int err = sys_zalloc(sizeof(struct fatfs), fs_handle);
        if (!err) {
                struct fatfs *hdl = *fs_handle;

                hdl->read_only = sys_stropt_is_flag(opts, "ro");

                err = sys_llist_create(cmp_ptr, dummy_free, &hdl->file_list);

                if (!err) {
                        err = sys_mutex_create(KMTX_TYPE_NORMAL, &hdl->mutex);
                }

                if (!err) {
                        err = sys_fopen(src_path, hdl->read_only ? O_RDONLY : O_RDWR, 0, &hdl->fsfile);
                }

                if (!err) {
                        err = faterr_2_errno(f_mount(&hdl->fatfs, hdl->fsfile, 1));
                        if (err) {
                                /*
                                 * f_mount() creates the volume sync object
                                 * before mounting; release it on failure.
                                 */
                                f_unmount(&hdl->fatfs);
                        }
                }

                if (!err) {
                        if (hdl->read_only) {
                                printk("FATFS: %s mounted read only", src_path);
                        }
                }

                if (err) {
                        if (hdl->mutex) {
                                sys_mutex_destroy(hdl->mutex);
                        }

                        if (hdl->file_list) {
                                sys_llist_destroy(hdl->file_list);
                        }

                        if (hdl->fsfile) {
                                sys_fclose(hdl->fsfile);
                        }

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
API_FS_RELEASE(fatfs, void *fs_handle)
{
        struct fatfs *hdl = fs_handle;

        kmtx_t *mtx = hdl->mutex;

        int err = sys_mutex_lock(mtx, MUTEX_TIMEOUT);
        if (!err) {
                if ((hdl->opened_dirs == 0) && (sys_llist_size(hdl->file_list) == 0)) {
                        err = faterr_2_errno(f_unmount(&hdl->fatfs));
                        if (!err or hdl->read_only) {

                                hdl->mutex = NULL;
                                sys_mutex_unlock(mtx);
                                sys_mutex_lock(mtx, _WAIT_FOREVER_MS);
                                sys_mutex_unlock(mtx);

                                sys_fclose(hdl->fsfile);
                                sys_mutex_destroy(mtx);
                                sys_llist_destroy(hdl->file_list);
                                sys_free(&fs_handle);

                                return ESUCC;
                        }
                } else {
                        err = EBUSY;
                }

                sys_mutex_unlock(mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Open file
 *
 * @note  O_APPEND positioning is handled by VFS (each write is done at the end
 *        of file, size taken from fs_fstat()).
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *fhdl                   file handler (user defined)
 * @param[out]          *fpos                   file position
 * @param[in]           *path                   file path
 * @param[in]            flags                  file open flags
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_OPEN(fatfs, void *fs_handle, void **fhdl, fpos_t *fpos, const char *path, u32_t flags)
{
        struct fatfs *hdl = fs_handle;

        const u32_t acc = flags & O_ACCMODE;
        const bool  rd  = (acc == O_RDONLY) || (acc == O_RDWR);
        const bool  wr  = (acc == O_WRONLY) || (acc == O_RDWR);

        bool excl = false;
#ifdef O_EXCL
        excl = (flags & O_EXCL) != 0;
#endif

        if (!rd && !wr) {
                return EINVAL;
        }

        if (hdl->read_only && (wr || (flags & O_CREAT))) {
                return EROFS;
        }

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (err) {
                return err;
        }

        struct fatobj *obj = NULL;
        bool truncate = wr && (flags & O_TRUNC);

        DWORD clu = 0, ofs = 0;
        BYTE  attr = 0;
        FRESULT fr = f_getid(&hdl->fatfs, path, &clu, &ofs, &attr);

        if (fr == FR_OK) {
                /* object exists */
                if (attr & AM_DIR) {
                        err = EISDIR;

                } else if ((flags & O_CREAT) && excl) {
                        err = EEXIST;

                } else {
                        obj = find_obj(hdl, clu, ofs);
                        if (obj) {
                                /* next handle to an already open file */
                                if (wr && !obj->writable) {
                                        f_close(&obj->fil);
                                        fr = obj_open(hdl, obj, path, FA_OPEN_EXISTING, true);
                                        if (fr != FR_OK) {
                                                err = faterr_2_errno(fr);
                                                /* restore read access of other handles */
                                                obj_open(hdl, obj, path, FA_OPEN_EXISTING, false);
                                        }
                                }

                                if (!err) {
                                        obj->refs++;
                                }
                        }
                }

        } else if ((fr == FR_NO_FILE) && (flags & O_CREAT)) {
                /* object will be created */

        } else {
                err = faterr_2_errno(fr);
        }

        if (!err && !obj) {
                /* first handle to the file */
                err = sys_zalloc(sizeof(struct fatobj), cast(void *, &obj));
                if (!err) {
                        BYTE method = FA_OPEN_EXISTING;

                        if (flags & O_CREAT) {
                                if (excl) {
                                        method = FA_CREATE_NEW;
                                } else if (truncate) {
                                        method = FA_CREATE_ALWAYS;
                                        truncate = false;
                                } else {
                                        method = FA_OPEN_ALWAYS;
                                }
                        }

                        err = faterr_2_errno(obj_open(hdl, obj, path, method, wr));

                        if (!err && (fr == FR_NO_FILE)) {
                                /* created now: read identity of the new object */
                                err = faterr_2_errno(f_getid(&hdl->fatfs, path, &clu, &ofs, NULL));
                                if (err) {
                                        f_close(&obj->fil);
                                }
                        }

                        if (!err) {
                                obj->id_clu = clu;
                                obj->id_ofs = ofs;
                                obj->refs   = 1;

                                if (sys_llist_push_back(hdl->file_list, obj) != obj) {
                                        f_close(&obj->fil);
                                        err = ENOMEM;
                                }
                        }

                        if (err) {
                                sys_free(cast(void *, &obj));
                        }
                }
        }

        if (!err && truncate) {
                err = faterr_2_errno(f_lseek(&obj->fil, 0));
                if (!err) {
                        err = faterr_2_errno(f_truncate(&obj->fil));
                }

                if (err && (--obj->refs == 0)) {
                        /* VFS does not close a file that failed to open */
                        f_close(&obj->fil);

                        sys_llist_foreach(struct fatobj *, o, hdl->file_list) {
                                if (o == obj) {
                                        sys_llist_erase_by_iterator(&_iterator);
                                }
                        }

                        sys_free(cast(void *, &obj));
                }
        }

        if (!err) {
                *fhdl = obj;
                *fpos = (flags & O_APPEND) ? cast(fpos_t, f_size(&obj->fil)) : 0;
        }

        sys_mutex_unlock(hdl->mutex);

        return err;
}

//==============================================================================
/**
 * @brief Close file
 *
 * Note: the file should always be closed (resources should be released) even
 *       function return error.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CLOSE(fatfs, void *fs_handle, void *fhdl)
{
        struct fatfs *hdl = fs_handle;

        struct fatobj *obj = fhdl;

        int lock_err = sys_mutex_lock(hdl->mutex, _MAX_DELAY_MS);

        int err = ESUCC;

        if (--obj->refs > 0) {
                /* other handles still use the object: just flush data */
                if (obj->writable) {
                        err = faterr_2_errno(f_sync(&obj->fil));
                }

        } else {
                err = faterr_2_errno(f_close(&obj->fil));
                if (err) {
                        printk("FATFS: file close error (%d)", err);
                }

                if (!lock_err) {
                        sys_llist_foreach(struct fatobj *, o, hdl->file_list) {
                                if (o == obj) {
                                        sys_llist_erase_by_iterator(&_iterator);
                                }
                        }
                }

                sys_free(cast(void *, &obj));
        }

        if (!lock_err) {
                sys_mutex_unlock(hdl->mutex);
        }

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
API_FS_WRITE(fatfs, void *fs_handle, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        struct fatfs *hdl = fs_handle;
        struct fatobj *obj = fhdl;
        FIL *fat_file = &obj->fil;

        if (hdl->read_only) {
                return EROFS;
        }

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (err) {
                return err;
        }

        const FSIZE_t pos = cast(FSIZE_t, *fpos);

        if (pos > f_size(fat_file)) {
                /* POSIX: gap after end of file must read as zeros */
                err = fill_zeros(fat_file, pos);

        } else if (f_tell(fat_file) != pos) {
                err = faterr_2_errno(f_lseek(fat_file, pos));
        }

        if (!err) {
                UINT n = 0;
                err = faterr_2_errno(f_write(fat_file, src, count, &n));
                if (!err) {
                        *wrcnt = n;

                        if ((n == 0) && (count > 0)) {
                                err = ENOSPC;
                        }
                }
        }

        sys_mutex_unlock(hdl->mutex);

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
API_FS_READ(fatfs, void *fs_handle, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        struct fatfs *hdl = fs_handle;
        struct fatobj *obj = fhdl;
        FIL *fat_file = &obj->fil;

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (err) {
                return err;
        }

        const FSIZE_t pos = cast(FSIZE_t, *fpos);

        if (pos >= f_size(fat_file)) {
                /*
                 * End of file. The object can be opened for writing (shared
                 * by other handles) and f_lseek() beyond the end of file in
                 * write mode would expand the file.
                 */
                *rdcnt = 0;

        } else {
                if (f_tell(fat_file) != pos) {
                        err = faterr_2_errno(f_lseek(fat_file, pos));
                }

                if (!err) {
                        UINT n = 0;
                        err = faterr_2_errno(f_read(fat_file, dst, count, &n));
                        if (!err) {
                                *rdcnt = n;
                        }
                }
        }

        sys_mutex_unlock(hdl->mutex);

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
API_FS_IOCTL(fatfs, void *fs_handle, void *fhdl, int request, void *arg)
{
        UNUSED_ARG4(fs_handle, fhdl, request, arg);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Flush file data
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                  file handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_FLUSH(fatfs, void *fs_handle, void *fhdl)
{
        struct fatfs *hdl = fs_handle;
        struct fatobj *obj = fhdl;

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (!err) {
                err = faterr_2_errno(f_sync(&obj->fil));
                sys_mutex_unlock(hdl->mutex);
        }

        return err;
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
API_FS_MKDIR(fatfs, void *fs_handle, const char *path, mode_t mode)
{
        struct fatfs *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        int err = faterr_2_errno(f_mkdir(&hdl->fatfs, path));
        if (!err && !(mode & S_IWUSR)) {
                err = faterr_2_errno(f_chmod(&hdl->fatfs, path, AM_RDO, AM_RDO));
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
API_FS_MKFIFO(fatfs, void *fs_handle, const char *path, mode_t mode)
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
API_FS_MKNOD(fatfs, void *fs_handle, const char *path, const dev_t dev)
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
API_FS_OPENDIR(fatfs, void *fs_handle, const char *path, kdir_t *dir)
{
        struct fatfs *hdl = fs_handle;

        size_t pathlen = strlen(path);
        char *dospath = NULL;

        int err = sys_zalloc(pathlen + 1, cast(void *, &dospath));
        if (err) {
                return err;
        }

        strcpy(dospath, path);
        if ((pathlen > 1) && (dospath[pathlen - 1] == '/')) {
                dospath[pathlen - 1] = '\0';
        }

        err = sys_zalloc(sizeof(struct fatdir), &dir->d_hdl);
        if (!err) {
                struct fatdir *fatdir = dir->d_hdl;

                dir->d_seek  = 0;
                dir->d_items = 0;

                err = faterr_2_errno(f_opendir(&hdl->fatfs, &fatdir->dir, dospath));
                if (!err) {
                        err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
                        if (!err) {
                                hdl->opened_dirs++;
                                sys_mutex_unlock(hdl->mutex);
                        } else {
                                f_closedir(&fatdir->dir);
                        }
                }

                if (err) {
                        sys_free(&dir->d_hdl);
                }
        }

        sys_free(cast(void *, &dospath));

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
API_FS_CLOSEDIR(fatfs, void *fs_handle, kdir_t *dir)
{
        struct fatfs *hdl = fs_handle;

        int err = ESUCC;

        if (dir->d_hdl) {
                struct fatdir *fatdir = dir->d_hdl;

                err = faterr_2_errno(f_closedir(&fatdir->dir));

                sys_free(&dir->d_hdl);

                if (sys_mutex_lock(hdl->mutex, _MAX_DELAY_MS) == ESUCC) {
                        hdl->opened_dirs--;
                        sys_mutex_unlock(hdl->mutex);
                }
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
API_FS_READDIR(fatfs, void *fs_handle, kdir_t *dir)
{
        UNUSED_ARG1(fs_handle);

        struct fatdir *fatdir = dir->d_hdl;
        FILINFO *fno = &fatdir->fno;
        int err = ESUCC;

        if (dir->d_seek != fatdir->seek) {
                err = faterr_2_errno(f_readdir(&fatdir->dir, NULL));    /* rewind */
                fatdir->seek = 0;

                while (!err && (fatdir->seek < dir->d_seek)) {
                        err = faterr_2_errno(f_readdir(&fatdir->dir, fno));
                        if (!err) {
                                if (fno->fname[0] == '\0') {
                                        break;
                                }

                                fatdir->seek = fatdir->dir.dptr;
                        }
                }
        }

        if (!err) {
                err = faterr_2_errno(f_readdir(&fatdir->dir, fno));
        }

        if (!err) {
                if (fno->fname[0] != '\0') {
                        fatdir->seek = fatdir->dir.dptr;

                        dir->dirent.dev    = 0;
                        dir->dirent.d_name = fno->fname;
                        dir->dirent.size   = fno->fsize;
                        dir->dirent.mode   = fattrib_2_mode(fno->fattrib);
                        dir->d_seek        = fatdir->seek;
                } else {
                        err = ENOENT;
                }
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
API_FS_REMOVE(fatfs, void *fs_handle, const char *path)
{
        struct fatfs *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        FRESULT fr = f_unlink(&hdl->fatfs, path);
        int err = faterr_2_errno(fr);

        if (fr == FR_DENIED) {
                /* FR_DENIED: not empty directory or read-only object */
                FILINFO *fno = NULL;
                if (fat_stat(hdl, path, &fno) == ESUCC) {
                        if ((fno->fattrib & AM_DIR) && !(fno->fattrib & AM_RDO)) {
                                err = ENOTEMPTY;
                        }
                        sys_free(cast(void *, &fno));
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Rename file/directory
 *
 * @note  Existing destination is replaced (POSIX). Replacement is not atomic:
 *        destination is removed first, then source is renamed.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *old_name               old object name
 * @param[in ]          *new_name               new object name
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_RENAME(fatfs, void *fs_handle, const char *old_name, const char *new_name)
{
        struct fatfs *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        /*
         * FatFs does not check if directory is moved into own subdirectory.
         * Such operation detaches the directory tree from the volume.
         */
        DWORD clu, ofs;
        BYTE  attr;
        int err = faterr_2_errno(f_getid(&hdl->fatfs, old_name, &clu, &ofs, &attr));
        if (err) {
                return err;
        }

        if (attr & AM_DIR) {
                bool inside = false;
                err = is_inside(hdl, new_name, clu, ofs, &inside);
                if (err) {
                        return err;
                } else if (inside) {
                        return EINVAL;
                }
        }

        FRESULT fr = f_rename(&hdl->fatfs, old_name, new_name);
        if (fr != FR_EXIST) {
                return faterr_2_errno(fr);
        }

        FILINFO *src = NULL;
        FILINFO *dst = NULL;

        err = fat_stat(hdl, old_name, &src);
        if (!err) {
                err = fat_stat(hdl, new_name, &dst);
        }

        if (!err) {
                const bool src_dir = (src->fattrib & AM_DIR) != 0;
                const bool dst_dir = (dst->fattrib & AM_DIR) != 0;

                if (src_dir && !dst_dir) {
                        err = ENOTDIR;

                } else if (!src_dir && dst_dir) {
                        err = EISDIR;

                } else {
                        fr = f_unlink(&hdl->fatfs, new_name);
                        if ((fr == FR_DENIED) && dst_dir && !(dst->fattrib & AM_RDO)) {
                                err = ENOTEMPTY;
                        } else {
                                err = faterr_2_errno(fr);
                        }

                        if (!err) {
                                err = faterr_2_errno(f_rename(&hdl->fatfs, old_name, new_name));
                        }
                }
        }

        if (src) {
                sys_free(cast(void *, &src));
        }

        if (dst) {
                sys_free(cast(void *, &dst));
        }

        return err;
}

//==============================================================================
/**
 * @brief Change file's mode
 *
 * @note  FAT stores only the read-only attribute: write permission is
 *        controlled by S_IWUSR, other permission bits are fixed.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[in ]           mode                   new file mode
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CHMOD(fatfs, void *fs_handle, const char *path, mode_t mode)
{
        struct fatfs *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        BYTE dosmode = (mode & S_IWUSR) ? 0 : AM_RDO;
        int err = faterr_2_errno(f_chmod(&hdl->fatfs, path, dosmode, AM_RDO));

        if (!err && (sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT) == ESUCC)) {
                /* update attribute cached by open file (fstat()) */
                DWORD clu, ofs;
                if (f_getid(&hdl->fatfs, path, &clu, &ofs, NULL) == FR_OK) {
                        struct fatobj *obj = find_obj(hdl, clu, ofs);
                        if (obj) {
                                obj->fil.obj.attr = (obj->fil.obj.attr & ~AM_RDO) | dosmode;
                        }
                }

                sys_mutex_unlock(hdl->mutex);
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
API_FS_CHOWN(fatfs, void *fs_handle, const char *path, uid_t owner, gid_t group)
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
API_FS_FSTAT(fatfs, void *fs_handle, void *fhdl, struct stat *stat)
{
        struct fatfs *hdl = fs_handle;
        struct fatobj *obj = fhdl;

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (!err) {
                memset(stat, 0, sizeof(*stat));

                stat->st_mode  = fattrib_2_mode(obj->fil.obj.attr);
                stat->st_mtime = time_fat2unix(obj->fil.obj.fdate, obj->fil.obj.ftime);
                stat->st_ctime = stat->st_mtime;
                stat->st_size  = f_size(&obj->fil);

                sys_mutex_unlock(hdl->mutex);
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
API_FS_STAT(fatfs, void *fs_handle, const char *path, struct stat *stat)
{
        struct fatfs *hdl = fs_handle;

        memset(stat, 0, sizeof(*stat));

        /* FatFs f_stat() does not support root directory */
        if (is_root_path(path)) {
                stat->st_mode = fattrib_2_mode(AM_DIR | (hdl->read_only ? AM_RDO : 0));
                return ESUCC;
        }

        FILINFO *fno = NULL;
        int err = fat_stat(hdl, path, &fno);
        if (!err) {
                stat->st_mode  = fattrib_2_mode(fno->fattrib);
                stat->st_mtime = time_fat2unix(fno->fdate, fno->ftime);
                stat->st_ctime = stat->st_mtime;
                stat->st_size  = fno->fsize;

                sys_free(cast(void *, &fno));
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
API_FS_STATFS(fatfs, void *fs_handle, struct statfs *statfs)
{
        struct fatfs *hdl = fs_handle;

        statfs->f_type   = SYS_FS_TYPE__SOLID;
        statfs->f_fsname = "FAT? (fatfs)";

        DWORD free_clusters = 0;
        int err = faterr_2_errno(f_getfree(&hdl->fatfs, &free_clusters));
        if (!err) {
                const DWORD csize = hdl->fatfs.csize;

                statfs->f_bsize  = FF_MAX_SS;
                statfs->f_blocks = (hdl->fatfs.n_fatent - 2) * csize;
                statfs->f_bfree  = free_clusters * csize;
                statfs->f_ffree  = 0;
                statfs->f_files  = 0;

                switch (hdl->fatfs.fs_type) {
                case FS_FAT12: statfs->f_fsname = "FAT12 (fatfs)"; break;
                case FS_FAT16: statfs->f_fsname = "FAT16 (fatfs)"; break;
                case FS_FAT32: statfs->f_fsname = "FAT32 (fatfs)"; break;
                case FS_EXFAT: statfs->f_fsname = "exFAT (fatfs)"; break;
                default: break;
                }
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
API_FS_SYNC(fatfs, void *fs_handle)
{
        struct fatfs *hdl = fs_handle;

        if (hdl->read_only) {
                return ESUCC;
        }

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (!err) {
                sys_llist_foreach(struct fatobj *, o, hdl->file_list) {
                        int serr = faterr_2_errno(f_sync(&o->fil));
                        if (!err) {
                                err = serr;
                        }
                }

                sys_mutex_unlock(hdl->mutex);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function handle libfat errors and translate to errno
 *
 * @param fresult       libfat result
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int faterr_2_errno(FRESULT fresult)
{
        static const int fat2errno[] = {
                [FR_OK]                  = ESUCC,
                [FR_DISK_ERR]            = EIO,
                [FR_INT_ERR]             = EIO,
                [FR_NOT_READY]           = EIO,
                [FR_NO_FILE]             = ENOENT,
                [FR_NO_PATH]             = ENOENT,
                [FR_INVALID_NAME]        = EINVAL,
                [FR_DENIED]              = EACCES,
                [FR_EXIST]               = EEXIST,
                [FR_INVALID_OBJECT]      = EBADF,
                [FR_WRITE_PROTECTED]     = EROFS,
                [FR_INVALID_DRIVE]       = EMEDIUMTYPE,
                [FR_NOT_ENABLED]         = ENODEV,
                [FR_NO_FILESYSTEM]       = EMEDIUMTYPE,
                [FR_MKFS_ABORTED]        = ECANCELED,
                [FR_TIMEOUT]             = ETIME,
                [FR_LOCKED]              = EBUSY,
                [FR_NOT_ENOUGH_CORE]     = ENOMEM,
                [FR_TOO_MANY_OPEN_FILES] = EMFILE,
                [FR_INVALID_PARAMETER]   = EINVAL,
        };

        return (cast(size_t, fresult) >= ARRAY_SIZE(fat2errno) ? ENOTSUP : fat2errno[fresult]);
}

//==============================================================================
/**
 * @brief  Function convert FAT date/time to UNIX timestamp.
 *
 * @note   FAT timestamps are interpreted as UTC, consistently with
 *         get_fattime() (ffsystem.c). Conversion does not depend on
 *         time zone settings.
 *
 * @param  fdate        FAT date
 * @param  ftime        FAT time
 *
 * @return Unix timestamp (0 if date is not set or invalid).
 */
//==============================================================================
static time_t time_fat2unix(WORD fdate, WORD ftime)
{
        const int year  = 1980 + ((fdate >> 9) & 0x7F);
        const int month = (fdate >> 5) & 0x0F;
        const int mday  = fdate & 0x1F;
        const int hour  = (ftime >> 11) & 0x1F;
        const int min   = (ftime >> 5) & 0x3F;
        const int sec   = (ftime & 0x1F) * 2;

        if ((month < 1) || (month > 12) || (mday < 1)) {
                return 0;
        }

        /* days from 1970-01-01 (H. Hinnant's days_from_civil, year >= 1980) */
        const int  y   = year - (month <= 2);
        const int  era = y / 400;
        const int  yoe = y - era * 400;
        const int  doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + mday - 1;
        const int  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        const long days = (long)era * 146097L + doe - 719468L;

        return cast(time_t, days) * 86400 + hour * 3600 + min * 60 + sec;
}

//==============================================================================
/**
 * @brief  Function convert FAT attributes to file mode.
 *
 * @note   FAT has no permission bits. Files are reported as rw-rw-rw-,
 *         directories as rwxrwxrwx; read-only attribute removes write bits.
 *
 * @param  fattrib      FAT attributes
 *
 * @return File mode.
 */
//==============================================================================
static mode_t fattrib_2_mode(BYTE fattrib)
{
        mode_t mode = PERM_R | ((fattrib & AM_RDO) ? 0 : PERM_W);

        if (fattrib & AM_DIR) {
                mode |= S_IFDIR | PERM_X;
        } else {
                mode |= S_IFREG;
        }

        return mode;
}

//==============================================================================
/**
 * @brief  Function check if path points to root directory.
 *
 * @param  path         path
 *
 * @return true if root directory.
 */
//==============================================================================
static bool is_root_path(const char *path)
{
        return (path[0] == '\0') || ((path[0] == '/') && (path[1] == '\0'));
}

//==============================================================================
/**
 * @brief  Function read object information. FILINFO is allocated on heap
 *         (large with LFN and UTF-8). Caller frees *fno on success.
 *
 * @param  hdl          FS handle
 * @param  path         object path
 * @param  fno          allocated object information
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int fat_stat(struct fatfs *hdl, const char *path, FILINFO **fno)
{
        int err = sys_malloc(sizeof(FILINFO), cast(void *, fno));
        if (!err) {
                err = faterr_2_errno(f_stat(&hdl->fatfs, path, *fno));
                if (err) {
                        sys_free(cast(void *, fno));
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function find open file object by identity. Call with FS mutex
 *         locked.
 *
 * @param  hdl          FS handle
 * @param  clu          containing directory cluster
 * @param  ofs          entry offset in directory
 *
 * @return Object or NULL if file is not open.
 */
//==============================================================================
static struct fatobj *find_obj(struct fatfs *hdl, DWORD clu, DWORD ofs)
{
        sys_llist_foreach(struct fatobj *, o, hdl->file_list) {
                if ((o->id_clu == clu) && (o->id_ofs == ofs)) {
                        return o;
                }
        }

        return NULL;
}

//==============================================================================
/**
 * @brief  Function open FatFs file object. The object is opened for read and
 *         write when possible, so next handles can share it. Read-only file
 *         or file system is opened for read only.
 *
 * @param  hdl          FS handle
 * @param  obj          file object
 * @param  path         file path
 * @param  method       FA_OPEN_EXISTING, FA_OPEN_ALWAYS, FA_CREATE_NEW or
 *                      FA_CREATE_ALWAYS
 * @param  need_wr      write access is required
 *
 * @return FatFs result.
 */
//==============================================================================
static FRESULT obj_open(struct fatfs *hdl, struct fatobj *obj, const char *path, BYTE method, bool need_wr)
{
        FRESULT fr = FR_WRITE_PROTECTED;

        if (!hdl->read_only) {
                fr = f_open(&hdl->fatfs, &obj->fil, path, FA_READ | FA_WRITE | method);
                obj->writable = (fr == FR_OK);
        }

        if (!need_wr && ((fr == FR_DENIED) || (fr == FR_WRITE_PROTECTED))
           && ((method == FA_OPEN_EXISTING) || (method == FA_OPEN_ALWAYS))) {

                /* read-only file: open for read */
                FRESULT rfr = f_open(&hdl->fatfs, &obj->fil, path, FA_READ | FA_OPEN_EXISTING);
                if ((rfr == FR_OK) || (fr == FR_WRITE_PROTECTED)) {
                        fr = rfr;
                }

                obj->writable = false;
        }

        return fr;
}

//==============================================================================
/**
 * @brief  Function check if any parent directory of path is selected object.
 *
 * @param  hdl          FS handle
 * @param  path         path to check
 * @param  clu          object identity: containing directory cluster
 * @param  ofs          object identity: entry offset
 * @param  inside       true if path is inside the object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int is_inside(struct fatfs *hdl, const char *path, DWORD clu, DWORD ofs, bool *inside)
{
        size_t len = strlen(path);
        char *p = NULL;

        *inside = false;

        int err = sys_malloc(len + 1, cast(void *, &p));
        if (err) {
                return err;
        }

        for (size_t i = 1; (i < len) && !*inside; i++) {
                if ((path[i] == '/') && (path[i - 1] != '/')) {
                        memcpy(p, path, i);
                        p[i] = '\0';

                        DWORD pclu, pofs;
                        FRESULT fr = f_getid(&hdl->fatfs, p, &pclu, &pofs, NULL);
                        if (fr != FR_OK) {
                                break;  /* parent does not exist, rename will fail */
                        }

                        *inside = (pclu == clu) && (pofs == ofs);
                }
        }

        sys_free(cast(void *, &p));

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function expand file to selected position with zeros. FatFs leaves
 *         area expanded by f_lseek() undefined.
 *
 * @param  fil          file
 * @param  pos          new end of file
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int fill_zeros(FIL *fil, FSIZE_t pos)
{
        static const BYTE zeros[ZERO_FILL_CHUNK];

        int err = faterr_2_errno(f_lseek(fil, f_size(fil)));

        while (!err && (f_tell(fil) < pos)) {
                FSIZE_t rem = pos - f_tell(fil);
                UINT len = (rem > sizeof(zeros)) ? sizeof(zeros) : cast(UINT, rem);
                UINT n = 0;

                err = faterr_2_errno(f_write(fil, zeros, len, &n));
                if (!err && (n < len)) {
                        err = ENOSPC;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function compare pointers.
 *
 * @param  a    pointer a
 * @param  b    pointer b
 *
 * @return Return 0 if equal.
 */
//==============================================================================
static int cmp_ptr(const void *a, const void *b)
{
        uintptr_t x = cast(uintptr_t, a);
        uintptr_t y = cast(uintptr_t, b);

        return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

//==============================================================================
/**
 * @brief  Dummy linked list free function.
 *
 * @param  obj          object to free
 */
//==============================================================================
static void dummy_free(void *obj)
{
        UNUSED_ARG1(obj);
}

/*==============================================================================
  End of file
==============================================================================*/
