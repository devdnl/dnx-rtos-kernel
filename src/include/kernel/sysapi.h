/*==============================================================================
@file    sysapi.h

@author  Daniel Zorychta

@brief   Kernel API common for drivers (modules) and file systems.

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
 * @defgroup sysapi-h System functions
 *
 * Kernel API common for drivers and file systems. The header is included by
 * the @ref fs-h and @ref driver-h headers and shall not be included directly
 * by drivers or file systems. Kernel internal headers are not available in
 * drivers and file systems (code compiled with the __DNX_MODULE__ macro).
 *
 * @{
 */

#ifndef _SYSAPI_H_
#define _SYSAPI_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "config.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "lib/sys/types.h"
#include "lib/cast.h"
#include "lib/unarg.h"
#include "lib/misc.h"
#include "lib/conv.h"
#include "kernel/errno.h"
#include "kernel/kres.h"
#include "cpu/cpuctl.h"
#include "drivers/ioctl_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/* set position equal to offset bytes */
#define VFS_SEEK_SET 0

/* set position to current location plus offset */
#define VFS_SEEK_CUR 1

/* set position to EOF plus offset */
#define VFS_SEEK_END 2

/* translate functions to STDC */
#ifndef SEEK_SET
#define SEEK_SET VFS_SEEK_SET
#endif

#ifndef SEEK_CUR
#define SEEK_CUR VFS_SEEK_CUR
#endif

#ifndef SEEK_END
#define SEEK_END VFS_SEEK_END
#endif

/* file access flags. Doxygen documentation in "fs/fs.h" */
#ifndef O_RDONLY
#define O_RDONLY 00
#endif

#ifndef O_WRONLY
#define O_WRONLY 01
#endif

#ifndef O_RDWR
#define O_RDWR 02
#endif

#ifndef O_CREAT
#define O_CREAT 0100
#endif

#ifndef O_EXCL
#define O_EXCL 0200
#endif

#ifndef O_TRUNC
#define O_TRUNC 01000
#endif

#ifndef O_APPEND
#define O_APPEND 02000
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK 04000
#endif

#ifndef O_DIRECTORY
#define O_DIRECTORY 0200000
#endif

#ifndef O_SHARED
#define O_SHARED 0400000
#endif

/* mask of access mode bits (O_RDONLY, O_WRONLY, O_RDWR) */
#ifndef O_ACCMODE
#define O_ACCMODE 03
#endif

/* modes */
#define S_IRWXU 0000700 /* RWX mask for owner */
#define S_IRUSR 0000400 /* R for owner */
#define S_IWUSR 0000200 /* W for owner */
#define S_IXUSR 0000100 /* X for owner */

#define S_IRWXG 0000070 /* RWX mask for group */
#define S_IRGRP 0000040 /* R for group */
#define S_IWGRP 0000020 /* W for group */
#define S_IXGRP 0000010 /* X for group */

#define S_IRWXO 0000007 /* RWX mask for other */
#define S_IROTH 0000004 /* R for other */
#define S_IWOTH 0000002 /* W for other */
#define S_IXOTH 0000001 /* X for other */

#define S_ISUID 0004000 /* set user id on execution */
#define S_ISGID 0002000 /* set group id on execution */
#define S_ISVTX 0001000 /* save swapped text even after use */

#define S_IPMT (000777)
#define S_IFMT (070000)

#define S_ISREG(mode_t_m)  ((mode_t_m & S_IFMT) == S_IFREG)
#define S_ISDIR(mode_t_m)  ((mode_t_m & S_IFMT) == S_IFDIR)
#define S_ISCHR(mode_t_m)  ((mode_t_m & S_IFMT) == S_IFCHR)
#define S_ISLNK(mode_t_m)  ((mode_t_m & S_IFMT) == S_IFLNK)
#define S_ISPROG(mode_t_m) ((mode_t_m & S_IFMT) == S_IFPROG)
#define S_ISFIFO(mode_t_m) ((mode_t_m & S_IFMT) == S_IFIFO)
#define S_ISBLK(mode_t_m)  ((mode_t_m & S_IFMT) == S_IFBLK)

#define S_IFREG  0000000
#define S_IFDIR  0010000
#define S_IFCHR  0020000
#define S_IFLNK  0030000
#define S_IFPROG 0040000
#define S_IFIFO  0050000
#define S_IFBLK  0060000

/* stream definitions */
#define EOF (-1)
#define ETX 0x03
#define EOT 0x04

/* IO operations on files */
#define IOCTL_PIPE__CLOSE                  0xF0000
#define IOCTL_PIPE__CLEAR                  0xF0001
#define IOCTL_PIPE__PERMANENT              0xF0002
#define IOCTL_VFS__NON_BLOCKING_RD_MODE    0xF0003
#define IOCTL_VFS__DEFAULT_RD_MODE         0xF0004
#define IOCTL_VFS__IS_NON_BLOCKING_RD_MODE 0xF0005
#define IOCTL_VFS__NON_BLOCKING_WR_MODE    0xF0006
#define IOCTL_VFS__DEFAULT_WR_MODE         0xF0007
#define IOCTL_VFS__IS_NON_BLOCKING_WR_MODE 0xF0008
#define IOCTL_VFS__NON_STRICT_RD_MODE      0xF0009
#define IOCTL_VFS__IS_NON_STRICT_RD_MODE   0xF000A

/** maximum timeout in seconds */
#define _MAX_DELAY_S ((0xFFFFFFFFUL - 1000UL) / 1000UL)

/** maximum timeout in milliseconds */
#define _MAX_DELAY_MS (_MAX_DELAY_S * 1000UL)

/** timeout value used to wait forever */
#define _WAIT_FOREVER_MS (_MAX_DELAY_MS + 1)

/** standard stack sizes (thread attributes) */
#define _STACK_DEPTH_MINIMAL    ((1 * (__OS_TASK_MIN_STACK_DEPTH__)) + (__OS_IRQ_STACK_DEPTH__))
#define _STACK_DEPTH_VERY_LOW   ((2 * (__OS_TASK_MIN_STACK_DEPTH__)) + (__OS_IRQ_STACK_DEPTH__))
#define _STACK_DEPTH_LOW        ((4 * (__OS_TASK_MIN_STACK_DEPTH__)) + (__OS_IRQ_STACK_DEPTH__))
#define _STACK_DEPTH_MEDIUM     ((8 * (__OS_TASK_MIN_STACK_DEPTH__)) + (__OS_IRQ_STACK_DEPTH__))
#define _STACK_DEPTH_LARGE      ((16 * (__OS_TASK_MIN_STACK_DEPTH__)) + (__OS_IRQ_STACK_DEPTH__))
#define _STACK_DEPTH_VERY_LARGE ((32 * (__OS_TASK_MIN_STACK_DEPTH__)) + (__OS_IRQ_STACK_DEPTH__))
#define _STACK_DEPTH_HUGE       ((64 * (__OS_TASK_MIN_STACK_DEPTH__)) + (__OS_IRQ_STACK_DEPTH__))
#define _STACK_DEPTH_VERY_HUGE  ((128 * (__OS_TASK_MIN_STACK_DEPTH__)) + (__OS_IRQ_STACK_DEPTH__))
#define _STACK_DEPTH_CUSTOM(depth) (depth))

/** spinlock initializer */
#define SYS_SPINLOCK_INIT   \
        {                   \
                .locked = 0 \
        }

/** default (normal) thread priority */
#define _PRIORITY_NORMAL 0

