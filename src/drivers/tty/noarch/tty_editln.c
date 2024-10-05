/*=========================================================================*//**
@file    tty_editln.c

@author  Daniel Zorychta

@brief   TTY edit line (input line).

@note    Copyright (C) 2013 Daniel Zorychta <daniel.zorychta@gmail.com>

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


*//*==========================================================================*/

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
#define EDITLINE_LEN       _TTY_TERMINAL_COLUMNS

#define UNICODE_UTF8_4     0x1fffff
#define UNI_SUR_HIGH_START 0xD800
#define UNI_SUR_HIGH_END   0xDBFF
#define UNI_SUR_LOW_START  0xDC00
#define UNI_SUR_LOW_END    0xDFFF
#define UNI_NOT_CHAR_MIN   0xFDD0
#define UNI_NOT_CHAR_MAX   0xFDEF

#define REJECT_FFFF(x)                \
        if ((x & 0xFFFF) >= 0xFFFE) { \
                return -EINVAL;       \
        }

#define REJECT_SURROGATE(ucs2)                                       \
        if (ucs2 >= UNI_SUR_HIGH_START && ucs2 <= UNI_SUR_LOW_END) { \
                return -EINVAL;                                      \
        }

#define REJECT_NOT_CHAR(r)                                    \
        if (r >= UNI_NOT_CHAR_MIN && r <= UNI_NOT_CHAR_MAX) { \
                return -EINVAL;                               \
        }

/*==============================================================================
  Local object types
==============================================================================*/
struct ttyedit {
        kfile_t **out_file;
        void     *self;
        char      input_char[8];
        u8_t      input_char_count;
        wchar_t   buffer_ucs2[EDITLINE_LEN + 1];
        char      buffer_utf8[4 * EDITLINE_LEN + 1];
        u16_t     length_ucs2;
        u16_t     cursor_position;
        bool      echo_enabled;
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static wchar_t utf8_to_ucs2(const char **text);
static int     ucs2_to_utf8(int32_t ucs2, char *utf8);
static int     unicode_count_chars(const char *utf8);
static void    ucs2_buffer_to_utf8_buffer(ttyedit_t *this, size_t pos);

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
                (*edit)->self         = *edit;
                (*edit)->out_file     = out_file;
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

        ucs2_buffer_to_utf8_buffer(this, 0);

        return this->buffer_utf8;
}

//==============================================================================
/**
 * @brief Set editline string
 *
 * @param this          editline object
 * @param str           string
 * @param show          true: new buffer is show
 */
//==============================================================================
void ttyedit_set_value(ttyedit_t *this, const char *str, bool show)
{
        if (not is_valid(this) or not str) {
                return;
        }

        int n = unicode_count_chars(str);

        if (n < 0) {
                return;

        } else if (n >= cast(int, ARRAY_SIZE(this->buffer_ucs2))) {
                n = ARRAY_SIZE(this->buffer_ucs2) - 1;
        }

        if (show) {
                ttyedit_move_cursor_home(this);
        }

        ttyedit_clear(this);

        const char *utf8 = str;
        wchar_t    *ucs2 = this->buffer_ucs2;
        while (*utf8 != '\0') {
                *ucs2 = utf8_to_ucs2(&utf8);
                if (ucs2 == 0) {
                        break;
                } else {
                        ucs2++;
                }
        }

        this->length_ucs2     = n;
        this->cursor_position = n;

        if (show) {
                size_t wrcnt;
                size_t utf8_len = cast(uintptr_t, utf8) - cast(uintptr_t, str);
                sys_fwrite(str, utf8_len, &wrcnt, *this->out_file);

                static const char *erase_line_end = VT100_ERASE_LINE_END;
                sys_fwrite(erase_line_end, strlen(erase_line_end), &wrcnt, *this->out_file);
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
                memset(this->buffer_ucs2, 0, sizeof(this->buffer_ucs2));
                memset(this->buffer_utf8, 0, sizeof(this->buffer_utf8));
                this->cursor_position = 0;
                this->length_ucs2     = 0;
        }
}

//==============================================================================
/**
 * @brief Insert character at cursor position
 *
 * @param this          editline object
 * @param c             character to insert
 */
