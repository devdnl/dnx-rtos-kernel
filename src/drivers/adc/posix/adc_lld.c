/*==============================================================================
File    adc_lld.c

Author  Daniel Zorychta

Brief   Analog-digital converter, POSIX simulator (voltages of channels are
        set by the board model)

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
#include "posix/host/host.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define ADC_COUNT  4
#define CHANNELS   16
#define CH_TEMP    16
#define CH_VREFINT 17
#define VREF_UV    3300000ULL /* reference voltage of simulated converter */
#define ADC_CLK_HZ 36000000ULL

/*==============================================================================
  Local objects
==============================================================================*/
/* sample times of STM32F4 [cycles x 2] (selection is tested in simulator) */
static const u16_t SMP_X2[] = {6, 30, 56, 112, 168, 224, 288, 960};

static u8_t resolution[ADC_COUNT];

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Enable converter.
 */
//==============================================================================
int _ADC_LLD__init(u8_t major, ADC_LLD_info_t *info)
{
        if (major >= ADC_COUNT) {
                return ENODEV;
        }

        info->resolution_max = 12;
        info->channels = CHANNELS;
        info->temp_channel = CH_TEMP;
        info->vrefint_channel = CH_VREFINT;
        info->oversampling_hw_max = 0;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Disable converter.
 */
//==============================================================================
void _ADC_LLD__deinit(u8_t major)
{
        UNUSED_ARG1(major);
}

//==============================================================================
/**
 * @brief  Configure converter (resolution 12, 10, 8, 6 bits).
 */
//==============================================================================
int _ADC_LLD__configure(u8_t major, u8_t res, u32_t sample_ns, u16_t oversampling, u32_t *actual_ns)
{
        UNUSED_ARG1(oversampling);

        if ((res < 6) || (res > 12) || (res % 2)) {
                return EINVAL;
        }

        u64_t need = ((cast(u64_t, sample_ns) * ADC_CLK_HZ * 2) + 999999999ULL) / 1000000000ULL;

        size_t i = 0;
        while ((i < ARRAY_SIZE(SMP_X2) - 1) && (SMP_X2[i] < need)) {
                i++;
        }

        *actual_ns = (cast(u64_t, SMP_X2[i]) * 1000000000ULL) / (ADC_CLK_HZ * 2);
        resolution[major] = res;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Single conversion (voltage of board model).
 */
//==============================================================================
int _ADC_LLD__convert(u8_t major, u8_t channel, u32_t *raw)
{
        u32_t uv;
        if (_host_adc_read(major, channel, &uv) != 0) {
                return ENODEV;
        }

        u64_t max = (1U << resolution[major]) - 1;
        u64_t val = ((cast(u64_t, uv) * max) + (VREF_UV / 2)) / VREF_UV;
        *raw = min(val, max);

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Temperature: sensor 0.76 V at 25 C, 2.5 mV/C.
 */
//==============================================================================
i32_t _ADC_LLD__temperature(u8_t major, u32_t raw, u8_t res, u16_t vref_mV)
{
        UNUSED_ARG1(major);

        i64_t uv = (cast(i64_t, raw) * vref_mV * 1000) / ((1 << res) - 1);
        return 25000 + ((uv - 760000) * 2) / 5;
}

/*==============================================================================
  End of file
==============================================================================*/