/** memory region flags */
#define _MM_FLAG__DMA    (1 << 0) /**< region is DMA capable */
#define _MM_FLAG__CACHE  (1 << 1) /**< region is cacheable */
#define _MM_FLAG__NOHEAP (1 << 2) /**< region is not used by heap allocator */

/** assertion (active when system assertions are enabled) */
#if (__OS_ENABLE_SYS_ASSERT__ > 0) || defined(DOXYGEN)
#define sys_assert(x)          sys_assert_hook(x, NULL)
#define sys_assert_msg(x, msg) sys_assert_hook(x, msg)
#else
#define sys_assert(x)          ((void)(x))
#define sys_assert_msg(x, msg) ((void)(x))
#endif

/*==============================================================================
  Exported object types
==============================================================================*/
/** mutex type */
enum kmtx_type {
        KMTX_TYPE_RECURSIVE, /**< recursive mutex */
        KMTX_TYPE_NORMAL     /**< normal mutex */
};

/** semaphore object (content is not accessible) */
typedef struct ksem ksem_t;

/** queue object (content is not accessible) */
typedef struct kqueue kqueue_t;

/** mutex object (content is not accessible) */
typedef struct kmtx kmtx_t;

/** flag object (content is not accessible) */
typedef struct kflag kflag_t;

/** file object (content is not accessible) */
typedef struct vfs_file kfile_t;

/**
 * Spinlock object. The object is a part of driver or file system data
 * (no allocation), initialized by zero (memory cleared by sys_zalloc())
 * or by @ref SYS_SPINLOCK_INIT.
 */
typedef struct {
        volatile u32_t locked; /**< lock state (used to detect recursive lock) */
} kspinlock_t;

/** file system interface (content is not accessible) */
struct vfs_FS_itf;

/** device info. Doxygen documentation in drivers/driver.h */
struct vfs_dev_stat {
        u64_t st_size;              /**< total size, in bytes */
        u32_t st_blksize;           /**< block size in bytes (block devices only) */
        u32_t st_rdbufalign;        /**< required read buffer alignment, power of 2 (block devices only) */
        u32_t st_wrbufalign;        /**< required write buffer alignment, power of 2 (block devices only) */
        const char **st_bufregions; /**< allowed memory regions of data buffer, NULL: any (block devices only) */
        bool st_bufdma;             /**< data buffer must be in DMA capable memory (block devices only) */
        u8_t st_major;              /**< device major number  */
        u8_t st_minor;              /**< device minor number  */
};

/** file write/read attributes. Doxygen documentation in fs/fs.h */
struct vfs_fattr {
        bool non_blocking_rd :1; /**< non-blocking file read access */
        bool non_strict_rd :1;   /**< non-strict file read access */
        bool non_blocking_wr :1; /**< non-blocking file write access */
};

/** directory type */
typedef struct vfs_dir {
        res_header_t header;
        void *FS_hdl;
        const struct vfs_FS_itf *FS_if;
        void *FS_entry;  //!< VFS mount entry that holds a reference
        void *d_hdl;     //!< Directory descriptor
        size_t d_items;  //!< number of items
        size_t d_seek;   //!< seek
        dirent_t dirent; //!< directory entry data
} kdir_t;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
#ifdef DOXYGEN /* Doxygen documentation only. Functions in fs.h and driver.h */
//==============================================================================
/**
 * @brief  Allocate memory.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param size             object size
 * @param mem              pointer to memory block pointer
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static inline int sys_malloc(size_t size, void **mem);

//==============================================================================
/**
 * @brief  Allocate memory and clear content.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param size             object size
 * @param mem              pointer to memory block pointer
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static inline int sys_zalloc(size_t size, void **mem);

//==============================================================================
/**
 * @brief  Free allocated memory.
 *
 * Function free selected memory block (by double pointer) and sets memory block
 * pointer to @ref NULL.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param mem           double pointer to memory block to free
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static inline int sys_free(void **mem);
#endif /* DOXYGEN */

//==============================================================================
/**
 * @brief Function opens file.
 *
 * The function opens the file whose name is the string pointed to by
 * <i>path</i> and associates a stream with it. The argument <i>mode</i> points
 * to a string beginning with one of the following sequences (possibly followed
 * by additional characters, as described below):<p>
 *
 * <b>r</b> - Open text file for reading. The stream is positioned at the
 * beginning of the file.<p>
 *
 * <b>r+</b> - Open for reading and writing. The stream is positioned at the
 * beginning of the file.<p>
 *
 * <b>w</b> - Truncate file to zero length or create text file for writing.
 * The stream is positioned at the beginning of the file.<p>
 *
 * <b>w+</b> - Open for reading and writing. The file is created if it does
 * not exist, otherwise it is truncated. The stream is positioned at the
 * beginning of the file.<p>
 *
 * <b>a</b> - Open for appending (writing at end of file). The file is
 * created if it does  not exist. The stream is positioned at the end of the
 * file.<p>
 *
 * <b>a+</b> - Open for reading and appending (writing at end of file). The
 * file is created if it does not exist. The initial file position for reading
 * is at the beginning of the file, but output is always appended to the end of
 * the file.<p>
 *
 * Pointer of opened stream is passed by <i>file</i> argument.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param path          path to file
 * @param flags         flags
 * @param mode          mode (O_CREAT)
 * @param file          pointer to file object
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/foo/bar", O_RDWR, &file) == ESUCC) {
               // ...

               sys_fclose(file);
        } else {
               // ...
        }

        // ...
   @endcode
 *
 * @see sys_fclose()
 */
//==============================================================================
extern int sys_fopen(const char *path, int flags, mode_t mode, kfile_t **file);

//==============================================================================
/**
 * @brief Function closes selected file.
 *
 * The function closes the created stream <i>file</i>.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param file          file to close
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/foo/bar", O_WRONLY | O_CREAT, 0666, &file) == ESUCC) {
               // ...

               sys_fclose(file);
        } else {
               // ...
        }

        // ...
   @endcode
 *
 * @see sys_fopen()
 */
//==============================================================================
extern int sys_fclose(kfile_t *file);

//==============================================================================
/**
 * @brief Function writes data to stream.
 *
 * The function writes <i>size</i> bytes long buffer, to the stream pointed to
 * by <i>file</i>, obtaining them from the location given by <i>ptr</i>.
 * Number of wrote bytes is passed by <i>wrcnt</i> argument.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param ptr           data buffer
 * @param size          buffer size
 * @param wrcnt         number of wrote bytes
 * @param file          stream
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/foo/bar", O_WRONLY | O_CREAT, 0666, &file) == ESUCC) {
               // ...

               const char buf[10] = {0,1,2,3,4,5,6,7,8,9};

               size_t wrcnt = 0;
               int err = sys_fwrite(buf, sizeof(buf), &wrcnt, file);

               // ...

               sys_fclose(file);
        } else {
               // ...
        }

        // ...
   @endcode
 *
 * @see sys_fread()
 */
//==============================================================================
extern int sys_fwrite(const void *ptr, size_t size, size_t *wrcnt, kfile_t *file);

//==============================================================================
/**
 * @brief Function reads data to stream.
 *
 * The function reads <i>size</i> bytes long buffer, from the stream pointed to
 * by <i>file</i>, storing them at the location given by <i>ptr</i>.
 * Number of read bytes is passed by <i>rdcnt</i> argument.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param ptr           data buffer
 * @param size          buffer size
 * @param rdcnt         number of read bytes
 * @param file          stream
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/foo/bar", O_WRONLY | O_CREAT, 0666, &file) == ESUCC) {
               // ...

               char buf[10];

               size_t rdcnt = 0;
               int err = sys_fread(buf, sizeof(buf), &rdcnt, file);

               // ...

               sys_fclose(file);
        } else {
               // ...
        }

        // ...
   @endcode
 *
 * @see sys_fwrite()
 */
