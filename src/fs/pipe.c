/*==============================================================================
@file    pipe.c

@author  Daniel Zorychta

@brief   File support creating of pipies in file systems.

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
#include "config.h"
#include "lib/sys/types.h"
#include "lib/misc.h"
#include "kernel/errno.h"
#include "kernel/kwrapper.h"
#include "fs/pipe.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define CLOSED    (1 << 0)
#define PERMANENT (1 << 1)

/*==============================================================================
  Local object types
==============================================================================*/
struct pipe {
        kqueue_t *queue;
        struct pipe *self;
        u32_t flag;
        size_t eof_marks;
};

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local objects
==============================================================================*/
#if __OS_ENABLE_MKFIFO__ == _YES_
static const u32_t PIPE_READ_TIMEOUT = _WAIT_FOREVER_MS;
static const u32_t PIPE_WRITE_TIMEOUT = _WAIT_FOREVER_MS;
#endif

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  External objects
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

#if __OS_ENABLE_MKFIFO__ == _YES_
//==============================================================================
/**
 * @brief  Check if pipe is valid
 * @param  this         pipe object
 * @return true if valid, otherwise false
 */
//==============================================================================
static bool is_valid(pipe_t *this)
{
        return this && this->self == this;
}

//==============================================================================
/**
 * @brief  Check if all data of closed pipe was read (only wake-up bytes left).
 * @param  this         pipe object
 * @return true if end of pipe, otherwise false
 */
//==============================================================================
static bool is_end_of_pipe(pipe_t *this)
{
        if (this->flag & CLOSED) {
                size_t noitm = 0;
                _queue_get_number_of_items(this->queue, &noitm);
                return noitm <= this->eof_marks;
        }

        return false;
}
#endif

//==============================================================================
/**
 * @brief Create pipe object
 *
 * @param[out] pipe     pointer to pointer of pipe handle
 *
 * @return One of errno value.
 */
