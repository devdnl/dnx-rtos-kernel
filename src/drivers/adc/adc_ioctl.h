/*==============================================================================
File    adc_ioctl.h

Author  Daniel Zorychta

Brief   Analog-digital converter

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

/**
@defgroup drv-adc ADC Driver

\section drv-adc-desc Description
Driver handles analog-digital converters with the same interface on all
architectures: single conversions of selected channel (software trigger),
resolution, sample time in nanoseconds (the driver selects the nearest longer
sample time of the converter for the current clock), oversampling (average
of samples, by hardware when available), internal temperature sensor and
voltage reference channels, temperature of the chip in m°C.

Pins of channels are configured as analog inputs by the GPIO driver.

\section drv-adc-sup-arch Supported architectures
\li stm32f1 (ADC1..3, 12 bits, channels 0..17),
\li stm32f3 (ADC1..4, 12/10/8/6 bits, channels 1..18; STM32F37x: ADC1 as
    stm32f1),
\li stm32f4, stm32f7 (ADC1..3, 12/10/8/6 bits, channels 0..18),
\li stm32h7 (ADC1..3, 16/14/12/10/8 bits, channels 0..19, hardware
    oversampling; STM32H72x/H73x: ADC1..2),
\li rpcm0 (ADC0, 12 bits, channels 0..3: GPIO26..29),
\li posix (ADC0..3, 12 bits, channels 0..15, voltages of channels set by the
    board model).

Internal channels (@ref ADC_CHANNEL__TEMP, @ref ADC_CHANNEL__VREFINT) are
available on: stm32f1/f3/f4/f7 ADC1, stm32h7 ADC3, rpcm0 (temperature
only), posix.

\section drv-adc-ddesc Details
\subsection drv-adc-ddesc-num Meaning of major and minor numbers
Major number is a converter number (0: ADC1 of STM32, ADC0 of RP2040), minor
number must be 0.

\subsection drv-adc-ddesc-init Driver initialization
@code
driver_init("ADC", 0, 0, "/dev/adc0");
@endcode

\subsection drv-adc-ddesc-release Driver release
@code
driver_release("ADC", 0, 0);
@endcode

\subsection drv-adc-ddesc-cfg Driver configuration
Configuration string (modprobe -c, IOCTL_ADC__CONFIGURE_STR):
\li resolution=<bits> - resolution, 0 (default): maximal,
\li sample_time_ns=<ns> - minimal sample time (default 10000),
\li oversampling=<n> - number of averaged samples, 1..1024, power of 2
    (default 1),
\li vref_mv=<mV> - voltage of reference (default 3300), used to calculate
    voltage of measurement and temperature.

@code
modprobe -i ADC 0 0 /dev/adc0 -c resolution=12,sample_time_ns=5000,oversampling=16
@endcode

\subsection drv-adc-ddesc-read Data read
Position of file / 2 is a channel number, each sample is 16-bit word (native
byte order). Reading 2 bytes at position 6 converts channel 3, reading 8
bytes at position 0 converts channels 0..3. Read beyond the last channel
returns 0 bytes.

@code
#include <stdio.h>
#include <sys/ioctl.h>

FILE *f = fopen("/dev/adc0", "r");

ADC_measurement_t m = {.channel = 5};
if (ioctl(fileno(f), IOCTL_ADC__READ_CHANNEL, &m) == 0) {
        printf("raw %u, %u uV\n", m.raw, m.uV);
}

i32_t mdeg;
if (ioctl(fileno(f), IOCTL_ADC__READ_TEMPERATURE, &mdeg) == 0) {
        printf("chip temperature %d.%03d C\n", mdeg / 1000, abs(mdeg % 1000));
}

fclose(f);
@endcode

\subsection drv-adc-ddesc-write Data write
Not supported.

@{
*/

#ifndef _ADC_IOCTL_H_
#define _ADC_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/ioctl_macros.h"
#include "drivers/class/device/ioctl.h"
#include "sys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/**
 *  @brief  Convert channel.
 *  @param  [WR,RD] @ref ADC_measurement_t * channel, result
 *  @return On success 0 is returned, otherwise -1 and errno is set (ENODEV:
 *          channel not available).
 */
#define IOCTL_ADC__READ_CHANNEL _IOWR(ADC, 0x00, ADC_measurement_t *)

/**
 *  @brief  Configure converter.
 *  @param  [WR] @ref ADC_config_t * configuration
 *  @return On success 0 is returned, otherwise -1 and errno is set.
 */
#define IOCTL_ADC__CONFIGURE _IOW(ADC, 0x01, const ADC_config_t *)

/**
 *  @brief  Read configuration.
 *  @param  [RD] @ref ADC_config_t * configuration
 *  @return On success 0 is returned, otherwise -1 and errno is set.
 */
#define IOCTL_ADC__GET_CONFIGURATION _IOR(ADC, 0x02, ADC_config_t *)

/**
 *  @brief  Read converter information.
 *  @param  [RD] @ref ADC_info_t * information
 *  @return On success 0 is returned, otherwise -1 and errno is set.
 */
#define IOCTL_ADC__GET_INFO _IOR(ADC, 0x03, ADC_info_t *)

/**
 *  @brief  Read temperature of the chip (internal sensor).
 *  @param  [RD] i32_t * temperature [m°C]
 *  @return On success 0 is returned, otherwise -1 and errno is set (ENODEV:
 *          sensor not available on the converter).
 */
#define IOCTL_ADC__READ_TEMPERATURE _IOR(ADC, 0x04, i32_t *)

/**
 *  @brief  Configure converter by configuration string.
 *  @param  [WR] const char * configuration string
 *  @return On success 0 is returned, otherwise -1 and errno is set.
 */
#define IOCTL_ADC__CONFIGURE_STR IOCTL_DEVICE__CONFIGURE_STR

/** Channel of internal temperature sensor. */
#define ADC_CHANNEL__TEMP 0xF0

/** Channel of internal voltage reference. */
#define ADC_CHANNEL__VREFINT 0xF1

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * Converter configuration.
 */
typedef struct {
        u8_t resolution;      /*!< Resolution [bits], 0: maximal.*/
        u16_t oversampling;   /*!< Number of averaged samples (1..1024, power of 2).*/
        u16_t vref_mV;        /*!< Reference voltage [mV].*/
        u32_t sample_time_ns; /*!< Minimal sample time [ns].*/
} ADC_config_t;

/**
 * Measurement.
 */
typedef struct {
        u8_t channel; /*!< [in] Channel number or ADC_CHANNEL__TEMP, ADC_CHANNEL__VREFINT.*/
        u32_t raw;    /*!< [out] Result.*/
        u32_t uV;     /*!< [out] Voltage [uV] (by vref_mV).*/
} ADC_measurement_t;

/**
 * Converter information.
 */
typedef struct {
        u8_t resolution_max;  /*!< Maximal resolution [bits].*/
        u8_t resolution;      /*!< Current resolution [bits].*/
        u8_t channels;        /*!< Number of channels (numbers 0..channels-1).*/
        bool temp;            /*!< Temperature sensor available.*/
        bool vrefint;         /*!< Internal reference available.*/
        u16_t vref_mV;        /*!< Reference voltage [mV].*/
        u16_t oversampling;   /*!< Number of averaged samples.*/
        bool hw_oversampling; /*!< Oversampling by hardware.*/
        u32_t sample_time_ns; /*!< Sample time [ns] set in converter.*/
} ADC_info_t;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _ADC_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
