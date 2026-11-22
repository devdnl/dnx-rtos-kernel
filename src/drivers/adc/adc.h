/*==============================================================================
File    adc.h

Author  Daniel Zorychta

Brief   Analog-digital converter: interface of low level drivers

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

#ifndef _ADC_H_
#define _ADC_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "adc_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/** No internal channel. */
#define _ADC_NO_CHANNEL 0xFF

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * Properties of converter (set by low level driver).
 */
typedef struct {
        u8_t resolution_max;       /*!< Maximal resolution [bits].*/
        u8_t channels;             /*!< Number of channels.*/
        u8_t temp_channel;         /*!< Hardware channel of temperature sensor.*/
        u8_t vrefint_channel;      /*!< Hardware channel of internal reference.*/
        u16_t oversampling_hw_max; /*!< Maximal hardware oversampling (0: none).*/
} ADC_LLD_info_t;

/*==============================================================================
  Exported functions
==============================================================================*/
/**
 * @brief  Enable converter (clock, calibration).
 * @param  major        converter number
 * @param  info         properties of converter
 * @return One of errno value (ENODEV: no converter).
 */
extern int _ADC_LLD__init(u8_t major, ADC_LLD_info_t *info);

/**
 * @brief  Disable converter.
 * @param  major        converter number
 */
extern void _ADC_LLD__deinit(u8_t major);

/**
 * @brief  Configure converter.
 * @param  major        converter number
 * @param  resolution   resolution [bits] (validated by LLD)
 * @param  sample_ns    minimal sample time [ns]
 * @param  oversampling hardware oversampling ratio (1: none)
 * @param  actual_ns    sample time set in converter [ns]
 * @return One of errno value (EINVAL: resolution not supported).
 */
extern int _ADC_LLD__configure(u8_t major, u8_t resolution, u32_t sample_ns, u16_t oversampling, u32_t *actual_ns);

/**
 * @brief  Single conversion.
 * @param  major        converter number
 * @param  channel      hardware channel (also internal)
 * @param  raw          result
 * @return One of errno value (ENODEV: channel not available).
 */
extern int _ADC_LLD__convert(u8_t major, u8_t channel, u32_t *raw);

/**
 * @brief  Temperature calculation.
 * @param  major        converter number
 * @param  raw          result of temperature channel
 * @param  resolution   resolution of result [bits]
 * @param  vref_mV      reference voltage [mV]
 * @return Temperature [m°C].
 */
extern i32_t _ADC_LLD__temperature(u8_t major, u32_t raw, u8_t resolution, u16_t vref_mV);

#ifdef __cplusplus
}
#endif

#endif /* _ADC_H_ */
/*==============================================================================
  End of file
==============================================================================*/