//==============================================================================
extern int sys_fread(void *ptr, size_t size, size_t *rdcnt, kfile_t *file);

//==============================================================================
/**
 * @brief Function sets file position indicator.
 *
 * The function sets the file position indicator for the stream
 * pointed to by <i>file</i>. The new position, measured in bytes, is obtained
 * by adding offset bytes to the position specified by whence. If whence is set
 * to @ref SEEK_SET, @ref SEEK_CUR, or @ref SEEK_END, the offset is
 * relative to the start of the file, the current position indicator, or
 * end-of-file, respectively. A successful call to the sys_fseek() function
 * clears the end-of-file indicator for the stream.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param file          stream
 * @param offset        offset
 * @param mode          seek mode
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/foo/bar", O_WRONLY | O_CREAT, 0666, &file) == ESUCC) {
               // ...

               sys_fseek(file, 0, SEEK_SET);

               char buf[10];

               size_t rdcnt = 0;
               int err = sys_fread(buf, sizeof(buf), &rdcnt, file);

               // ...

               sys_fclose(file);
        } else {
               // ...
        }

        // ...
   @endcode
 *
 * @see sys_ftell()
 */
//==============================================================================
extern int sys_fseek(kfile_t *file, i64_t offset, int mode);

//==============================================================================
/**
 * @brief Function sends request to selected file to do non-standard operation.
 *
 * The function manipulates the file parameters. In particular, many
 * operating characteristics of character special files (e.g., drivers) may
 * be controlled with ioctl() requests.
 *
 * The second argument is a device-dependent request code. The third
 * argument is an untyped pointer to memory.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param file          stream to control
 * @param rq            request number (each driver has own requests)
 * @param ...           untyped pointer to memory (optional in some requests)
 *
 * @return One of @ref errno value.
 *
 * @b Example @b 1
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/dev/tty0", O_RDONLY, 0, &file) == ESUCC) {
                int err = sys_ioctl(file, IOCTL_TTY__CLEAR_SCR);

                // ...
        } else {
                // ...
        }

        sys_fclose(file);

        // ...

   @endcode

 * @b Example @b 2
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/dev/tty0", O_RDONLY, 0, &file) == ESUCC) {
                int row = -1;
                int err = sys_ioctl(file, IOCTL_TTY__GET_ROW, &row);

                // ...
        } else {
                // ...
        }

        sys_fclose(file);

        // ...

   @endcode
 *
 * @note
 * The names of all requests are constructed in the same way: @b
 IOCTL_<MODULE_NAME>__<REQUEST_NAME>.
 */
//==============================================================================
extern int sys_ioctl(kfile_t *file, int rq, ...);

//==============================================================================
/**
 * @brief Function gets file information.
 *
 * The function return information about a file pointed by <i>file</i>.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param file          file object
 * @param buf           file's information
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/dev/tty0", O_RDONLY, 0, &file) == ESUCC) {
                // ...

                struct stat buf;
                int err = sys_fstat(file, &buf);

                // ...
        } else {
                // ...
        }

        sys_fclose(file);

        // ...

   @endcode
 */
//==============================================================================
extern int sys_fstat(kfile_t *file, struct stat *buf);

//==============================================================================
/**
 * @brief Function forces write buffers to stream.
 *
 * For output streams, sys_fflush() forces a write of all buffered data for
 * the given output or update stream via the stream's underlying write function.
 * For input streams, sys_fflush() discards any buffered data that has been
 * fetched from the underlying file. The open status of the stream is unaffected.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param file          stream
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @b Example
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/foo/bar", O_WRONLY | O_CREAT, 0666, &file) == ESUCC) {
               // ...

               const char buf[10] = {0,1,2,3,4,5,6,7,8,9};

               size_t wrcnt = 0;
               int err = sys_fwrite(buf, sizeof(buf), &wrcnt, file);

               err = sys_fflush(file);

               // ...

               sys_fclose(file);
        } else {
               // ...
        }

        // ...
   @endcode
 */
//==============================================================================
extern int sys_fflush(kfile_t *file);

//==============================================================================
/**
 * @brief Function put message to kernel log.
 *
 * @note Function can be used only by file system or driver code.
 *
 * The function produce output according to a <i>format</i> as described below.
 * The function write output to <b>kernel log buffer</b>.
 *
 * <b>%</b> - The flag starts interpreting of formatting. After character can be type a
 * numbers which determine e.g. buffer length, number of digits, etc. When
 * next character is <i>%</i> then per cent is printed.
 *
 * The format string follows POSIX (C99) rules. A conversion specification is:
 * <b>%[flags][width][.precision][length]conversion</b>@n
 *   flags:      <b>-</b> left-justify, <b>+</b> always print sign, <b>space</b>
 *               prefix positive value with space, <b>#</b> alternative form,
 *               <b>0</b> pad with zeros@n
 *   width:      decimal number or <b>*</b> (taken from argument, negative value
 *               means <b>-</b> flag)@n
 *   precision:  <b>.</b> followed by decimal number or <b>*</b>@n
 *   length:     hh, h, l, ll, j, z, t, L@n
 *   conversion: d, i, o, u, x, X, f, F, e, E, g, G, c, s, p, %@n
 *
 *   @code printf("[%-*s|%+5d|%08.3f|%#x|%.2e]", 6, "ab", 42, 3.14159, 255, 1234.5);
 *   //=> [ab    |  +42|0003.142|0xff|1.23e+03] @endcode
 *
 * Not supported (to keep kernel small): %a, %A, %ls, %n and positional
 * arguments (%n$). Such conversion specification is printed as is and its
 * argument is skipped. %lc is printed as %c. Floating point values from about
 * 1e-6 to 1e38 are correctly rounded up to 17 significant digits, other values
 * up to about 15 digits.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param format        formatting string
 * @param ...           argument sequence
 *
 * @return None
 *
 * @b Example
 * @code
        // ...

        int foo = 12;
        int bar = 0x12;
        printk("foo is %d; bar is 0x%x\n", foo, bar);

        // ...
   @endcode
 *
 * @see sys_strfmt()
 */
//==============================================================================
#if (__OS_SYSTEM_MSG_ENABLE__ > 0) || defined(DOXYGEN)
extern void printk(const char *format, ...);
#else
#define printk(...)
#endif

