/*==============================================================================
@file    fsapi.c

@author  Daniel Zorychta

@brief   Kernel API used only by file systems (fs/fs.h).

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

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/sysfunc.h"
#include "fs/fs.h"
#include "fs/pipe.h"
#include "mm/mm.h"

/*==============================================================================
  Local macros
==============================================================================*/

/*==============================================================================
  Local object types
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

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
 * @brief Function open directory
 *
 * @param[in]  path                 directory path
 * @param[out] dir                  pointer to dir pointer
 *
 * @return One of errno values
 */
//==============================================================================
int fs_opendir(const char *path, kdir_t **dir)
{
        struct vfs_path cpath;
        cpath.CWD = NULL;
        cpath.PATH = path;

        int r = _vfs_opendir(&cpath, dir);
        if (r == ESUCC) {
                _process_register_resource(_kworker_proc, cast(res_header_t *, *dir));
        }
        return r;
}

//==============================================================================
/**
 * @brief Function close opened directory
 *
 * @param[in] *dir                  directory object
 *
 * @return One of errno values
 */
//==============================================================================
int fs_closedir(kdir_t *dir)
{
        return _process_release_resource(_kworker_proc, cast(res_header_t *, dir), RES_TYPE_DIR);
}

//==============================================================================
/**
 * @brief Function creates device node (device file).
 *
 * Function creates a file system node (device special file) named
 * <i>path</i>. Node is connected to the device <i>dev</i>.
 *
 * @param path          node name
 * @param dev           device number
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_mknod(const char *path, dev_t dev)
{
        struct vfs_path cpath;
        cpath.CWD = NULL;
        cpath.PATH = path;

        return _vfs_mknod(&cpath, dev);
}

//==============================================================================
/**
 * @brief Function creates new directory.
 *
 * The function attempts to create a directory named <i>pathname</i>. The
 * argument <i>mode</i> specifies the permissions to use.
 *
 * @param pathname      directory name
 * @param mode          directory permissions
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_mkdir(const char *pathname, mode_t mode)
{
#if __OS_ENABLE_MKDIR__ == _YES_
        struct vfs_path path;
        path.CWD = NULL;
        path.PATH = pathname;

        return _vfs_mkdir(&path, mode);
#else
        UNUSED_ARG2(pathname, mode);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Function creates FIFO file.
 *
 * The mfunction makes a FIFO special file with name <i>pathname</i>. <i>mode</i>
 * specifies the FIFO's permissions. A FIFO special file is similar to pipe, but
 * is created in filesystem and is not an anonymous. Access to FIFO is the same
 * as to regular file, except that data can be read only one time. Not all
 * filesystems support this file type.
 *
 * @param pathname      FIFO name
 * @param mode          FIFO permissions
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_mkfifo(const char *pathname, mode_t mode)
{
#if __OS_ENABLE_MKFIFO__ == _YES_
        struct vfs_path cpath;
        cpath.CWD = NULL;
        cpath.PATH = pathname;

        return _vfs_mkfifo(&cpath, mode);
#else
        UNUSED_ARG2(pathname, mode);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Function reads entry from opened directory stream.
 *
 * Function returns a pointer <i>dirent</i> to object <b>dirent_t</b> type
 * representing the next directory entry in the directory stream pointed to by
 * <i>dir</i>.<p>
 *
 * @param dir           directory object
 * @param dirent        directory entry
 *
 * @exception | @ref EINVAL
 * @exception | @ref ENOMEM
 * @exception | @ref EACCES
 * @exception | @ref ENOENT
 * @exception | ...
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_readdir(kdir_t *dir, dirent_t **dirent)
{
        return _vfs_readdir(dir, dirent);
}

//==============================================================================
/**
 * @brief Function remove selected file.
 *
 * The function deletes a name from the file system. If the removed name was
 * the last link to a file and no processes have the file open, the file is
 * deleted and the space it was using is made available for reuse.<p>
 *
 * If the name referred to a FIFO, or device, the name is removed, but
 * processes which have the object open may continue to use it.
 *
 * @param path      path to file
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_remove(const char *path)
{
#if __OS_ENABLE_REMOVE__ == _YES_
        struct vfs_path cpath;
        cpath.CWD = NULL;
        cpath.PATH = path;
        return _vfs_remove(&cpath);
#else
        UNUSED_ARG1(path);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Function renames selected file.
 *
 * The function renames a file. In contrast to standard C library this function
 * don't move files between directories if <i>new_name</i> is localized on other
 * filesystem than <i>old_name</i>, otherwise it's depending on filesystem.
 *
 * @param old_name      old file name
 * @param new_name      new file name
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_rename(const char *old_name, const char *new_name)
{
#if __OS_ENABLE_RENAME__ == _YES_
        struct vfs_path cpath_old;
        cpath_old.CWD = NULL;
        cpath_old.PATH = old_name;

        struct vfs_path cpath_new;
        cpath_new.CWD = NULL;
        cpath_new.PATH = new_name;

        return _vfs_rename(&cpath_old, &cpath_new);
#else
        UNUSED_ARG2(old_name, new_name);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Function changes file mode.
 *
 * The function change the permissions of a file specified by <i>path</i>.
 *
 * @param path          file to permission change
 * @param mode          new permissions
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_chmod(const char *path, mode_t mode)
{
#if __OS_ENABLE_CHMOD__ == _YES_
        struct vfs_path cpath;
        cpath.CWD = NULL;
        cpath.PATH = path;
        return _vfs_chmod(&cpath, mode);
#else
        UNUSED_ARG2(path, mode);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Function changes the ownership of file.
 *
 * The function changes the ownership of the file specified by <i>path</i>.
 *
 * @param path          path to file
 * @param owner         owner ID
 * @param group         group ID
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_chown(const char *path, uid_t owner, gid_t group)
{
#if __OS_ENABLE_CHOWN__ == _YES_
        struct vfs_path cpath;
        cpath.CWD = NULL;
        cpath.PATH = path;
        return _vfs_chown(&cpath, owner, group);
#else
        UNUSED_ARG3(path, owner, group);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Function gets file information.
 *
 * The function return information about a file specified by <i>path</i>.
 *
 * @param path          file to inspect
 * @param buf           file's information
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_stat(const char *path, struct stat *buf)
{
#if __OS_ENABLE_FSTAT__ == _YES_
        struct vfs_path cpath;
        cpath.CWD = NULL;
        cpath.PATH = path;
        return _vfs_stat(&cpath, buf);
#else
        UNUSED_ARG2(path, buf);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Function gets file system information.
 *
 * The function returns information about a mounted file system.
 * A <i>path</i> is directory of the mount point of file system.
 *
 * @param path          node name
 * @param statfs        file system information container
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_statfs(const char *path, struct statfs *statfs)
{
#if __OS_ENABLE_STATFS__ == _YES_
        struct vfs_path cpath;
        cpath.CWD = NULL;
        cpath.PATH = path;
        return _vfs_statfs(&cpath, statfs);
#else
        UNUSED_ARG2(path, statfs);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Function synchronizes files buffers with file systems.
 */
