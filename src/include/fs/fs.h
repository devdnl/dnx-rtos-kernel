/*==============================================================================
@file    fs.h

@author  Daniel Zorychta

@brief   This function provide all required function needed to write file systems.

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

/**
 * @defgroup fs-h "fs/fs.h"
 *
 * This library is used by each file system, contains file system-specific
 * functions and @subpage sysapi-h. This is main and only one library that shall
 * be used by file system code.
 *
 * @{
 */

#ifndef _DNXFS_H_
#define _DNXFS_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/sysapi.h"
#include "lib/llist.h"
#include <errno.h>
#include "drivers/ioctl_requests.h"

/*==============================================================================
  Exported symbolic constants/macros
==============================================================================*/
/**
 * File system types.
 * @see struct statfs
 */
enum SYS_FS_TYPE {
        SYS_FS_TYPE__RAM,   //!< RAM file system
        SYS_FS_TYPE__SOLID, //!< Solid file system storage
        SYS_FS_TYPE__DEV,   //!< Special file system for device files
        SYS_FS_TYPE__SYS,   //!< Special file system for system purposes
        SYS_FS_TYPE__NET,   //!< Network file system
};

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of initialization function.
 *
 * Function created by this macro is called by system when file system has to
 * be initialized.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void **</b>]        file system memory handler
 * @param src_path      [<b>const char *</b>]   source file path
 * @param opts          [<b>const char *</b>]   options
 *
 * @return One of @ref errno value.
 */
#define API_FS_INIT(fsname, fs_handle, src_path, opts)
#else
#define API_FS_INIT(fsname, ...) int _##fsname##_init(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of release function.
 *
 * Function created by this macro is called by system when file system has to
 * be released. If it is possible, all resources shall be released, if not then
 * error code shall be returned.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @return One of @ref errno value.
 */
#define API_FS_RELEASE(fsname, fs_handle)
#else
#define API_FS_RELEASE(fsname, ...) int _##fsname##_release(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file open function.
 *
 * Function created by this macro is called by system when file system has to
 * open selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param fhdl          [<b>void **</b>]        file handle (user defined)
 * @param fpos          [<b>fpos_t *</b>]       file position indicator
 * @param path          [<b>const char *</b>]   file path
 * @param flags         [<b>u32_t</b>]          file flags (O_*)
 * @return One of @ref errno value.
 *
 * @see Flags: O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_EXCL, O_TRUNC, O_APPEND
 */
#define API_FS_OPEN(fsname, fs_handle, fhdl, fpos, path, flags)
#else
#define API_FS_OPEN(fsname, ...) int _##fsname##_open(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file close function.
 *
 * Function created by this macro is called by system when file system has to
 * close selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param fhdl          [<b>void *</b>]         file handle (user defined)
 * @return One of @ref errno value.
 *
 * @note The VFS releases its file object even if this function returns an error.
 */
#define API_FS_CLOSE(fsname, fs_handle, fhdl)
#else
#define API_FS_CLOSE(fsname, ...) int _##fsname##_close(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file write function.
 *
 * Function created by this macro is called by system when file system has to
 * write to selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param fhdl          [<b>void *</b>]         file handle (user defined)
 * @param src           [<b>const u8_t *</b>]   source buffer
 * @param count         [<b>size_t</b>]         bytes to write
 * @param fpos          [<b>fpos_t *</b>]       file position indicator. If the file system
 *                                              sets a new value, the VFS uses it as the new
 *                                              file position; if left unchanged, the VFS
 *                                              advances the position by *wrcnt.
 * @param wrcnt         [<b>size_t *</b>]       number of wrote bytes
 * @param fattr         [<b>struct vfs_fattr</b>] file access attributes
 * @return One of @ref errno value.
 *
 * @see struct vfs_fattr
 */
#define API_FS_WRITE(fsname, fs_handle, fhdl, src, count, fpos, wrcnt, fattr)
#else
#define API_FS_WRITE(fsname, ...) int _##fsname##_write(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file read function.
 *
 * Function created by this macro is called by system when file system has to
 * read from selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param fhdl          [<b>void *</b>]         file handle (user defined)
 * @param dst           [<b>u8_t *</b>]         destination buffer
 * @param count         [<b>size_t</b>]         bytes to read
 * @param fpos          [<b>fpos_t *</b>]       file position indicator. If the file system
 *                                              sets a new value, the VFS uses it as the new
 *                                              file position; if left unchanged, the VFS
 *                                              advances the position by *rdcnt.
 * @param rdcnt         [<b>size_t *</b>]       number of read bytes
 * @param fattr         [<b>struct vfs_fattr</b>] file access attributes
 * @return One of @ref errno value.
 *
 * @see struct vfs_fattr
 */
#define API_FS_READ(fsname, fs_handle, fhdl, dst, count, fpos, rdcnt, fattr)
#else
#define API_FS_READ(fsname, ...) int _##fsname##_read(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file ioctl function.
 *
 * Function created by this macro is called by system when file system has to
 * do non-standard operation on selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param fhdl          [<b>void *</b>]         file handle (user defined)
 * @param request       [<b>int</b>]            ioctl request
 * @param arg           [<b>void *</b>]         ioctl argument
 * @return One of @ref errno value.
 *
 * @see ioctl()
 */
#define API_FS_IOCTL(fsname, fs_handle, fhdl, request, arg)
#else
#define API_FS_IOCTL(fsname, ...) int _##fsname##_ioctl(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file buffer flush function.
 *
 * Function created by this macro is called by system when file system has to
 * flush buffers of selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param fhdl          [<b>void *</b>]         file handle (user defined)
 *
 * @return One of @ref errno value.
 *
 * @see sync()
 */
