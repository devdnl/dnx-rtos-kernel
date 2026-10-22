/*==============================================================================
@file    printk.c

@author  Daniel Zorychta

@brief   Kernel print support

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
#include <stddef.h>
#include "kernel/kwrapper.h"
#include "kernel/printk.h"
#include "config.h"
#include "fs/vfs.h"
#include "mm/mm.h"
#include "lib/misc.h"
#include "lib/vsnprintf.h"
#include "kernel/errno.h"

#if (__OS_SYSTEM_MSG_ENABLE__ > 0)

/*==============================================================================
  Local macros
==============================================================================*/
#define SOH 0x01
#define increment_index(_idx)             \
        if (++_idx >= sizeof(printk.buf)) \
        _idx = 0
#define size_of_timestamp_type sizeof(u64_t)
#define size_of_msg_len_type   sizeof(u16_t)

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        u64_t last_timestamp_os;
        u64_t last_timestamp;
        char line[__OS_SYSTEM_MSG_COLS__];
        char buf[max(__OS_SYSTEM_LOG_SIZE__, 2 * __OS_SYSTEM_MSG_COLS__)];
        size_t head;
        size_t tail;
        bool non_empty;
} printk_log_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local objects
==============================================================================*/
static printk_log_t printk;

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
 * @brief  Initialize printk subsystem.
 *
 * @return On success 0 is returned.
 */
//==============================================================================
int _printk_init(void)
{
        return 0;
}

//==============================================================================
/**
 * @brief  Put character into circular buffer.
 *
 * @param  c            character
 */
//==============================================================================
static void put_char(char c)
{
        if (printk.non_empty && (printk.tail == printk.head)) {
                for (size_t i = 0; i < (size_of_timestamp_type + size_of_msg_len_type); i++) {
                        printk.buf[printk.tail] = '\0';
                        increment_index(printk.tail);
                }

                while (printk.buf[printk.tail] != SOH) {
                        printk.buf[printk.tail] = '\0';
                        increment_index(printk.tail);
                }
        }

        printk.buf[printk.head] = c;
        increment_index(printk.head);

        printk.non_empty = true;
}

//==============================================================================
/**
 * @brief  Get character from circular buffer.
 *
 * @param  idx          pointer to character index
 *
 * @return Character.
 */
//==============================================================================
static char get_char(size_t *idx)
{
        size_t i = *idx;
        char c = printk.buf[i];
        increment_index(i);
        *idx = i;
        return c;
}

//==============================================================================
/**
 * @brief Function send kernel message on terminal
 *
 * @param *format             formated text
 * @param ...                 format arguments
 */
//==============================================================================
void _printk(const char *format, ...)
{
        if (not format) {
                return;
        }

        _kernel_scheduler_lock();
        {
                va_list args;
                va_start(args, format);
                int len = _vscnprintf(printk.line, sizeof(printk.line), format, args);
                va_end(args);

                if ((len >= 1) and (printk.line[len - 1] == '\n')) {
                        printk.line[len - 1] = '\0';
                        len--;
                }

                u64_t now_us = _kernel_get_uptime_ms() * 1000;
                if (now_us == printk.last_timestamp_os) {
                        printk.last_timestamp += 1;
                        now_us = printk.last_timestamp;
                } else {
                        printk.last_timestamp_os = now_us;
                        printk.last_timestamp = now_us;
                }

                // start of message
                put_char(SOH);

                // timestamp
                put_char((now_us >> 0) & 0xFF);
                put_char((now_us >> 8) & 0xFF);
                put_char((now_us >> 16) & 0xFF);
                put_char((now_us >> 24) & 0xFF);
                put_char((now_us >> 32) & 0xFF);
                put_char((now_us >> 40) & 0xFF);
                put_char((now_us >> 48) & 0xFF);
                put_char((now_us >> 56) & 0xFF);

                // message size
                put_char((len >> 0) & 0xFF);
                put_char((len >> 8) & 0xFF);

                // message
                for (int i = 0; i < len; i++) {
                        put_char(printk.line[i]);
                }
        }
        _kernel_scheduler_unlock();
}

//==============================================================================
/**
 * Function read log message.
 *
 * @param str           destination buffer
 * @param len           destination buffer length
 * @param from_time     log search time starting from system start
 * @param msg_time      current message time from system start
 *
 * @return Number of characters copied to the buffer. 0 if message is empty.
 */
//==============================================================================
size_t _printk_read(char *str, size_t len, const struct timeval *from_time, struct timeval *msg_time)
{
        size_t n = 0;

        if (not str or (len < 2) or not from_time or not msg_time) {
                return n;
        }

        _kernel_scheduler_lock();
        {
                u64_t from_us = (from_time->tv_sec * 1000000ULL) + from_time->tv_usec;

                size_t tail = printk.tail;

                do {
                        char c = get_char(&tail);
                        if (c == SOH) {
                                u64_t timestamp = 0;
                                timestamp |= cast(u64_t, get_char(&tail)) << 0;
                                timestamp |= cast(u64_t, get_char(&tail)) << 8;
                                timestamp |= cast(u64_t, get_char(&tail)) << 16;
                                timestamp |= cast(u64_t, get_char(&tail)) << 24;
                                timestamp |= cast(u64_t, get_char(&tail)) << 32;
                                timestamp |= cast(u64_t, get_char(&tail)) << 40;
                                timestamp |= cast(u64_t, get_char(&tail)) << 48;
                                timestamp |= cast(u64_t, get_char(&tail)) << 56;

                                u16_t msg_len = 0;
                                msg_len |= get_char(&tail) << 0;
                                msg_len |= get_char(&tail) << 8;

                                if (timestamp > from_us) {
                                        msg_time->tv_sec = timestamp / 1000000ULL;
                                        msg_time->tv_usec = timestamp % 1000000ULL;

                                        len = min(len - 1, msg_len);

                                        while (((c = get_char(&tail)) != '\0') && (len > 0)) {
                                                *str++ = c;
                                                len--;
                                                n++;
                                        }
                                        *str = '\0';
                                        break;

                                } else {
                                        tail += msg_len;
                                        tail %= sizeof(printk.buf);
                                }

                        } else {
                                n = 0;
                                break;
                        }

                } while (tail != printk.tail);
        }
        _kernel_scheduler_unlock();

        return n;
}

//==============================================================================
/**
 * Function clear system circular buffer.
 */
//==============================================================================
void _printk_clear(void)
{
        _kernel_scheduler_lock();
        {
                memset(&printk, 0, sizeof(printk));
                printk.non_empty = false;
        }
        _kernel_scheduler_unlock();
}

#endif

/*==============================================================================
  End of file
==============================================================================*/