//==============================================================================
void fs_sync(void)
{
        _vfs_sync();
}

//==============================================================================
/**
 * @brief  Function return collected process statistics.
 *
 * @param  pid      PID
 * @param  stat     process statistics
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_process_get_stat_pid(pid_t pid, _process_stat_t *stat)
{
        return _process_get_stat_pid(pid, stat);
}

//==============================================================================
/**
 * @brief  Function return collected process statistics
 *
 * @param  seek     process seek (start from 0)
 * @param  stat     process statistics
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_process_get_stat_seek(size_t seek, _process_stat_t *stat)
{
        return _process_get_stat_seek(seek, stat);
}

//==============================================================================
/**
 * @brief  Function return number of processes.
 *
 * @return Number of processes.
 */
//==============================================================================
size_t fs_process_get_count(void)
{
        return _process_get_count();
}

//==============================================================================
/**
 * @brief Function open selected driver
 *
 * @param id            module id
 * @param flags         flags
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_dev_open(dev_t id, u32_t flags)
{
        return _driver_open(id, flags);
}

//==============================================================================
/**
 * @brief Function close selected driver
 *
 * @param id            module id
 * @param force         force close request
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_dev_close(dev_t id, bool force)
{
        return _driver_close(id, force);
}

//==============================================================================
/**
 * @brief Function write data to driver
 *
 * @param id            module id
 * @param src           data source
 * @param count         buffer size
 * @param fpos          file position
 * @param wrcnt         number of written bytes
 * @param fattr         file attributes
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_dev_write(dev_t id, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt, struct vfs_fattr fattr)
{
        return _driver_write(id, src, count, fpos, wrcnt, fattr);
}

//==============================================================================
/**
 * @brief Function read data to driver
 *
 * @param id            module id
 * @param dst           data destination
 * @param count         buffer size
 * @param fpos          file position
 * @param rdcnt         number of read byes
 * @param fattr         file attributes
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_dev_read(dev_t id, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        return _driver_read(id, dst, count, fpos, rdcnt, fattr);
}

//==============================================================================
/**
 * @brief IO control
 *
 * @param id            module id
 * @param request       io request
 * @param arg           argument
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_dev_ioctl(dev_t id, int request, void *arg)
{
        return _driver_ioctl(id, request, arg);
}

//==============================================================================
/**
 * @brief Flush device buffer (forces write)
 *
 * @param id            module id
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_dev_flush(dev_t id)
{
        return _driver_flush(id);
}

//==============================================================================
/**
 * @brief Device information
 *
 * @param id            module id
 * @param stat          status object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int fs_dev_stat(dev_t id, struct vfs_dev_stat *stat)
{
        return _driver_stat(id, stat);
}

//==============================================================================
/**
 * @brief Function returns device type: character or block device.
 *
 * @param id            device id
 *
 * @return @ref S_IFBLK for block devices, @ref S_IFCHR for character devices.
 */