//==============================================================================
/**
 * @brief Function prints message according to format to buffer.
 *
 * The function produce output according to a <i>format</i> as described below.
 * The function write output pointed to by <i>bfr</i> of size <i>size</i>.<p>
 *
 * The format string follows POSIX (C99) rules. A conversion specification is:
 * <b>%[flags][width][.precision][length]conversion</b>@n
 *   flags:      <b>-</b> left-justify, <b>+</b> always print sign, <b>space</b>
 *               prefix positive value with space, <b>#</b> alternative form,
 *               <b>0</b> pad with zeros@n
 *   width:      decimal number or <b>*</b> (taken from argument, negative value
 *               means <b>-</b> flag)@n
 *   precision:  <b>.</b> followed by decimal number or <b>*</b>@n
 *   length:     hh, h, l, ll, j, z, t, L@n
 *   conversion: d, i, o, u, x, X, f, F, e, E, g, G, c, s, p, %@n
 *
 *   @code printf("[%-*s|%+5d|%08.3f|%#x|%.2e]", 6, "ab", 42, 3.14159, 255, 1234.5);
 *   //=> [ab    |  +42|0003.142|0xff|1.23e+03] @endcode
 *
 * Not supported (to keep kernel small): %a, %A, %ls, %n and positional
 * arguments (%n$). Such conversion specification is printed as is and its
 * argument is skipped. %lc is printed as %c. Floating point values from about
 * 1e-6 to 1e38 are correctly rounded up to 17 significant digits, other values
 * up to about 15 digits.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param bfr           buffer where output was produced
 * @param size          buffer size
 * @param format        formatting string
 * @param ...           argument sequence
 *
 * @return Number of characters written to the buffer, excluding the terminating
 * null byte. The output is truncated if it does not fit to the buffer, so the
 * value is always in range 0..size-1 (0 if size is 0). This is different than
 * POSIX snprintf(), the function works as scnprintf() in Linux kernel.
 *
 * @b Example
 * @code
        // ...

        char buffer[20];
        int foo = 12;
        int bar = 0x12;
        sys_strfmt(buffer, 20, "foo is %d; bar is 0x%x\n", foo, bar);

        // ...
   @endcode
 *
 * @see sys_sscanf()
 */
//==============================================================================
extern int sys_strfmt(char *bfr, size_t size, const char *format, ...);

//==============================================================================
/**
 * @brief Function scans input according to format.
 *
 * The function scans input according to format as described below. This format
 * may contain conversion specifications; the results from such conversions,
 * if any, are stored in the locations pointed to by the pointer arguments that
 * follow format. Each pointer argument must be of a type that is appropriate
 * for the value returned by the corresponding conversion specification.<p>
 *
 * <b>%</b> - The flag starts interpreting of formatting. After character can be type a
 * numbers which determine e.g. buffer length, number of digits, etc.<p>
 *
 * <b>c</b> - Scans single character.<p>
 *
 * <b>s</b> - Scans string.<p>
 *
 * <b>d, i</b> - Scans signed <i>int</i> type number.<p>
 *
 * <b>u</b> - Scans unsigned type number.<p>
 *
 * <b>x, X</b> - Scans value in HEX formatting.<p>
 *
 * <b>o</b> - Scans value in Octal formatting.<p>
 *
 * <b>f, F, g, G</b> - Scans float value.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param str           input string (must be <i>null</i> terminated)
 * @param format        formatting string
 * @param ...           argument sequence list
 *
 * @return The function return the number of input items successfully matched
 * and assigned, which can be fewer than provided for, or even zero in the event
 * of an early matching failure.<p>
 *
 * The value @ref EOF is returned if the end of input is reached before either
 * the first successful conversion or a matching failure occurs. @ref EOF is
 * also returned if a read error occurs, in which case the error indicator for
 * the stream is set.
 *
 * @b Example
 * @code
        // ...

        char *buffer = "12, 1256";
        int foo, bar;
        sys_sscanf(buffer, "%i, %i", &foo, &bar);

        // ...
   @endcode
 *
 * @see sys_strfmt()
 */
//==============================================================================
extern int sys_sscanf(const char *str, const char *format, ...);

//==============================================================================
/**
 * @brief Function return OS time in milliseconds.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @return OS time in milliseconds.
 *
 */
//==============================================================================
extern clock_t sys_get_uptime_ms(void);

//==============================================================================
/**
 * @brief Check if timer expired.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param time_ref      time reference
 * @param time          time to check
 *
 * @return If timer expired then @b true is returned, otherwise @b false.
 *
 * @b Example
 * @code
        // ...

        clock_t tref = sys_get_uptime_ms();

        while (!sys_is_time_expired(tref, 2000)) {
                // ...
        }

        // ...
   @endcode
 *
 * @see sys_get_uptime_ms(), sys_is_time_expired()
 */
//==============================================================================
static inline bool sys_is_time_expired(clock_t time_ref, u32_t time)
{
        return (sys_get_uptime_ms() - time_ref >= time);
}

//==============================================================================
/**
 * @brief Function creates binary semaphore.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param cnt_max          max count value (1 for binary)
 * @param cnt_init         initial value (0 or 1 for binary)
 * @param sem              created semaphore handle
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        ksem_t *sem = NULL;
        if (sys_semaphore_create(1, 0, &sem) == ESUCC) {

                // ...

                sys_semaphore_destroy(sem);
        }

        // ...
   @endcode
 *
 * @see sys_semaphore_destroy()
 */
//==============================================================================
extern int sys_semaphore_create(const size_t cnt_max, const size_t cnt_init, ksem_t **sem);

//==============================================================================
/**
 * @brief Function deletes semaphore.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param sem       semaphore object
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        ksem_t *sem = NULL;
        if (sys_semaphore_create(1, 0, &sem) == ESUCC) {

                // ...

                int err = sys_semaphore_destroy(sem);
        }

        // ...
   @endcode
 *
 * @see sys_semaphore_create()
 */
//==============================================================================
extern int sys_semaphore_destroy(ksem_t *sem);

//==============================================================================
/**
 * @brief Function wait for semaphore.
 *
 * The function waits for semaphore signal pointed by
 * <i>sem</i> by <i>timeout</i> milliseconds. If semaphore was signaled then
 * ESUCC is returned, otherwise (timeout) ETIME. When <i>timeout</i>
 * value is set to 0 then semaphore is polling without timeout.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param sem           semaphore object pointer
 * @param timeout       timeout value in milliseconds
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        void thread2(void *arg)
        {
                while (true) {
                        // this task will wait for semaphore signal
                        sys_semaphore_wait(sem, _WAIT_FOREVER_MS);

                        // ...
                }

                // ...
        }

        void thread1(void *arg)
        {
                while (true) {
                       // ...

                       // this task signal to thread2 that can execute part of code
                       sys_semaphore_signal(sem);
                }

                // ...
        }

        // ...

   @endcode
 *
 * @see sys_semaphore_signal()
 */
//==============================================================================
extern int sys_semaphore_wait(ksem_t *sem, const u32_t timeout);

//==============================================================================
/**
 * @brief Function signal semaphore.
 *
 * The function signals semaphore pointed by <i>sem</i>.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param sem           semaphore object pointer
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        void thread2(void *arg)
        {
                while (true) {
                        // this task will wait for semaphore signal
                        sys_semaphore_wait(sem, _WAIT_FOREVER_MS);

                        // ...
                }

                // ...
        }

        void thread1(void *arg)
        {
                while (true) {
                       // ...

                       // this task signal to thread2 that can execute part of code
                       sys_semaphore_signal(sem);
                }

                // ...
        }

        // ...

   @endcode
 *
 * @see sys_semaphore_wait()
 */
//==============================================================================
extern int sys_semaphore_signal(ksem_t *sem);

//==============================================================================
/**
 * @brief Function wait for semaphore from ISR.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param sem              semaphore object
 * @param task_woken       true if higher priority task woken, otherwise false (can be
 @ref NULL)
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        void ISR(void)
        {
                bool woken = false;
                if (sys_semaphore_wait_from_ISR(sem, &woken) == ESUCC) {
                        // ...
                } else {
                        // ...
                }

                sys_thread_yield_from_ISR(woken);
        }

        // ...

   @endcode
 *
 * @see sys_semaphore_signal_from_ISR()
 */
//==============================================================================
extern int sys_semaphore_wait_from_ISR(ksem_t *sem, bool *task_woken);