#define API_FS_FLUSH(fsname, fs_handle, fhdl)
#else
#define API_FS_FLUSH(fsname, ...) int _##fsname##_flush(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of directory create function.
 *
 * Function created by this macro is called by system when file system has to
 * create a new directory.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param path          [<b>const char *</b>]   directory path
 * @param mode          [<b>mode_t</b>]         directory mode (permissions)
 * @return One of @ref errno value.
 *
 * @see mode_t
 */
#define API_FS_MKDIR(fsname, fs_handle, path, mode)
#else
#define API_FS_MKDIR(fsname, ...) int _##fsname##_mkdir(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of FIFO create function.
 *
 * Function created by this macro is called by system when file system has to
 * create a new FIFO file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param path          [<b>const char *</b>]   FIFO path
 * @param mode          [<b>mode_t</b>]         FIFO mode (permissions)
 * @return One of @ref errno value.
 *
 * @see mode_t
 */
#define API_FS_MKFIFO(fsname, fs_handle, path, mode)
#else
#define API_FS_MKFIFO(fsname, ...) int _##fsname##_mkfifo(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of device node create function.
 *
 * Function created by this macro is called by system when file system has to
 * create a new device node file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param path          [<b>const char *</b>]   device node path
 * @param dev           [<b>dev_t</b>]          device ID
 * @return One of @ref errno value.
 *
 * @see dev_t
 */
#define API_FS_MKNOD(fsname, fs_handle, path, dev)
#else
#define API_FS_MKNOD(fsname, ...) int _##fsname##_mknod(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of open directory function.
 *
 * Function created by this macro is called by system when file system has to
 * open selected directory.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param path          [<b>const char *</b>]   directory path
 * @param dir           [<b>DIR *</b>]          directory object (already created)
 * @return One of @ref errno value.
 *
 * @see DIR
 */
#define API_FS_OPENDIR(fsname, fs_handle, path, dir)
#else
#define API_FS_OPENDIR(fsname, ...) int _##fsname##_opendir(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of close directory function.
 *
 * Function created by this macro is called by system when file system has to
 * close selected directory.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param dir           [<b>DIR *</b>]          directory object (already created)
 * @return One of @ref errno value.
 *
 * @see DIR
 */
#define API_FS_CLOSEDIR(fsname, fs_handle, dir)
#else
#define API_FS_CLOSEDIR(fsname, ...) int _##fsname##_closedir(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of read directory function.
 *
 * Function created by this macro is called by system when file system has to
 * read selected directory.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param dir           [<b>DIR *</b>]          directory object (already created)
 * @return One of @ref errno value.
 *
 * @see DIR
 */
#define API_FS_READDIR(fsname, fs_handle, dir)
#else
#define API_FS_READDIR(fsname, ...) int _##fsname##_readdir(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file remove function.
 *
 * Function created by this macro is called by system when file system has to
 * remove selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param path          [<b>const char *</b>]   file path
 * @return One of @ref errno value.
 */
#define API_FS_REMOVE(fsname, fs_handle, path)
#else
#define API_FS_REMOVE(fsname, ...) int _##fsname##_remove(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file rename function.
 *
 * Function created by this macro is called by system when file system has to
 * rename selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param old_name      [<b>const char *</b>]   old file path (name)
 * @param new_name      [<b>const char *</b>]   new file path (name)
 * @return One of @ref errno value.
 */
#define API_FS_RENAME(fsname, fs_handle, old_name, new_name)
#else
#define API_FS_RENAME(fsname, ...) int _##fsname##_rename(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file change mode function.
 *
 * Function created by this macro is called by system when file system has to
 * change permissions of selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param path          [<b>const char *</b>]   file path
 * @param mode          [<b>mode_t</b>]         new file mode (permissions)
 * @return One of @ref errno value.
 *
 * @see mode_t
 */
#define API_FS_CHMOD(fsname, fs_handle, path, mode)
#else
#define API_FS_CHMOD(fsname, ...) int _##fsname##_chmod(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file change owner function.
 *
 * Function created by this macro is called by system when file system has to
 * change owner of selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param path          [<b>const char *</b>]   file path
 * @param owner         [<b>uid_t</b>]          owner ID
 * @param group         [<b>gid_t</b>]          group ID
 * @return One of @ref errno value.
 *
 * @see uid_t, gid_t
 */
#define API_FS_CHOWN(fsname, fs_handle, path, owner, group)
#else
#define API_FS_CHOWN(fsname, ...) int _##fsname##_chown(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file statistics function.
 *
 * Function created by this macro is called by system when file system has to
 * get statistics of selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param fhdl          [<b>void *</b>]         file handle (user defined)
 * @param stat          [<b>struct stat *</b>]  file information
 * @return One of @ref errno value.
 *
 * @see struct stat
 */
#define API_FS_FSTAT(fsname, fs_handle, fhdl, stat)
#else
#define API_FS_FSTAT(fsname, ...) int _##fsname##_fstat(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file statistics function.
 *
 * Function created by this macro is called by system when file system has to
 * get statistics selected file.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @param path          [<b>const char *</b>]   file path
 * @param stat          [<b>struct stat *</b>]  file information
 * @return One of @ref errno value.
 *
 * @see struct stat
 */
#define API_FS_STAT(fsname, fs_handle, path, stat)
#else
#define API_FS_STAT(fsname, ...) int _##fsname##_stat(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file system statistics function.
 *
 * Function created by this macro is called by system when file system has to
 * get statistics.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]          file system memory handler
 * @param statfs        [<b>struct statfs *</b>] file system information
 * @return One of @ref errno value.
 *
 * @see struct statfs
 */
