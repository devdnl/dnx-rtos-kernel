/*==============================================================================
@file    host.h

@author  Daniel Zorychta

@brief   Interface between dnx RTOS and host operating system (POSIX simulator).

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
 * NOTE: This header is the only connection between dnx RTOS code and the host
 *       code. It is included by both sides, so it can use only basic C types.
 *       The dnx RTOS headers redefine standard types (pid_t, time_t, struct
 *       timeval, ...), so host headers and dnx headers cannot be mixed in
 *       a single compilation unit.
 *
 *       All functions are short and non-blocking. They are executed in the
 *       critical section, so the scheduler tick (signal) cannot switch tasks
 *       while host library is in use (host library locks).
 */

#ifndef _HOST_H_
#define _HOST_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*==============================================================================
  Exported macros
==============================================================================*/
/** maximal number of simulated UARTs */
#define _HOST_UART_COUNT 4

/** maximal number of host disks */
#define _HOST_DISK_COUNT 8

/** maximal length of host file system path */
#define _HOST_FS_PATH_MAX 1024

/** maximal length of host file name */
#define _HOST_FS_NAME_MAX 255

/* host file system open flags */
#define _HOST_FS_O_RDONLY 0x00
#define _HOST_FS_O_WRONLY 0x01
#define _HOST_FS_O_RDWR   0x02
#define _HOST_FS_O_CREAT  0x10
#define _HOST_FS_O_EXCL   0x20
#define _HOST_FS_O_TRUNC  0x40

/*==============================================================================
  Exported object types
==============================================================================*/
/** error codes returned by host file system functions (independent of errno values) */
enum _host_err {
        _HOST_ESUCC = 0,
        _HOST_EPERM,
        _HOST_ENOENT,
        _HOST_EIO,
        _HOST_EBADF,
        _HOST_ENOMEM,
        _HOST_EACCES,
        _HOST_EBUSY,
        _HOST_EEXIST,
        _HOST_EXDEV,
        _HOST_ENOTDIR,
        _HOST_EISDIR,
        _HOST_EINVAL,
        _HOST_EMFILE,
        _HOST_EFBIG,
        _HOST_ENOSPC,
        _HOST_EROFS,
        _HOST_ENAMETOOLONG,
        _HOST_ENOTEMPTY,
        _HOST_ENOTSUP,
        _HOST_EOTHER,
};

/** host file type */
enum _host_ftype {
        _HOST_FTYPE_FILE,
        _HOST_FTYPE_DIR,
        _HOST_FTYPE_OTHER,
};

/** host file information */
struct _host_stat {
        uint64_t size;
        int64_t mtime;
        int64_t ctime;
        uint32_t perm; /*!< permission bits (0777) */
        enum _host_ftype type;
};

/** host file system information */
struct _host_statfs {
        uint64_t bsize;
        uint64_t blocks;
        uint64_t bfree;
        uint64_t files;
        uint64_t ffree;
};

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
/* system */
extern int _dnx_main(void);
extern void _host_exit(int status) __attribute__((noreturn));
extern void _host_restart(void) __attribute__((noreturn));
extern void _host_print(const char *str);
extern const char *_host_get_fault_info(void);

/* memory */
extern void *_host_get_ram(size_t *size);

/* time */
extern uint64_t _host_get_monotonic_us(void);
extern int64_t _host_get_realtime_us(void);
extern void _host_sleep_us(uint32_t us);

/* UART */
extern bool _host_uart_is_available(int port);
extern int _host_uart_configure(int port, uint32_t baud, int parity, int stop_bits);
extern int _host_uart_read(int port, void *buf, size_t len);
extern int _host_uart_write(int port, const void *buf, size_t len);

/* GPIO (board model) */
extern void _host_gpio_output(int port, uint32_t state, uint32_t changed);
extern uint32_t _host_gpio_input(int port, uint32_t *level);

/* I2C master (board model), results: 0: ACK, 1: NACK, -1: error */
#define _HOST_I2C_ADDR_10BIT 0x8000
extern int _host_i2c_start(int bus);
extern int _host_i2c_address(int bus, uint16_t address, bool read);
extern int _host_i2c_write(int bus, const uint8_t *src, size_t count, size_t *written);
extern int _host_i2c_read(int bus, uint8_t *dst, size_t count);
extern void _host_i2c_stop(int bus);

/* SPI master (board model), flags: bits 0-1 SPI mode, _HOST_SPI_LSB_FIRST; result: 0 or -1 */
#define _HOST_SPI_LSB_FIRST 0x04
extern int _host_spi_transfer(int bus, const uint8_t *tx, uint8_t *rx, size_t count, uint8_t fill, unsigned flags);

/* Ethernet (TAP) */
extern bool _host_eth_is_available(void);
extern bool _host_eth_is_link_up(void);
extern int _host_eth_read(void *buf, size_t len);
extern int _host_eth_write(const void *buf, size_t len);

/* disks */
extern int _host_disk_open(int disk, uint64_t *size, bool *read_only);
extern void _host_disk_close(int disk);
extern int _host_disk_read(int disk, uint64_t offset, void *buf, size_t len);
extern int _host_disk_write(int disk, uint64_t offset, const void *buf, size_t len);
extern int _host_disk_sync(int disk);

/* host file system (directory shared with simulator) */
extern bool _host_fs_is_available(void);
extern int _host_fs_open(const char *path, uint32_t flags, uint32_t perm, int *fd);
extern int _host_fs_close(int fd);
extern int _host_fs_read(int fd, uint64_t offset, void *buf, size_t len, size_t *rdcnt);
extern int _host_fs_write(int fd, uint64_t offset, const void *buf, size_t len, size_t *wrcnt);
extern int _host_fs_sync(int fd);
extern int _host_fs_syncfs(void);
extern int _host_fs_fstat(int fd, struct _host_stat *stat);
extern int _host_fs_stat(const char *path, struct _host_stat *stat);
extern int _host_fs_mkdir(const char *path, uint32_t perm);
extern int _host_fs_remove(const char *path);
extern int _host_fs_rename(const char *old_path, const char *new_path);
extern int _host_fs_chmod(const char *path, uint32_t perm);
extern int _host_fs_opendir(const char *path, void **dir);
extern int _host_fs_readdir(void *dir, char *name, size_t name_len, struct _host_stat *stat);
extern void _host_fs_rewinddir(void *dir);
extern int _host_fs_closedir(void *dir);
extern int _host_fs_statfs(struct _host_statfs *statfs);

#ifdef __cplusplus
}
#endif

#endif /* _HOST_H_ */
/*==============================================================================
  End of file
==============================================================================*/