//==============================================================================
/**
 * @brief Function signal semaphore from ISR.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param sem              semaphore object
 * @param task_woken       true if higher priority task woken, otherwise false (can be
 @ref NULL)
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        void thread(void *arg)
        {
                while (true) {
                        // this task will wait for semaphore signal
                        sys_semaphore_wait(sem, _WAIT_FOREVER_MS);

                        // ...
                }

                // ...
        }

        void ISR(void)
        {
               // ...

               bool woken = false;
               sys_semaphore_signal_from_ISR(sem, &woken);

               // ...

               if (woken) {
                       sys_thread_yield_from_ISR();
               }
        }

        // ...

   @endcode
 *
 * @see sys_semaphore_wait_from_ISR()
 */
//==============================================================================
extern int sys_semaphore_signal_from_ISR(ksem_t *sem, bool *task_woken);

//==============================================================================
/**
 * @brief Function creates flags. Flag is similar to semaphore except single
 *        object contains more than one synchronization point.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param flag            created flag handle
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        flag_t *flag = NULL;
        if (sys_flag_create(&flag) == ESUCC) {

                // ...

                sys_flag_destroy(flag);
        }

        // ...
   @endcode
 *
 * @see sys_flag_destroy()
 */
//==============================================================================
extern int sys_flag_create(kflag_t **flag);

//==============================================================================
/**
 * @brief Function deletes flag.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param flag      flag object
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        flag_t *flag = NULL;
        if (sys_flag_create(&flag) == ESUCC) {

                // ...

                sys_flag_destroy(flag);
        }

        // ...
   @endcode
 *
 * @see sys_flag_create()
 */
//==============================================================================
extern int sys_flag_destroy(kflag_t *flag);

//==============================================================================
/**
 * @brief Function wait for selected flags.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param flag          flag object
 * @param bits          bits to wait for (logical AND)
 * @param blocktime_ms  timeout in milliseconds
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        flag_t *flag = NULL;
        if (sys_flag_create(&flag) == ESUCC) {

                // ...

                sys_flag_wait(flag, (1<<0), 100);

                // ...

                sys_flag_destroy(flag);
        }

        // ...

        ISR()
        {
                sys_flag_set_from_ISR(flag, (1<<0));
        }
   @endcode
 *
 * @see sys_flag_set(), sys_flag_clear()
 */
//==============================================================================
extern int sys_flag_wait(kflag_t *flag, u32_t bits, const u32_t blocktime_ms);

//==============================================================================
/**
 * @brief Function set selected flags.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param flag          flag object
 * @param bits          bits to set
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        flag_t *flag = NULL;
        if (sys_flag_create(&flag) == ESUCC) {

                // ...

                sys_flag_wait(flag, (1<<0), 100);

                // ...

                sys_flag_destroy(flag);
        }

        // ...

        ISR()
        {
                sys_flag_set_from_ISR(flag, (1<<0));
        }
   @endcode
 *
 * @see sys_flag_clear()
 */
//==============================================================================
extern int sys_flag_set(kflag_t *flag, u32_t bits);

//==============================================================================
/**
 * @brief Function clear selected flags.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param flag          flag object
 * @param bits          bits to clear
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        flag_t *flag = NULL;
        if (sys_flag_create(&flag) == ESUCC) {

                // ...

                sys_flag_wait(flag, (1<<0), 100);

                // ...

                sys_flag_destroy(flag);
        }

        // ...

        ISR()
        {
                sys_flag_set_from_ISR(flag, (1<<0));
        }
   @endcode
 *
 * @see sys_flag_set()
 */
//==============================================================================
extern int sys_flag_clear(kflag_t *flag, u32_t bits);

//==============================================================================
/**
 * @brief Function create new mutex.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param type     mutex type
 * @param mtx      created mutex handle
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        // resource that be accessed from many threads
        int resource;


        // create mutex instance
        kmtx_t *mtx = NULL;
        int      err = sys_mutex_create(KMTX_TYPE_NORMAL, &mtx);
        if (err != ESUCC) {
                return err;
        }

        // ...


        void thread1()
        {
                // protected access to resource
                if (sys_mutex_lock(mtx, _WAIT_FOREVER_MS) == ESUCC) {
                        // write to buffer is allowed
                        resource = ...;

                        // ...

                        sys_mutex_unlock(mtx);
                }
        }

        void thread2()
        {
                // protected access to resource
                if (sys_mutex_lock(mtx, _WAIT_FOREVER_MS) == ESUCC) {
                        // write to buffer is allowed
                        resource = ...;

                        // ...

                        sys_mutex_unlock(mtx);
                }
        }


        // ...

        // destroy created mutex (if not used anymore)
        if (sys_mutex_destroy(mtx) == ESUCC) {
                // ...
        } else {
                // ...
        }

        // ...

   @endcode
 *
 * @see sys_mutex_destroy()
 */
//==============================================================================
extern int sys_mutex_create(enum kmtx_type type, kmtx_t **mtx);

//==============================================================================
/**
 * @brief Function destroy mutex.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param mutex    mutex object
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        // resource that be accessed from many threads
        int resource;


        // create mutex instance
        kmtx_t *mtx = NULL;
        int      err = sys_mutex_create(KMTX_TYPE_NORMAL, &mtx);
        if (err != ESUCC) {
                return err;
        }

        // ...


        void thread1()
        {
                // protected access to resource
                if (sys_mutex_lock(mtx, _WAIT_FOREVER_MS) == ESUCC) {
                        // write to buffer is allowed
                        resource = ...;

                        // ...

                        sys_mutex_unlock(mtx);
                }
        }

        void thread2()
        {
                // protected access to resource
                if (sys_mutex_lock(mtx, _WAIT_FOREVER_MS) == ESUCC) {
                        // write to buffer is allowed
                        resource = ...;

                        // ...

                        sys_mutex_unlock(mtx);
                }
        }


        // ...

        // destroy created mutex (if not used anymore)
        if (sys_mutex_destroy(mtx) == ESUCC) {
                // ...
        } else {
                // ...
        }

        // ...

   @endcode
 *
 * @see sys_mutex_create()
 */
//==============================================================================
extern int sys_mutex_destroy(kmtx_t *mutex);

//==============================================================================
/**
 * @brief Function lock mutex.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param mutex             mutex object
 * @param timeout           polling time
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        // resource that be accessed from many threads
        int resource;


        // create mutex instance
        kmtx_t *mtx = NULL;
        int      err = sys_mutex_create(KMTX_TYPE_NORMAL, &mtx);
        if (err != ESUCC) {
                return err;
        }

        // ...


        void thread1()
        {
                // protected access to resource
                if (sys_mutex_lock(mtx, _WAIT_FOREVER_MS) == ESUCC) {
                        // write to buffer is allowed
                        resource = ...;

                        // ...

                        sys_mutex_unlock(mtx);
                }
        }

        void thread2()
        {
                // protected access to resource
                if (sys_mutex_lock(mtx, _WAIT_FOREVER_MS) == ESUCC) {
                        // write to buffer is allowed
                        resource = ...;

                        // ...

                        sys_mutex_unlock(mtx);
                }
        }


        // ...

        // destroy created mutex (if not used anymore)
        if (sys_mutex_destroy(mtx) == ESUCC) {
                // ...
        } else {
                // ...
        }

        // ...

   @endcode
 *
 * @see sys_mutex_trylock(), sys_mutex_unlock()
 */
