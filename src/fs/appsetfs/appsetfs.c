/*==============================================================================
File    appsetfs.c

Author  Daniel Zorychta

Brief   Application set file system (programs of appset as executable files)

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
  The file system presents programs of an application set (appset) as files.
  The appset image is executed in place (XIP), so the source is a memory mapped
  region:
        mount("appsetfs", "/dev/appset", "/opt/progs", "")   memblock device
                                                              (IOCTL_MEMBLOCK__GET_CONFIGURATION:
                                                              table address and region size)
        mount("appsetfs", "", "/bin", "")                    main appset of the kernel
                                                              (shell: mount - appsetfs /bin)

  Programs are regular executable read-only files of the root directory (size
  0, read returns end of file). The kernel starts program of the file by path
  (IOCTL_VFS__GET_PROGRAM) or found in __OS_PROGRAM_PATH__. Appset of memblock
  device is attached to the kernel at mount (data and bss initialized), it can
  be mounted once; unmount is refused while program of the appset is used by
  a process (EBUSY).
==============================================================================*/

/*==============================================================================
  Include files
==============================================================================*/
#include "fs/fs.h"
#include "kernel/process.h" /* appsetfs is a kernel part: appset tables of kernel */

/*==============================================================================
  Local macros
==============================================================================*/
#define FILE_MODE (S_IFREG | S_IRUSR | S_IRGRP | S_IROTH | S_IXUSR | S_IXGRP | S_IXOTH)
#define DIR_MODE  (S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IXUSR | S_IXGRP | S_IXOTH)

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        const _program_table_desc_t *table; //!< program table
        kfile_t *src;                       //!< source device (NULL: main appset)
        bool attached;                      //!< table attached to the kernel
} appsetfs_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int appsetfs_destroy(void **fs_handle);
static int open_source(appsetfs_t *hdl, const char *src_path);
static int find_program(appsetfs_t *hdl, const char *path, const _program_entry_t **entry);
static bool is_root(const char *path);
static bool is_main_appset_source(const char *src_path);

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
 * @brief Initialize file system.
 *
 * @param[out]          **fs_handle             file system allocated memory
 * @param[in ]           *src_path              memblock device or empty (main appset)
 * @param[in ]           *opts                  file system options (not used)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_INIT(appsetfs, void **fs_handle, const char *src_path, const char *opts)
{
        UNUSED_ARG1(opts);

        int err = sys_zalloc(sizeof(appsetfs_t), fs_handle);
        if (err) {
                return err;
        }

        appsetfs_t *hdl = *fs_handle;

        if (is_main_appset_source(src_path)) {
                hdl->table = _process_get_appset_table()->table;
        } else {
                err = open_source(hdl, src_path);

                if (not err) {
                        err = _process_appset_attach(hdl->table);
                        hdl->attached = (err == ESUCC);
                }
        }

        if (err) {
                appsetfs_destroy(fs_handle);
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
API_FS_RELEASE(appsetfs, void *fs_handle)
{
        appsetfs_t *hdl = fs_handle;

        if (hdl->attached) {
                int err = _process_appset_detach(hdl->table);
                if (err) {
                        return err;
                }

                hdl->attached = false;
        }

        return appsetfs_destroy(&fs_handle);
}

//==============================================================================
/**
 * @brief Open file.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *fhdl                   file extra data (program entry)
 * @param[out]          *fpos                   file position
 * @param[in]           *path                   file path
 * @param[in]            flags                  file open flags
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_OPEN(appsetfs, void *fs_handle, void **fhdl, fpos_t *fpos, const char *path, u32_t flags)
{
        if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) {
                return EROFS;
        }

        if (is_root(path)) {
                return EISDIR;
        }

        const _program_entry_t *entry;
        int err = find_program(fs_handle, path, &entry);
        if (not err) {
                *fhdl = cast(void *, entry);
                *fpos = 0;
        }

        return err;
}

//==============================================================================
/**
 * @brief Close file.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_CLOSE(appsetfs, void *fs_handle, void *fhdl)
{
        UNUSED_ARG2(fs_handle, fhdl);
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
API_FS_WRITE(appsetfs, void *fs_handle, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG7(fs_handle, fhdl, src, count, fpos, wrcnt, fattr);
        return EROFS;
}

//==============================================================================
/**
 * @brief Read data from file (program file is empty).
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
API_FS_READ(appsetfs, void *fs_handle, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG6(fs_handle, fhdl, dst, count, fpos, fattr);
        *rdcnt = 0;
        return ESUCC;
}

//==============================================================================
/**
 * @brief IO control.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_IOCTL(appsetfs, void *fs_handle, void *fhdl, int request, void *arg)
{
        UNUSED_ARG1(fs_handle);

        if ((request == IOCTL_VFS__GET_PROGRAM) and arg) {
                *cast(const void **, arg) = fhdl;
                return ESUCC;
        }

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
API_FS_FLUSH(appsetfs, void *fs_handle, void *fhdl)
{
        UNUSED_ARG2(fs_handle, fhdl);
        return ESUCC;
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
API_FS_FSTAT(appsetfs, void *fs_handle, void *fhdl, struct stat *stat)
{
        UNUSED_ARG2(fs_handle, fhdl);

        memset(stat, 0, sizeof(*stat));
        stat->st_mode = FILE_MODE;

        return ESUCC;
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
API_FS_STAT(appsetfs, void *fs_handle, const char *path, struct stat *stat)
{
        int err = ESUCC;

        memset(stat, 0, sizeof(*stat));

        if (is_root(path)) {
                stat->st_mode = DIR_MODE;
        } else {
                const _program_entry_t *entry;
                err = find_program(fs_handle, path, &entry);
                stat->st_mode = FILE_MODE;
        }

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
API_FS_STATFS(appsetfs, void *fs_handle, struct statfs *statfs)
{
        appsetfs_t *hdl = fs_handle;

        statfs->f_bsize = 1;
        statfs->f_blocks = 0;
        statfs->f_bfree = 0;
        statfs->f_ffree = 0;
        statfs->f_files = hdl->table->number_of_programs;
        statfs->f_type = SYS_FS_TYPE__SOLID;
        statfs->f_fsname = "appsetfs";

        return ESUCC;
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
API_FS_MKDIR(appsetfs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);
        return EROFS;
}

//==============================================================================
/**
 * @brief Create pipe.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of pipe
 * @param[in ]           mode                   pipe mode
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_MKFIFO(appsetfs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);
        return EROFS;
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
API_FS_MKNOD(appsetfs, void *fs_handle, const char *path, const dev_t dev)
{
        UNUSED_ARG3(fs_handle, path, dev);
        return EROFS;
}

//==============================================================================
/**
 * @brief Open directory (only the root directory exists).
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of opened directory
 * @param[in ]          *kdir                   directory object
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_OPENDIR(appsetfs, void *fs_handle, const char *path, kdir_t *kdir)
{
        appsetfs_t *hdl = fs_handle;

        if (not is_root(path)) {
                const _program_entry_t *entry;
                return (find_program(hdl, path, &entry) == ESUCC) ? ENOTDIR : ENOENT;
        }

        kdir->d_hdl = NULL;
        kdir->d_items = hdl->table->number_of_programs;
        kdir->d_seek = 0;

        return ESUCC;
}

//==============================================================================
/**
 * @brief Close directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *kdir                   directory object
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_CLOSEDIR(appsetfs, void *fs_handle, kdir_t *kdir)
{
        UNUSED_ARG2(fs_handle, kdir);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Read directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in,out]       *kdir                   directory object
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_READDIR(appsetfs, void *fs_handle, kdir_t *kdir)
{
        appsetfs_t *hdl = fs_handle;

        if (kdir->d_seek >= hdl->table->number_of_programs) {
                return ENOENT;
        }

        kdir->dirent.d_name = hdl->table->program_entry[kdir->d_seek].name;
        kdir->dirent.mode = FILE_MODE;
        kdir->dirent.size = 0;
        kdir->dirent.dev = 0;
        kdir->d_seek++;

        return ESUCC;
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
API_FS_REMOVE(appsetfs, void *fs_handle, const char *path)
{
        UNUSED_ARG2(fs_handle, path);
        return EROFS;
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
API_FS_RENAME(appsetfs, void *fs_handle, const char *old_name, const char *new_name)
{
        UNUSED_ARG3(fs_handle, old_name, new_name);
        return EROFS;
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
API_FS_CHMOD(appsetfs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);
        return EROFS;
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
API_FS_CHOWN(appsetfs, void *fs_handle, const char *path, uid_t owner, gid_t group)
{
        UNUSED_ARG4(fs_handle, path, owner, group);
        return EROFS;
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
API_FS_SYNC(appsetfs, void *fs_handle)
{
        UNUSED_ARG1(fs_handle);
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Release all file system resources. Safe for partially initialized
 *         object.
 *
 * @param  fs_handle    pointer to file system handle
 *
 * @return One of errno value.
 */
//==============================================================================
static int appsetfs_destroy(void **fs_handle)
{
        appsetfs_t *hdl = *fs_handle;

        if (hdl->src) {
                sys_fclose(hdl->src);
        }

        return sys_free(fs_handle);
}

//==============================================================================
/**
 * @brief  Open source device (memblock) and check program table of the region.
 *         The device stays open while the file system is mounted.
 *
 * @param  hdl          FS handler
 * @param  src_path     source device path
 *
 * @return One of errno value: ENOTSUP when the device is not a memory mapped
 *         region (memblock), EINVAL when the region has no program table.
 */
//==============================================================================
static int open_source(appsetfs_t *hdl, const char *src_path)
{
#if defined(IOCTL_MEMBLOCK__GET_CONFIGURATION)
        int err = sys_fopen(src_path, O_RDONLY, 0, &hdl->src);
        if (err) {
                return err;
        }

        MEMBLOCK_config_t cfg;
        if (sys_ioctl(hdl->src, IOCTL_MEMBLOCK__GET_CONFIGURATION, &cfg) != ESUCC) {
                return ENOTSUP;
        }

        const _program_table_desc_t *table = cast(const _program_table_desc_t *, cfg.memory_address);

        // table header shall be in the region before its fields are read
        if ((cfg.memory_address == 0) or (cfg.block_size < sizeof(_program_table_desc_t))) {
                return EINVAL;
        }

        if ((table->number_of_programs == 0) or
                (table->number_of_programs >
                        ((cfg.block_size - sizeof(_program_table_desc_t)) / sizeof(_program_entry_t)))) {
                return EINVAL;
        }

        hdl->table = table;

        return ESUCC;
#else
        UNUSED_ARG2(hdl, src_path);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief  Find program of the file path ("/<name>").
 *
 * @param  hdl          FS handler
 * @param  path         file path
 * @param  entry        program entry
 *
 * @return One of errno value.
 */
//==============================================================================
static int find_program(appsetfs_t *hdl, const char *path, const _program_entry_t **entry)
{
        path += strspn(path, "/");

        size_t len = strcspn(path, "/");

        // only the root directory exists
        if ((len == 0) or (path[len + strspn(&path[len], "/")] != '\0')) {
                return ENOENT;
        }

        for (size_t i = 0; i < hdl->table->number_of_programs; i++) {
                const char *name = hdl->table->program_entry[i].name;

                if ((strncmp(name, path, len) == 0) and (name[len] == '\0')) {
                        *entry = &hdl->table->program_entry[i];
                        return ESUCC;
                }
        }

        return ENOENT;
}

//==============================================================================
/**
 * @brief  Check that path is the root directory.
 *
 * @param  path         path
 *
 * @return True when path is the root directory.
 */
//==============================================================================
static bool is_root(const char *path)
{
        return path[strspn(path, "/")] == '\0';
}

//==============================================================================
/**
 * @brief  Check that source is the main appset: empty source, the VFS gives the
 *         working directory (as for file systems without source).
 *
 * @param  src_path     source path
 *
 * @return True when source is the main appset.
 */
//==============================================================================
static bool is_main_appset_source(const char *src_path)
{
        if (not src_path or (src_path[0] == '\0')) {
                return true;
        }

        struct stat st;
        return (fs_stat(src_path, &st) == ESUCC) and S_ISDIR(st.st_mode);
}

/*==============================================================================
  End of file
==============================================================================*/