#define API_FS_STATFS(fsname, fs_handle, statfs)
#else
#define API_FS_STATFS(fsname, ...) int _##fsname##_statfs(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of file system synchronize function.
 *
 * Function created by this macro is called by system when file system has to
 * synchronize  with storage all buffers of opened files.
 *
 * @note Macro can be used only by file system code.
 *
 * @param fsname        file system name
 * @param fs_handle     [<b>void *</b>]         file system memory handler
 * @return One of @ref errno value.
 */
#define API_FS_SYNC(fsname, fs_handle)
#else
#define API_FS_SYNC(fsname, ...) int _##fsname##_sync(__VA_ARGS__)
#endif

/*==============================================================================
  Exported types, enums definitions
==============================================================================*/
#ifdef DOXYGEN
/**
 * @brief Structure indicate file access mode.
 */
struct vfs_fattr {
        bool non_blocking_rd; /*!< Non-blocking file read access.*/
        bool non_strict_rd;   /*!< Non-strict file read access (short read is not EOF).*/
        bool non_blocking_wr; /*!< Non-blocking file write access.*/
};

/**
 * @brief Structure describe built-in program data.
 */
struct _prog_data {
        const char *name;           /*!< Program name.*/
        const size_t *globals_size; /*!< Size of program global variables.*/
        const size_t *stack_depth;  /*!< Stack depth.*/
        process_func_t main;        /*!< Program main function.*/
};
#endif

/** pipe object (content is not accessible) */
typedef struct pipe pipe_t;

/*==============================================================================
  Exported object declarations
==============================================================================*/

/*==============================================================================
  Exported function prototypes
==============================================================================*/
#ifdef DOXYGEN /* function documentation only */
//==============================================================================
/**
 * @brief  Linked list constructor function.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  functor      compare functor (can be @ref NULL)
 * @param  obj_dtor     object destructor (can be @ref NULL, then free() is used as
 destructor)
 * @param  list         pointer to list object
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...
        llist_t *list = NULL;
        int      err  = sys_llist_create(NULL, NULL, &list);
        if (err != ESUCC) {
                // ...
        } else {
                // ...

                sys_llist_destroy(list);
        }
   @endcode
 *
 * @see sys_llist_destroy()
 */
//==============================================================================
static inline int sys_llist_create(llist_cmp_functor_t functor, llist_obj_dtor_t obj_dtor, llist_t **list);
#endif

//==============================================================================
/**
 * @brief  Linked list destructor function.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         pointer to list object
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...
        llist_t *list = NULL;
        int      err  = sys_llist_create(NULL, NULL, &list);
        if (err != ESUCC) {
                // ...
        } else {
                // ...

                sys_llist_destroy(list);
        }
   @endcode
 *
 * @see sys_llist_create()
 */
//==============================================================================
extern int sys_llist_destroy(llist_t *list);

//==============================================================================
/**
 * @brief  Checks if list is empty.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 *
 * @return If list is empty then @b true is returned, otherwise @b false.
 *
 * @b Example
 * @code
        // ...
        llist_t *list = NULL;
        int      err  = sys_llist_create(NULL, NULL, &list);
        if (err != ESUCC) {
                // ...
        } else {
                if (sys_llist_empty()) {
                        // ...
                }

                // ...

                sys_llist_destroy(list);
        }
   @endcode
 *
 * @see sys_llist_size(), sys_llist_erase(), sys_llist_clear()
 */
//==============================================================================
extern bool sys_llist_empty(llist_t *list);

//==============================================================================
/**
 * @brief  Function returns a number of elements of the list.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 *
 * @return Number of elements of the list or @b -1 on error.
 *
 * @b Example
 * @code
        // ...
        llist_t *list = NULL;
        int      err  = sys_llist_create(NULL, NULL, &list);
        if (err != ESUCC) {
                // ...
        } else {
                if (sys_llist_size() > 10) {
                        // ...
                } else {
                        // ...
                }

                sys_llist_destroy(list);
        }
   @endcode
 *
 * @see sys_llist_empty(), sys_llist_erase(), sys_llist_clear()
 */
//==============================================================================
extern int sys_llist_size(llist_t *list);

//==============================================================================
/**
 * @brief  Pushes selected data to the front of the list. Creates a new object.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  size         data size
 * @param  data         data source
 *
 * @return On success allocated memory pointer is returned, otherwise @ref NULL.
 *
 * @b Example
 * @code
        // ...
        llist_t *list = NULL;
        int      err  = sys_llist_create(NULL, NULL, &list);
        if (err != ESUCC) {
                // ...
        } else {
                // ...

                int val = 100;
                if (sys_llist_push_emplace_front(list, sizeof(int), &val) != NULL) {
                        // success ...
                } else {
                        // error handling ...
                }

                sys_llist_destroy(list);
        }
   @endcode
 *
 * @see sys_llist_push_front(), sys_llist_push_emplace_back()
 */
//==============================================================================
extern void *sys_llist_push_emplace_front(llist_t *list, size_t size, const void *data);

//==============================================================================
/**
 * @brief  Pushes selected object to the front of the list.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  object       object to push
 *
 * @return On success pointer to the object is returned, otherwise @ref NULL.
 *
 * @b Example
 * @code
        // ...
        llist_t *list = NULL;
        int      err  = sys_llist_create(NULL, NULL, &list);
        if (err != ESUCC) {
                // ...
        } else {
                // ...

                int *val = malloc(sizeof(int));
                if (!val) {
                        // no memory ...
                } else {
                        *val = 100;
                }

                if (sys_llist_push_front(list, val) != NULL) {
                        // success ...
                } else {
                        // error handling ...
                }

                sys_llist_destroy(list);
        }
   @endcode
 *
 * @see sys_llist_push_emplace_front(), sys_llist_push_back()
 */