//==============================================================================
extern int sys_mutex_lock(kmtx_t *mutex, const u32_t timeout);

//==============================================================================
/**
 * @brief Function try lock mutex.
 *
 * The function try to lock mutex. If mutex is free then is immediately locked,
 * if not then error is returned. Function is equivalent to:
 * sys_mutex_lock(mtx, 0) call.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param mutex             mutex object
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        // resource that be accessed from many threads
        int resource;


        // create mutex instance
        kmtx_t *mtx = NULL;
        int      err = sys_mutex_create(KMTX_TYPE_NORMAL, &mtx);
        if (err != ESUCC) {
                return err;
        }

        // ...


        void thread1()
        {
                // protected access to resource
                if (sys_mutex_trylock(mtx) == ESUCC) {
                        // write to buffer is allowed
                        resource = ...;

                        // ...

                        sys_mutex_unlock(mtx);
                }
        }

        void thread2()
        {
                // protected access to resource
                if (sys_mutex_trylock(mtx) == ESUCC) {
                        // write to buffer is allowed
                        resource = ...;

                        // ...

                        sys_mutex_unlock(mtx);
                }
        }


        // ...

        // destroy created mutex (if not used anymore)
        if (sys_mutex_destroy(mtx) == ESUCC) {
                // ...
        } else {
                // ...
        }

        // ...

   @endcode
 *
 * @see sys_mutex_lock(), sys_mutex_unlock()
 */
//==============================================================================
extern int sys_mutex_trylock(kmtx_t *mutex);

//==============================================================================
/**
 * @brief Function unlock mutex.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param *mutex            mutex object
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        // resource that be accessed from many threads
        int resource;


        // create mutex instance
        kmtx_t *mtx = NULL;
        int      err = sys_mutex_create(KMTX_TYPE_NORMAL, &mtx);
        if (err != ESUCC) {
                return err;
        }

        // ...


        void thread1()
        {
                // protected access to resource
                if (sys_mutex_lock(mtx, _WAIT_FOREVER_MS) == ESUCC) {
                        // write to buffer is allowed
                        resource = ...;

                        // ...

                        sys_mutex_unlock(mtx);
                }
        }

        void thread2()
        {
                // protected access to resource
                if (sys_mutex_lock(mtx, _WAIT_FOREVER_MS) == ESUCC) {
                        // write to buffer is allowed
                        resource = ...;

                        // ...

                        sys_mutex_unlock(mtx);
                }
        }


        // ...

        // destroy created mutex (if not used anymore)
        if (sys_mutex_destroy(mtx) == ESUCC) {
                // ...
        } else {
                // ...
        }

        // ...

   @endcode
 *
 * @see sys_mutex_trylock(), sys_mutex_lock()
 */
//==============================================================================
extern int sys_mutex_unlock(kmtx_t *mutex);

//==============================================================================
/**
 * @brief Function create new queue.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param length           queue length
 * @param item_size        queue item size
 * @param queue            created queue
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int sys_queue_create(const size_t length, const size_t item_size, kqueue_t **queue);

//==============================================================================
/**
 * @brief Function delete queue.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param *queue            queue object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int sys_queue_destroy(kqueue_t *queue);

//==============================================================================
/**
 * @brief Function reset queue.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param *queue            queue object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int sys_queue_reset(kqueue_t *queue);

//==============================================================================
/**
 * @brief Function send queue.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param queue            queue object
 * @param item             item
 * @param waittime_ms      wait time
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int sys_queue_send(kqueue_t *queue, const void *item, const u32_t waittime_ms);

//==============================================================================
/**
 * @brief Function send queue.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param *queue            queue object
 * @param *item             item
 * @param *task_woken       true if higher priority task woken, otherwise false (can be
 * @ref NULL)
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int sys_queue_send_from_ISR(kqueue_t *queue, const void *item, bool *task_woken);

//==============================================================================
/**
 * @brief Function send queue.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param queue            queue object
 * @param item             item
 * @param waittime_ms      wait time
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int sys_queue_receive(kqueue_t *queue, void *item, const u32_t waittime_ms);

//==============================================================================
/**
 * @brief Function receive queue from ISR.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param queue            queue object
 * @param item             item
 * @param task_woken       true if higher priority task woke, otherwise false (can be @ref
 * NULL)
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int sys_queue_receive_from_ISR(kqueue_t *queue, void *item, bool *task_woken);

//==============================================================================
/**
 * @brief Function gets number of items in queue.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param queue            queue object
 * @param items            number of items in queue
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int sys_queue_get_number_of_items(kqueue_t *queue, size_t *items);

//==============================================================================
/**
 * @brief Function gets number of items in queue from ISR.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param queue            queue object
 * @param items            number of items in queue
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int sys_queue_get_number_of_items_from_ISR(kqueue_t *queue, size_t *items);

//==============================================================================
/**
 * @brief Function create new thread (task), and if enabled, add to monitor list.
 *
 * Function by default allocate memory for task data (localized in task tag)
 * which is used to CPU load calculation/ standard IO, and etc.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param func             thread code
 * @param attr             thread attributes
 * @param arg              thread argument
 * @param tid              thread ID
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int sys_thread_create(thread_func_t func, const _thread_attr_t *attr, void *arg, tid_t *tid);

//==============================================================================
/**
 * @brief Function delete thread.
 *
 * Function clear all allocated resources by thread and remove it from execution
 * list.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param tid        thread ID
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int sys_thread_destroy(tid_t tid);

//==============================================================================
/**
 * @brief  Function checks if selected thread handle is valid.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  tid       thread ID
 *
 * @return If thread object is valid the @b true is returned otherwise @b false.
 */
//==============================================================================
static inline bool sys_thread_is_valid(tid_t tid)
{
        return ((tid >= 1) && (tid < __OS_PROCESS_MAX_THREADS__));
}

//==============================================================================
/**
 * @brief Function yield thread.
 *
 * @note Function can be used only by file system or driver code.
 */
//==============================================================================
extern void sys_thread_yield(void);

//==============================================================================
/**
 * @brief Function yield thread from ISR.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param yield         thread yield
 */
//==============================================================================
extern void sys_thread_yield_from_ISR(bool yield);

//==============================================================================
/**
 * @brief Function enter to critical section.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @see sys_critical_section_end()
 */
//==============================================================================
extern void sys_critical_section_begin(void);

//==============================================================================
/**
 * @brief Function exit from critical section.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @see sys_critical_section_begin()
 */
//==============================================================================
extern void sys_critical_section_end(void);

//==============================================================================
/**
 * @brief Function put to sleep thread for microseconds.
 *
 * @note Function can sleep longer that declared because of context switch
 *       settings.
 *
 * @note Function work in critical section.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param microseconds          number of microseconds of sleep
 *
 * @see sys_sleep()
 */
//==============================================================================
extern void sys_sleep_us(const u32_t microseconds);

//==============================================================================
/**
 * @brief Function put to sleep thread for milliseconds.
 *
 * @note Function can sleep longer that declared because of context switch
 *       settings. Context switch can be longer than 1ms.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param milliseconds          number of milliseconds of sleep
 *
 * @see sys_sleep()
 */
//==============================================================================
extern void sys_sleep_ms(const u32_t milliseconds);