//==============================================================================
int _pipe_create(pipe_t **pipe)
{
#if __OS_ENABLE_MKFIFO__ == _YES_
        int err = EINVAL;

        if (pipe) {
                err = _kmalloc(_MM_KRN, sizeof(pipe_t), 0, NULL, 0, 0, cast(void **, pipe));
                if (!err) {

                        err = _queue_create(__OS_PIPE_LENGTH__, sizeof(u8_t), &(*pipe)->queue);
                        if (err == ESUCC) {
                                (*pipe)->self = *pipe;
                                (*pipe)->flag = 0;
                                (*pipe)->eof_marks = 0;
                        } else {
                                _kfree(_MM_KRN, cast(void **, pipe));
                        }
                }
        }

        return err;
#else
        UNUSED_ARG1(pipe);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Destroy pipe object
 *
 * @param pipe          a pipe object
 *
 * @return One of errno value.
 */
//==============================================================================
int _pipe_destroy(pipe_t *pipe)
{
#if __OS_ENABLE_MKFIFO__ == _YES_
        if (is_valid(pipe)) {
                _queue_destroy(pipe->queue);
                pipe->self = NULL;
                _kfree(_MM_KRN, cast(void **, &pipe));
                return ESUCC;
        } else {
                return EINVAL;
        }
#else
        UNUSED_ARG1(pipe);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Return length of pipe
 *
 * @param pipe          a pipe object
 * @param len           a pipe length
 *
 * @return One of errno value.
 */
//==============================================================================
int _pipe_get_length(pipe_t *pipe, size_t *len)
{
#if __OS_ENABLE_MKFIFO__ == _YES_
        if (len && is_valid(pipe)) {
                int err = _queue_get_number_of_items(pipe->queue, len);
                if (!err) {
                        *len = (*len > pipe->eof_marks) ? (*len - pipe->eof_marks) : 0;
                }

                return err;
        } else {
                return EINVAL;
        }
#else
        UNUSED_ARG2(pipe, len);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Read data from pipe
 *
 * @param pipe          a pipe object
 * @param buf           a destination buffer
 * @param count         a count of bytes to read
 * @param rdcnt         a number of read bytes
 * @param non_blocking  a non-blocking access mode
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _pipe_read(pipe_t *pipe, u8_t *buf, size_t count, size_t *rdcnt, bool non_blocking)
{
#if __OS_ENABLE_MKFIFO__ == _YES_
        if (is_valid(pipe) && buf && count) {

                /*
                 * Wake-up bytes of closed pipe stay in the queue (they are only
                 * peeked), so all blocked readers are released and none of them
                 * gets these bytes as data.
                 */
                size_t n = 0;
                while (n < count) {

                        if (is_end_of_pipe(pipe)) {
                                break;
                        }

                        if (_queue_receive(pipe->queue, &buf[n], 0) == ESUCC) {

                                /* pipe closed between check and receive: wake-up byte received */
                                if (pipe->eof_marks > 0) {
                                        size_t noitm = 0;
                                        _queue_get_number_of_items(pipe->queue, &noitm);

                                        if (noitm < pipe->eof_marks) {
                                                u8_t nul = '\0';
                                                _queue_send(pipe->queue, &nul, 0);
                                                break;
                                        }
                                }

                                n++;
                                continue;
                        }

                        if (non_blocking || (n > 0)) {
                                break;
                        }

                        u8_t byte;
                        if (_queue_receive_peek(pipe->queue, &byte, PIPE_READ_TIMEOUT) != ESUCC) {
                                break;
                        }
                }

                *rdcnt = n;
                return ESUCC;
        } else {
                return EINVAL;
        }
#else
        UNUSED_ARG5(pipe, buf, count, rdcnt, non_blocking);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Read data from pipe
 *
 * @param pipe          a pipe object
 * @param buf           a destination buffer
 * @param count         a count of bytes to read
 * @param wrcnt         a number of written bytes
 * @param non_blocking  a non-blocking access mode
 *
 * @return One of errno value (errno.h). EPIPE when pipe is closed.
 */
//==============================================================================
int _pipe_write(pipe_t *pipe, const u8_t *buf, size_t count, size_t *wrcnt, bool non_blocking)
{
#if __OS_ENABLE_MKFIFO__ == _YES_
        if (is_valid(pipe) && buf && count) {

                size_t n = 0;
                for (; n < count; n++) {

                        if (pipe->flag & CLOSED) {
                                break;
                        }

                        u32_t tout = non_blocking ? 10 : PIPE_WRITE_TIMEOUT;
                        if (_queue_send(pipe->queue, &buf[n], tout) != ESUCC) {
                                break;
                        }
                }

                *wrcnt = n;

                if ((n == 0) && (pipe->flag & CLOSED)) {
                        return EPIPE;
                }

                return ESUCC;
        } else {
                return EINVAL;
        }
#else
        UNUSED_ARG5(pipe, buf, count, wrcnt, non_blocking);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief Close pipe
 *
 * @param pipe          a pipe object
 *
 * @return One of errno value.
 */
//==============================================================================
int _pipe_close(pipe_t *pipe)
{
#if __OS_ENABLE_MKFIFO__ == _YES_
        if (is_valid(pipe)) {

                if (not(pipe->flag & PERMANENT)) {
                        pipe->flag |= CLOSED;

                        /*
                         * Reader blocked on empty pipe is released by wake-up
                         * byte (it is not data, see _pipe_read()). Pipe with
                         * data does not need it: reader gets EOF after data.
                         */
                        size_t noitm = 0;
                        _queue_get_number_of_items(pipe->queue, &noitm);

                        if (noitm == 0) {
                                pipe->eof_marks++;

                                u8_t nul = '\0';
                                if (_queue_send(pipe->queue, &nul, 10) != ESUCC) {
                                        pipe->eof_marks--;
                                }
                        }
                }

                return ESUCC;
        } else {
                return EINVAL;
        }
#else
        UNUSED_ARG1(pipe);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief  Clear pipe
 *
 * @param  pipe         a pipe object
 *
 * @return One of errno value.
 */
//==============================================================================
int _pipe_clear(pipe_t *pipe)
{
#if __OS_ENABLE_MKFIFO__ == _YES_
        if (is_valid(pipe)) {
                pipe->eof_marks = 0;
                return _queue_reset(pipe->queue);
        } else {
                return EINVAL;
        }
#else
        UNUSED_ARG1(pipe);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief  Permanent pipe. FIFO is not closed at file close.
 *
 * @param  pipe         a pipe object
 *
 * @return One of errno value.
 */
//==============================================================================
int _pipe_permanent(pipe_t *pipe)
{
#if __OS_ENABLE_MKFIFO__ == _YES_
        if (is_valid(pipe)) {
                pipe->flag |= PERMANENT;
                return ESUCC;
        } else {
                return EINVAL;
        }
#else
        UNUSED_ARG1(pipe);
        return ENOTSUP;
#endif
}

/*==============================================================================
  End of file
==============================================================================*/