//==============================================================================
extern void *sys_llist_push_front(llist_t *list, void *object);

//==============================================================================
/**
 * @brief  Deletes first element of the list. This destroys an element by using
 *         selected destructor function.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 *
 * @return On success @b 1 is returned, otherwise @b 0.
 *
 * @b Example
 * @code
        // ...
        llist_t *list = NULL;
        int      err  = sys_llist_create(NULL, NULL, &list);
        if (err != ESUCC) {
                // ...
        } else {
                // ...

                int *val = malloc(sizeof(int));
                if (!val) {
                        // no memory ...
                } else {
                        *val = 100;
                }

                if (sys_llist_push_front(list, val) != NULL) {
                        // success ...
                        // ...

                        sys_llist_pop_front(list);

                        // ...
                } else {
                        // error handling ...
                }

                sys_llist_destroy(list);
        }
   @endcode
 *
 * @see sys_llist_push_front(), sys_llist_push_emplace_front()
 */
//==============================================================================
extern int sys_llist_pop_front(llist_t *list);

//==============================================================================
/**
 * @brief  Push selected data to the back of the list. Creates a new object.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  size         data size
 * @param  data         data source
 *
 * @return On success allocated memory pointer is returned, otherwise @ref NULL.
 *
 * @b Example
 * @code
        // ...
        llist_t *list = NULL;
        int      err  = sys_llist_create(NULL, NULL, &list);
        if (err != ESUCC) {
                // ...
        } else {
                // ...

                int val = 100;
                if (sys_llist_push_emplace_back(list, sizeof(int), &val) != NULL) {
                        // success ...
                } else {
                        // error handling ...
                }

                sys_llist_destroy(list);
        }
   @endcode
 *
 * @see sys_llist_push_emplace_front(), sys_llist_push_front(), sys_llist_push_back()
 */
//==============================================================================
extern void *sys_llist_push_emplace_back(llist_t *list, size_t size, const void *data);

//==============================================================================
/**
 * @brief  Push selected object to the back of the list.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  object       object to push
 *
 * @return On success pointer to the object is returned, otherwise @ref NULL.
 */
//==============================================================================
extern void *sys_llist_push_back(llist_t *list, void *object);

//==============================================================================
/**
 * @brief  Delete the last element of the list. This destroys element.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 *
 * @return On success 1 is returned, otherwise 0.
 */
//==============================================================================
extern int sys_llist_pop_back(llist_t *list);

//==============================================================================
/**
 * @brief  Allocate and append data at selected position in the list.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  position     element position
 * @param  size         element's size
 * @param  data         element's data
 *
 * @return On success pointer to the object is returned, otherwise @ref NULL.
 */
//==============================================================================
extern void *sys_llist_emplace(llist_t *list, int position, size_t size, const void *data);

//==============================================================================
/**
 * @brief  Insert an element to the list.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  position     position to insert
 * @param  object       object to insert
 *
 * @return On success object is returned, otherwise @ref NULL.
 */
//==============================================================================
extern void *sys_llist_insert(llist_t *list, int position, void *object);

//==============================================================================
/**
 * @brief  Erase selected begin of the list. The element is destroyed.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  position     position to remove
 *
 * @return On success 1 is returned, otherwise 0.
 */
//==============================================================================
extern int sys_llist_erase(llist_t *list, int position);

//==============================================================================
/**
 * @brief  Return selected begin and remove from the list. The element is not destroyed.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  position     position to take (unlink)
 *
 * @return On success taken object is returned, otherwise @ref NULL.
 */
//==============================================================================
extern void *sys_llist_take(llist_t *list, int position);

//==============================================================================
/**
 * @brief  Return first begin and remove from the list. The element is not destroyed.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 *
 * @return On success taken object is returned, otherwise @ref NULL.
 */
//==============================================================================
extern void *sys_llist_take_front(llist_t *list);

//==============================================================================
/**
 * @brief  Return last begin and remove from the list. The element is not destroyed.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 *
 * @return On success taken object is returned, otherwise @ref NULL.
 */
//==============================================================================
extern void *sys_llist_take_back(llist_t *list);

//==============================================================================
/**
 * @brief  Clear entire list (objects are destroyed).
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object

 * @return On success 1 is returned, otherwise 0.
 */
//==============================================================================
extern int sys_llist_clear(llist_t *list);

//==============================================================================
/**
 * @brief  Swap 2 elements.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  j            position of element a
 * @param  k            position of element b
 *
 * @return On success 1 is returned, otherwise 0.
 */
//==============================================================================
extern int sys_llist_swap(llist_t *list, int j, int k);

//==============================================================================
/**
 * @brief  Quick sort elements of the list.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 */
//==============================================================================
extern void sys_llist_sort(llist_t *list);

//==============================================================================
/**
 * @brief  Leave only an unique elements, all not unique are removed (are destroyed).
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 */
//==============================================================================
extern void sys_llist_unique(llist_t *list);

//==============================================================================
/**
 * @brief  Reverse entire table.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 */
//==============================================================================
extern void sys_llist_reverse(llist_t *list);

//==============================================================================
/**
 * @brief  Get element from the list at selected position.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  position     begin position
 *
 * @return Pointer to data, or @ref NULL on error.
 */
//==============================================================================
extern void *sys_llist_at(llist_t *list, int position);

//==============================================================================
/**
 * @brief  Check if list contains selected object.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  object       object to find
 *
 * @return Number of found objects, or -1 on error.
 */
//==============================================================================
extern int sys_llist_contains(llist_t *list, const void *object);

