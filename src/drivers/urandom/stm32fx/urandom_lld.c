/*==============================================================================
File    urandom_lld.c

Author  Daniel Zorychta

Brief   Random number generator: entropy source of STM32 (RNG peripheral or weak source)

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

        This program is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License version 2 as
        published by the Free Software Foundation.

        The dnx RTOS is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
        GNU General Public License for more details.

        Programs using the kernel by system calls are not covered by the license,
        drivers and file systems of drvset partition: exception, see the license
        file: doc/license.txt.

==============================================================================*/

/*==============================================================================
  Include files
==============================================================================*/
#include "../urandom.h"

#if defined(ARCH_stm32f1)
#include "stm32f1/stm32f10x.h"
#define UID_BASE 0x1FFFF7E8UL
#elif defined(ARCH_stm32f3)
#include "stm32f3/stm32f3xx.h"
#elif defined(ARCH_stm32f4)
#include "stm32f4/stm32f4xx.h"
#elif defined(ARCH_stm32f7)
#include "stm32f7/stm32f7xx.h"
#elif defined(ARCH_stm32h7)
#include "stm32h7/stm32h7xx.h"
#endif

/*==============================================================================
  Local macros
==============================================================================*/
#define TIMEOUT_MS   10
#define UID_SIZE     12
#define REPEAT_LIMIT 4

/*==============================================================================
  Local object types
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/
#if defined(RNG)
static bool rng_start(void);
static bool rng_word(u32_t *word);
#endif

/*==============================================================================
  Local objects
==============================================================================*/
static u8_t users;
static URANDOM_source_t source;

#if defined(RNG)
static u32_t last_word;
#endif

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Enable entropy source.
 */
//==============================================================================
URANDOM_source_t _URANDOM_LLD__init(void)
{
        if (users++ == 0) {
                source = URANDOM_SOURCE__WEAK;
#if defined(RNG)
                if (rng_start()) {
                        source = URANDOM_SOURCE__HARDWARE;
                } else {
                        RNG->CR = 0;
                        CLEAR_BIT(RCC->AHB2ENR, RCC_AHB2ENR_RNGEN);
                }
#endif
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
        if (users && (--users == 0)) {
#if defined(RNG)
                if (source == URANDOM_SOURCE__HARDWARE) {
                        RNG->CR = 0;
                        CLEAR_BIT(RCC->AHB2ENR, RCC_AHB2ENR_RNGEN);
                }
#endif
        }
}

//==============================================================================
/**
 * @brief  Read entropy.
 */
//==============================================================================
int _URANDOM_LLD__read(u8_t *dst, size_t len)
{
#if defined(RNG)
        if (source == URANDOM_SOURCE__HARDWARE) {
                int err = ESUCC;

                sys_scheduler_lock();

                while (len) {
                        u32_t word;
                        if (!rng_word(&word)) {
                                err = EIO;
                                break;
                        }

                        size_t n = min(len, sizeof(word));
                        memcpy(dst, &word, n);
                        dst += n;
                        len -= n;
                }

                sys_scheduler_unlock();

                return err;
        }
#endif

        /* weak source: unique device ID and system timer */
        const u8_t *uid = cast(const u8_t *, UID_BASE);

        for (size_t i = 0; i < len; i++) {
                dst[i] = uid[i % UID_SIZE] ^ cast(u8_t, SysTick->VAL);
        }

        return ESUCC;
}

#if defined(RNG)
//==============================================================================
/**
 * @brief  Enable RNG clock and generator, the first word is discarded.
 *
 * @return True when generator works, false otherwise (e.g. no 48 MHz clock).
 */
//==============================================================================
static bool rng_start(void)
{
        SET_BIT(RCC->AHB2ENR, RCC_AHB2ENR_RNGEN);
        (void)READ_BIT(RCC->AHB2ENR, RCC_AHB2ENR_RNGEN);

#if defined(RCC_D2CCIP2R_RNGSEL) || defined(RCC_CDCCIP2R_RNGSEL)
#if defined(RCC_D2CCIP2R_RNGSEL)
        bool hsi48 = (RCC->D2CCIP2R & RCC_D2CCIP2R_RNGSEL) == 0;
#else
        bool hsi48 = (RCC->CDCCIP2R & RCC_CDCCIP2R_RNGSEL) == 0;
#endif
        /* HSI48 is RNG clock source: enable it (it can be also used by USB) */
        if (hsi48 && !(RCC->CR & RCC_CR_HSI48RDY)) {
                SET_BIT(RCC->CR, RCC_CR_HSI48ON);

                clock_t tref = sys_get_uptime_ms();
                while (!(RCC->CR & RCC_CR_HSI48RDY)) {
                        if (sys_is_time_expired(tref, TIMEOUT_MS)) {
                                return false;
                        }
                }
        }
#endif

        RNG->CR = RNG_CR_RNGEN;
        last_word = 0;

        u32_t word;
        return rng_word(&word);
}

//==============================================================================
/**
 * @brief  Read word of RNG. Seed error restarts generator, word equal to
 *         previous one is discarded (continuous test).
 *
 * @param  word         read word
 *
 * @return True on success, false on timeout.
 */
//==============================================================================
static bool rng_word(u32_t *word)
{
        clock_t tref = sys_get_uptime_ms();
        int repeated = 0;

        while (!sys_is_time_expired(tref, TIMEOUT_MS)) {
                u32_t sr = RNG->SR;

                if (sr & (RNG_SR_SECS | RNG_SR_SEIS)) {
                        CLEAR_BIT(RNG->SR, RNG_SR_SEIS);
                        CLEAR_BIT(RNG->CR, RNG_CR_RNGEN);
                        SET_BIT(RNG->CR, RNG_CR_RNGEN);

                } else if (sr & RNG_SR_CEIS) {
                        CLEAR_BIT(RNG->SR, RNG_SR_CEIS);

                } else if (sr & RNG_SR_DRDY) {
                        *word = RNG->DR;

                        if ((*word != last_word) && (*word != 0)) {
                                last_word = *word;
                                return true;
                        }

                        if (++repeated >= REPEAT_LIMIT) {
                                break;
                        }
                }
        }

        return false;
}
#endif

/*==============================================================================
  End of file
==============================================================================*/
