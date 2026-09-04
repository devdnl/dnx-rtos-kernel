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
#include "tty_cfg.h"

/*==============================================================================
  Local macros
==============================================================================*/

/*==============================================================================
  Local object types
==============================================================================*/
struct ttybfr {
        void *self;

        struct {
                char *line[_TTY_TERMINAL_ROWS];
                u16_t line_head;
                bool fresh_line[_TTY_TERMINAL_ROWS];
        } screen;

        struct {
                u32_t buf[_TTY_TERMINAL_COLUMNS * 2];
                u16_t length;
                u16_t carriage;
        } new_line;
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void new_line_clear(ttybfr_t *this);

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
                return _TTY_TERMINAL_ROWS - (go_back - this->screen.line_head);
        } else {
                return this->screen.line_head - go_back;
        }
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
        if (this->screen.line[this->screen.line_head]) {
                sys_free(cast(void **, &this->screen.line[this->screen.line_head]));
                this->screen.line[this->screen.line_head] = NULL;
        }

        this->screen.line[this->screen.line_head] = line;
        this->screen.fresh_line[this->screen.line_head] = true;

        if (LAST_CHARACTER(line) == '\n') {
                this->screen.line_head = (this->screen.line_head + 1) % _TTY_TERMINAL_ROWS;

                if (this->screen.line[this->screen.line_head]) {
                        sys_free(cast(void **, &this->screen.line[this->screen.line_head]));
                        this->screen.line[this->screen.line_head] = NULL;
                }
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
        if (this->screen.line[this->screen.line_head]) {
                sys_free(cast(void **, &this->screen.line[this->screen.line_head]));
                this->screen.line[this->screen.line_head] = NULL;
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
        size_t buf_utf8_len = 0;
        for (size_t i = 0; i < this->new_line.length; i++) {
                int size = tty_ucs4_to_utf8_size(this->new_line.buf[i]);
                if (size <= 0) {
                        this->new_line.buf[i] = 0xFFFD;
                        size = 3;
                }
                buf_utf8_len += size;
        }

        if (add_cr_lf) {
                buf_utf8_len += 2;
        }

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
                        strcpy(line, last_line);
                }

                size_t idx = last_line_len;
                for (size_t i = 0; i < this->new_line.length; i++) {
                        char utf8[8];
                        int len = tty_ucs4_to_utf8(this->new_line.buf[i], utf8);
                        memcpy(&line[idx], utf8, len);
                        idx += len;
                }

                if (add_cr_lf) {
                        line[idx++] = '\r';
                        line[idx++] = '\n';
                }

                line[idx] = '\0';

                char *clrscr = line;
                while ((clrscr = strstr(clrscr, VT100_CLEAR_SCREEN))) {
                        ttybfr_clear(this);
                        memmove(line, clrscr, (buf_utf8_len + 1) - (clrscr - line));
                        clrscr += strlen(VT100_CLEAR_SCREEN);
                }

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
        this->new_line.carriage = 0;
}

//==============================================================================
/**
 * @brief  Put string buffer to the line
 *
 * @param  this         buffer object
 * @param  chr          character
 */
//==============================================================================
static void new_line_put_char(ttybfr_t *this, uint32_t chr)
{
        if (this->new_line.length >= ARRAY_SIZE(this->new_line.buf)) {
                return;
        }

        this->new_line.buf[this->new_line.carriage++] = chr;

        if (this->new_line.carriage > this->new_line.length) {
                this->new_line.length = this->new_line.carriage;
        }
}

/*------------------------------------------------------------------------------
 * INTERFACES
 *----------------------------------------------------------------------------*/

//==============================================================================
/**
 * @brief  Initialize buffer
 *
 * @param  bfr          pointer to buffer pointer
 *
 * @return One of errno value
 */
//==============================================================================
int ttybfr_create(ttybfr_t **bfr)
{
        int err = sys_zalloc(sizeof(ttybfr_t), cast(void **, bfr));
        if (err == ESUCC) {
                (*bfr)->self = *bfr;
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
 * @param  this          buffer object
 * @param  src           source
 * @param  len           length
 * @return None
 */
//==============================================================================
void ttybfr_put(ttybfr_t *this, const char *src, size_t len)
{
        if (is_valid(this)) {
                bool VT100_cmd = false;
                int32_t chr;
                const char *e = src + len;

                while (true) {

                        chr = tty_utf8_to_ucs4(&src, e - src);
                        if (chr <= 0) {
                                chr = 0xFFFD;
                        }

                        if (chr == '\r') {
                                this->new_line.carriage = 0;

                                if (this->screen.line[this->screen.line_head]) {
                                        if (LAST_CHARACTER(this->screen.line[this->screen.line_head]) != '\n') {
                                                screen_clear_last_line(this);
                                        }
                                }

                        } else if (chr == '\n') {
                                screen_put_new_line_buffer(this, true);

                        } else if (chr == '\e') {
                                VT100_cmd = true;
                                new_line_put_char(this, chr);

                        } else if (VT100_cmd) {
                                new_line_put_char(this, chr);

                                if (strchr("hlHKCDm", chr)) {
                                        VT100_cmd = false;
                                }

                        } else if (this->new_line.carriage >= _TTY_TERMINAL_COLUMNS) {
                                screen_put_new_line_buffer(this, true);
                                new_line_put_char(this, chr);

                        } else {
                                new_line_put_char(this, chr);
                        }
                }
        }
}

//==============================================================================
/**
 * @brief  Clear whole terminal
 * @param  this          buffer object
 * @return None
 */
//==============================================================================
void ttybfr_clear(ttybfr_t *this)
{
        if (is_valid(this)) {
                for (int i = 0; i < _TTY_TERMINAL_ROWS; i++) {
                        if (this->screen.line[i]) {
                                sys_free(cast(void **, &this->screen.line[i]));
                                this->screen.line[i] = NULL;
                        }

                        this->screen.fresh_line[i] = false;
                }

                this->screen.line_head = 0;
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
        if (is_valid(this) && n >= 0 && n < _TTY_TERMINAL_ROWS) {
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
                for (int i = _TTY_TERMINAL_ROWS - 1; i >= 0; i--) {
                        uint idx = screen_get_line_index(this, i);
                        if (this->screen.fresh_line[idx]) {
                                this->screen.fresh_line[idx] = false;
                                return this->screen.line[idx];
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
                for (int i = 0; i < _TTY_TERMINAL_ROWS; i++) {
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
                        new_line_clear(this);
                }
        }
}

/*==============================================================================
  End of file
==============================================================================*/