//==============================================================================
void ttyedit_insert_char(ttyedit_t *this, char c)
{
        if (not is_valid(this)) {
                return;
        }

        if (this->length_ucs2 >= ARRAY_SIZE(this->buffer_ucs2) - 1) {
                return;
        }

        if (this->input_char_count < 4) {
                this->input_char[this->input_char_count++] = c;

        } else {
                memset(this->input_char, 0, sizeof(this->input_char));
                this->input_char_count                     = 0;
                this->input_char[this->input_char_count++] = c;
        }

        const char *utf8 = this->input_char;
        wchar_t     ucs2 = utf8_to_ucs2(&utf8);
        if (ucs2 == 0) {
                return;
        }

        memset(this->input_char, 0, sizeof(this->input_char));
        this->input_char_count = 0;

        if (this->cursor_position < this->length_ucs2) {

                for (uint i = this->length_ucs2; i > this->cursor_position; i--) {
                        this->buffer_ucs2[i] = this->buffer_ucs2[i - 1];
                }

                this->buffer_ucs2[this->cursor_position++] = ucs2;
                this->length_ucs2++;

                if (this->echo_enabled) {
                        size_t wrcnt;

                        const char *cmd = VT100_SAVE_CURSOR_POSITION;
                        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);

                        ucs2_buffer_to_utf8_buffer(this, this->cursor_position - 1);
                        cmd = this->buffer_utf8;
                        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);

                        cmd = VT100_RESTORE_CURSOR_POSITION VT100_SHIFT_CURSOR_RIGHT(1);
                        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);
                }
        } else {
                this->buffer_ucs2[this->cursor_position++] = ucs2;
                this->length_ucs2++;

                if (this->echo_enabled) {
                        size_t wrcnt;
                        char   utf8[8];
                        ucs2_to_utf8(ucs2, utf8);
                        sys_fwrite(utf8, strlen(utf8), &wrcnt, *this->out_file);
                }
        }
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

        if (this->cursor_position == 0 || this->length_ucs2 == 0) {
                return;
        }

        this->cursor_position--;

        for (uint i = this->cursor_position; i < this->length_ucs2; i++) {
                this->buffer_ucs2[i] = this->buffer_ucs2[i + 1];
        }

        this->length_ucs2--;

        size_t wrcnt;

        const char *cmd = "\b" VT100_ERASE_LINE_FROM_CUR VT100_SAVE_CURSOR_POSITION;
        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);

        ucs2_buffer_to_utf8_buffer(this, this->cursor_position);
        cmd = this->buffer_utf8;
        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);

        cmd = VT100_RESTORE_CURSOR_POSITION;
        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);
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

        if (this->length_ucs2 == 0 || this->cursor_position == this->length_ucs2) {
                return;
        }

        for (uint i = this->cursor_position; i <= this->length_ucs2; i++) {
                this->buffer_ucs2[i] = this->buffer_ucs2[i + 1];
        }

        this->length_ucs2--;

        size_t wrcnt;

        const char *cmd = VT100_SAVE_CURSOR_POSITION VT100_ERASE_LINE_FROM_CUR;
        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);

        ucs2_buffer_to_utf8_buffer(this, this->cursor_position);
        cmd = this->buffer_utf8;
        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);

        cmd = VT100_RESTORE_CURSOR_POSITION;
        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);
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
                if (this->cursor_position > 0) {
                        size_t      wrcnt;
                        const char *cmd = VT100_SHIFT_CURSOR_LEFT(1);
                        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);
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
                if (this->cursor_position < this->length_ucs2) {
                        size_t      wrcnt;
                        const char *cmd = VT100_SHIFT_CURSOR_RIGHT(1);
                        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);
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
                size_t      wrcnt;
                const char *cmd = VT100_CURSOR_OFF;
                sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);

                while (this->cursor_position > 0) {
                        sys_fwrite("\b", 1, &wrcnt, *this->out_file);
                        this->cursor_position--;
                }

                cmd = VT100_CURSOR_ON;
                sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);
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
                size_t      wrcnt;
                const char *cmd = VT100_CURSOR_OFF;
                sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);

                while (this->cursor_position < this->length_ucs2) {
                        char *cmd = VT100_SHIFT_CURSOR_RIGHT(1);
                        sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);
                        this->cursor_position++;
                }

                cmd = VT100_CURSOR_ON;
                sys_fwrite(cmd, strlen(cmd), &wrcnt, *this->out_file);
        }
}

//==============================================================================
/**
 * @brief  Convert UCS2 buffer to UTF8 buffer.
 *
 * @param  this         editline object
 * @param  pos          position in UCS2 buffer
 */