//==============================================================================
/**
 * @brief  Find selected object in the list from the beginning.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  object       object to find
 *
 * @return Object position, or -1 on error.
 */
//==============================================================================
extern int sys_llist_find_begin(llist_t *list, const void *object);

//==============================================================================
/**
 * @brief  Find selected object in the list from the end.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 * @param  object       object to find
 *
 * @return Object position, or -1 on error.
 */
//==============================================================================
extern int sys_llist_find_end(llist_t *list, const void *object);

//==============================================================================
/**
 * @brief  Access first element.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 *
 * @return Pointer to data, or @ref NULL on error.
 */
//==============================================================================
extern void *sys_llist_front(llist_t *list);

//==============================================================================
/**
 * @brief  Access last element.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 *
 * @return Pointer to data, or @ref NULL on error.
 */
//==============================================================================
extern void *sys_llist_back(llist_t *list);

//==============================================================================
/**
 * @brief  Create an iterator to the list.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  list         list object
 *
 * @return Iterator object.
 *
 * @b Example
 * @code
        // ...
        llist_iterator_t it;

        // ...

        // iterate elements of the list from beginning
        it = sys_llist_iterator(list);
        for (void *obj = sys_llist_begin(&it); obj; obj = sys_llist_interator_next(&it)) {
                obj->...;
        }

        // ...

        // iterate elements of list for the end
        it = sys_llist_iterator(list);
        for (void *obj = sys_llist_end(&it); obj; obj = sys_llist_interator_prev(&it)) {
                obj->...;
        }

        // ...
   @endcode
 *
 * @see sys_llist_foreach(), sys_llist_foreach_reverse()
 */
//==============================================================================
extern llist_iterator_t sys_llist_iterator(llist_t *list);

//==============================================================================
/**
 * @brief  Return first object from list by using iterator.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  iterator     iterator object
 *
 * @return Pointer to data object.
 *
 * @b Example
 * @code
        // ...
        llist_iterator_t it;

        // ...

        // iterate elements of the list from beginning
        it = sys_llist_iterator(list);
        for (void *obj = sys_llist_begin(&it); obj; obj = sys_llist_interator_next(&it)) {
                obj->...;
        }

        // ...

        // iterate elements of list for the end
        it = sys_llist_iterator(list);
        for (void *obj = sys_llist_end(&it); obj; obj = sys_llist_interator_prev(&it)) {
                obj->...;
        }

        // ...
   @endcode
 *
 * @see sys_llist_foreach(), sys_llist_foreach_reverse()
 */
//==============================================================================
extern void *sys_llist_begin(llist_iterator_t *iterator);

//==============================================================================
/**
 * @brief  Return last object from list by using iterator.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  iterator     iterator object
 *
 * @return Pointer to data object.
 *
 * @b Example
 * @code
        // ...
        llist_iterator_t it;

        // ...

        // iterate elements of the list from beginning
        it = sys_llist_iterator(list);
        for (void *obj = sys_llist_begin(&it); obj; obj = sys_llist_interator_next(&it)) {
                obj->...;
        }

        // ...

        // iterate elements of list for the end
        it = sys_llist_iterator(list);
        for (void *obj = sys_llist_end(&it); obj; obj = sys_llist_interator_prev(&it)) {
                obj->...;
        }

        // ...
   @endcode
 *
 * @see sys_llist_foreach(), sys_llist_foreach_reverse()
 */
//==============================================================================
extern void *sys_llist_end(llist_iterator_t *iterator);

//==============================================================================
/**
 * @brief  Return selected objects from list by using range iterator (forward).
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  iterator     iterator object
 * @param  begin        begin position
 * @param  end          end position
 *
 * @return Pointer to data object.
 *
 * @b Example
 * @code
        // ...

        // iterate list from object <5, 8>
        llist_iterator_t it = sys_llist_iterator(list);
        for (void *obj = sys_llist_range(&it, 5, 8); obj; obj =
 sys_llist_interator_next(&it)) { obj->...;
        }

        // ...
   @endcode
 *
 * @see sys_llist_foreach(), sys_llist_foreach_reverse()
 */
//==============================================================================
extern void *sys_llist_range(llist_iterator_t *iterator, int begin, int end);

//==============================================================================
/**
 * @brief  Return next data object from list by using iterator.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  iterator     iterator object
 *
 * @return Pointer to data object.
 *
 * @b Example
 * @code
        // ...
        llist_iterator_t it;

        // ...

        // iterate elements of the list from beginning
        it = sys_llist_iterator(list);
        for (void *obj = sys_llist_begin(&it); obj; obj = sys_llist_interator_next(&it)) {
                obj->...;
        }

        // ...

        // iterate elements of list for the end
        it = sys_llist_iterator(list);
        for (void *obj = sys_llist_end(&it); obj; obj = sys_llist_interator_prev(&it)) {
                obj->...;
        }

        // ...
   @endcode
 *
 * @see sys_llist_foreach(), sys_llist_foreach_reverse()
 */
//==============================================================================
extern void *sys_llist_iterator_next(llist_iterator_t *iterator);

//==============================================================================
/**
 * @brief  Return previous data object from list by using iterator.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  iterator     iterator object
 *
 * @return Pointer to data object.
 *
 * @b Example
 * @code
        // ...
        llist_iterator_t it;

        // ...

        // iterate elements of the list from beginning
        it = sys_llist_iterator(list);
        for (void *obj = sys_llist_begin(&it); obj; obj = sys_llist_interator_next(&it)) {
                obj->...;
        }

        // ...

        // iterate elements of list for the end
        it = sys_llist_iterator(list);
        for (void *obj = sys_llist_end(&it); obj; obj = sys_llist_interator_prev(&it)) {
                obj->...;
        }

        // ...
   @endcode
 *
 * @see sys_llist_foreach(), sys_llist_foreach_reverse()
 */
