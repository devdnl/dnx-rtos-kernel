/*==============================================================================
File    urandom_lld.c

Author  Daniel Zorychta

Brief   Random number generator: entropy source of RP2040 (ring oscillator)

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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
#include "../urandom.h"
#include "hardware/structs/rosc.h"
#include "hardware/structs/timer.h"

/*==============================================================================
  Local macros
==============================================================================*/
/* number of random bits XORed into each output bit (ring oscillator bit is
 * correlated, oversampling and mixing in driver make output unpredictable) */
#define OVERSAMPLING 8

/*==============================================================================
  Local objects
==============================================================================*/
static URANDOM_source_t source;
static u8_t users;

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Enable entropy source. Ring oscillator works when random bit
 *         changes (oscillator can be disabled by clock configuration).
 */
//==============================================================================
URANDOM_source_t _URANDOM_LLD__init(void)
{
        if (users++ == 0) {
                u32_t ones = 0;

                for (int i = 0; i < 256; i++) {
                        ones += rosc_hw->randombit & 1;
                }

                source = ((ones > 0) && (ones < 256)) ? URANDOM_SOURCE__HARDWARE : URANDOM_SOURCE__WEAK;
        }

        return source;
}

//==============================================================================
/**
 * @brief  Disable entropy source.
 */
//==============================================================================
void _URANDOM_LLD__deinit(void)
{
        if (users) {
                users--;
        }
}

//==============================================================================
/**
 * @brief  Read entropy (ring oscillator bits XORed with microsecond timer).
 */
//==============================================================================
int _URANDOM_LLD__read(u8_t *dst, size_t len)
{
        for (size_t i = 0; i < len; i++) {
                u8_t byte = cast(u8_t, timer_hw->timerawl);

                if (source == URANDOM_SOURCE__HARDWARE) {
                        for (int bit = 0; bit < 8 * OVERSAMPLING; bit++) {
                                byte ^= (rosc_hw->randombit & 1) << (bit % 8);
                        }
                }

                dst[i] = byte;
        }

        return ESUCC;
}

/*==============================================================================
  End of file
==============================================================================*/
