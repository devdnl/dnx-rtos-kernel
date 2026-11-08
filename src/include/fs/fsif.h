/*==============================================================================
@file    fsif.h

@author  Daniel Zorychta

@brief   File system interface (registration of file systems built into the
         kernel and file systems of the drvset partition).

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
 * Public header: used by the kernel (VFS, file system registration) and by
 * file systems of the drvset partition (drivers/drvset.h). The interface
 * layout depends on kernel configuration (__OS_ENABLE_*__), fs_magic is the
 * last member and is checked when the drvset partition is started.
 */

#ifndef _FSIF_H_
#define _FSIF_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/sysapi.h"

/*==============================================================================
  Exported macros
==============================================================================*/
/* file system identifier */
#define _VFS_FILE_SYSTEM_MAGIC_NO 0xD9EFD24F

/*==============================================================================
  Exported object types
==============================================================================*/
/** file system interface */
typedef struct vfs_FS_itf {
        int (*fs_init)(void **fshdl, const char *path, const char *opts);
        int (*fs_release)(void *fshdl);
        int (*fs_open)(void *fshdl, void **fhdl, fpos_t *fpos, const char *path, u32_t flags);
        int (*fs_close)(void *fshdl, void *fhdl);
        int (*fs_write)(void *fshdl, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
                struct vfs_fattr attr);
        int (*fs_read)(void *fshdl, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
                struct vfs_fattr attr);
        int (*fs_ioctl)(void *fshdl, void *fhdl, int iroq, void *arg);
        int (*fs_fstat)(void *fshdl, void *fhdl, struct stat *stat);
        int (*fs_flush)(void *fshdl, void *fhdl);
        int (*fs_mknod)(void *fshdl, const char *path, const dev_t dev);
        int (*fs_sync)(void *fshdl);
        int (*fs_opendir)(void *fshdl, const char *path, struct vfs_dir *dir);
        int (*fs_closedir)(void *fshdl, struct vfs_dir *dir);
        int (*fs_readdir)(void *fshdl, struct vfs_dir *dir);
#if __OS_ENABLE_FSTAT__ == _YES_
        int (*fs_stat)(void *fshdl, const char *path, struct stat *stat);
#endif
#if __OS_ENABLE_STATFS__ == _YES_
        int (*fs_statfs)(void *fshdl, struct statfs *statfs);
#endif
#if __OS_ENABLE_MKDIR__ == _YES_
        int (*fs_mkdir)(void *fshdl, const char *path, mode_t);
#endif
#if __OS_ENABLE_MKFIFO__ == _YES_
        int (*fs_mkfifo)(void *fshdl, const char *path, mode_t);
#endif
#if __OS_ENABLE_REMOVE__ == _YES_
        int (*fs_remove)(void *fshdl, const char *path);
#endif
#if __OS_ENABLE_RENAME__ == _YES_
        int (*fs_rename)(void *fshdl, const char *old_name, const char *new_name);
#endif
#if __OS_ENABLE_CHMOD__ == _YES_
        int (*fs_chmod)(void *fshdl, const char *path, mode_t mode);
#endif
#if __OS_ENABLE_CHOWN__ == _YES_
        int (*fs_chown)(void *fshdl, const char *path, uid_t owner, gid_t group);
#endif
        uint32_t fs_magic;
} vfs_FS_itf_t;

/** file system entry (file system registration table) */
struct _FS_entry {
        const char *FS_name;
        const struct vfs_FS_itf FS_if;
};

#ifdef __cplusplus
}
#endif

#endif /* _FSIF_H_ */
/*==============================================================================
  End of file
==============================================================================*/