//==============================================================================
extern void *sys_llist_iterator_prev(llist_iterator_t *iterator);

//==============================================================================
/**
 * @brief  Erase selected begin of the list. The element is destroyed.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  iterator     position to remove
 *
 * @return On success 1 is returned, otherwise 0.
 *
 * @b Example
 * @code
        // ...

        llist_iterator_t it = sys_llist_iterator(list);
        for (void *obj = sys_llist_begin(&it); obj; obj = sys_llist_interator_next(&it)) {
                if (obj->... > 0) {
                        sys_llist_erase_by_iterator(&it);
                }
        }

        // ...
   @endcode
 */
//==============================================================================
extern int sys_llist_erase_by_iterator(llist_iterator_t *iterator);

//==============================================================================
/**
 * @brief  Compare functor that compares two pointers (not contents).
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  a    pointer a
 * @param  b    pointer b
 *
 * @retval a > b:  1
 * @retval a = b:  0
 * @retval a < b: -1
 */
//==============================================================================
extern int sys_llist_functor_cmp_pointers(const void *a, const void *b);

//==============================================================================
/**
 * @brief  Compare functor that compares two strings (contents).
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  a    string a
 * @param  b    string b
 *
 * @retval a > b:  1
 * @retval a = b:  0
 * @retval a < b: -1
 */
//==============================================================================
extern int sys_llist_functor_cmp_strings(const void *a, const void *b);

//==============================================================================
/**
 * @brief Function creates device node (device file).
 *
 * Function creates a file system node (device special file) named
 * <i>path</i>. Node is connected to the device <i>dev</i>.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param path          node name
 * @param dev           device number
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        dev_t dev = _dev_t__create(_module_get_ID("UART"), 0, 0);
        fs_mknod("/dev/uart0", dev);

        // ...
   @endcode
 */
//==============================================================================
extern int fs_mknod(const char *path, dev_t dev);

//==============================================================================
/**
 * @brief Function creates new directory.
 *
 * The function attempts to create a directory named <i>pathname</i>. The
 * argument <i>mode</i> specifies the permissions to use.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param pathname      directory name
 * @param mode          directory permissions
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        fs_mkdir("/dev", 0666);    // wr+rd access for all users, groups and others

        // ...

   @endcode
 */
//==============================================================================
extern int fs_mkdir(const char *pathname, mode_t mode);

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
 * @note Function can be used only by file system or driver code.
 *
 * @param pathname      FIFO name
 * @param mode          FIFO permissions
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        fs_mkfifo("/dev/my_fifo", 0666);    // wr+rd access for all users, groups and
 others

        // ...

   @endcode
 */
//==============================================================================
extern int fs_mkfifo(const char *pathname, mode_t mode);

//==============================================================================
/**
 * @brief Function opens selected directory.
 *
 * Function opens a directory stream corresponding to the directory <i>path</i>, and
 * returns a pointer to the directory stream. The stream is positioned at the first
 * entry in the directory. Opened stream is passed by pointer <i>dir</i>.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param path          directory path
 * @param dir           pointer to stream object
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        DIR *dir = NULL;

        if (fs_opendir("/foo/bar", &dir) == ESUCC) {

                dirent_t *dirent = NULL;

                while (fs_readdir(dir, &dirent) == ESUCC) {
                        // ...
                }

                fs_closedir(dir);
        } else {
                // ...
        }

        // ...
   @endcode
 *
 * @see fs_closedir(), fs_readdir()
 */
//==============================================================================
extern int fs_opendir(const char *path, kdir_t **dir);

//==============================================================================
/**
 * @brief Function closes selected directory stream.
 *
 * Function closes the directory stream associated with <i>dir</i>. The directory
 * stream descriptor <i>dir</i> is not available after this call.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param dir           pinter to directory object
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        DIR *dir = NULL;

        if (fs_opendir("/foo/bar", &dir) == ESUCC) {

                dirent_t *dirent = NULL;

                while (fs_readdir(dir, &dirent) == ESUCC) {
                        // ...
                }

                fs_closedir(dir);
        } else {
                // ...
        }

        // ...
   @endcode
 *
 * @see fs_opendir(), fs_readdir()
 */
//==============================================================================
extern int fs_closedir(kdir_t *dir);

//==============================================================================
/**
 * @brief Function reads entry from opened directory stream.
 *
 * Function returns a pointer <i>dirent</i> to object <b>dirent_t</b> type
 * representing the next directory entry in the directory stream pointed to by
 * <i>dir</i>.<p>
 *
 * @note Function can be used only by file system or driver code.
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
 *
 * @b Example
 * @code
        // ...

        DIR *dir = NULL;

        if (fs_opendir("/foo/bar", &dir) == ESUCC) {

                dirent_t *dirent = NULL;

                while (fs_readdir(dir, &dirent) == ESUCC) {
                        // ...
                }

                fs_closedir(dir);
        } else {
                // ...
        }

        // ...
   @endcode
 *
 * @see fs_opendir(), fs_closedir()
 */
//==============================================================================
extern int fs_readdir(kdir_t *dir, dirent_t **dirent);

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
 * @note Function can be used only by file system or driver code.
 *
 * @param path      path to file
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        int err = fs_remove("/foo/bar");

        // ...
   @endcode
 */
//==============================================================================
extern int fs_remove(const char *path);

