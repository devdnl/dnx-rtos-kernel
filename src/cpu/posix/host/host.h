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

/*==============================================================================
  Exported object types
==============================================================================*/

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
extern int _host_uart_read(int port, void *buf, size_t len);
extern int _host_uart_write(int port, const void *buf, size_t len);

/* disks */
extern int _host_disk_open(int disk, uint64_t *size, bool *read_only);
extern void _host_disk_close(int disk);
extern int _host_disk_read(int disk, uint64_t offset, void *buf, size_t len);
extern int _host_disk_write(int disk, uint64_t offset, const void *buf, size_t len);
extern int _host_disk_sync(int disk);

#ifdef __cplusplus
}
#endif

#endif /* _HOST_H_ */
/*==============================================================================
  End of file
==============================================================================*/