//==============================================================================
static void ucs2_buffer_to_utf8_buffer(ttyedit_t *this, size_t pos)
{
        if (pos >= this->length_ucs2) {
                memset(this->buffer_utf8, 0, sizeof(this->buffer_utf8));
                return;
        }

        wchar_t *ucs2 = &this->buffer_ucs2[pos];
        char    *utf8 = this->buffer_utf8;

        while (*ucs2) {
                int n = ucs2_to_utf8(*ucs2, utf8);
                if (n > 0) {
                        utf8 += n;
                        ucs2++;
                } else {
                        break;
                }
        }
}

//==============================================================================
/**
 * @brief  Function get next utf8-coded character.
 *
 * @param  text         pointer to text buffer
 *
 * @return Unicode character
 */
//==============================================================================
static wchar_t utf8_to_ucs2(const char **text)
{
        wchar_t unicode = 0;

        if (text && *text && **text != '\0') {

                const char *str = *text;

                char c = *str++;
                if (c >= 0x80) {
                        if ((c & 0xE0) == 0xC0) {
                                if (*str != '\0') {
                                        unicode = ((c & 0x1F) << 6);
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F);
                                }
                                *text += 2;

                        } else if ((c & 0xF0) == 0xE0) {
                                unicode = ((c & 0x0F) << 12);
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 6;
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 0;
                                }
                                *text += 3;

                        } else if ((c & 0xF8) == 0xF0) {
                                if (*str != '\0') {
                                        unicode = ((c & 0x07) << 18);
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 12;
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 6;
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 0;
                                }
                                *text += 4;

                        } else if ((c & 0xFC) == 0xF8) {
                                if (*str != '\0') {
                                        unicode = ((c & 0x03) << 24);
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 18;
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 12;
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 6;
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 0;
                                }
                                *text += 5;

                        } else if ((c & 0xFE) == 0xFC) {
                                if (*str != '\0') {
                                        unicode = ((c & 0x01) << 30);
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 24;
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 18;
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 12;
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 6;
                                }
                                if (*str != '\0') {
                                        unicode |= (*str++ & 0x3F) << 0;
                                }
                                *text += 6;
                        } else {
                                unicode = 0;
                                *text += 1;
                        }
                } else {
                        unicode = c;
                        *text += 1;
                }
        }

        return unicode;
}

//==============================================================================
/**
 * @brief  Convert UCS2 to UTF8.
 *
 * @param  ucs2         UCS2 character
 * @param  utf8         UTF8 buffer output
 *
 * @return Number of converted bytes or -1 if conversion error.
 */
//==============================================================================
static int ucs2_to_utf8(int32_t ucs2, char *utf8)
{
        REJECT_FFFF(ucs2);

        if (ucs2 < 0x80) {
                utf8[0] = ucs2;
                utf8[1] = '\0';
                return 1;
        }

        if (ucs2 < 0x800) {
                utf8[0] = (ucs2 >> 6) | 0xC0;
                utf8[1] = (ucs2 & 0x3F) | 0x80;
                utf8[2] = '\0';
                return 2;
        }

        if (ucs2 < 0xFFFF) {
                utf8[0] = ((ucs2 >> 12)) | 0xE0;
                utf8[1] = ((ucs2 >> 6) & 0x3F) | 0x80;
                utf8[2] = ((ucs2) & 0x3F) | 0x80;
                utf8[3] = '\0';
                REJECT_SURROGATE(ucs2);
                REJECT_NOT_CHAR(ucs2);
                return 3;
        }

        if (ucs2 <= UNICODE_UTF8_4) {
                /* http://tidy.sourceforge.net/cgi-bin/lxr/source/src/utf8.c#L380 */
                utf8[0] = 0xF0 | (ucs2 >> 18);
                utf8[1] = 0x80 | ((ucs2 >> 12) & 0x3F);
                utf8[2] = 0x80 | ((ucs2 >> 6) & 0x3F);
                utf8[3] = 0x80 | ((ucs2 & 0x3F));
                utf8[4] = '\0';
                return 4;
        }

        return -1;
}

//==============================================================================
/**
 * @brief  Calculate number of unicode characters in UTF8 buffer.
 *
 * @param  utf8         UTF8 buffer
 *
 * @return Number characters or -1 if error.
 */
//==============================================================================
static int unicode_count_chars(const char *utf8)
{
        int         chars = 0;
        const char *p     = utf8;
        int         len   = strlen(utf8);

        if (len == 0) {
                return 0;
        }

        while (p - utf8 < len) {
                utf8_to_ucs2(&p);

                chars++;
                if (*p == '\0') {
                        return chars;
                }
        }

        return -1;
}

/*==============================================================================
  End of file
==============================================================================*/
