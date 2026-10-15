/*==============================================================================
@file    tty_bfr.c

@author  Daniel Zorychta

@brief   Code in this file is responsible for buffer support.

@note    Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/*==============================================================================
  Local macros
==============================================================================*/

/*==============================================================================
  Local object types
==============================================================================*/
struct ttybfr {
        void *self;
        u8_t major;
        u8_t minor;

        struct {
                char *line[__TTY_TERM_ROWS__];
                bool fresh_line[__TTY_TERM_ROWS__];
                u16_t line_head;
        } screen;

        struct {
                char buf[__TTY_LINE_BUF_SIZE__ + 1];
                u16_t length;
        } new_line;

        /* CR received, its effect is deferred until next character */
        bool pending_cr;
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void new_line_clear(ttybfr_t *this);
static void screen_clear(ttybfr_t *this);

/*==============================================================================
  Local objects
==============================================================================*/
MODULE_NAME(TTY);

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
 * @brief  Check if given object is valid
 * @param  this         object to examine
 * @return On success true is returned, otherwise false.
 */
//==============================================================================
static bool is_valid(ttybfr_t *this)
{
        return this && this->self == this;
}

//==============================================================================
/**
 * @brief  Get last or selected line
 * @param  this          buffer object
 * @param  go_back       number of lines from current index
 * @return line's index
 */
//==============================================================================
static uint screen_get_line_index(ttybfr_t *this, uint go_back)
{
        if (this->screen.line_head < go_back) {
                return __TTY_TERM_ROWS__ - (go_back - this->screen.line_head);
        } else {
                return this->screen.line_head - go_back;
        }
}

//==============================================================================
/**
 * @brief  Free selected screen slot.
 * @param  this          buffer object
 * @param  idx           slot index
 */
//==============================================================================
static void screen_free_slot(ttybfr_t *this, uint idx)
{
        if (this->screen.line[idx]) {
                sys_free(cast(void **, &this->screen.line[idx]));
                this->screen.line[idx] = NULL;
        }

        this->screen.fresh_line[idx] = false;
}

//==============================================================================
/**
 * @brief  Function link prepared line to buffer
 * @param  this          buffer object
 * @param  line          line
 * @return None
 */
//==============================================================================
static void screen_link_line(ttybfr_t *this, char *line)
{
        screen_free_slot(this, this->screen.line_head);

        this->screen.line[this->screen.line_head] = line;
        this->screen.fresh_line[this->screen.line_head] = true;

        if (LAST_CHARACTER(line) == '\n') {
                this->screen.line_head = (this->screen.line_head + 1) % __TTY_TERM_ROWS__;
                screen_free_slot(this, this->screen.line_head);
        }
}

//==============================================================================
/**
 * @brief  Clear last line
 * @param  this          buffer object
 * @return None
 */
//==============================================================================
static void screen_clear_last_line(ttybfr_t *this)
{
        screen_free_slot(this, this->screen.line_head);
}

//==============================================================================
/**
 * @brief  Clear screen lines (new line buffer is not touched).
 * @param  this          buffer object
 */
//==============================================================================
static void screen_clear(ttybfr_t *this)
{
        for (int i = 0; i < __TTY_TERM_ROWS__; i++) {
                screen_free_slot(this, i);
        }

        this->screen.line_head = 0;
}

//==============================================================================
/**
 * @brief  Truncate to last clear screen command.
 *
 * @param  this         buffer object
 */
//==============================================================================
static void truncate_to_last_clrscr(ttybfr_t *this)
{
        this->new_line.buf[this->new_line.length] = '\0';

        if (this->new_line.length == 0) {
                return;
        }

        char *last_clrscr = NULL;
        char *current_clrsrc = this->new_line.buf;

        while ((current_clrsrc = strstr(current_clrsrc, VT100_CLEAR_SCREEN)) != NULL) {
                last_clrscr = current_clrsrc;
                current_clrsrc += strlen(VT100_CLEAR_SCREEN);
        }

        if (last_clrscr != NULL) {
                size_t offset = last_clrscr - this->new_line.buf;

                if (offset <= this->new_line.length) {
                        this->new_line.length = this->new_line.length - offset;
                        memmove(this->new_line.buf, last_clrscr, this->new_line.length);
                        this->new_line.buf[this->new_line.length] = '\0';
                        screen_clear(this);
                }
        }
}

//==============================================================================
/**
 * @brief  Put the new line buffer to the main buffer
 * @param  this         buffer object
 * @param  add_cr_lf    add CR LF characters
 * @return None
 */
//==============================================================================
static void screen_put_new_line_buffer(ttybfr_t *this, bool add_cr_lf)
{
        truncate_to_last_clrscr(this);

        size_t buf_utf8_len = this->new_line.length + (add_cr_lf ? 2 : 0);

        size_t last_line_len = 0;
        char *last_line = this->screen.line[screen_get_line_index(this, 0)];
        if (last_line) {
                if (LAST_CHARACTER(last_line) != '\n') {
                        last_line_len = strlen(last_line);
                }
        }

        char *line = NULL;
        int err = sys_malloc(last_line_len + buf_utf8_len + 1, cast(void **, &line));
        if (!err && line) {

                if (last_line_len) {
                        memcpy(line, last_line, last_line_len);
                }

                size_t idx = last_line_len;
                memcpy(&line[idx], this->new_line.buf, this->new_line.length);
                idx += this->new_line.length;

                if (add_cr_lf) {
                        line[idx++] = '\r';
                        line[idx++] = '\n';
                }

                line[idx] = '\0';

                screen_link_line(this, line);
        }

        new_line_clear(this);
}

//==============================================================================
/**
 * @brief  Clear the new line buffer
 * @param  this         buffer object
 * @return None
 */
//==============================================================================
static void new_line_clear(ttybfr_t *this)
{
        this->new_line.length = 0;
        this->new_line.buf[0] = '\0';
}

//==============================================================================
/**
 * @brief  Put character to the new line buffer
 *
 * @param  this         buffer object
 * @param  chr          character
 */
//==============================================================================
static void new_line_put_char(ttybfr_t *this, char chr)
{
        if (this->new_line.length >= ARRAY_SIZE(this->new_line.buf) - 1) {
                return;
        }

        this->new_line.buf[this->new_line.length++] = chr;
        this->new_line.buf[this->new_line.length] = '\0';
}

//==============================================================================
/**
 * @brief  Apply carriage return: line that is not finished is discarded.
 *
 * @param  this         buffer object
 */
//==============================================================================
static void apply_carriage_return(ttybfr_t *this)
{
        new_line_clear(this);

        const char *line = this->screen.line[this->screen.line_head];
        if (line && (LAST_CHARACTER(line) != '\n')) {
                screen_clear_last_line(this);
        }
}

//==============================================================================
/**
 * @brief  Initialize buffer
 *
 * @param  bfr          pointer to buffer pointer
 * @param  major        major number
 * @param  minor        minor number
 *
 * @return One of errno value
 */
//==============================================================================
int ttybfr_create(ttybfr_t **bfr, u8_t major, u8_t minor)
{
        int err = sys_zalloc(sizeof(ttybfr_t), cast(void **, bfr));
        if (err == ESUCC) {
                (*bfr)->self = *bfr;
                (*bfr)->major = major;
                (*bfr)->minor = minor;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Destroy buffer object
 *
 * @param  this          buffer object pointer
 *
 * @return One of errno value
 */
//==============================================================================
int ttybfr_destroy(ttybfr_t *this)
{
        if (is_valid(this)) {
                ttybfr_clear(this);
                this->self = NULL;
                sys_free(cast(void **, &this));
                return ESUCC;
        } else {
                return EINVAL;
        }
}

//==============================================================================
/**
 * @brief  Put string buffer to the line
 *
 * All bytes are consumed. NUL bytes are ignored. "\r\n" (also split between
 * calls) is handled as a single new line, a lone '\r' discards the line that
 * is not finished yet.
 *
 * @param  this          buffer object
 * @param  src           source
 * @param  len           length
 * @return None
 */
//==============================================================================
void ttybfr_put(ttybfr_t *this, const char *src, size_t len)
{
        if (not is_valid(this) or not src) {
                return;
        }

        for (; len > 0; len--) {

                char chr = *src++;

                if (chr == '\0') {
                        continue;
                }

                if (this->pending_cr) {
                        this->pending_cr = false;

                        if (chr != '\n') {
                                apply_carriage_return(this);
                        }
                }

                if (chr == '\r') {
                        this->pending_cr = true;

                } else if (chr == '\n') {
                        screen_put_new_line_buffer(this, true);

                } else {
                        if (this->new_line.length >= ARRAY_SIZE(this->new_line.buf) - 1) {
                                screen_put_new_line_buffer(this, false);
                        }

                        new_line_put_char(this, chr);
                }
        }
}

//==============================================================================
/**
 * @brief  Clear whole terminal (screen lines and not flushed data)
 * @param  this          buffer object
 * @return None
 */
//==============================================================================
void ttybfr_clear(ttybfr_t *this)
{
        if (is_valid(this)) {
                screen_clear(this);
                new_line_clear(this);
                this->pending_cr = false;
        }
}

//==============================================================================
/**
 * @brief  Return n-line
 * @param  this          buffer object
 * @param  n             n-line from head
 * @return pointer to line or NULL if line doesn't exist
 */
//==============================================================================
const char *ttybfr_get_line(ttybfr_t *this, int n)
{
        if (is_valid(this) && n >= 0 && n < __TTY_TERM_ROWS__) {
                return this->screen.line[screen_get_line_index(this, n)];
        }

        return NULL;
}

//==============================================================================
/**
 * @brief  Return fresh line
 * @param  this          buffer object
 * @return pointer to new line or NULL if no new line
 */
//==============================================================================
const char *ttybfr_get_fresh_line(ttybfr_t *this)
{
        if (is_valid(this)) {
                for (int i = __TTY_TERM_ROWS__ - 1; i >= 0; i--) {
                        uint idx = screen_get_line_index(this, i);
                        if (this->screen.fresh_line[idx]) {
                                this->screen.fresh_line[idx] = false;

                                if (this->screen.line[idx]) {
                                        return this->screen.line[idx];
                                }
                        }
                }
        }

        return NULL;
}

//==============================================================================
/**
 * @brief  Clear fresh line counter
 * @param  this          buffer object
 * @return None
 */
//==============================================================================
void ttybfr_clear_fresh_line_counter(ttybfr_t *this)
{
        if (is_valid(this)) {
                for (int i = 0; i < __TTY_TERM_ROWS__; i++) {
                        this->screen.fresh_line[i] = false;
                }
        }
}

//==============================================================================
/**
 * @brief  Flush prepare buffer
 * @param  this          buffer object
 * @return None
 */
//==============================================================================
void ttybfr_flush(ttybfr_t *this)
{
        if (is_valid(this)) {
                if (this->new_line.length) {
                        screen_put_new_line_buffer(this, false);
                }
        }
}

/*==============================================================================
  End of file
==============================================================================*/