//==============================================================================
/**
 * @brief  Convert tm structure to time_t.
 *
 * This function performs the reverse translation that localtime does.
 * The values of the members tm_wday and tm_yday of timeptr are ignored, and
 * the values of the other members are interpreted even if out of their valid
 * ranges (see struct tm). For example, tm_mday may contain values above 31,
 * which are interpreted accordingly as the days that follow the last day of
 * the selected month.
 * A call to this function automatically adjusts the values of the members of
 * timeptr if they are off-range or -in the case of tm_wday and tm_yday- if they
 * have values that do not match the date described by the other members.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  timeptr      Pointer to a tm structure that contains a calendar time
 *                      broken down into its components (see struct tm)
 *
 * @return A time_t value corresponding to the calendar time passed as argument.
 *         If the calendar time cannot be represented, a value of -1 is returned.
 *
 * @see sys_gettime()
 */
//==============================================================================
extern time_t sys_mktime(struct tm *timeptr);

//==============================================================================
/**
 * @brief  Convert time_t to tm as UTC time.
 *
 * Uses the value pointed by timer to fill a tm structure with the values that
 * represent the corresponding time, expressed as a UTC time (i.e., the time
 * at the GMT timezone).
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param timer    Pointer to an object of type time_t that contains a time value.
 *                      time_t is an alias of a fundamental arithmetic type
 *                      capable of representing times as returned by function time.
 *
 * @param tm       Pointer to an object of type struct tm that will contains
 *                      converted timer value to time structure.
 *
 * @return A pointer to a tm structure with its members filled with the values
 *         that correspond to the UTC time representation of timer.
 *
 */
//==============================================================================
extern struct tm *sys_gmtime_r(const time_t *timer, struct tm *tm);

//==============================================================================
/**
 * @brief  Function return integer value from given configuration.
 *
 * Function found expression: VAR=VAL. If variable is not found then <i>defval</i>
 * is returned.
 *
 * @param opts          options to analyze
 * @param var           variable to search
 * @param defval        value used if variable does not found
 *
 * @return Integer (on error or when variable does not found the default value).
 */
//==============================================================================
extern int sys_opt_get_int(const char *opts, const char *var, int defval);

//==============================================================================
/**
 * @brief  Function return string from given configuration.
 *
 * Function found expression: VAR=STR.
 *
 * @param opts          options to analyze
 * @param var           variable to search
 * @param len           string length
 *
 * @return On success string pointer and length passed by len pointer, otherwise NULL.
 */
//==============================================================================
extern const char *sys_opt_get_str_ref(const char *opts, const char *var, size_t *len);

//==============================================================================
/**
 * @brief  Function copy string from option.
 *
 * Function found expression: VAR=STR.
 *
 * @param opts          options to analyze
 * @param var           variable to search
 * @param buf           destination buffer
 * @param buflen        destination buffer length
 *
 * @return On success string pointer and length passed by len pointer, otherwise NULL.
 */
//==============================================================================
extern size_t sys_opt_get_str_copy(const char *opts, const char *var, char *buf, size_t buflen);

//==============================================================================
/**
 * @brief  Function return integer value from given configuration.
 *
 * Function found expression: VAR=BOOL(false, true, on, off, enable, disable, yes, no).
 * If none of given values is found, then function return <i>defval</i> defined
 * by user.
 *
 * @param opts          options to analyze
 * @param var           variable to search
 * @param defval        default value
 *
 * @return On success return 1 or 0
 */
//==============================================================================
extern int sys_opt_get_bool(const char *opts, const char *var, int defval);

//==============================================================================
/**
 * @brief  Function check if flag exist.
 *
 * @param opts          options to analyze
 * @param flag          flag to find
 *
 * @return On success return true, otherwise false.
 */
//==============================================================================
extern bool sys_opt_has_flag(const char *opts, const char *flag);

//==============================================================================
/**
 * @brief  Function return enumeration value from given configuration.
 *
 * Function found expression: VAR=NAME or VAR=NUMBER, e.g. "parity=even".
 *
 * @param opts          options to analyze
 * @param var           variable to search
 * @param names         names of enumeration values (index is value)
 * @param count         number of names
 * @param defval        value used if variable does not found
 *
 * @return Enumeration value, <i>defval</i> if variable does not exist, -1 if
 *         value is not correct.
 */
//==============================================================================
extern int sys_opt_get_enum(const char *opts, const char *var, const char *const names[], size_t count, int defval);

//==============================================================================
/**
 * @brief Function returns a pseudo-random integer.
 *
 * The rand() function returns a pseudo-random integer in the range 0 to
 * @ref RAND_MAX inclusive (i.e., the mathematical range [0, RAND_MAX]).
 *
 * @param  seed
 *
 * @return The rand() function return a value between @b 0 and @ref RAND_MAX (inclusive).
 *
 * @b Example
 * @code
        // ...
        unsigned int seed = 213;
        int foo = rand_r(&seed);
        // ...
   @endcode
 *
 * @see RAND_MAX
 */
//==============================================================================
extern u32_t sys_rand_r(unsigned int *seed);

//==============================================================================
/**
 * @brief Function returns file position indicator.
 *
 * The function obtains the current value of the file position
 * indicator pointed by <i>lseek</i> for the stream pointed to by <i>file</i>.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param file          stream
 * @param lseek         file position indicator
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/foo/bar", O_WRONLY | O_CREAT, 0666, &file) == ESUCC) {
               // ...

               i64_t seek = 0;
               int err = sys_ftell(file, &seek);

               if (err == ESUCC && seek > 0) {
                       sys_fseek(file, 0, SEEK_SET);
               }

               char   buf[10];
               size_t rdcnt = 0;
               err = sys_fread(buf, sizeof(buf), &rdcnt, file);

               // ...

               sys_fclose(file);
        } else {
               // ...
        }

        // ...
   @endcode
 *
 * @see sys_fseek()
 */
//==============================================================================
extern int sys_ftell(kfile_t *file, i64_t *lseek);

//==============================================================================
/**
 * @brief Function clears end-of-file and error indicators.
 *
 * The function clears the end-of-file and error indicators
 * for the stream pointed to by <i>file</i>.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param file          stream
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/foo/bar", O_WRONLY | O_CREAT, 0666, &file) == ESUCC) {
               // file operations...

               int error = 0;
               if (sys_ferror(file) == ESUCC && error) {
                       sys_clearerr();
               }

               // file operations...

               sys_fclose(file);
        }

        // ...
   @endcode
 *
 * @see sys_ferror()
 */
//==============================================================================
extern int sys_clearerr(kfile_t *file);

//==============================================================================
/**
 * @brief Function tests error indicator.
 *
 * The function tests the error indicator for the stream pointed
 * to by <i>file</i>, returning nonzero if it is set.  The error indicator can
 * be reset only by the sys_clearerr() function.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param file          stream
 * @param error         pointer to error indicator
 *
 * @return One of @ref errno value.
 *
 * @b Example
 * @code
        // ...

        FILE *file = NULL;
        if (sys_fopen("/foo/bar", O_WRONLY | O_CREAT, 0666, &file) == ESUCC) {
               // file operations...

               int error = 0;
               if (sys_ferror(file) == ESUCC && error) {
                       sys_clearerr();
               }

               // file operations...

               sys_fclose(file);
        }

        // ...
   @endcode
 *
 * @see sys_clearerr()
 */
//==============================================================================
extern int sys_ferror(kfile_t *file, int *error);

