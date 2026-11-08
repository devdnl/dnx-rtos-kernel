/*==============================================================================
@file    vfs.h

@author  Daniel Zorychta

@brief   This file support virtual file system

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

#ifndef _VFS_H_
#define _VFS_H_

#ifdef __DNX_MODULE__
#error "Kernel internal header. Drivers include drivers/driver.h, file systems include fs/fs.h."
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include "kernel/ktypes.h"
#include "lib/sys/types.h"
#include "drivers/ioctl_macros.h"
#include "kernel/sysapi.h"
#include "fs/fsif.h"

/*==============================================================================
  Exported macros
==============================================================================*/
/* _VFS_FILE_SYSTEM_MAGIC_NO: fs/fsif.h */

/*==============================================================================
  Exported object types
==============================================================================*/
/* file system interface (vfs_FS_itf_t): fs/fsif.h */

/** file flags */
typedef struct vfs_file_flags {
        bool rd :1;      //! file read mode
        bool wr :1;      //! file write mode
        bool append :1;  //! file append mode (one of rd/wr is set)
        bool eof :1;     //! end of file
        bool error :1;   //! error occurred
        bool seekmod :1; //! file position modified
        struct vfs_fattr fattr;
} vfs_file_flags_t;

/** file type */
typedef struct vfs_file {
        res_header_t header;
        void *FS_hdl;
        const vfs_FS_itf_t *FS_if;
        void *f_hdl;
        fpos_t f_lseek;
        vfs_file_flags_t f_flag;
        int references;
        void *FS_entry; //!< VFS mount entry that holds a reference (NULL for direct device files)
} kfile_t;

/** Path with CWD */
struct vfs_path {
        const char *CWD;
        const char *PATH;
};

/** Path slash correction */
enum path_correction {
        ADD_SLASH,
        SUB_SLASH,
        NO_SLASH_ACTION,
};

/*==============================================================================
  Exported API functions
==============================================================================*/
extern int _vfs_init(void);
extern int _vfs_realpath(char *path, enum path_correction corr);
extern int _vfs_mount(const struct vfs_path *, const struct vfs_path *, const struct vfs_FS_itf *, const char *);
extern int _vfs_umount(const struct vfs_path *);
extern int _vfs_getmntentry(int, struct mntent *);
extern int _vfs_mknod(const struct vfs_path *, dev_t);
extern int _vfs_mkdir(const struct vfs_path *, mode_t);
extern int _vfs_mkfifo(const struct vfs_path *, mode_t);
extern int _vfs_opendir(const struct vfs_path *, kdir_t **);
extern int _vfs_closedir(kdir_t *);
extern int _vfs_readdir(kdir_t *, dirent_t **);
extern int _vfs_seekdir(kdir_t *, u32_t);
extern int _vfs_telldir(kdir_t *, u32_t *);
extern int _vfs_remove(const struct vfs_path *);
extern int _vfs_rename(const struct vfs_path *, const struct vfs_path *);
extern int _vfs_chmod(const struct vfs_path *, mode_t);
extern int _vfs_chown(const struct vfs_path *, uid_t, gid_t);
extern int _vfs_stat(const struct vfs_path *, struct stat *);
extern int _vfs_statfs(const struct vfs_path *, struct statfs *);
extern int _vfs_fopen(const struct vfs_path *, int, mode_t, kfile_t **);
extern int _vfs_fopen_ref(kfile_t *);
extern int _vfs_fclose(kfile_t *);
extern int _vfs_fwrite(const void *, size_t, size_t *, kfile_t *);
extern int _vfs_fread(void *, size_t, size_t *, kfile_t *);
extern int _vfs_fseek(kfile_t *, i64_t, int);
extern int _vfs_ftell(kfile_t *, i64_t *);
extern int _vfs_vfioctl(kfile_t *, int, va_list);
extern int _vfs_fstat(kfile_t *, struct stat *);
extern int _vfs_fflush(kfile_t *);
extern int _vfs_feof(kfile_t *, int *);
extern int _vfs_clearerr(kfile_t *);
extern int _vfs_ferror(kfile_t *, int *);
extern void _vfs_sync(void);

/*==============================================================================
  Exported inline functions
==============================================================================*/
//==============================================================================
/**
 * @brief vfs_filter_open_flags_for_device
 * Function filter open flags for device.
 *
 * @param flags	flags to filter
 *
 * @errors None
 *
 * @return Filtered flags
 */
//==============================================================================
static inline u32_t vfs_filter_flags_for_device(u32_t flags)
{
        return (flags & (O_RDONLY | O_WRONLY | O_RDWR));
}

#ifdef __cplusplus
}
#endif

#endif /* _VFS_H_ */
/*==============================================================================
  End of file
==============================================================================*/
