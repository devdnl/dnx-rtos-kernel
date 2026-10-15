/*==============================================================================
@file    tty_cmd.c

@author  Daniel Zorychta

@brief   Code in this file is responsible for decode VT100 commands.

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
#define VT100_TOKEN_LEN          15
#define VT100_TOKEN_READ_TIMEOUT 250

/* characters that finish VT100 key sequence ('H' added: xterm Home) */
#define VT100_TOKEN_TERMINATORS  "~ABCDFHPQRS"

/*==============================================================================
  Local object types
==============================================================================*/
struct ttycmd {
        void *self;
        char token[VT100_TOKEN_LEN + 1];
        u8_t token_cnt;
        clock_t timer;
};

/*==============================================================================
  Local function prototypes
==============================================================================*/

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
 * @param  this         command object
 * @return If valid then true is returned, otherwise false.
 */
//==============================================================================
static inline bool is_valid(ttycmd_t *this)
{
        return this && this->self == this;
}

//==============================================================================
/**
 * @brief  Check if character is a control character (C0 set or DEL).
 * @param  c            character
 * @return true if control character.
 */
//==============================================================================
static inline bool is_control_char(char c)
{
        unsigned char uc = cast(unsigned char, c);
        return (uc < 0x20) || (uc == 0x7F);
}

//==============================================================================
/**
 * @brief  Reset token collector.
 * @param  this         command object
 */
//==============================================================================
static void token_reset(ttycmd_t *this)
{
        memset(this->token, 0, sizeof(this->token));
        this->token_cnt = 0;
}

//==============================================================================
/**
 * @brief  Decode collected (complete) token.
 * @param  this         command object
 * @return Decoded key or TTYCMD_BUSY if sequence is unknown.
 */
//==============================================================================
static ttycmd_resp_t decode_token(ttycmd_t *this)
{
        const char *t = this->token;

        if (strcmp(VT100_DEL, t) == 0) {
                return TTYCMD_KEY_DELETE;
        } else if (strcmp(VT100_ARROW_LEFT, t) == 0) {
                return TTYCMD_KEY_ARROW_LEFT;
        } else if (strcmp(VT100_ARROW_RIGHT, t) == 0) {
                return TTYCMD_KEY_ARROW_RIGHT;
        } else if (strcmp(VT100_ARROW_UP, t) == 0) {
                return TTYCMD_KEY_ARROW_UP;
        } else if (strcmp(VT100_ARROW_DOWN, t) == 0) {
                return TTYCMD_KEY_ARROW_DOWN;
        } else if (strcmp(VT100_HOME, t) == 0) {
                return TTYCMD_KEY_HOME;
        } else if ((strcmp("\033[H", t) == 0) || (strcmp("\033OH", t) == 0)) {
                return TTYCMD_KEY_HOME;
        } else if (strcmp(VT100_END_1, t) == 0) {
                return TTYCMD_KEY_END;
        } else if (strcmp(VT100_END_2, t) == 0) {
                return TTYCMD_KEY_END;
        } else if ((strcmp("\033[F", t) == 0) || (strcmp("\033OF", t) == 0)) {
                return TTYCMD_KEY_END;
        } else if (strcmp(VT100_F1_XTERM, t) == 0) {
                return TTYCMD_KEY_F1;
        } else if (strcmp(VT100_F1_VT, t) == 0) {
                return TTYCMD_KEY_F1;
        } else if (strcmp(VT100_F2_XTERM, t) == 0) {
                return TTYCMD_KEY_F2;
        } else if (strcmp(VT100_F2_VT, t) == 0) {
                return TTYCMD_KEY_F2;
        } else if (strcmp(VT100_F3_XTERM, t) == 0) {
                return TTYCMD_KEY_F3;
        } else if (strcmp(VT100_F3_VT, t) == 0) {
                return TTYCMD_KEY_F3;
        } else if (strcmp(VT100_F4_XTERM, t) == 0) {
                return TTYCMD_KEY_F4;
        } else if (strcmp(VT100_F4_VT, t) == 0) {
                return TTYCMD_KEY_F4;
        } else if (strcmp(VT100_F5_XTERM, t) == 0) {
                return TTYCMD_KEY_F5;
        } else if (strcmp(VT100_F5_VT, t) == 0) {
                return TTYCMD_KEY_F5;
        } else if (strcmp(VT100_F6_VT, t) == 0) {
                return TTYCMD_KEY_F6;
        } else if (strcmp(VT100_F7_VT, t) == 0) {
                return TTYCMD_KEY_F7;
        } else if (strcmp(VT100_F8_VT, t) == 0) {
                return TTYCMD_KEY_F8;
        } else if (strcmp(VT100_F9_VT, t) == 0) {
                return TTYCMD_KEY_F9;
        } else if (strcmp(VT100_F10_VT, t) == 0) {
                return TTYCMD_KEY_F10;
        } else if (strcmp(VT100_F11_VT, t) == 0) {
                return TTYCMD_KEY_F11;
        } else if (strcmp(VT100_F12_VT, t) == 0) {
                return TTYCMD_KEY_F12;
        } else {
                return TTYCMD_BUSY;
        }
}