//==============================================================================
/**
 * @brief Function renames selected file.
 *
 * The function renames a file. In contrast to standard C library this function
 * don't move files between directories if <i>new_name</i> is localized on other
 * filesystem than <i>old_name</i>, otherwise it's depending on filesystem.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param old_name      old file name
 * @param new_name      new file name
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        int err = fs_rename("/foo/bar", "/foo/baz");

        // ...
   @endcode
 */
//==============================================================================
extern int fs_rename(const char *old_name, const char *new_name);

//==============================================================================
/**
 * @brief Function changes file mode.
 *
 * The function change the permissions of a file specified by <i>path</i>.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param path          file to permission change
 * @param mode          new permissions
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        fs_chmod("/foo/bar", 0666);   // wr+rd access for all users, groups and others

        // ...

   @endcode
 */
//==============================================================================
extern int fs_chmod(const char *path, mode_t mode);

//==============================================================================
/**
 * @brief Function changes the ownership of file.
 *
 * The function changes the ownership of the file specified by <i>path</i>.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param path          path to file
 * @param owner         owner ID
 * @param group         group ID
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        int err = fs_chown("/foo/bar", 1000, 1000);

        // ...
   @endcode
 */
//==============================================================================
extern int fs_chown(const char *path, uid_t owner, gid_t group);

//==============================================================================
/**
 * @brief Function gets file information.
 *
 * The function return information about a file specified by <i>path</i>.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param path          file to inspect
 * @param buf           file's information
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        struct stat info;

        if (fs_stat("/dev/foo", &info) == ESUCC) {
                // ...
        } else {
                // ...
        }

        // ...

   @endcode
 */
//==============================================================================
extern int fs_stat(const char *path, struct stat *buf);

//==============================================================================
/**
 * @brief Function gets file system information.
 *
 * The function returns information about a mounted file system.
 * A <i>path</i> is directory of the mount point of file system.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param path          node name
 * @param statfs        file system information container
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        struct statfs info;
        if (fs_statfs("/proc", &info) == ESUCC) {
                // ...
        } else {
                // ...
        }

        // ...
   @endcode
 */
//==============================================================================
extern int fs_statfs(const char *path, struct statfs *statfs);

//==============================================================================
/**
 * @brief Function synchronizes files buffers with file systems.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @b Example
 * @code
        // ...

        fs_sync();

        // ...
   @endcode
 */
//==============================================================================
extern void fs_sync(void);

//==============================================================================
/**
 * @brief  Function return collected process statistics.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  pid      PID
 * @param  stat     process statistics
 *
 * @return One of @ref errno value.
 *
 * @see fs_process_get_stat_seek()
 */
//==============================================================================
extern int fs_process_get_stat_pid(pid_t pid, _process_stat_t *stat);

//==============================================================================
/**
 * @brief  Function return collected process statistics
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  seek     process seek (start from 0)
 * @param  stat     process statistics
 *
 * @return One of @ref errno value.
 *
 * @see fs_process_get_count(), fs_process_get_stat_pid()
 */
//==============================================================================
extern int fs_process_get_stat_seek(size_t seek, _process_stat_t *stat);

//==============================================================================
/**
 * @brief  Function return number of processes.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @return Number of processes.
 */
//==============================================================================
extern size_t fs_process_get_count(void);

//==============================================================================
/**
 * @brief Function open selected driver
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param id            module id
 * @param flags         flags
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_dev_open(dev_t id, u32_t flags);

//==============================================================================
/**
 * @brief Function close selected driver
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param id            module id
 * @param force         force close request
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_dev_close(dev_t id, bool force);

//==============================================================================
/**
 * @brief Function write data to driver
 *
 * @note Function can be used only by file system or driver code.
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
extern int fs_dev_write(dev_t id, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt, struct vfs_fattr fattr);

//==============================================================================
/**
 * @brief Function read data to driver
 *
 * @note Function can be used only by file system or driver code.
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
extern int fs_dev_read(dev_t id, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr);

//==============================================================================
/**
 * @brief IO control
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param id            module id
 * @param request       io request
 * @param arg           argument
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_dev_ioctl(dev_t id, int request, void *arg);

//==============================================================================
/**
 * @brief Flush device buffer (forces write)
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param id            module id
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_dev_flush(dev_t id);

//==============================================================================
/**
 * @brief Device information
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param id            module id
 * @param stat          status object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_dev_stat(dev_t id, struct vfs_dev_stat *stat);

//==============================================================================
/**
 * @brief Function returns device type: character or block device.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param id            device id
 *
 * @return @ref S_IFBLK for block devices, @ref S_IFCHR for character devices.
 */
//==============================================================================
extern mode_t fs_dev_type(dev_t id);

