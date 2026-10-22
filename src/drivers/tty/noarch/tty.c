/*==============================================================================
@file    tty.c

@author  Daniel Zorychta

@brief   This file support virtual terminal.

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

// #pragma GCC optimize("Og")

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "tty.h"
#include "../tty_ioctl.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define UNICODE_UTF8_4            0x1fffff
#define RELEASE_LOCK_TIMEOUT_MS   2000
#define NEW_LINE_QUEUE_TIMEOUT_MS 1000

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
struct tty_io;

typedef struct {
        struct tty_io *io;
        kqueue_t *queue_out;
        kmtx_t *secure_mtx;
        ttybfr_t *screen;
        ttyedit_t *editline;
        ttycmd_t *vtcmd;
        bool flushed;
        u8_t major;
        u8_t minor;
} tty_t;

typedef struct tty_io {
        kfile_t *infile;
        kfile_t *outfile;
        kmtx_t *io_mtx;
        tty_t *tty[__TTY_NUMBER_OF_TERM__];
        tid_t service_in;
        u8_t current_tty;
} tty_io_t;

/*
 * Locking order (to avoid deadlocks):
 *   1. io->io_mtx
 *   2. tty->secure_mtx
 * No code path takes io_mtx while holding a secure_mtx without already
 * holding io_mtx (both mutexes are recursive).
 */

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int configure(tty_t *tty, const TTY_config_t *conf);
static int service_in(void *arg);
static void vt100_init(tty_io_t *io, bool force_clean);
static void vt100_analyze(tty_io_t *io, char c);
static bool copy_string_to_queue(const char *str, kqueue_t *queue, bool lfend, uint timeout);
static int switch_terminal(tty_io_t *io, int term_no);
static void handle_new_line(tty_t *tty);
static int show_fresh_line(tty_t *tty);
static int clear_tty(tty_t *tty);
static int refresh_last_line(tty_t *tty);
static int dump_tty_buffer(tty_t *tty, char *dst, size_t *size);
static void io_destroy(tty_io_t **io);

/*==============================================================================
  Local object definitions
==============================================================================*/
MODULE_NAME(TTY);

