/*==============================================================================
File    adc_lld.c

Author  Daniel Zorychta

Brief   Analog-digital converter, RP2040 (channels 0..3: GPIO26..29,
        channel 4: temperature sensor)

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
#include "../adc.h"
#include "rpcm0/misc/misc.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/adc.h"
#include "hardware/regs/resets.h"
#include "hardware/structs/adc.h"
#include "hardware/structs/resets.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define CHANNELS    4
#define CH_TEMP     4
#define CONV_CYCLES 96
#define TIMEOUT_MS  10

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Function waits for ready flag.
 */
//==============================================================================
static int wait_ready(void)
{
        clock_t tref = sys_get_uptime_ms();

        while (!(adc_hw->cs & ADC_CS_READY_BITS)) {
                if (sys_is_time_expired(tref, TIMEOUT_MS)) {
                        return ETIME;
                }
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Enable converter.
 */
//==============================================================================
int _ADC_LLD__init(u8_t major, ADC_LLD_info_t *info)
{
        if ((major != 0) || (clocks__get_clk_adc_frequency() == 0)) {
                return ENODEV;
        }

        info->resolution_max = 12;
        info->channels = CHANNELS;
        info->temp_channel = CH_TEMP;
        info->vrefint_channel = _ADC_NO_CHANNEL;
        info->oversampling_hw_max = 0;

        SET_BIT(resets_hw->reset, RESETS_RESET_ADC_BITS);
        CLEAR_BIT(resets_hw->reset, RESETS_RESET_ADC_BITS);
        while (!(resets_hw->reset_done & RESETS_RESET_DONE_ADC_BITS)) {
        }

        adc_hw->cs = ADC_CS_EN_BITS;

        return wait_ready();
}

//==============================================================================
/**
 * @brief  Disable converter.
 */
//==============================================================================
void _ADC_LLD__deinit(u8_t major)
{
        UNUSED_ARG1(major);

        adc_hw->cs = 0;
        SET_BIT(resets_hw->reset, RESETS_RESET_ADC_BITS);
}

//==============================================================================
/**
 * @brief  Configure converter (12 bits, fixed conversion time of 96 cycles).
 */
//==============================================================================
int _ADC_LLD__configure(u8_t major, u8_t res, u32_t sample_ns, u16_t oversampling, u32_t *actual_ns)
{
        UNUSED_ARG3(major, sample_ns, oversampling);

        if (res != 12) {
                return EINVAL;
        }

        *actual_ns = (cast(u64_t, CONV_CYCLES) * 1000000000ULL) / clocks__get_clk_adc_frequency();

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Single conversion.
 */
//==============================================================================
int _ADC_LLD__convert(u8_t major, u8_t channel, u32_t *raw)
{
        UNUSED_ARG1(major);

        u32_t cs = ADC_CS_EN_BITS | ADC_CS_START_ONCE_BITS | (cast(u32_t, channel) << ADC_CS_AINSEL_LSB);
        if (channel == CH_TEMP) {
                cs |= ADC_CS_TS_EN_BITS;
        }

        adc_hw->cs = cs;

        int err = wait_ready();
        if (!err) {
                if (adc_hw->cs & ADC_CS_ERR_BITS) {
                        err = EIO;
                } else {
                        *raw = adc_hw->result;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Temperature: 0.706 V at 27 C, -1.721 mV/C.
 */
//==============================================================================
i32_t _ADC_LLD__temperature(u8_t major, u32_t raw, u8_t res, u16_t vref_mV)
{
        UNUSED_ARG1(major);

        i64_t uv = (cast(i64_t, raw) * vref_mV * 1000) / ((1 << res) - 1);
        return 27000 - ((uv - 706000) * 1000) / 1721;
}

/*==============================================================================
  End of file
==============================================================================*/