//==============================================================================
/**
 * @brief  Initialize command object
 *
 * @param  ttycmd        pointer to target pointer of created object
 *
 * @return One of errno value.
 */
//==============================================================================
int ttycmd_create(ttycmd_t **ttycmd)
{
        int err = sys_zalloc(sizeof(ttycmd_t), cast(void **, ttycmd));
        if (err == ESUCC) {
                (*ttycmd)->self = *ttycmd;
        }

        return err;
}

//==============================================================================
/**
 * @brief Delete command object
 *
 * @param this          command analyze object
 */
//==============================================================================
int ttycmd_destroy(ttycmd_t *this)
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
 * @brief Function analyze input stream
 *
 * @param this          command analyze object
 * @param c             input character
 *
 * @return analyzed command
 */
//==============================================================================
ttycmd_resp_t ttycmd_analyze(ttycmd_t *this, const char c)
{
        if (not is_valid(this)) {
                return TTYCMD_BUSY;
        }

        /*
         * Escape sequence in progress.
         */
        if (this->token_cnt > 0) {
                if (sys_is_time_expired(this->timer, VT100_TOKEN_READ_TIMEOUT)) {
                        /* incomplete sequence is abandoned, character is handled normally */
                        token_reset(this);

                } else if (c == '\033') {
                        /* new sequence started, handled below */

                } else if (is_control_char(c)) {
                        /* control character aborts sequence and is handled normally */
                        token_reset(this);

                } else if (strchr(VT100_TOKEN_TERMINATORS, c)) {
                        if (this->token_cnt < VT100_TOKEN_LEN) {
                                this->token[this->token_cnt++] = c;
                        }

                        ttycmd_resp_t resp = decode_token(this);
                        token_reset(this);
                        return resp;

                } else {
                        if (this->token_cnt < VT100_TOKEN_LEN) {
                                this->token[this->token_cnt++] = c;
                        } else {
                                token_reset(this);
                        }

                        return TTYCMD_BUSY;
                }
        }

        if (c == '\033') {
                token_reset(this);
                this->token[0] = c;
                this->token_cnt = 1;
                this->timer = sys_get_uptime_ms();
                return TTYCMD_BUSY;
        }

        /* NOTE: strchr("\r\n", c) also matches '\0' (terminator) */
        if ((c == '\r') || (c == '\n')) {
                return TTYCMD_KEY_ENTER;
        }

        if ((c == '\b') or (c == 127)) {
                return TTYCMD_KEY_BACKSPACE;
        }

        if (c == '\t') {
                return TTYCMD_KEY_TAB;
        }

        if (c == ETX) {
                return TTYCMD_KEY_ENDTEXT;
        }

        return TTYCMD_KEY_CHAR;
}

//==============================================================================
/**
 * @brief Check if command decoder is in idle state
 *
 * @param this          command analyze object
 *
 * @return true if idle, false if operation in progress
 */
//==============================================================================
bool ttycmd_is_idle(ttycmd_t *this)
{
        if (is_valid(this)) {
                return (this->token_cnt == 0)
                    || sys_is_time_expired(this->timer, VT100_TOKEN_READ_TIMEOUT);
        }

        return false;
}

/*==============================================================================
  End of file
==============================================================================*/