//==============================================================================
/**
 * @brief  Function allocates memory for file system. Used by sys_malloc() family functions.
 *
 * @param size             block size
 * @param align            block alignment (0: default)
 * @param prefreg          memory regions searched in order (NULL terminated),
 *                         NULL: regions selected for file systems by CPU configuration
 * @param zero             clear allocated memory
 * @param mem              allocated memory
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_mem_alloc(size_t size, size_t align, const char **prefreg, bool zero, void **mem);

//==============================================================================
/**
 * @brief  Function frees memory of file system. Used by sys_free() function.
 *
 * @param mem              memory to free (pointer is cleared)
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_mem_free(void **mem);

//==============================================================================
/**
 * @brief  Linked list constructor of file system. Used by sys_llist_create() function.
 *
 * @param  functor      compare functor (can be @ref NULL)
 * @param  obj_dtor     object destructor (can be @ref NULL)
 * @param  list         pointer to list object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_llist_create(llist_cmp_functor_t functor, llist_obj_dtor_t obj_dtor, llist_t **list);

/*==============================================================================
  Exported inline function
==============================================================================*/
#ifndef DOXYGEN /* functions described in sysapi.h header */
//==============================================================================
/**
 * @brief  Allocate memory
 *
 * @param size             object size
 * @param mem              pointer to memory block pointer
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static inline int sys_malloc(size_t size, void **mem)
{
        return fs_mem_alloc(size, 0, NULL, false, mem);
}

//==============================================================================
/**
 * @brief  Allocate memory with selected alignment
 *
 * @param size             object size
 * @param align            alignment (power of 2, e.g. _CPUCTL_CACHE_ALIGN for
 *                         DMA buffers), 0: default heap alignment
 * @param prefreg          region list searched in order (NULL terminated),
 *                         NULL: regions selected for file systems by CPU configuration
 * @param mem              pointer to memory block pointer
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static inline int sys_malloc2(size_t size, size_t align, const char *prefreg[], void **mem)
{
        return fs_mem_alloc(size, align, prefreg, false, mem);
}

//==============================================================================
/**
 * @brief  Allocate memory and clear content
 *
 * @param size             object size
 * @param mem              pointer to memory block pointer
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static inline int sys_zalloc(size_t size, void **mem)
{
        return fs_mem_alloc(size, 0, NULL, true, mem);
}

//==============================================================================
/**
 * @brief  Allocate memory with selected alignment and clear content
 *
 * @param size             object size
 * @param align            alignment (power of 2, e.g. _CPUCTL_CACHE_ALIGN for
 *                         DMA buffers), 0: default heap alignment
 * @param prefreg          region list searched in order (NULL terminated),
 *                         NULL: regions selected for file systems by CPU configuration
 * @param mem              pointer to memory block pointer
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static inline int sys_zalloc2(size_t size, size_t align, const char *prefreg[], void **mem)
{
        return fs_mem_alloc(size, align, prefreg, true, mem);
}

//==============================================================================
/**
 * @brief  Free allocated memory
 *
 * @param mem           pointer to memory block to free
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static inline int sys_free(void **mem)
{
        return fs_mem_free(mem);
}

//==============================================================================
/**
 * @brief  List constructor (for FS only)
 * @param  cmp_functor          compare functor (can be NULL)
 * @param  obj_dtor             object destructor (can be NULL, then free() is destructor)
 * @return One of @ref errno value.
 */
//==============================================================================
static inline int sys_llist_create(llist_cmp_functor_t functor, llist_obj_dtor_t obj_dtor, llist_t **list)
{
        return fs_llist_create(functor, obj_dtor, list);
}
#endif /* DOXYGEN */

//==============================================================================
/**
 * @brief Create pipe object
 *
 * @note Function can be used only by file system code.
 *
 * @param pipe     pointer to pointer of pipe handle
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_pipe_create(pipe_t **pipe);

//==============================================================================
/**
 * @brief Destroy pipe object
 *
 * @note Function can be used only by file system code.
 *
 * @param pipe          a pipe object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_pipe_destroy(pipe_t *pipe);

//==============================================================================
/**
 * @brief Return length of pipe
 *
 * @note Function can be used only by file system code.
 *
 * @param pipe          a pipe object
 * @param len           a pipe length
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_pipe_get_length(pipe_t *pipe, size_t *len);

//==============================================================================
/**
 * @brief Read data from pipe
 *
 * @note Function can be used only by file system code.
 *
 * @param pipe          a pipe object
 * @param buf           a destination buffer
 * @param count         a count of bytes to read
 * @param rdcnt         a number of read bytes
 * @param non_blocking  a non-blocking access mode
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_pipe_read(pipe_t *pipe, u8_t *buf, size_t count, size_t *rdcnt, bool non_blocking);

//==============================================================================
/**
 * @brief Read data from pipe
 *
 * @note Function can be used only by file system code.
 *
 * @param pipe          a pipe object
 * @param buf           a destination buffer
 * @param count         a count of bytes to read
 * @param wrcnt         a number of written bytes
 * @param non_blocking  a non-blocking access mode
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_pipe_write(pipe_t *pipe, const u8_t *buf, size_t count, size_t *wrcnt, bool non_blocking);

//==============================================================================
/**
 * @brief Close pipe
 *
 * @note Function can be used only by file system code.
 *
 * @param pipe          a pipe object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_pipe_close(pipe_t *pipe);

//==============================================================================
/**
 * @brief Make FIFO permanent - FIFO is not closed at pipe close.
 *
 * @note Function can be used only by file system code.
 *
 * @param pipe          a pipe object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_pipe_permanent(pipe_t *pipe);

//==============================================================================
/**
 * @brief  Clear pipe
 *
 * @note Function can be used only by file system code.
 *
 * @param  pipe         a pipe object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int fs_pipe_clear(pipe_t *pipe);

//==============================================================================
/**
 * @brief  Function duplicate selected string.
 *
 * @param  str          source string
 * @param  stddup       duplicated string
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static inline int fs_strdup(const char *str, char **strdup)
{
        int err = EINVAL;
        if (str && strdup) {
                err = sys_malloc(strsize(str), cast(void **, strdup));
                if (!err) {
                        strcpy(*strdup, str);
                }
        }
        return err;
}

//==============================================================================
/**
 * @brief  Function duplicate selected string.
 *
 * @param  str          source string
 * @param  len          source string len
 * @param  stddup       duplicated string
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static inline int fs_strndup(const char *str, size_t len, char **strdup)
{
        int err = EINVAL;
        if (str && strdup) {
                err = sys_malloc(len + 1, cast(void **, strdup));
                if (!err) {
                        sys_strlcpy(*strdup, str, len + 1);
                }
        }
        return err;
}

#ifdef __cplusplus
}
#endif

#endif /* _DNXFS_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
