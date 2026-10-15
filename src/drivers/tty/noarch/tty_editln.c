/*==============================================================================
@file    tty_editln.c

@author  Daniel Zorychta

@brief   TTY edit line (input line).

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
#define UTF8_MAX_CHAR_LEN       4
#define EDITLINE_MAX_UTF8_BYTES min((__TTY_LINE_BUF_SIZE__ - 1), (__TTY_OUT_STREAM_LEN__ - 1))

/*==============================================================================
  Local object types
==============================================================================*/
struct ttyedit {
        kfile_t **out_file;
        void *self;
        char input_char[8];
        u8_t input_char_count;
        u32_t buffer_ucs4[__TTY_OUT_STREAM_LEN__ + 1];
        char buffer_utf8[__TTY_LINE_BUF_SIZE__];
        u16_t length_ucs4;
        u16_t length_utf8;
        u16_t cursor_position;
        bool echo_enabled;
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void ucs4_buffer_to_utf8_buffer(ttyedit_t *this, size_t pos);
static void insert_ucs4(ttyedit_t *this, int32_t ucs4);

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
 * @brief  Check if object is valid
 * @param  this         edit line object
 * @return If valid then true is returned, otherwise false.
 */
//==============================================================================
static inline bool is_valid(ttyedit_t *this)
{
        return this && this->self == this;
}

//==============================================================================
/**
 * @brief  Write string to output file.
 * @param  this         edit line object
 * @param  str          string
 * @param  len          length
 */
//==============================================================================
static void out_write(ttyedit_t *this, const char *str, size_t len)
{
        if (this->out_file && *this->out_file && str && len) {
                size_t wrcnt;
                sys_fwrite(str, len, &wrcnt, *this->out_file);
        }
}

//==============================================================================
/**
 * @brief  Write string to output file.
 * @param  this         edit line object
 * @param  str          string
 */
//==============================================================================
static void out_puts(ttyedit_t *this, const char *str)
{
        if (str) {
                out_write(this, str, strlen(str));
        }
}

//==============================================================================
/**
 * @brief Initialize editline
 *
 * @param[in]  out_file      output file object
 * @param[put] edit          pointer to pointer of created object
 *
 * @param On of errno value.
 */
//==============================================================================
int ttyedit_create(kfile_t **out_file, ttyedit_t **edit)
{
        int err = sys_zalloc(sizeof(ttyedit_t), cast(void **, edit));
        if (err == ESUCC) {
                (*edit)->self = *edit;
                (*edit)->out_file = out_file;
                (*edit)->echo_enabled = true;
        }

        return err;
}

//==============================================================================
/**
 * @brief Destroy editline
 *
 * @param this          editline object
 *
 * @return One of errno value.
 */
//==============================================================================
int ttyedit_destroy(ttyedit_t *this)
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
 * @brief Enable editline echo
 *
 * @param this          editline object
 *
 * @return One of errno value.
 */
//==============================================================================
int ttyedit_enable_echo(ttyedit_t *this)
{
        if (is_valid(this)) {
                this->echo_enabled = true;
                return ESUCC;
        } else {
                return EINVAL;
        }
}

//==============================================================================
/**
 * @brief Disable editline echo
 *
 * @param this          editline object
 *
 * @return One of errno value.
 */
//==============================================================================
int ttyedit_disable_echo(ttyedit_t *this)
{
        if (is_valid(this)) {
                this->echo_enabled = false;
                return ESUCC;
        } else {
                return EINVAL;
        }
}

//==============================================================================
/**
 * @brief Return echo status
 *
 * @param this          editline object
 *
 * @return true if echo enabled, false if not
 */
//==============================================================================
bool ttyedit_is_echo_enabled(ttyedit_t *this)
{
        if (is_valid(this)) {
                return this->echo_enabled;
        }

        return false;
}

//==============================================================================
/**
 * @brief Return collected string
 *
 * @param this          editline object
 *
 * @return edit line string, NULL on error
 */
//==============================================================================
char *ttyedit_get_value(ttyedit_t *this)
{
        if (not is_valid(this)) {
                return NULL;
        }

        ucs4_buffer_to_utf8_buffer(this, 0);

        return this->buffer_utf8;
}

//==============================================================================
/**
 * @brief Set editline string
 *
 * @param this          editline object
 * @param str           string
 * @param show          true: new buffer is show (only if echo is enabled)
 */
//==============================================================================
void ttyedit_set_value(ttyedit_t *this, const char *str, bool show)
{
        if (not is_valid(this) or not str) {
                return;
        }

        show = show && this->echo_enabled;

        if (show) {
                ttyedit_move_cursor_home(this);
        }

        ttyedit_clear(this);
        ttyedit_clear_collected_character(this);

        const char *utf8 = str;
        const char *utf8_end = str;     /* end of accepted characters */
        size_t remaining = strlen(str);

        while ((remaining > 0) && (this->length_ucs4 < ARRAY_SIZE(this->buffer_ucs4) - 1)) {

                const char *prev = utf8;
                int32_t ucs4 = tty_utf8_to_ucs4(&utf8, remaining);
                remaining -= (utf8 - prev);

                if (ucs4 < 0) {
                        /* incomplete sequence at the end of string */
                        break;

                } else if (ucs4 == 0) {
                        /* invalid byte skipped */
                        if (utf8 == prev) {
                                break;
                        }
                        continue;
                }

                int n = tty_ucs4_to_utf8_size(ucs4);
                if ((n < 0) || (this->length_utf8 + n > EDITLINE_MAX_UTF8_BYTES)) {
                        break;
                }

                this->buffer_ucs4[this->length_ucs4++] = ucs4;
                this->length_utf8 += n;
                utf8_end = utf8;
        }

        this->buffer_ucs4[this->length_ucs4] = 0;
        this->cursor_position = this->length_ucs4;

        if (show) {
                out_write(this, str, cast(uintptr_t, utf8_end) - cast(uintptr_t, str));
                out_puts(this, VT100_ERASE_LINE_END);
        }
}

//==============================================================================
/**
 * @brief Clear collected string
 *
 * @param this          editline object
 */
//==============================================================================
void ttyedit_clear(ttyedit_t *this)
{
        if (is_valid(this)) {
                memset(this->buffer_ucs4, 0, sizeof(this->buffer_ucs4));
                memset(this->buffer_utf8, 0, sizeof(this->buffer_utf8));
                this->cursor_position = 0;
                this->length_ucs4 = 0;
                this->length_utf8 = 0;
        }
}

//==============================================================================
/**
 * @brief Clear collected character.
 *
 * @param this          editline object
 */
//==============================================================================
void ttyedit_clear_collected_character(ttyedit_t *this)
{
        if (is_valid(this)) {
                memset(this->input_char, 0, sizeof(this->input_char));
                this->input_char_count = 0;
        }
}

//==============================================================================
/**
 * @brief Insert character at cursor position
 *
 * Bytes are collected until complete UTF-8 character is received. Invalid
 * sequence is dropped; if the byte that broke the sequence can start a new
 * character it is used as the beginning of the new sequence.
 *
 * @param this          editline object
 * @param c             character (byte) to insert
 */
//==============================================================================
void ttyedit_insert_char(ttyedit_t *this, char c)
{
        if (not is_valid(this)) {
                return;
        }

        if (this->length_ucs4 >= ARRAY_SIZE(this->buffer_ucs4) - 1) {
                ttyedit_clear_collected_character(this);
                return;
        }

        if (this->input_char_count >= UTF8_MAX_CHAR_LEN) {
                ttyedit_clear_collected_character(this);
        }

        this->input_char[this->input_char_count++] = c;
        this->input_char[this->input_char_count] = '\0';

        for (int attempt = 0; attempt < 2; attempt++) {
                const char *utf8 = this->input_char;
                int32_t ucs4 = tty_utf8_to_ucs4(&utf8, this->input_char_count);

                if (ucs4 < 0) {
                        /* sequence not complete, wait for next byte */
                        return;

                } else if (ucs4 > 0) {
                        ttyedit_clear_collected_character(this);
                        insert_ucs4(this, ucs4);
                        return;
                }

                /* invalid sequence */
                bool retry = (this->input_char_count > 1);
                ttyedit_clear_collected_character(this);

                if (!retry) {
                        return;
                }

                /* restart sequence with the last received byte */
                this->input_char[0] = c;
                this->input_char[1] = '\0';
                this->input_char_count = 1;
        }

        ttyedit_clear_collected_character(this);
}

//==============================================================================
/**
 * @brief Insert decoded character at cursor position
 *
 * @param this          editline object
 * @param ucs4          character
 */
//==============================================================================
static void insert_ucs4(ttyedit_t *this, int32_t ucs4)
{
        int n = tty_ucs4_to_utf8_size(ucs4);
        if ((n < 0) || (this->length_utf8 + n > EDITLINE_MAX_UTF8_BYTES)) {
                return;
        }

        if (this->cursor_position < this->length_ucs4) {

                for (uint i = this->length_ucs4; i > this->cursor_position; i--) {
                        this->buffer_ucs4[i] = this->buffer_ucs4[i - 1];
                }

                this->buffer_ucs4[this->cursor_position++] = ucs4;
                this->length_ucs4++;
                this->length_utf8 += n;
                this->buffer_ucs4[this->length_ucs4] = 0;

                if (this->echo_enabled) {
                        out_puts(this, VT100_SAVE_CURSOR_POSITION);

                        ucs4_buffer_to_utf8_buffer(this, this->cursor_position - 1);
                        out_puts(this, this->buffer_utf8);

                        out_puts(this, VT100_RESTORE_CURSOR_POSITION VT100_SHIFT_CURSOR_RIGHT(1));
                }
        } else {
                this->buffer_ucs4[this->cursor_position++] = ucs4;
                this->length_ucs4++;
                this->length_utf8 += n;
                this->buffer_ucs4[this->length_ucs4] = 0;

                if (this->echo_enabled) {
                        char utf8[8];
                        if (tty_ucs4_to_utf8(ucs4, utf8) > 0) {
                                out_puts(this, utf8);
                        }
                }
        }
}

//==============================================================================
/**
 * @brief Remove character at selected position (no echo).
 *
 * @param this          editline object
 * @param pos           position (pos < length_ucs4)
 */
//==============================================================================
static void remove_at(ttyedit_t *this, uint pos)
{
        int n = tty_ucs4_to_utf8_size(this->buffer_ucs4[pos]);
        if (n > 0) {
                this->length_utf8 -= min(cast(u16_t, n), this->length_utf8);
        }

        for (uint i = pos; i + 1 < this->length_ucs4; i++) {
                this->buffer_ucs4[i] = this->buffer_ucs4[i + 1];
        }

        this->length_ucs4--;
        this->buffer_ucs4[this->length_ucs4] = 0;
}

//==============================================================================
/**
 * @brief Remove character from edit line in front of cursor
 *
 * @param this          editline object
 */
//==============================================================================
void ttyedit_remove_char(ttyedit_t *this)
{
        if (not is_valid(this)) {
                return;
        }

        ttyedit_clear_collected_character(this);

        if (this->cursor_position == 0 || this->length_ucs4 == 0) {
                return;
        }

        this->cursor_position--;

        remove_at(this, this->cursor_position);

        if (ttyedit_is_echo_enabled(this)) {
                out_puts(this, "\b" VT100_ERASE_LINE_FROM_CUR VT100_SAVE_CURSOR_POSITION);

                ucs4_buffer_to_utf8_buffer(this, this->cursor_position);
                out_puts(this, this->buffer_utf8);

                out_puts(this, VT100_RESTORE_CURSOR_POSITION);
        }
}

//==============================================================================
/**
 * @brief Delete character from edit line at cursor position
 *
 * @param this          editline object
 */
//==============================================================================
void ttyedit_delete_char(ttyedit_t *this)
{
        if (not is_valid(this)) {
                return;
        }

        ttyedit_clear_collected_character(this);

        if (this->length_ucs4 == 0 || this->cursor_position >= this->length_ucs4) {
                return;
        }

        remove_at(this, this->cursor_position);

        if (ttyedit_is_echo_enabled(this)) {
                out_puts(this, VT100_SAVE_CURSOR_POSITION VT100_ERASE_LINE_FROM_CUR);

                ucs4_buffer_to_utf8_buffer(this, this->cursor_position);
                out_puts(this, this->buffer_utf8);

                out_puts(this, VT100_RESTORE_CURSOR_POSITION);
        }
}

//==============================================================================
/**
 * @brief Move cursor left
 *
 * @param this          editline object
 */
//==============================================================================
void ttyedit_move_cursor_left(ttyedit_t *this)
{
        if (is_valid(this)) {
                ttyedit_clear_collected_character(this);

                if (this->cursor_position > 0) {
                        if (ttyedit_is_echo_enabled(this)) {
                                out_puts(this, VT100_SHIFT_CURSOR_LEFT(1));
                        }
                        this->cursor_position--;
                }
        }
}

//==============================================================================
/**
 * @brief Move cursor right
 *
 * @param this          editline object
 */
//==============================================================================
void ttyedit_move_cursor_right(ttyedit_t *this)
{
        if (is_valid(this)) {
                ttyedit_clear_collected_character(this);

                if (this->cursor_position < this->length_ucs4) {
                        if (ttyedit_is_echo_enabled(this)) {
                                out_puts(this, VT100_SHIFT_CURSOR_RIGHT(1));
                        }
                        this->cursor_position++;
                }
        }
}

//==============================================================================
/**
 * @brief Move cursor home
 *
 * @param this          editline object
 */
//==============================================================================
void ttyedit_move_cursor_home(ttyedit_t *this)
{
        if (is_valid(this)) {
                ttyedit_clear_collected_character(this);

                bool echo = ttyedit_is_echo_enabled(this);

                if (echo) {
                        out_puts(this, VT100_CURSOR_OFF);
                }

                while (this->cursor_position > 0) {
                        if (echo) {
                                out_write(this, "\b", 1);
                        }
                        this->cursor_position--;
                }

                if (echo) {
                        out_puts(this, VT100_CURSOR_ON);
                }
        }
}

//==============================================================================
/**
 * @brief Move cursor end
 *
 * @param this          editline object
 */
//==============================================================================
void ttyedit_move_cursor_end(ttyedit_t *this)
{
        if (is_valid(this)) {
                ttyedit_clear_collected_character(this);

                bool echo = ttyedit_is_echo_enabled(this);

                if (echo) {
                        out_puts(this, VT100_CURSOR_OFF);
                }

                while (this->cursor_position < this->length_ucs4) {
                        if (echo) {
                                out_puts(this, VT100_SHIFT_CURSOR_RIGHT(1));
                        }
                        this->cursor_position++;
                }

                if (echo) {
                        out_puts(this, VT100_CURSOR_ON);
                }
        }
}

//==============================================================================
/**
 * @brief  Convert ucs4 buffer (from selected position) to UTF8 buffer.
 *         Result is always terminated by '\0'.
 *
 * @param  this         editline object
 * @param  pos          position in ucs4 buffer
 */
//==============================================================================
static void ucs4_buffer_to_utf8_buffer(ttyedit_t *this, size_t pos)
{
        char *utf8_buf = this->buffer_utf8;
        size_t utf8_len = 0;

        for (size_t i = pos; i < this->length_ucs4; i++) {
                char utf8[8];
                int n = tty_ucs4_to_utf8(this->buffer_ucs4[i], utf8);
                if ((n > 0) and ((utf8_len + n) < ARRAY_SIZE(this->buffer_utf8))) {
                        memcpy(utf8_buf, utf8, n);
                        utf8_buf += n;
                        utf8_len += n;
                } else {
                        break;
                }
        }

        *utf8_buf = '\0';
}

/*==============================================================================
  End of file
==============================================================================*/