//==============================================================================
mode_t fs_dev_type(dev_t id)
{
        return _driver_get_type(id);
}

int fs_pipe_create(pipe_t **pipe)
{
        return _pipe_create(pipe);
}

int fs_pipe_destroy(pipe_t *pipe)
{
        return _pipe_destroy(pipe);
}

int fs_pipe_get_length(pipe_t *pipe, size_t *len)
{
        return _pipe_get_length(pipe, len);
}

int fs_pipe_read(pipe_t *pipe, u8_t *buf, size_t count, size_t *rdcnt, bool non_blocking)
{
        return _pipe_read(pipe, buf, count, rdcnt, non_blocking);
}

int fs_pipe_write(pipe_t *pipe, const u8_t *buf, size_t count, size_t *wrcnt, bool non_blocking)
{
        return _pipe_write(pipe, buf, count, wrcnt, non_blocking);
}

int fs_pipe_close(pipe_t *pipe)
{
        return _pipe_close(pipe);
}

int fs_pipe_permanent(pipe_t *pipe)
{
        return _pipe_permanent(pipe);
}

int fs_pipe_clear(pipe_t *pipe)
{
        return _pipe_clear(pipe);
}

//==============================================================================
/**
 * @brief  Function allocates memory for file system.
 *
 * @param size             block size
 * @param align            block alignment (0: default)
 * @param prefreg          memory regions searched in order (NULL terminated),
 *                         NULL: regions selected for file systems by CPU configuration
 * @param zero             clear allocated memory
 * @param mem              allocated memory
 *
 * @return One of errno value.
 */
//==============================================================================
int fs_mem_alloc(size_t size, size_t align, const char **prefreg, bool zero, void **mem)
{
        if (!prefreg) {
                prefreg = _cpuctl_preferred_FS_RAM;
        }

        if (zero) {
                return _kzalloc(_MM_FS, size, align, prefreg, _MM_FLAG__DMA, _MM_FLAG__DMA, mem);
        } else {
                return _kmalloc(_MM_FS, size, align, prefreg, _MM_FLAG__DMA, _MM_FLAG__DMA, mem);
        }
}

//==============================================================================
/**
 * @brief  Function frees memory of file system.
 *
 * @param mem              memory to free (pointer is cleared)
 *
 * @return One of errno value.
 */
//==============================================================================
int fs_mem_free(void **mem)
{
        return _kfree(_MM_FS, mem);
}

//==============================================================================
/**
 * @brief  Linked list constructor of file system.
 *
 * @param  functor      compare functor (can be NULL)
 * @param  obj_dtor     object destructor (can be NULL)
 * @param  list         pointer to list object
 *
 * @return One of errno value.
 */
//==============================================================================
int fs_llist_create(llist_cmp_functor_t functor, llist_obj_dtor_t obj_dtor, llist_t **list)
{
        return _llist_create_krn(_MM_FS, functor, obj_dtor, list);
}

/*==============================================================================
  End of file
==============================================================================*/
