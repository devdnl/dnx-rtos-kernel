/*==============================================================================
@file    drvset_fs.c

@author  Daniel Zorychta

@brief   File system table of drvset partition.

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
 * The file is not a part of the kernel. It is compiled by the drvset build
 * (tools/drvset of the workspace) and creates the table of file systems of the
 * partition (header field filesystems, drvset_header.c) from the generated
 * list drvset_fs.h. Interface members depend on kernel configuration as in
 * the kernel (fs/addfs.sh), fs_magic is checked by the kernel at start.
 */

/*==============================================================================
  Include files
==============================================================================*/
#include "fs/fs.h"
#include "fs/fsif.h"
#include "drvset_fs.h"

/*==============================================================================
  Local macros
==============================================================================*/
/* file system functions (the same as fs/addfs.sh of the kernel) */
#define IMPORT_FS(name, p)                                                                                             \
        extern API_FS_INIT(p, void **, const char *, const char *);                                                    \
        extern API_FS_RELEASE(p, void *);                                                                              \
        extern API_FS_OPEN(p, void *, void **, fpos_t *, const char *, u32_t);                                         \
        extern API_FS_CLOSE(p, void *, void *);                                                                        \
        extern API_FS_WRITE(p, void *, void *, const u8_t *, size_t, fpos_t *, size_t *, struct vfs_fattr);            \
        extern API_FS_READ(p, void *, void *, u8_t *, size_t, fpos_t *, size_t *, struct vfs_fattr);                   \
        extern API_FS_IOCTL(p, void *, void *, int, void *);                                                           \
        extern API_FS_FLUSH(p, void *, void *);                                                                        \
        extern API_FS_SYNC(p, void *);                                                                                 \
        extern API_FS_MKNOD(p, void *, const char *, const dev_t);                                                     \
        extern API_FS_OPENDIR(p, void *, const char *, struct vfs_dir *);                                              \
        extern API_FS_CLOSEDIR(p, void *, struct vfs_dir *);                                                           \
        extern API_FS_READDIR(p, void *, struct vfs_dir *);                                                            \
        extern API_FS_FSTAT(p, void *, void *, struct stat *);                                                         \
        extern API_FS_STAT(p, void *, const char *, struct stat *);                                                    \
        extern API_FS_MKDIR(p, void *, const char *, mode_t);                                                          \
        extern API_FS_MKFIFO(p, void *, const char *, mode_t);                                                         \
        extern API_FS_REMOVE(p, void *, const char *);                                                                 \
        extern API_FS_RENAME(p, void *, const char *, const char *);                                                   \
        extern API_FS_CHMOD(p, void *, const char *, mode_t);                                                          \
        extern API_FS_CHOWN(p, void *, const char *, uid_t, gid_t);                                                    \
        extern API_FS_STATFS(p, void *, struct statfs *);

#if __OS_ENABLE_FSTAT__ == _YES_
#define FS_IF_FSTAT(p) .fs_fstat = _##p##_fstat, .fs_stat = _##p##_stat,
#else
#define FS_IF_FSTAT(p)
#endif
#if __OS_ENABLE_STATFS__ == _YES_
#define FS_IF_STATFS(p) .fs_statfs = _##p##_statfs,
#else
#define FS_IF_STATFS(p)
#endif
#if __OS_ENABLE_CHMOD__ == _YES_
#define FS_IF_CHMOD(p) .fs_chmod = _##p##_chmod,
#else
#define FS_IF_CHMOD(p)
#endif
#if __OS_ENABLE_CHOWN__ == _YES_
#define FS_IF_CHOWN(p) .fs_chown = _##p##_chown,
#else
#define FS_IF_CHOWN(p)
#endif
#if __OS_ENABLE_MKDIR__ == _YES_
#define FS_IF_MKDIR(p) .fs_mkdir = _##p##_mkdir,
#else
#define FS_IF_MKDIR(p)
#endif
#if __OS_ENABLE_MKFIFO__ == _YES_
#define FS_IF_MKFIFO(p) .fs_mkfifo = _##p##_mkfifo,
#else
#define FS_IF_MKFIFO(p)
#endif
#if __OS_ENABLE_REMOVE__ == _YES_
#define FS_IF_REMOVE(p) .fs_remove = _##p##_remove,
#else
#define FS_IF_REMOVE(p)
#endif
#if __OS_ENABLE_RENAME__ == _YES_
#define FS_IF_RENAME(p) .fs_rename = _##p##_rename,
#else
#define FS_IF_RENAME(p)
#endif

#define FS_ENTRY(name, p)                                                                                              \
        {.FS_name = #name,                                                                                             \
         .FS_if = {.fs_init = _##p##_init,                                                                             \
                   .fs_release = _##p##_release,                                                                       \
                   .fs_open = _##p##_open,                                                                             \
                   .fs_close = _##p##_close,                                                                           \
                   .fs_read = _##p##_read,                                                                             \
                   .fs_ioctl = _##p##_ioctl,                                                                           \
                   .fs_flush = _##p##_flush,                                                                           \
                   .fs_write = _##p##_write,                                                                           \
                   .fs_sync = _##p##_sync,                                                                             \
                   .fs_mknod = _##p##_mknod,                                                                           \
                   .fs_opendir = _##p##_opendir,                                                                       \
                   .fs_closedir = _##p##_closedir,                                                                     \
                   .fs_readdir = _##p##_readdir,                                                                       \
                   FS_IF_FSTAT(p) FS_IF_STATFS(p) FS_IF_CHMOD(p) FS_IF_CHOWN(p) FS_IF_MKDIR(p) FS_IF_MKFIFO(p)       \
                   FS_IF_REMOVE(p) FS_IF_RENAME(p).fs_magic = _VFS_FILE_SYSTEM_MAGIC_NO}},

/*==============================================================================
  External objects
==============================================================================*/
DRVSET_FS_LIST(IMPORT_FS)

/*==============================================================================
  Exported objects
==============================================================================*/
const struct _FS_entry _drvset_filesystems[DRVSET_FS_COUNT] = {DRVSET_FS_LIST(FS_ENTRY)};

/*==============================================================================
  End of file
==============================================================================*/
