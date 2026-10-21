/*==============================================================================
@file    procfs.c

@author  Daniel Zorychta

@brief   This file support process file system (procfs)

@note    Copyright (C) 2012-2015 Daniel Zorychta <daniel.zorychta@gmail.com>

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
#include <build/vars.h>

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define CLK_FILE_PATH "/dev/clk"

#define PATH_ROOT         "/"
#define PATH_ROOT_BIN     "/bin"
#define PATH_ROOT_PID     "/pid"
#define PATH_ROOT_CPUINFO "/cpuinfo"

#define FILE_BUFFER 384
#define PID_STR_LEN 12
#define LINE_LEN 128

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
struct procfs {
        llist_t *file_list;
        kmtx_t *resource_mtx;
        char *offset_path;
};

enum path_content {
        FILE_CONTENT_ROOT,
        FILE_CONTENT_BIN,
        FILE_CONTENT_PID,
        FILE_CONTENT_CPUINFO,
        _FILE_CONTENT_COUNT
};

struct file_info {
        enum path_content content;
        i32_t arg; /* -1: directory, >= 0: file argument */
};

struct dir_info {
        const char *dir_name;
        char name[32];
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int procfs_readdir_root(struct procfs *hdl, kdir_t *dir);
static int procfs_readdir_pid(struct procfs *hdl, kdir_t *dir);
static int procfs_readdir_bin(struct procfs *hdl, kdir_t *dir);
static int add_file_to_list(struct procfs *hdl, i32_t arg, enum path_content content, void **object);
static size_t get_file_content(struct file_info *file, u8_t *buff, size_t size, i32_t seek);
static void buf_snprintf(u8_t *buf, size_t *size, size_t *clen, i32_t *seek, const char *fmt, ...);
static size_t get_file_size(struct file_info *file);
static int build_path(struct procfs *hdl, const char *path, char **opath);
static void strip_trailing_slashes(char *path);
static bool is_number(const char *str);

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
API_FS_INIT(procfs, void **fs_handle, const char *src_path, const char *opts)
{
        UNUSED_ARG1(src_path);

        int err = sys_zalloc(sizeof(struct procfs), fs_handle);
        if (err == ESUCC) {
                struct procfs *procfs = *fs_handle;

                err = sys_llist_create(sys_llist_functor_cmp_pointers, NULL, &procfs->file_list);
                if (err != ESUCC) {
                        goto finish;
                }

                err = sys_mutex_create(KMTX_TYPE_NORMAL, &procfs->resource_mtx);
                if (err != ESUCC) {
                        goto finish;
                }

                if (!isstrempty(opts)) {
                        err = sys_zalloc(strsize(opts), cast(void *, &procfs->offset_path));
                        if (err) {
                                goto finish;
                        }

                        strcpy(procfs->offset_path, opts);
                        strip_trailing_slashes(procfs->offset_path);

                        if (isstrempty(procfs->offset_path)) {
                                // offset "/" is equal to no offset
                                sys_free(cast(void *, &procfs->offset_path));

                        } else if (!(isstreq(procfs->offset_path, PATH_ROOT_BIN) ||
                                           isstreq(procfs->offset_path, PATH_ROOT_PID))) {
                                err = ENOENT;
                                goto finish;
                        }
                }

        finish:
                if (err != ESUCC) {
                        if (procfs->file_list) {
                                sys_llist_destroy(procfs->file_list);
                        }

                        if (procfs->resource_mtx) {
                                sys_mutex_destroy(procfs->resource_mtx);
                        }

                        if (procfs->offset_path) {
                                sys_free(cast(void *, &procfs->offset_path));
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
API_FS_RELEASE(procfs, void *fs_handle)
{
        struct procfs *procfs = fs_handle;

        int err = sys_mutex_lock(procfs->resource_mtx, 100);
        if (!err) {
                if (sys_llist_size(procfs->file_list) != 0) {
                        sys_mutex_unlock(procfs->resource_mtx);
                        err = EBUSY;
                } else {
                        sys_llist_destroy(procfs->file_list);
                        kmtx_t *mtx = procfs->resource_mtx;
                        if (procfs->offset_path) {
                                sys_free(cast(void *, &procfs->offset_path));
                        }
                        memset(procfs, 0, sizeof(struct procfs));
                        sys_mutex_unlock(mtx);
                        sys_mutex_destroy(mtx);
                        sys_free(&fs_handle);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Open file
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *fhdl                   file handle
 * @param[out]          *fpos                   file position
 * @param[in]           *path                   file path
 * @param[in]            flags                  file open flags
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_OPEN(procfs, void *fs_handle, void **fhdl, fpos_t *fpos, const char *path, u32_t flags)
{
        struct procfs *hdl = fs_handle;

        if (flags != O_RDONLY) {
                return EROFS;
        }

        *fpos = 0;

        char *opath = NULL;
        int err = build_path(hdl, path, &opath);
        if (err) {
                return err;
        }

        err = ENOENT;

        if (isstreq(opath, PATH_ROOT)) {
                // "/" path
                err = add_file_to_list(hdl, -1, FILE_CONTENT_ROOT, fhdl);

        } else if (isstreq(opath, PATH_ROOT_PID)) {
                // "/pid" path
                err = add_file_to_list(hdl, -1, FILE_CONTENT_PID, fhdl);

        } else if (isstreqn(opath, PATH_ROOT_PID "/", strlen(PATH_ROOT_PID) + 1)) {
                // "/pid/<pid>" path
                const char *mpath = opath + strlen(PATH_ROOT_PID) + 1;

                if (is_number(mpath)) {
                        i32_t pid = 0;
                        sys_strtoi32(mpath, 10, &pid);

                        _process_stat_t stat = {0};
                        if ((pid >= 0) && (sys_process_get_stat_pid(pid, &stat) == ESUCC)) {
                                err = add_file_to_list(hdl, pid, FILE_CONTENT_PID, fhdl);
                        }
                }

        } else if (isstreq(opath, PATH_ROOT_BIN)) {
                // "/bin" path
                err = add_file_to_list(hdl, -1, FILE_CONTENT_BIN, fhdl);

        } else if (isstreqn(opath, PATH_ROOT_BIN "/", strlen(PATH_ROOT_BIN) + 1)) {
                // "/bin/<prog>" path
                const char *mpath = opath + strlen(PATH_ROOT_BIN) + 1;

                // global program index (across all application sets)
                i32_t idx = 0;
                bool found = false;

                for (const appset_table_t *appset = sys_get_program_table(); appset && !found; appset = appset->next) {

                        for (size_t i = 0; i < appset->table->number_of_programs; i++, idx++) {
                                if (isstreq(mpath, appset->table->program_entry[i].name)) {
                                        err = add_file_to_list(hdl, idx, FILE_CONTENT_BIN, fhdl);
                                        found = true;
                                        break;
                                }
                        }
                }

        } else if (isstreq(opath, PATH_ROOT_CPUINFO)) {
                // "/cpuinfo" path
                err = add_file_to_list(hdl, 0, FILE_CONTENT_CPUINFO, fhdl);
        }

        sys_free(cast(void *, &opath));

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
 * @param[in ]          *fhdl                   file fhdl data
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CLOSE(procfs, void *fs_handle, void *fhdl)
{
        struct procfs *fsctx = fs_handle;

        int err = sys_mutex_lock(fsctx->resource_mtx, _WAIT_FOREVER_MS);
        if (!err) {
                int pos = sys_llist_find_begin(fsctx->file_list, fhdl);
                err = ((pos >= 0) && sys_llist_erase(fsctx->file_list, pos)) ? ESUCC : ENOENT;
                if (err) {
                        printk("PROCFS: no such descriptor!");
                }

                sys_mutex_unlock(fsctx->resource_mtx);
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
API_FS_WRITE(procfs, void *fs_handle, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG7(fs_handle, fhdl, src, count, wrcnt, fpos, fattr);

        return EROFS;
}

//==============================================================================
/**
 * @brief Read data from file
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file fhdl data
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ]          *fpos                   position in file
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_READ(procfs, void *fs_handle, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG2(fs_handle, fattr);

        struct file_info *file = fhdl;
        int err = ENOENT;

        if (file && file->content < _FILE_CONTENT_COUNT) {
                if (file->arg < 0) {
                        err = EISDIR;
                } else if (*fpos > INT32_MAX) {
                        *rdcnt = 0;
                        err = ESUCC;
                } else {
                        *rdcnt = get_file_content(file, dst, count, cast(i32_t, *fpos));
                        err = ESUCC;
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
API_FS_IOCTL(procfs, void *fs_handle, void *fhdl, int request, void *arg)
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
API_FS_FLUSH(procfs, void *fs_handle, void *fhdl)
{
        UNUSED_ARG2(fs_handle, fhdl);

        return ESUCC;
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
API_FS_FSTAT(procfs, void *fs_handle, void *fhdl, struct stat *stat)
{
        UNUSED_ARG1(fs_handle);

        struct file_info *file = fhdl;

        if (!file) {
                return EINVAL;
        }

        stat->st_dev = 0;
        stat->st_mode = S_IRUSR | S_IRGRP | S_IROTH;
        stat->st_mtime = COMPILE_EPOCH_TIME;
        stat->st_ctime = COMPILE_EPOCH_TIME;
        stat->st_size = 0;
        stat->st_gid = 0;
        stat->st_uid = 0;

        int err = EINVAL;

        if (file->content < _FILE_CONTENT_COUNT) {

                if (file->arg >= 0) {
                        stat->st_size = get_file_size(file);
                        stat->st_mode |= S_IFREG;

                        if ((file->content == FILE_CONTENT_PID) || (file->content == FILE_CONTENT_CPUINFO)) {

                                time_t t = 0;
                                sys_gettime(&t);

                                stat->st_mtime = t;
                                stat->st_ctime = t;
                        }

                        if (file->content == FILE_CONTENT_BIN) {
                                stat->st_mode |= S_IFPROG;
                                stat->st_mode |= S_IXUSR;
                        }
                } else {
                        stat->st_mode |= S_IFDIR;
                }

                err = ESUCC;
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
API_FS_STAT(procfs, void *fs_handle, const char *path, struct stat *stat)
{
        void *fhdl = NULL;
        fpos_t fpos = 0;

        int err = _procfs_open(fs_handle, &fhdl, &fpos, path, O_RDONLY);
        if (!err) {
                err = _procfs_fstat(fs_handle, fhdl, stat);
                _procfs_close(fs_handle, fhdl);
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
API_FS_MKDIR(procfs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);

        return EROFS;
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
API_FS_MKFIFO(procfs, void *fs_handle, const char *path, mode_t mode)
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
API_FS_MKNOD(procfs, void *fs_handle, const char *path, const dev_t dev)
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
API_FS_OPENDIR(procfs, void *fs_handle, const char *path, kdir_t *dir)
{
        struct procfs *hdl = fs_handle;

        char *opath = NULL;
        int err = build_path(hdl, path, &opath);
        if (err) {
                return err;
        }

        dir->d_seek = 0;

        err = sys_zalloc(sizeof(struct dir_info), &dir->d_hdl);
        if (!err) {
                struct dir_info *dirinfo = dir->d_hdl;

                if (isstreq(opath, PATH_ROOT)) {
                        dirinfo->dir_name = PATH_ROOT;
                        dir->d_items = 3;

                } else if (isstreq(opath, PATH_ROOT_PID)) {
                        dirinfo->dir_name = PATH_ROOT_PID;
                        dir->d_items = sys_process_get_count();

                } else if (isstreq(opath, PATH_ROOT_BIN)) {
                        dirinfo->dir_name = PATH_ROOT_BIN;
                        dir->d_items = 0;

                        for (const appset_table_t *appset = sys_get_program_table(); appset; appset = appset->next) {
                                dir->d_items += appset->table->number_of_programs;
                        }

                } else {
                        sys_free(&dir->d_hdl);
                        err = ENOENT;
                }
        }

        sys_free(cast(void *, &opath));

        return err;
}

//==============================================================================
/**
 * @brief Close directory
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *dir                    directory object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CLOSEDIR(procfs, void *fs_handle, kdir_t *dir)
{
        UNUSED_ARG1(fs_handle);

        if (dir->d_hdl) {
                return sys_free(&dir->d_hdl);
        } else {
                return ESUCC;
        }
}

//==============================================================================
/**
 * @brief Read directory
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in,out]       *dir                    directory object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_READDIR(procfs, void *fs_handle, kdir_t *dir)
{
        int err = EFAULT;

        struct dir_info *dirinfo = dir->d_hdl;
        if (dirinfo) {
                if (isstreq(dirinfo->dir_name, PATH_ROOT)) {
                        err = procfs_readdir_root(fs_handle, dir);

                } else if (isstreq(dirinfo->dir_name, PATH_ROOT_PID)) {
                        err = procfs_readdir_pid(fs_handle, dir);

                } else if (isstreq(dirinfo->dir_name, PATH_ROOT_BIN)) {
                        err = procfs_readdir_bin(fs_handle, dir);
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
API_FS_REMOVE(procfs, void *fs_handle, const char *path)
{
        UNUSED_ARG2(fs_handle, path);

        return EROFS;
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
API_FS_RENAME(procfs, void *fs_handle, const char *old_name, const char *new_name)
{
        UNUSED_ARG3(fs_handle, old_name, new_name);

        return EROFS;
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
API_FS_CHMOD(procfs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);

        return EROFS;
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
API_FS_CHOWN(procfs, void *fs_handle, const char *path, uid_t owner, gid_t group)
{
        UNUSED_ARG4(fs_handle, path, owner, group);

        return EROFS;
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
API_FS_STATFS(procfs, void *fs_handle, struct statfs *statfs)
{
        UNUSED_ARG1(fs_handle);

        statfs->f_bfree = 0;
        statfs->f_blocks = 0;
        statfs->f_ffree = 0;
        statfs->f_files = 0;
        statfs->f_type = SYS_FS_TYPE__SYS;
        statfs->f_fsname = "procfs";

        return ESUCC;
}

//==============================================================================
/**
 * @brief Synchronize all buffers to a medium
 *
 * @param[in ]          *fs_handle              file system allocated memory
 *
 * @return None
 */
//==============================================================================
API_FS_SYNC(procfs, void *fs_handle)
{
        UNUSED_ARG1(fs_handle);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Read root directory
 *
 * @param[in ]          *hdl                    file system allocated memory
 * @param[in,out]       *dir                    directory object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int procfs_readdir_root(struct procfs *hdl, kdir_t *dir)
{
        UNUSED_ARG1(hdl);

        int err = ESUCC;

        dir->dirent.dev = 0;
        dir->dirent.size = 0;

        switch (dir->d_seek) {
        case 0:
                dir->dirent.d_name = "bin";
                dir->dirent.mode = S_IRUSR | S_IRGRP | S_IROTH | S_IFDIR;
                break;

        case 1:
                dir->dirent.d_name = "pid";
                dir->dirent.mode = S_IRUSR | S_IRGRP | S_IROTH | S_IFDIR;
                break;

        case 2: {
                struct file_info file = {.content = FILE_CONTENT_CPUINFO, .arg = 0};
                dir->dirent.d_name = "cpuinfo";
                dir->dirent.mode = S_IRUSR | S_IRGRP | S_IROTH | S_IFREG;
                dir->dirent.size = get_file_size(&file);
                break;
        }

        default:
                err = ENOENT;
                break;
        }

        if (!err) {
                dir->d_seek++;
        }

        return err;
}

//==============================================================================
/**
 * @brief Read /pid directory
 *
 * @param[in ]          *hdl                    file system allocated memory
 * @param[in,out]       *dir                    directory object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int procfs_readdir_pid(struct procfs *hdl, kdir_t *dir)
{
        UNUSED_ARG1(hdl);

        _process_stat_t stat = {0};
        int err = sys_process_get_stat_seek(dir->d_seek, &stat);
        if (!err) {
                dir->d_seek++;

                struct dir_info *dirinfo = dir->d_hdl;

                sys_scnprintf(dirinfo->name, sizeof(dirinfo->name), "%u", stat.pid);

                dir->dirent.d_name = dirinfo->name;
                dir->dirent.mode = S_IRUSR | S_IRGRP | S_IROTH | S_IFREG;
                dir->dirent.dev = 0;

                struct file_info file = {.arg = stat.pid, .content = FILE_CONTENT_PID};
                dir->dirent.size = get_file_size(&file);
        }

        return err;
}

//==============================================================================
/**
 * @brief Read /bin directory
 *
 * @param[in ]          *hdl                    file system allocated memory
 * @param[in,out]       *dir                    directory object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int procfs_readdir_bin(struct procfs *hdl, kdir_t *dir)
{
        UNUSED_ARG1(hdl);

        int err = ENOENT;

        size_t seek = dir->d_seek;

        for (const appset_table_t *appset = sys_get_program_table(); appset; appset = appset->next) {

                if (seek < appset->table->number_of_programs) {
                        dir->dirent.d_name = appset->table->program_entry[seek].name;
                        dir->dirent.mode = S_IRUSR | S_IRGRP | S_IROTH | S_IXUSR | S_IFPROG;
                        dir->dirent.dev = 0;

                        // global index is required by get_file_content()
                        struct file_info file = {
                                .arg = cast(i32_t, dir->d_seek),
                                .content = FILE_CONTENT_BIN,
                        };
                        dir->dirent.size = get_file_size(&file);

                        dir->d_seek++;

                        err = ESUCC;
                        break;
                } else {
                        seek -= appset->table->number_of_programs;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Add file info to list
 *
 * @param hdl                   FS context
 * @param arg                   additional argument
 * @param content               file content to write in file info
 * @param object                file object (result)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int add_file_to_list(struct procfs *hdl, i32_t arg, enum path_content content, void **object)
{
        struct file_info *file;
        int err = sys_zalloc(sizeof(struct file_info), cast(void **, &file));
        if (!err) {
                file->content = content;
                file->arg = arg;

                err = sys_mutex_lock(hdl->resource_mtx, _WAIT_FOREVER_MS);
                if (!err) {
                        if (sys_llist_push_back(hdl->file_list, file)) {
                                *object = file;
                        } else {
                                sys_free(cast(void **, &file));
                                err = ENOMEM;
                        }

                        sys_mutex_unlock(hdl->resource_mtx);
                } else {
                        sys_free(cast(void **, &file));
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Build normalized path: offset + path, without trailing slashes.
 *        Root directory is always returned as "/". Result must be freed.
 */
//==============================================================================
static int build_path(struct procfs *hdl, const char *path, char **opath)
{
        if (!path) {
                path = "";
        }

        size_t olen = hdl->offset_path ? strlen(hdl->offset_path) : 0;

        // offset + optional '/' + path + '\0'
        int err = sys_malloc(olen + strsize(path) + 1, cast(void *, opath));
        if (!err) {
                char *p = *opath;
                p[0] = '\0';

                if (olen) {
                        strcpy(p, hdl->offset_path);
                }

                if (path[0] != '/') {
                        strcat(p, "/");
                }

                strcat(p, path);

                strip_trailing_slashes(p);

                if (p[0] == '\0') {
                        strcpy(p, PATH_ROOT);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Remove trailing slashes. Path "/" becomes empty string.
 */
//==============================================================================
static void strip_trailing_slashes(char *path)
{
        size_t len = strlen(path);
        while ((len > 0) && (path[len - 1] == '/')) {
                path[--len] = '\0';
        }
}

//==============================================================================
/**
 * @brief Check if string is non-empty decimal number.
 */
//==============================================================================
static bool is_number(const char *str)
{
        if (isstrempty(str)) {
                return false;
        }

        for (; *str; str++) {
                if ((*str < '0') || (*str > '9')) {
                        return false;
                }
        }

        return true;
}

//==============================================================================
/**
 * @brief  Return file size
 */
//==============================================================================
static size_t get_file_size(struct file_info *file)
{
        size_t size = UINT16_MAX;
        return get_file_content(file, NULL, size, 0);
}

//==============================================================================
/**
 * @brief Function return file content and size
 *
 * @param file          file information
 * @param buff          buffer (can be NULL)
 * @param size          buffer size
 * @param seek          file position
 *
 * @return number of bytes written to buffer
 */
//==============================================================================
static size_t get_file_content(struct file_info *file, u8_t *buff, size_t size, i32_t seek)
{
        size_t clen = 0;

        if (file->arg < 0) {
                return 0;
        }

        switch (file->content) {
        case FILE_CONTENT_PID: {
                // cmdline + '\n' + '\0' must fit in a single LINE_LEN line,
                // otherwise sys_vscnprintf() truncates the line and '\n' is lost
                char cmdline[LINE_LEN - 1];

                _process_stat_t stat = {0};
                stat.cmdline_buf = cmdline;
                stat.cmdline_len = sizeof(cmdline);

                if (sys_process_get_stat_pid(file->arg, &stat) == ESUCC) {

                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Command: ");
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "%s\n", stat.cmdline_buf);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "PID: %d\n", stat.pid);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Memory usage: %d bytes\n", stat.memory_usage);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Memory Block Count: %d\n",
                                        stat.memory_block_count);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Open Files: %d\n", stat.files_count);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Open Dirs: %d\n", stat.dir_count);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Open Mutexes: %d\n", stat.mutexes_count);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Open Semaphores: %d\n", stat.semaphores_count);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Open Queues: %d\n", stat.queue_count);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Open Sockets: %d\n", stat.socket_count);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Threads: %d\n", stat.threads_count);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "CPU Load: %d.%d%%\n", stat.CPU_load / 10,
                                        stat.CPU_load % 10);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Stack Size: %d\n", stat.stack_size);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Stack Usage: %d\n", stat.stack_max_usage);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Priority: %d\n", stat.priority);
                        }
                        if (size) {
                                buf_snprintf(buff, &size, &clen, &seek, "Syscalls/s: %u\n", stat.syscalls_per_sec);
                        }
                }
                break;
        }

        case FILE_CONTENT_CPUINFO: {
                buf_snprintf(buff, &size, &clen, &seek,
                        "CPU name  : %s\n"
                        "CPU vendor: %s\n"
                        "CPU cores : %u\n",
                        _CPUCTL_PLATFORM_NAME, _CPUCTL_VENDOR_NAME, _CPUCTL_CORE_NUM);

                kfile_t *pll;
                if (sys_fopen(CLK_FILE_PATH, O_RDWR, 0, &pll) == ESUCC) {

                        CLK_info_t clkinf;
                        clkinf.iterator = 0;

                        while (size > 0 && sys_ioctl(pll, IOCTL_CLK__GET_CLK_INFO, &clkinf) == ESUCC && clkinf.name) {

                                buf_snprintf(buff, &size, &clen, &seek, "%-10s: %lu Hz\n", clkinf.name, clkinf.freq_Hz);
                        }

                        sys_fclose(pll);
                } else {
                        buf_snprintf(buff, &size, &clen, &seek,
                                "Warning: no '" CLK_FILE_PATH "' file to read clocks\n");
                }
                break;
        }

#if __OS_SYSTEM_SHEBANG_ENABLE__ > 0
        case FILE_CONTENT_BIN: {
                size_t n = file->arg;

                for (const appset_table_t *appset = sys_get_program_table(); appset; appset = appset->next) {

                        if (n < appset->table->number_of_programs) {

                                buf_snprintf(buff, &size, &clen, &seek, "#!%s\n", appset->table->program_entry[n].name);
                                break;

                        } else {
                                n -= appset->table->number_of_programs;
                        }
                }
                break;
        }
#endif

        default:
                break;
        }

        return clen;
}

//==============================================================================
/**
 * @brief  Function fill buffer with text according to file position and buffer size.
 *
 * @param  buf          destination buffer (can be null)
 * @param  size         destination buffer size
 * @param  clen         content length (output)
 * @param  seek         file position
 * @param  fmt          printf() format
 * @param  ...          printf() arguments
 */
//==============================================================================
static void buf_snprintf(u8_t *buf, size_t *size, size_t *clen, i32_t *seek, const char *fmt, ...)
{
        char line[LINE_LEN];

        va_list arg;
        va_start(arg, fmt);
        i32_t len = sys_vscnprintf(line, sizeof(line), fmt, arg);
        va_end(arg);

        if (len <= 0) {
                return;
        }

        if (len >= cast(i32_t, sizeof(line))) {
                len = sizeof(line) - 1;
        }

        if (len - *seek > 0) {
                len -= *seek;
                len = min(len, (i32_t)*size);

                if (buf) {
                        memcpy(buf + *clen, line + *seek, len);
                }

                *clen += len;
                *size -= len;
                *seek = 0;
        } else {
                *seek -= min(*seek, len);
        }
}

/*==============================================================================
  End of file
==============================================================================*/