//==============================================================================
/**
 * @brief  Get current time.
 *
 * The function returns this value, and if the argument is not a @ref NULL pointer,
 * it also sets this value to the object pointed by timer.
 * The value returned generally represents the number of seconds since 00:00
 * hours, Jan 1, 1970 UTC (i.e., the current unix timestamp). Although libraries
 * may use a different representation of time: Portable programs should not use
 * the value returned by this function directly, but always rely on calls to
 * other elements of the standard library to translate them to portable types
 * (such as localtime, gmtime or difftime).
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param  timer        Pointer to an object of type time_t, where the time
 *                      value is stored.
 *                      Alternatively, this parameter can be a @ref NULL pointer,
 *                      in which case the parameter is not used (the function
 *                      still returns a value of type time_t with the result).
 *
 * @return One of @ref errno value.
 *
 */
//==============================================================================
extern int sys_gettime(time_t *timer);

//==============================================================================
/**
 * @brief  Function return free memory in bytes.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @return Free memory value.
 *
 * @see sys_get_mem_size()
 */
//==============================================================================
extern size_t sys_get_free_mem(void);

//==============================================================================
/**
 * @brief  Function return memory size (RAM) in bytes.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @return Memory size.
 *
 * @see sys_get_free_mem()
 */
//==============================================================================
extern size_t sys_get_mem_size(void);

#if (__OS_ENABLE_SYS_ASSERT__ > 0) || defined(DOXYGEN)
//==============================================================================
/**
 * @brief Assertion hook: when <i>assert</i> is false, the system reports the
 *        failure (kernel panic). Use sys_assert() and sys_assert_msg() macros.
 *
 * @param assert        assertion result
 * @param msg           message (can be @ref NULL)
 */
//==============================================================================
extern void sys_assert_hook(bool assert, const char *msg);
#endif

//==============================================================================
/**
 * @brief Function locks the scheduler (context switch).
 *
 * Interrupts stay enabled, but no other thread is executed until
 * sys_scheduler_unlock() is called. Calls can be nested.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @see sys_scheduler_unlock()
 */
//==============================================================================
extern void sys_scheduler_lock(void);

//==============================================================================
/**
 * @brief Function unlocks the scheduler (context switch).
 *
 * @note Function can be used only by file system or driver code.
 *
 * @see sys_scheduler_lock()
 */
//==============================================================================
extern void sys_scheduler_unlock(void);

//==============================================================================
/**
 * @brief Function locks spinlock.
 *
 * Spinlock protects short code that does not wait (no sleep, no mutex,
 * semaphore or queue wait, no device I/O). The thread that holds the spinlock
 * is not preempted (scheduler is locked), interrupts stay enabled. Use the
 * spinlock instead of mutex when data is changed in a few instructions.
 * On multi-core CPU (RP2040) the interrupts are masked as well and the lock
 * is shared by all cores. Spinlock cannot be locked recursively and cannot
 * be used in interrupts.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param spinlock      spinlock object
 *
 * @b Example
 * @code
        // ...

        sys_spinlock_lock(&hdl->lock);
        hdl->counter++;
        sys_spinlock_unlock(&hdl->lock);

        // ...
   @endcode
 *
 * @see sys_spinlock_unlock(), sys_spinlock_lock_irq()
 */
//==============================================================================
extern void sys_spinlock_lock(kspinlock_t *spinlock);

//==============================================================================
/**
 * @brief Function unlocks spinlock locked by sys_spinlock_lock().
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param spinlock      spinlock object
 *
 * @see sys_spinlock_lock()
 */
//==============================================================================
extern void sys_spinlock_unlock(kspinlock_t *spinlock);

//==============================================================================
/**
 * @brief Function locks spinlock and masks interrupts.
 *
 * Variant of sys_spinlock_lock() used when data is shared with interrupt.
 * The interrupt (on single-core CPU) does not lock the spinlock, because it
 * cannot be interrupted by thread code.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param spinlock      spinlock object
 *
 * @see sys_spinlock_unlock_irq(), sys_spinlock_lock()
 */
//==============================================================================
extern void sys_spinlock_lock_irq(kspinlock_t *spinlock);

//==============================================================================
/**
 * @brief Function unlocks spinlock locked by sys_spinlock_lock_irq().
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param spinlock      spinlock object
 *
 * @see sys_spinlock_lock_irq()
 */
//==============================================================================
extern void sys_spinlock_unlock_irq(kspinlock_t *spinlock);

#if (_CPUCTL_CACHE_ALIGN > 1) || defined(DOXYGEN)
//==============================================================================
/**
 * @brief Function cleans data cache (writes cached data back to memory).
 *
 * Function shall be called before DMA reads memory written by CPU.
 * On CPUs without data cache the function does nothing.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param addr      memory address (@ref NULL: whole data cache)
 * @param size      memory size in bytes (ignored when <i>addr</i> is @ref NULL)
 *
 * @see sys_dcache_invalidate(), sys_dcache_flush(), sys_cache_sync_code()
 */
//==============================================================================
extern void sys_dcache_clean(void *addr, size_t size);

//==============================================================================
/**
 * @brief Function invalidates data cache (discards cached data).
 *
 * Function shall be called after DMA wrote memory that is read by CPU.
 * The memory should be aligned to @ref _CPUCTL_CACHE_ALIGN, otherwise
 * data of other objects in the same cache line are lost.
 * On CPUs without data cache the function does nothing.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param addr      memory address
 * @param size      memory size in bytes
 *
 * @see sys_dcache_clean(), sys_dcache_flush()
 */
//==============================================================================
extern void sys_dcache_invalidate(void *addr, size_t size);

//==============================================================================
/**
 * @brief Function cleans and invalidates data cache.
 *
 * On CPUs without data cache the function does nothing.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param addr      memory address (@ref NULL: whole data cache)
 * @param size      memory size in bytes (ignored when <i>addr</i> is @ref NULL)
 *
 * @see sys_dcache_clean(), sys_dcache_invalidate()
 */
//==============================================================================
extern void sys_dcache_flush(void *addr, size_t size);

//==============================================================================
/**
 * @brief Function invalidates instruction cache.
 *
 * Instruction cache is never dirty, so only invalidation exists. Function
 * shall be called when new code was written to memory (code loaded to RAM,
 * programmed flash memory). Use sys_cache_sync_code() to synchronize both
 * caches in correct order. Current implementation invalidates whole cache.
 * On CPUs without instruction cache the function does nothing.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param addr      memory address (@ref NULL: whole instruction cache)
 * @param size      memory size in bytes
 *
 * @see sys_cache_sync_code()
 */
//==============================================================================
extern void sys_icache_invalidate(void *addr, size_t size);

//==============================================================================
/**
 * @brief Function synchronizes caches after code was written to memory.
 *
 * Function cleans data cache (code is written to memory) and next invalidates
 * instruction cache, so CPU fetches new code. Function shall be called before
 * code loaded to RAM or programmed to flash memory is executed.
 * On CPUs without cache the function does nothing.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param addr      code address (@ref NULL: whole caches)
 * @param size      code size in bytes
 *
 * @see sys_dcache_clean(), sys_icache_invalidate()
 */
//==============================================================================
extern void sys_cache_sync_code(void *addr, size_t size);
#else
#define sys_dcache_clean(addr, size)      ((void)(addr), (void)(size))
#define sys_dcache_invalidate(addr, size) ((void)(addr), (void)(size))
#define sys_dcache_flush(addr, size)      ((void)(addr), (void)(size))
#define sys_icache_invalidate(addr, size) ((void)(addr), (void)(size))
#define sys_cache_sync_code(addr, size)   ((void)(addr), (void)(size))
#endif

#ifdef __cplusplus
}
#endif

#endif /* _SYSAPI_H_ */

/**@}*/

/*==============================================================================
  End of file
==============================================================================*/