static const _thread_attr_t SERVICE_IN_ATTR = {
        .stack_depth = _STACK_DEPTH_LOW,
        .priority = _PRIORITY_NORMAL,
        .detached = true,
};

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize device
 *
 * @param[out]          **device_handle        device allocated memory
 * @param[in ]            major                major device number
 * @param[in ]            minor                minor device number (terminal number)
 * @param[in ]            config               optional module configuration
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_INIT(TTY, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        int err = ENODEV;

        if (minor >= __TTY_NUMBER_OF_TERM__) {
                return err;
        }

        /* initialize module base */
        tty_io_t *io = NULL;
        tty_t *tty = NULL;

        if (minor == 0) {
                err = sys_zalloc(sizeof(tty_io_t), cast(void **, &io));

                if (!err) {
                        err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &io->io_mtx);
                }

                if (!err) {
                        err = sys_thread_create(service_in, &SERVICE_IN_ATTR, io, &io->service_in);
                }

                if (err) {
                        io_destroy(&io);
                } else {
                        dev_dbg2(major, minor, "thread ID: %u", io->service_in);
                }

        } else {
                tty_t *tty0 = NULL;
                err = sys_module_get_instance(major, 0, (void **)&tty0);

                if (!err) {
                        io = tty0 ? tty0->io : NULL;

                        if (io == NULL) {
                                err = EFAULT;
                        }
                }
        }

        if (!err && io) {
                err = sys_zalloc(sizeof(tty_t), device_handle);
                if (err) {
                        goto tty_alloc_finish;
                }

                tty = *device_handle;

                err = sys_queue_create(__TTY_OUT_STREAM_LEN__, sizeof(char), &tty->queue_out);
                if (err) {
                        goto tty_alloc_finish;
                }

                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &tty->secure_mtx);
                if (err) {
                        goto tty_alloc_finish;
                }

                err = ttybfr_create(&tty->screen, major, minor);
                if (err) {
                        goto tty_alloc_finish;
                }

                err = ttyedit_create(&io->outfile, &tty->editline);
                if (err) {
                        goto tty_alloc_finish;
                }

                err = ttycmd_create(&tty->vtcmd);
                if (err) {
                        goto tty_alloc_finish;
                }

                tty->io = io;
                tty->major = major;
                tty->minor = minor;

                if (config) {
                        err = configure(tty, config);
                        if (err) {
                                goto tty_alloc_finish;
                        }
                }

                /* terminal is published only when fully initialized */
                err = sys_mutex_lock(io->io_mtx, _WAIT_FOREVER_MS);
                if (!err) {
                        io->tty[minor] = tty;
                        sys_mutex_unlock(io->io_mtx);
                }

        tty_alloc_finish:
                if (err) {
                        if (tty) {
                                if (tty->vtcmd) {
                                        ttycmd_destroy(tty->vtcmd);
                                }

                                if (tty->editline) {
                                        ttyedit_destroy(tty->editline);
                                }

                                if (tty->screen) {
                                        ttybfr_destroy(tty->screen);
                                }

                                if (tty->secure_mtx) {
                                        sys_mutex_destroy(tty->secure_mtx);
                                }

                                if (tty->queue_out) {
                                        sys_queue_destroy(tty->queue_out);
                                }

                                sys_free(device_handle);
                        }

                        /* IO object is owned by terminal 0 */
                        if (minor == 0) {
                                io_destroy(&io);
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Release device
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_RELEASE(TTY, void *device_handle)
{
        tty_t *tty = device_handle;
        tty_io_t *io = tty->io;

        int err = sys_mutex_lock(io->io_mtx, RELEASE_LOCK_TIMEOUT_MS);
        if (err) {
                return err;
        }

        err = sys_mutex_trylock(tty->secure_mtx);
        if (err) {
                sys_mutex_unlock(io->io_mtx);
                return err;
        }

        /*
         * Unpublish terminal first. Input thread uses terminals only when
         * io_mtx is locked, so after this point no one can reach this object.
         */
        io->tty[tty->minor] = NULL;

        sys_queue_destroy(tty->queue_out);
        ttybfr_destroy(tty->screen);
        ttyedit_destroy(tty->editline);
        ttycmd_destroy(tty->vtcmd);

        sys_mutex_unlock(tty->secure_mtx);
        sys_mutex_destroy(tty->secure_mtx);

        /* release IO if all TTYs are released */
        bool release_IO = true;
        for (int i = 0; i < __TTY_NUMBER_OF_TERM__ && release_IO; i++) {
                release_IO = !io->tty[i];
        }

        if (release_IO) {
                io_destroy(&io);
        } else {
                sys_mutex_unlock(io->io_mtx);
        }

        sys_free(&device_handle);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Open device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           flags                  file operation flags (O_RDONLY, O_WRONLY, O_RDWR)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_OPEN(TTY, void *device_handle, u32_t flags)
{
        UNUSED_ARG2(device_handle, flags);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Close device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           force                  device force close (true)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_CLOSE(TTY, void *device_handle, bool force)
{
        UNUSED_ARG2(device_handle, force);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_WRITE(TTY, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        tty_t *tty = device_handle;

        int err = sys_mutex_lock(tty->secure_mtx, _WAIT_FOREVER_MS);
        if (!err) {
                ttybfr_put(tty->screen, cast(const char *, src), count);
                err = show_fresh_line(tty);
                sys_mutex_unlock(tty->secure_mtx);

                *wrcnt = count;
        }

        return err;
}

//==============================================================================
/**
 * @brief Read data from device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_READ(TTY, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        tty_t *tty = device_handle;

        *rdcnt = 0;

        u32_t timeout_ms = fattr.non_blocking_rd ? 0 : _WAIT_FOREVER_MS;

        if (fattr.non_blocking_rd && (count > 0)) {
                /*
                 * Move already typed characters to the output queue. Never
                 * wait for queue space here: the queue is emptied only by
                 * the reader (this thread), so waiting would deadlock while
                 * holding the mutex.
                 */
                if (sys_mutex_lock(tty->secure_mtx, _WAIT_FOREVER_MS) == ESUCC) {
                        const char *str = ttyedit_get_value(tty->editline);
                        if (str && (str[0] != '\0')) {
                                copy_string_to_queue(str, tty->queue_out, false, 0);
                                ttyedit_clear(tty->editline);
                        }
                        sys_mutex_unlock(tty->secure_mtx);
                } else {
                        return ETIME;
                }
        }

        while (count--) {
                if (sys_queue_receive(tty->queue_out, dst, timeout_ms) == ESUCC) {

                        (*rdcnt)++;

                        if (*dst == '\n') {
                                break;
                        }

                        dst++;

                        if (fattr.non_strict_rd) {
                                timeout_ms = 0;
                        }
                } else if (timeout_ms == 0) {
                        break;
                }
        }

        *fpos = 0;

        return ESUCC;
}

//==============================================================================
/**
 * @brief IO control
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_IOCTL(TTY, void *device_handle, int request, void *arg)
{
        tty_t *tty = device_handle;
        int err = EINVAL;

        switch (request) {
        case IOCTL_TTY__CONFIGURE:
                if (arg) {
                        err = configure(tty, arg);
                }
                break;

        case IOCTL_TTY__GET_CURRENT_TTY:
                if (arg) {
                        *cast(int *, arg) = tty->io->current_tty;
                        err = ESUCC;
                }
                break;

        case IOCTL_TTY__GET_COL:
                if (arg) {
                        *cast(int *, arg) = __TTY_LINE_BUF_SIZE__;
                        err = ESUCC;
                }
                break;

        case IOCTL_TTY__GET_ROW:
                if (arg) {
                        *cast(int *, arg) = __TTY_TERM_ROWS__;
                        err = ESUCC;
                }
                break;

        case IOCTL_TTY__SET_EDITLINE:
                if (arg) {
                        err = sys_mutex_lock(tty->secure_mtx, _WAIT_FOREVER_MS);
                        if (err == ESUCC) {
                                ttyedit_set_value(tty->editline, arg, tty->io->current_tty == tty->minor);
                                sys_mutex_unlock(tty->secure_mtx);
                        }
                }
                break;

        case IOCTL_TTY__SWITCH_TTY_TO:
                if (arg) {
                        err = switch_terminal(tty->io, *cast(int *, arg));
                }
                break;

        case IOCTL_TTY__CLEAR_SCR:
                err = clear_tty(tty);
                break;

        case IOCTL_TTY__ECHO_ON:
                err = sys_mutex_lock(tty->secure_mtx, _WAIT_FOREVER_MS);
                if (!err) {
                        err = ttyedit_enable_echo(tty->editline);
                        sys_mutex_unlock(tty->secure_mtx);
                }
                break;

        case IOCTL_TTY__ECHO_OFF:
                err = sys_mutex_lock(tty->secure_mtx, _WAIT_FOREVER_MS);
                if (!err) {
                        err = ttyedit_disable_echo(tty->editline);
                        sys_mutex_unlock(tty->secure_mtx);
                }
                break;

        case IOCTL_TTY__GET_NUMBER_OF_TTYS:
                if (arg) {
                        *cast(int *, arg) = __TTY_NUMBER_OF_TERM__;
                        err = ESUCC;
                }
                break;

        case IOCTL_TTY__REFRESH_LAST_LINE:
                err = refresh_last_line(tty);
                break;

        case IOCTL_TTY__READ_BUFFER: {
                err = EINVAL;
                TTY_buffer_t *buf = arg;
                if (buf && buf->ptr && buf->size) {
                        err = dump_tty_buffer(tty, buf->ptr, &buf->size);
                }
                break;
        }

        default:
                err = EBADRQC;
                break;
        }

        return err;
}

//==============================================================================
/**
 * @brief Flush device
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_FLUSH(TTY, void *device_handle)
{
        tty_t *tty = device_handle;

        int err = sys_mutex_lock(tty->secure_mtx, _WAIT_FOREVER_MS);
        if (!err) {
                ttybfr_flush(tty->screen);
                err = refresh_last_line(tty);
                sys_mutex_unlock(tty->secure_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Device information
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *device_stat            device status
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_STAT(TTY, void *device_handle, struct vfs_dev_stat *device_stat)
{
        tty_t *tty = device_handle;

        device_stat->st_size = 0;
        device_stat->st_major = tty->major;
        device_stat->st_minor = tty->minor;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Destroy IO object (input thread, files, mutex, memory).
 *
 * @param  io           pointer to IO object (set to NULL). If io_mtx is
 *                      locked by the caller it is unlocked before destroy.
 */
//==============================================================================
static void io_destroy(tty_io_t **io)
{
        if (io && *io) {
                tty_io_t *p = *io;

                if (sys_thread_is_valid(p->service_in)) {
                        sys_thread_destroy(p->service_in);
                }

                if (p->outfile && (p->outfile != p->infile)) {
                        sys_fclose(p->outfile);
                }

                if (p->infile) {
                        sys_fclose(p->infile);
                }

                p->infile = NULL;
                p->outfile = NULL;

                if (p->io_mtx) {
                        sys_mutex_unlock(p->io_mtx);
                        sys_mutex_destroy(p->io_mtx);
                }

                sys_free(cast(void **, io));
        }
}

//==============================================================================
/**
 * @brief  Function configure TTY IO.
 *
 * @param  tty          tty instance
 * @param  conf         configuration
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int configure(tty_t *tty, const TTY_config_t *conf)
{
        kfile_t *in = NULL, *out = NULL;

        int err = ESUCC;

        bool same_file = conf->input_file && conf->output_file && (strcmp(conf->input_file, conf->output_file) == 0);

        if (same_file) {
                err = sys_fopen(conf->input_file, O_RDWR, 0, &in);
                out = in;

        } else {
                if (conf->input_file) {
                        err = sys_fopen(conf->input_file, O_RDONLY, 0, &in);
                }

                if (!err && conf->output_file) {
                        err = sys_fopen(conf->output_file, O_WRONLY, 0, &out);
                }
        }

        if (!err) {
                err = sys_mutex_lock(tty->io->io_mtx, _WAIT_FOREVER_MS);
        }

        if (!err) {
                if (tty->io->outfile && (tty->io->outfile != tty->io->infile)) {
                        sys_fclose(tty->io->outfile);
                }

                if (tty->io->infile) {
                        sys_fclose(tty->io->infile);
                }

                tty->io->infile = in;
                tty->io->outfile = out;

                vt100_init(tty->io, false);

                sys_mutex_unlock(tty->io->io_mtx);

        } else {
                if (out && (out != in)) {
                        sys_fclose(out);
                }

                if (in) {
                        sys_fclose(in);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief TTY input service (helper task)
 *
 * @param arg           tty io
 */
//==============================================================================
static int service_in(void *arg)
{
        tty_io_t *io = arg;

        bool last_CR = false;

        for (;;) {
                char c = '\0';
                size_t rdcnt = 0;

                kfile_t *infile = io->infile;

                if (infile && (sys_fread(&c, 1, &rdcnt, infile) == ESUCC) && (rdcnt == 1)) {
                        /*
                         * Standard TTY protocol is to sent a CR from the keyboard
                         * when the user has finished typing and for the computer
                         * to echo LF when it's ready for the next line.
                         */
                        if (last_CR && c == '\n') {
                                last_CR = false;
                                continue;
                        }

                        last_CR = (c == '\r');

                        /* NUL bytes (line noise, BREAK, telnet CR NUL) are ignored */
                        if (c != '\0') {
                                vt100_analyze(io, c);
                        } else {
                                sys_sleep_ms(25);
                        }
                } else {
                        sys_sleep_ms(100);
                }
        }

        return 0;
}

//==============================================================================
/**
 * @brief Configure VT100 terminal
 *
 * @param io            TTY io
 * @param force_clen    force screen clean
 */
//==============================================================================
static void vt100_init(tty_io_t *io, bool force_clean)
{
        if (!io->outfile) {
                return;
        }

        size_t wrcnt;

        const char *cmd = VT100_RESET_ATTRIBUTES;
        sys_fwrite(cmd, strlen(cmd), &wrcnt, io->outfile);

        if (force_clean) {
                cmd = VT100_CLEAR_SCREEN VT100_CURSOR_HOME;
                sys_fwrite(cmd, strlen(cmd), &wrcnt, io->outfile);
        }

        cmd = VT100_ENABLE_LINE_WRAP;
        sys_fwrite(cmd, strlen(cmd), &wrcnt, io->outfile);
}

//==============================================================================
/**
 * @brief Control analysis of VT100 input stream
 *
 * @param io            tty io
 * @param c             input character
 */
//==============================================================================
static void vt100_analyze(tty_io_t *io, char c)
{
        if (sys_mutex_lock(io->io_mtx, _WAIT_FOREVER_MS) != ESUCC) {
                return;
        }

        tty_t *tty = io->tty[io->current_tty];

        if (tty && (sys_mutex_lock(tty->secure_mtx, _WAIT_FOREVER_MS) == ESUCC)) {

                ttycmd_resp_t resp = ttycmd_analyze(tty->vtcmd, c);
                switch (resp) {
                case TTYCMD_KEY_ENTER:
                        ttyedit_clear_collected_character(tty->editline);
                        handle_new_line(tty);
                        break;

                case TTYCMD_KEY_CHAR:
                        ttyedit_insert_char(tty->editline, c);
                        break;

                case TTYCMD_KEY_ENDTEXT:
                        ttyedit_insert_char(tty->editline, c);
                        handle_new_line(tty);
                        break;

                case TTYCMD_KEY_BACKSPACE:
                        ttyedit_remove_char(tty->editline);
                        break;

                case TTYCMD_KEY_DELETE:
                        ttyedit_delete_char(tty->editline);
                        break;

                case TTYCMD_KEY_ARROW_LEFT:
                        ttyedit_move_cursor_left(tty->editline);
                        break;

                case TTYCMD_KEY_ARROW_RIGHT:
                        ttyedit_move_cursor_right(tty->editline);
                        break;

                case TTYCMD_KEY_ARROW_UP:
                        copy_string_to_queue(VT100_ARROW_UP_STDOUT, tty->queue_out, true, 0);
                        break;

                case TTYCMD_KEY_ARROW_DOWN:
                        copy_string_to_queue(VT100_ARROW_DOWN_STDOUT, tty->queue_out, true, 0);
                        break;

                case TTYCMD_KEY_TAB:
                        if (copy_string_to_queue(ttyedit_get_value(tty->editline), tty->queue_out, false, 0)) {
                                copy_string_to_queue(VT100_TAB, tty->queue_out, true, 0);
                        }
                        break;

                case TTYCMD_KEY_HOME:
                        ttyedit_move_cursor_home(tty->editline);
                        break;

                case TTYCMD_KEY_END:
                        ttyedit_move_cursor_end(tty->editline);
                        break;

                case TTYCMD_KEY_F1 ... TTYCMD_KEY_F12:
                        /* io_mtx is held (recursive), new terminal is locked inside */
                        switch_terminal(tty->io, resp - TTYCMD_KEY_F1);
                        break;

                default:
                        break;
                }

                sys_mutex_unlock(tty->secure_mtx);
        }

        sys_mutex_unlock(io->io_mtx);
}

//==============================================================================
/**
 * @brief Copy string to queue
 *
 * @param str           string
 * @param queue         queue
 * @param lfend         true: adds LF, false: without LF
 * @param timeout       operation timeout [ms]
 *
 * @return true if whole string (and LF) was sent, false otherwise.
 */
//==============================================================================
static bool copy_string_to_queue(const char *str, kqueue_t *queue, bool lfend, uint timeout)
{
        bool ok = true;

        if (str) {
                size_t len = strlen(str);

                for (size_t i = 0; i < len; i++) {
                        if (sys_queue_send(queue, &str[i], timeout) != ESUCC) {
                                ok = false;
                                break;
                        }
                }
        }

        /*
         * LF is sent even if string was truncated so the reader is not left
         * waiting for the end of line forever.
         */
        if (lfend) {
                const char lf = '\n';
                if (sys_queue_send(queue, &lf, timeout) != ESUCC) {
                        ok = false;
                }
        }

        return ok;
}

//==============================================================================
/**
 * @brief Switch terminal
 *
 * @param io            tty io
 * @param term_no       terminal number
 *
 * @return One of errno value.
 */
//==============================================================================
static int switch_terminal(tty_io_t *io, int term_no)
{
        if ((term_no < 0) || (term_no >= __TTY_NUMBER_OF_TERM__)) {
                return EINVAL;
        }

        int err = sys_mutex_lock(io->io_mtx, _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        if (!io->tty[term_no]) {
                err = EINVAL;

        } else if (io->current_tty == term_no) {
                err = ESUCC;

        } else {
                tty_t *tty = io->tty[io->current_tty];

                /*
                 * Wait for current terminal's VT100 decoder idle state. The
                 * decoder becomes idle at least after sequence timeout.
                 */
                if (tty) {
                        clock_t tref = sys_get_uptime_ms();

                        while (not ttycmd_is_idle(tty->vtcmd) && not sys_is_time_expired(tref, 1000)) {
                                sys_sleep_ms(50);
                        }

                        err = ttycmd_is_idle(tty->vtcmd) ? ESUCC : EBUSY;
                }

                if (!err) {
                        io->current_tty = term_no;

                        tty = io->tty[io->current_tty];

                        err = sys_mutex_lock(tty->secure_mtx, _WAIT_FOREVER_MS);
                        if (!err) {
                                vt100_init(io, true);

                                size_t wrcnt;
                                const char *str;

                                for (int i = __TTY_TERM_ROWS__ - 1; i >= 0; i--) {
                                        str = ttybfr_get_line(tty->screen, i);

                                        if (str) {
                                                sys_fwrite(str, strlen(str), &wrcnt, io->outfile);
                                        }
                                }

                                str = ttyedit_get_value(tty->editline);
                                if (str && ttyedit_is_echo_enabled(tty->editline)) {
                                        sys_fwrite(str, strlen(str), &wrcnt, io->outfile);
                                }

                                ttybfr_clear_fresh_line_counter(tty->screen);

                                sys_mutex_unlock(tty->secure_mtx);
                        }
                }
        }

        sys_mutex_unlock(io->io_mtx);

        return err;
}

//==============================================================================
/**
 * @brief Handle adding a new line to buffer and prepare editline.
 *
 * @param tty           terminal object
 */
//==============================================================================
static void handle_new_line(tty_t *tty)
{
        if (sys_mutex_lock(tty->secure_mtx, _WAIT_FOREVER_MS) == ESUCC) {
                const char *str = ttyedit_get_value(tty->editline);
                const char *lf = "\n";

                if (str) {
                        if (ttyedit_is_echo_enabled(tty->editline)) {

                                ttybfr_put(tty->screen, str, strlen(str));
                                ttybfr_put(tty->screen, lf, strlen(lf));
                                ttybfr_clear_fresh_line_counter(tty->screen);

                                const char *crlf = "\r\n";
                                size_t wrcnt;
                                sys_fwrite(crlf, strlen(crlf), &wrcnt, tty->io->outfile);
                        }

                        /*
                         * NOTE: the edit line is limited to the queue length
                         *       (see tty_editln.c), so a complete line always
                         *       fits into an empty queue.
                         */
                        copy_string_to_queue(str, tty->queue_out, true, NEW_LINE_QUEUE_TIMEOUT_MS);
                }

                ttyedit_clear(tty->editline);

                sys_mutex_unlock(tty->secure_mtx);
        }
}

//==============================================================================
/**
 * @brief  Function show fresh line.
 *
 * @param  tty          TTY instance
 *
 * @return One of errno value.
 */
//==============================================================================
static int show_fresh_line(tty_t *tty)
{
        int err = ESUCC;

        if (tty->minor == tty->io->current_tty) {
                err = sys_mutex_lock(tty->secure_mtx, 100);
                if (!err) {

                        const char *str;
                        while ((str = ttybfr_get_fresh_line(tty->screen))) {
                                size_t wrcnt;

                                if (tty->flushed) {
                                        sys_fwrite(VT100_CLEAR_LINE, strlen(VT100_CLEAR_LINE), &wrcnt,
                                                tty->io->outfile);

                                        tty->flushed = false;
                                }

                                sys_fwrite(str, strlen(str), &wrcnt, tty->io->outfile);
                        }

                        sys_mutex_unlock(tty->secure_mtx);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Clear TTY output.
 *
 * @param  tty          TTY instance
 *
 * @return One of errno value.
 */
//==============================================================================
static int clear_tty(tty_t *tty)
{
        int err = sys_mutex_lock(tty->secure_mtx, 100);
        if (!err) {
                ttybfr_clear(tty->screen);

                if (tty->minor == tty->io->current_tty) {
                        vt100_init(tty->io, true);
                }

                sys_mutex_unlock(tty->secure_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Refresh last line.
 *
 * @param  tty          TTY instance
 *
 * @return One of errno value. Background terminal: nothing to do, ESUCC.
 */
//==============================================================================
static int refresh_last_line(tty_t *tty)
{
        int err = ESUCC;

        if (tty->minor == tty->io->current_tty) {

                err = sys_mutex_lock(tty->secure_mtx, _WAIT_FOREVER_MS);
                if (!err) {
                        size_t wrcnt;

                        sys_fwrite(VT100_CLEAR_LINE, strlen(VT100_CLEAR_LINE), &wrcnt, tty->io->outfile);

                        /* last line is NULL when previous line was finished by LF */
                        const char *last_line = ttybfr_get_line(tty->screen, 0);
                        if (last_line) {
                                sys_fwrite(last_line, strlen(last_line), &wrcnt, tty->io->outfile);
                        }

                        const char *editline = ttyedit_get_value(tty->editline);
                        if (editline && ttyedit_is_echo_enabled(tty->editline)) {
                                sys_fwrite(editline, strlen(editline), &wrcnt, tty->io->outfile);
                        }

                        tty->flushed = true;

                        sys_mutex_unlock(tty->secure_mtx);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Dump TTY buffer.
 *
 * Lines are copied from the oldest one. Line endings "\r\n" are converted
 * to "\n". Output is always terminated by '\0'. If buffer is too small the
 * output is truncated.
 *
 * @param  tty          TTY instance
 * @param  dst          destination buffer
 * @param  size         [in] destination buffer size,
 *                      [out] number of free bytes left (excluding '\0')
 *
 * @return One of errno value.
 */
//==============================================================================
static int dump_tty_buffer(tty_t *tty, char *dst, size_t *size)
{
        if (*size == 0) {
                return EINVAL;
        }

        int err = sys_mutex_lock(tty->secure_mtx, _WAIT_FOREVER_MS);
        if (!err) {

                size_t free_bytes = *size - 1; /* one byte reserved for '\0' */
                char *p = dst;

                for (int i = __TTY_TERM_ROWS__ - 1; i >= 0 && free_bytes > 0; i--) {

                        const char *str = ttybfr_get_line(tty->screen, i);
                        if (!str) {
                                continue;
                        }

                        size_t len = strlen(str);
                        bool lf = false;

                        if ((len > 0) && (str[len - 1] == '\n')) {
                                lf = true;
                                len--;

                                if ((len > 0) && (str[len - 1] == '\r')) {
                                        len--;
                                }
                        }

                        size_t n = min(len, free_bytes);
                        memcpy(p, str, n);
                        p += n;
                        free_bytes -= n;

                        if (lf && (free_bytes > 0)) {
                                *p++ = '\n';
                                free_bytes--;
                        }
                }

                *p = '\0';
                *size = free_bytes;

                sys_mutex_unlock(tty->secure_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function get next utf8-coded character.
 *
 * Supported are 1..4 byte sequences (RFC 3629). Continuation bytes are
 * validated.
 *
 * @param  text         pointer to text buffer (moved to the next character)
 * @param  len          number of bytes available in buffer
 *
 * @return Unicode character,
 *         0 if invalid byte was found (pointer moved by 1 byte) or end of text,
 *         -1 if sequence is incomplete (more bytes are needed).
 */
//==============================================================================
int32_t tty_utf8_to_ucs4(const char **text, size_t len)
{
        if (!text || !*text || (len == 0) || (**text == '\0')) {
                return 0;
        }

        const unsigned char *s = cast(const unsigned char *, *text);
        unsigned char c = s[0];
        int32_t unicode;
        size_t n;

        if (c < 0x80) {
                *text += 1;
                return c;

        } else if ((c & 0xE0) == 0xC0) {
                n = 2;
                unicode = c & 0x1F;

        } else if ((c & 0xF0) == 0xE0) {
                n = 3;
                unicode = c & 0x0F;

        } else if ((c & 0xF8) == 0xF0) {
                n = 4;
                unicode = c & 0x07;

        } else {
                /* stray continuation byte or unsupported lead byte */
                *text += 1;
                return 0;
        }

        for (size_t i = 1; i < n; i++) {
                if ((i >= len) || (s[i] == '\0')) {
                        *text += i;
                        return -1;
                }

                if ((s[i] & 0xC0) != 0x80) {
                        /* invalid sequence: skip lead byte only */
                        *text += 1;
                        return 0;
                }

                unicode = (unicode << 6) | (s[i] & 0x3F);
        }

        *text += n;

        return unicode;
}

//==============================================================================
/**
 * @brief  Convert ucs4 to UTF8.
 *
 * @param  ucs4         ucs4 character
 * @param  utf8         UTF8 buffer output (at least 5 bytes)
 *
 * @return Number of converted bytes or -1 if conversion error.
 */
//==============================================================================
int tty_ucs4_to_utf8(int32_t ucs4, char *utf8)
{
        if (ucs4 < 0) {
                utf8[0] = '\0';
                return -1;

        } else if (ucs4 < 0x80) {
                utf8[0] = ucs4;
                utf8[1] = '\0';
                return 1;

        } else if (ucs4 < 0x800) {
                utf8[0] = (ucs4 >> 6) | 0xC0;
                utf8[1] = (ucs4 & 0x3F) | 0x80;
                utf8[2] = '\0';
                return 2;

        } else if (ucs4 <= 0xFFFF) {
                utf8[0] = ((ucs4 >> 12)) | 0xE0;
                utf8[1] = ((ucs4 >> 6) & 0x3F) | 0x80;
                utf8[2] = ((ucs4) & 0x3F) | 0x80;
                utf8[3] = '\0';
                return 3;

        } else if (ucs4 <= UNICODE_UTF8_4) {
                utf8[0] = 0xF0 | (ucs4 >> 18);
                utf8[1] = 0x80 | ((ucs4 >> 12) & 0x3F);
                utf8[2] = 0x80 | ((ucs4 >> 6) & 0x3F);
                utf8[3] = 0x80 | ((ucs4 & 0x3F));
                utf8[4] = '\0';
                return 4;

        } else {
                utf8[0] = '\0';
                return -1;
        }
}

//==============================================================================
/**
 * @brief  Calculate size of utf8 code of selected UCS4 character.
 *
 * @param  ucs4         ucs4 character
 *
 * @return Size of UTF8 code, negative on error.
 */
//==============================================================================
int tty_ucs4_to_utf8_size(int32_t ucs4)
{
        if (ucs4 < 0) {
                return -1;

        } else if (ucs4 < 0x80) {
                return 1;

        } else if (ucs4 < 0x800) {
                return 2;

        } else if (ucs4 <= 0xFFFF) {
                return 3;

        } else if (ucs4 <= UNICODE_UTF8_4) {
                return 4;

        } else {
                return -1;
        }
}

/*==============================================================================
  End of file
==============================================================================*/
