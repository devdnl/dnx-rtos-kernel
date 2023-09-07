/*=========================================================================*//**
@file    gpio_ioctl.h

@author  Daniel Zorychta

@brief   This driver support GPIO ioctl request codes.

@note    Copyright (C) 2014 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/**
@defgroup drv-gpio GPIO Driver

\section drv-gpio-desc Description
Driver handles GPIO peripheral.

\section drv-gpio-sup-arch Supported architectures
\li stm32f1
\li stm32f3
\li stm32f4
\li stm32f7
\li stm32h7

\section drv-gpio-ddesc Details
\subsection drv-gpio-ddesc-num Meaning of major and minor numbers
Some manufactures enumerate devices starting from 1 instead of 0 (e.g. ST).
In this case major number starts from 0 and is connected to the first device
e.g. GPIOA. Major number selects GPIO peripheral.

\subsubsection drv-gpio-ddesc-numres Numeration restrictions
Number of peripherals determines how big the major number can be. If there is
only one GPIO peripheral then the major number is always 0.

\subsection drv-gpio-ddesc-init Driver initialization
To initialize driver the following code can be used:

@code
driver_init("GPIO", 0, 0, "/dev/GPIOA");
@endcode
@code
driver_init("GPIO", 1, 0, "/dev/GPIOB");
@endcode

\subsection drv-gpio-ddesc-release Driver release
To release driver the following code can be used:
@code
driver_release("GPIO", 0, 0);
@endcode
@code
driver_release("GPIO", 1, 0);
@endcode

\subsection drv-gpio-ddesc-cfg Driver configuration
Driver configuration can be done by using Configtool or configuration files.

\subsection drv-gpio-ddesc-write Data write
Data to the GPIO device can be wrote as regular file. The entire wide of port
is used to send bytes. If port is 16-bit then entire word will set output
pins to selected state.

\subsection drv-gpio-ddesc-read Data read
Data to the GPIO device can be read as regular file. The entire word of port
is read to buffer in read operation.

\subsection drv-gpio-ddesc-pinctr Pin control
Each bit can be controlled by using ioctl() function. There is possibility to
control selected pin on not opened port for example: user opened GPIOA and
by using IOCTL_GPIO__SET_PIN_IN_PORT ioctl()'s request can control pin in
GPIOB. This is possible only by port index.

@code
#include <stdio.h>
#include <sys/ioctl.h>

FILE *f = fopen("/dev/GPIOA", "r+);
if (f) {

     // control pin on port A...
     static const u8_t LED = IOCTL_GPIO_PIN_IDX__LED;
     ioctl(fileno(f), IOCTL_GPIO__CLEAR_PIN, &LED);

     // ... this same can be done by using alternative interface:
     static const GPIO_pin_in_port_t LED_PORT = {
            .pin_idx  = IOCTL_GPIO_PIN_IDX__LED,
            .port_idx = IOCTL_GPIO_PORT_IDX__LED
     };
     ioctl(fileno(f), IOCTL_GPIO__CLEAR_PIN_IN_PORT, &LED_PORT);


     // there is possiblity to control pin on other port
     static const GPIO_pin_in_port_t OTHER_PIN = {
            .pin_idx  = IOCTL_GPIO_PIN_IDX__OTHER_PIN,
            .port_idx = IOCTL_GPIO_PORT_IDX__OTHER_PIN
     };
     ioctl(fileno(f), IOCTL_GPIO__SET_PIN_IN_PORT, &OTHER_PIN);

     fclose(f);

} else {
     perror(NULL);
}
@endcode

There are restrictions that allow user to control pins by using other port
file: the port that is controlled should be earlier initialized. If port is
not initialized then pin behavior is undefined (depends on microcontroller).

@{
*/

#ifndef _GPIO_IOCTL_H_
#define _GPIO_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/ioctl_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * Type used to read pin state
 */
typedef struct {
        u8_t pin_idx; /*!< Pin index.*/
        int  state;   /*!< Pin state.*/
} GPIO_pin_state_t;

/**
 * Type represent pin on port selected by index.
 */
typedef struct {
        u8_t port_idx; /*!< Port index.*/
        u8_t pin_idx;  /*!< Pin index.*/
} GPIO_pin_in_port_t;

/**
 * Type represent pin state on port selected by index.
 */
typedef struct {
        GPIO_pin_in_port_t pin;   /*!< Pin address.*/
        int                state; /*!< Pin state.*/
} GPIO_pin_in_port_state_t;

/**
 * GPIO pin speed definitions.
 */
typedef enum {
        GPIO_pin_speed__low,       /*!< Low speed.*/
        GPIO_pin_speed__medium,    /*!< Medium speed.*/
        GPIO_pin_speed__high,      /*!< High speed.*/
        GPIO_pin_speed__very_high, /*!< Very high speed.*/
        GPIO_pin_speed_amount,
} GPIO_pin_speed_t;

/**
 * GPIO pin mode definitions.
 */
typedef enum {
        GPIO_pin_mode__analog,       /*!< Analog input/output.*/
        GPIO_pin_mode__input,        /*!< Input.*/
        GPIO_pin_mode__input_PU,     /*!< Input with Pull-Up.*/
        GPIO_pin_mode__input_PD,     /*!< Input with Pull-Down.*/
        GPIO_pin_mode__output_PP,    /*!< Output Push-Pull.*/
        GPIO_pin_mode__output_OD,    /*!< Output Open Drain.*/
        GPIO_pin_mode__output_OD_PU, /*!< Output Open Drain Pull-Up.*/
        GPIO_pin_mode__output_OD_PD, /*!< Output Open Drain Pull-Down.*/
        GPIO_pin_mode_amount,
} GPIO_pin_mode_t;

/**
 * Type represent pin multiplexer.
 */
typedef enum {
        GPIO_pin_mux__disable,
#if defined(ARCH_stm32f1)
        GPIO_pin_mux__alternate_function_output,
#elif defined(ARCH_stm32f3)
        GPIO_pin_mux__AF0_MCO,
        GPIO_pin_mux__AF0_RTC_50Hz,
        GPIO_pin_mux__AF0_RTC_50Hz,
        GPIO_pin_mux__AF0_SWJ,
        GPIO_pin_mux__AF0_TAMPER,
        GPIO_pin_mux__AF0_TRACE,
        GPIO_pin_mux__AF1_EVENTOUT,
        GPIO_pin_mux__AF1_TIM15,
        GPIO_pin_mux__AF1_TIM16,
        GPIO_pin_mux__AF1_TIM17,
        GPIO_pin_mux__AF1_TIM2,
        GPIO_pin_mux__AF2_COMP1,
        GPIO_pin_mux__AF2_I2C3,
        GPIO_pin_mux__AF2_TIM1,
        GPIO_pin_mux__AF2_TIM13,
        GPIO_pin_mux__AF2_TIM14,
        GPIO_pin_mux__AF2_TIM15,
        GPIO_pin_mux__AF2_TIM16,
        GPIO_pin_mux__AF2_TIM19,
        GPIO_pin_mux__AF2_TIM2,
        GPIO_pin_mux__AF2_TIM20,
        GPIO_pin_mux__AF2_TIM3,
        GPIO_pin_mux__AF2_TIM4,
        GPIO_pin_mux__AF2_TIM5,
        GPIO_pin_mux__AF2_TIM8,
        GPIO_pin_mux__AF3_COMP7,
        GPIO_pin_mux__AF3_HRTIM1,
        GPIO_pin_mux__AF3_I2C3,
        GPIO_pin_mux__AF3_I2C3,
        GPIO_pin_mux__AF3_TIM15,
        GPIO_pin_mux__AF3_TIM15,
        GPIO_pin_mux__AF3_TIM20,
        GPIO_pin_mux__AF3_TIM8,
        GPIO_pin_mux__AF3_TSC,
        GPIO_pin_mux__AF4_I2C1,
        GPIO_pin_mux__AF4_I2C2,
        GPIO_pin_mux__AF4_TIM1,
        GPIO_pin_mux__AF4_TIM16,
        GPIO_pin_mux__AF4_TIM17,
        GPIO_pin_mux__AF4_TIM8,
        GPIO_pin_mux__AF5_I2S,
        GPIO_pin_mux__AF5_I2S2ext,
        GPIO_pin_mux__AF5_IR,
        GPIO_pin_mux__AF5_SPI1,
        GPIO_pin_mux__AF5_SPI1,
        GPIO_pin_mux__AF5_SPI1,
        GPIO_pin_mux__AF5_SPI2,
        GPIO_pin_mux__AF5_SPI3,
        GPIO_pin_mux__AF5_SPI4,
        GPIO_pin_mux__AF5_TIM8,
        GPIO_pin_mux__AF5_UART4,
        GPIO_pin_mux__AF5_UART5,
        GPIO_pin_mux__AF6_CEC,
        GPIO_pin_mux__AF6_I2S3ext,
        GPIO_pin_mux__AF6_IR,
        GPIO_pin_mux__AF6_SPI1,
        GPIO_pin_mux__AF6_SPI2,
        GPIO_pin_mux__AF6_SPI3,
        GPIO_pin_mux__AF6_TIM1,
        GPIO_pin_mux__AF6_TIM20,
        GPIO_pin_mux__AF6_TIM8,
        GPIO_pin_mux__AF7_CAN,
        GPIO_pin_mux__AF7_CAN,
        GPIO_pin_mux__AF7_CEC,
        GPIO_pin_mux__AF7_COMP3,
        GPIO_pin_mux__AF7_COMP5,
        GPIO_pin_mux__AF7_COMP6,
        GPIO_pin_mux__AF7_GPCOMP6,
        GPIO_pin_mux__AF7_GPCOMP6,
        GPIO_pin_mux__AF7_USART1,
        GPIO_pin_mux__AF7_USART2,
        GPIO_pin_mux__AF7_USART3,
        GPIO_pin_mux__AF8_COMP1,
        GPIO_pin_mux__AF8_COMP2,
        GPIO_pin_mux__AF8_COMP3,
        GPIO_pin_mux__AF8_COMP4,
        GPIO_pin_mux__AF8_COMP5,
        GPIO_pin_mux__AF8_COMP6,
        GPIO_pin_mux__AF8_GPCOMP2,
        GPIO_pin_mux__AF8_GPCOMP4,
        GPIO_pin_mux__AF8_GPCOMP6,
        GPIO_pin_mux__AF8_I2C3,
        GPIO_pin_mux__AF8_I2C3,
        GPIO_pin_mux__AF8_I2C3,
        GPIO_pin_mux__AF9_CAN,
        GPIO_pin_mux__AF9_CAN,
        GPIO_pin_mux__AF9_TIM1,
        GPIO_pin_mux__AF9_TIM12,
        GPIO_pin_mux__AF9_TIM13,
        GPIO_pin_mux__AF9_TIM14,
        GPIO_pin_mux__AF9_TIM15,
        GPIO_pin_mux__AF9_TIM8,
        GPIO_pin_mux__AF10_TIM12,
        GPIO_pin_mux__AF10_TIM17,
        GPIO_pin_mux__AF10_TIM2,
        GPIO_pin_mux__AF10_TIM3,
        GPIO_pin_mux__AF10_TIM4,
        GPIO_pin_mux__AF10_TIM8,
        GPIO_pin_mux__AF11_TIM1,
        GPIO_pin_mux__AF11_TIM1,
        GPIO_pin_mux__AF11_TIM19,
        GPIO_pin_mux__AF11_TIM8,
        GPIO_pin_mux__AF12_FMC,
        GPIO_pin_mux__AF12_HRTIM1,
        GPIO_pin_mux__AF12_SDIO,
        GPIO_pin_mux__AF12_TIM1,
        GPIO_pin_mux__AF12_TIM1,
        GPIO_pin_mux__AF13_HRTIM1,
        GPIO_pin_mux__AF13_OPAMP2,
        GPIO_pin_mux__AF14_USB,
        GPIO_pin_mux__AF15_EVENTOUT,
#elif defined(ARCH_stm32f4)
        GPIO_pin_mux__AF0_MCO,
        GPIO_pin_mux__AF0_RTC_50Hz,
        GPIO_pin_mux__AF0_SWJ,
        GPIO_pin_mux__AF0_TAMPER,
        GPIO_pin_mux__AF0_TRACE,
        GPIO_pin_mux__AF1_LPTIM1,
        GPIO_pin_mux__AF1_TIM1,
        GPIO_pin_mux__AF1_TIM2,
        GPIO_pin_mux__AF2_TIM3,
        GPIO_pin_mux__AF2_TIM4,
        GPIO_pin_mux__AF2_TIM5,
        GPIO_pin_mux__AF3_CEC,
        GPIO_pin_mux__AF3_DFSDM2,
        GPIO_pin_mux__AF3_TIM10,
        GPIO_pin_mux__AF3_TIM11,
        GPIO_pin_mux__AF3_TIM8,
        GPIO_pin_mux__AF3_TIM9,
        GPIO_pin_mux__AF4_CEC,
        GPIO_pin_mux__AF4_FMPI2C1,
        GPIO_pin_mux__AF4_I2C1,
        GPIO_pin_mux__AF4_I2C1,
        GPIO_pin_mux__AF4_I2C2,
        GPIO_pin_mux__AF4_I2C2,
        GPIO_pin_mux__AF4_I2C3,
        GPIO_pin_mux__AF4_I2C3,
        GPIO_pin_mux__AF5_I2S3ext,
        GPIO_pin_mux__AF5_I2S3ext,
        GPIO_pin_mux__AF5_SPI3,
        GPIO_pin_mux__AF5_SPI1,
        GPIO_pin_mux__AF5_SPI1,
        GPIO_pin_mux__AF5_SPI2,
        GPIO_pin_mux__AF5_SPI3,
        GPIO_pin_mux__AF5_SPI3,
        GPIO_pin_mux__AF5_SPI4,
        GPIO_pin_mux__AF5_SPI4,
        GPIO_pin_mux__AF5_SPI5,
        GPIO_pin_mux__AF5_SPI6,
        GPIO_pin_mux__AF6_DFSDM1,
        GPIO_pin_mux__AF6_DFSDM2,
        GPIO_pin_mux__AF6_I2S2ext,
        GPIO_pin_mux__AF6_SAI1,
        GPIO_pin_mux__AF6_SPI1,
        GPIO_pin_mux__AF6_SPI2,
        GPIO_pin_mux__AF6_SPI2,
        GPIO_pin_mux__AF6_SPI3,
        GPIO_pin_mux__AF6_SPI4,
        GPIO_pin_mux__AF6_SPI4,
        GPIO_pin_mux__AF6_SPI5,
        GPIO_pin_mux__AF7_DFSDM2,
        GPIO_pin_mux__AF7_I2S3ext,
        GPIO_pin_mux__AF7_SAI1,
        GPIO_pin_mux__AF7_SPDIFRX,
        GPIO_pin_mux__AF7_SPI2,
        GPIO_pin_mux__AF7_SPI3,
        GPIO_pin_mux__AF7_UART5,
        GPIO_pin_mux__AF7_USART1,
        GPIO_pin_mux__AF7_USART2,
        GPIO_pin_mux__AF7_USART3,
        GPIO_pin_mux__AF8_CAN1,
        GPIO_pin_mux__AF8_DFSDM1,
        GPIO_pin_mux__AF8_SAI2,
        GPIO_pin_mux__AF8_SPDIFRX,
        GPIO_pin_mux__AF8_UART4,
        GPIO_pin_mux__AF8_UART5,
        GPIO_pin_mux__AF8_UART7,
        GPIO_pin_mux__AF8_UART7,
        GPIO_pin_mux__AF8_UART8,
        GPIO_pin_mux__AF8_USART3,
        GPIO_pin_mux__AF8_USART6,
        GPIO_pin_mux__AF9_CAN1,
        GPIO_pin_mux__AF9_CAN1,
        GPIO_pin_mux__AF9_CAN2,
        GPIO_pin_mux__AF9_CAN2,
        GPIO_pin_mux__AF9_CAN2,
        GPIO_pin_mux__AF9_CAN2,
        GPIO_pin_mux__AF9_FMPI2C1,
        GPIO_pin_mux__AF9_I2C2,
        GPIO_pin_mux__AF9_I2C2,
        GPIO_pin_mux__AF9_I2C3,
        GPIO_pin_mux__AF9_I2C3,
        GPIO_pin_mux__AF9_LTDC,
        GPIO_pin_mux__AF9_QSPI,
        GPIO_pin_mux__AF9_QSPI,
        GPIO_pin_mux__AF9_TIM12,
        GPIO_pin_mux__AF9_TIM12,
        GPIO_pin_mux__AF9_TIM13,
        GPIO_pin_mux__AF9_TIM13,
        GPIO_pin_mux__AF9_TIM14,
        GPIO_pin_mux__AF9_TIM14,
        GPIO_pin_mux__AF10_DFSDM1,
        GPIO_pin_mux__AF10_DFSDM2,
        GPIO_pin_mux__AF10_FMC,
        GPIO_pin_mux__AF10_FSMC,
        GPIO_pin_mux__AF10_OTG_FS,
        GPIO_pin_mux__AF10_OTG_HS,
        GPIO_pin_mux__AF10_QSPI,
        GPIO_pin_mux__AF10_QSPI,
        GPIO_pin_mux__AF10_SAI1,
        GPIO_pin_mux__AF10_SAI2,
        GPIO_pin_mux__AF11_CAN3,
        GPIO_pin_mux__AF11_ETH,
        GPIO_pin_mux__AF11_UART10,
        GPIO_pin_mux__AF11_UART4,
        GPIO_pin_mux__AF11_UART5,
        GPIO_pin_mux__AF11_UART9,
        GPIO_pin_mux__AF12_FMC,
        GPIO_pin_mux__AF12_FSMC,
        GPIO_pin_mux__AF12_FSMC,
        GPIO_pin_mux__AF12_OTG_HS_FS,
        GPIO_pin_mux__AF12_SDIO,
        GPIO_pin_mux__AF12_SDIO,
        GPIO_pin_mux__AF13_DCMI,
        GPIO_pin_mux__AF13_DSI,
        GPIO_pin_mux__AF14_LTDC,
        GPIO_pin_mux__AF14_RNG,
        GPIO_pin_mux__AF15_EVENTOUT,
#elif defined(ARCH_stm32f7)
        GPIO_pin_mux__AF0_MCO,
        GPIO_pin_mux__AF0_RTC_50Hz,
        GPIO_pin_mux__AF0_SWJ,
        GPIO_pin_mux__AF0_TRACE,
        GPIO_pin_mux__AF1_I2C4,
        GPIO_pin_mux__AF1_TIM1,
        GPIO_pin_mux__AF1_TIM2,
        GPIO_pin_mux__AF1_UART5,
        GPIO_pin_mux__AF2_TIM3,
        GPIO_pin_mux__AF2_TIM4,
        GPIO_pin_mux__AF2_TIM5,
        GPIO_pin_mux__AF3_CEC,
        GPIO_pin_mux__AF3_DFSDM1,
        GPIO_pin_mux__AF3_LPTIM1,
        GPIO_pin_mux__AF3_TIM10,
        GPIO_pin_mux__AF3_TIM11,
        GPIO_pin_mux__AF3_TIM8,
        GPIO_pin_mux__AF3_TIM9,
        GPIO_pin_mux__AF4_CEC,
        GPIO_pin_mux__AF4_I2C1,
        GPIO_pin_mux__AF4_I2C2,
        GPIO_pin_mux__AF4_I2C3,
        GPIO_pin_mux__AF4_I2C4,
        GPIO_pin_mux__AF4_USART1,
        GPIO_pin_mux__AF5_SPI1,
        GPIO_pin_mux__AF5_SPI2,
        GPIO_pin_mux__AF5_SPI3,
        GPIO_pin_mux__AF5_SPI4,
        GPIO_pin_mux__AF5_SPI5,
        GPIO_pin_mux__AF5_SPI6,
        GPIO_pin_mux__AF6_DFSDM1,
        GPIO_pin_mux__AF6_SAI1,
        GPIO_pin_mux__AF6_SPI3,
        GPIO_pin_mux__AF6_UART4,
        GPIO_pin_mux__AF7_DFSDM1,
        GPIO_pin_mux__AF7_SPDIFRX,
        GPIO_pin_mux__AF7_SPI2,
        GPIO_pin_mux__AF7_SPI3,
        GPIO_pin_mux__AF7_SPI6,
        GPIO_pin_mux__AF7_UART5,
        GPIO_pin_mux__AF7_USART1,
        GPIO_pin_mux__AF7_USART2,
        GPIO_pin_mux__AF7_USART3,
        GPIO_pin_mux__AF8_SAI2,
        GPIO_pin_mux__AF8_SPDIFRX,
        GPIO_pin_mux__AF8_SPI6,
        GPIO_pin_mux__AF8_UART4,
        GPIO_pin_mux__AF8_UART5,
        GPIO_pin_mux__AF8_UART7,
        GPIO_pin_mux__AF8_UART8,
        GPIO_pin_mux__AF8_USART6,
        GPIO_pin_mux__AF9_CAN1,
        GPIO_pin_mux__AF9_CAN2,
        GPIO_pin_mux__AF9_FMC,
        GPIO_pin_mux__AF9_LTDC,
        GPIO_pin_mux__AF9_QUADSPI,
        GPIO_pin_mux__AF9_TIM12,
        GPIO_pin_mux__AF9_TIM13,
        GPIO_pin_mux__AF9_TIM14,
        GPIO_pin_mux__AF10_DFSDM1,
        GPIO_pin_mux__AF10_LTDC,
        GPIO_pin_mux__AF10_OTG_FS,
        GPIO_pin_mux__AF10_OTG_HS,
        GPIO_pin_mux__AF10_QUADSPI,
        GPIO_pin_mux__AF10_SAI2,
        GPIO_pin_mux__AF10_SDMMC2,
        GPIO_pin_mux__AF10_SDMMC2,
        GPIO_pin_mux__AF11_CAN3,
        GPIO_pin_mux__AF11_ETH,
        GPIO_pin_mux__AF11_I2C4,
        GPIO_pin_mux__AF11_SDMMC2,
        GPIO_pin_mux__AF11_SDMMC2,
        GPIO_pin_mux__AF12_FMC,
        GPIO_pin_mux__AF12_MDIOS,
        GPIO_pin_mux__AF12_OTG_HS_FS,
        GPIO_pin_mux__AF12_SDMMC1,
        GPIO_pin_mux__AF12_UART7,
        GPIO_pin_mux__AF13_DCMI,
        GPIO_pin_mux__AF13_DSI,
        GPIO_pin_mux__AF13_LTDC,
        GPIO_pin_mux__AF13_RNG,
        GPIO_pin_mux__AF14_LTDC,
        GPIO_pin_mux__AF15_EVENTOUT,
#elif defined(ARCH_stm32h7)
        GPIO_pin_mux__AF0_C1DSLEEP,
        GPIO_pin_mux__AF0_C1SLEEP,
        GPIO_pin_mux__AF0_C2DSLEEP,
        GPIO_pin_mux__AF0_C2SLEEP,
        GPIO_pin_mux__AF0_D1PWREN,
        GPIO_pin_mux__AF0_D2PWREN,
        GPIO_pin_mux__AF0_LCDBIAS,
        GPIO_pin_mux__AF0_MCO,
        GPIO_pin_mux__AF0_RTC_50Hz,
        GPIO_pin_mux__AF0_SWJ,
        GPIO_pin_mux__AF0_TRACE,
        GPIO_pin_mux__AF1_FMC,
        GPIO_pin_mux__AF1_HRTIM1,
        GPIO_pin_mux__AF1_LPTIM1,
        GPIO_pin_mux__AF1_SAI4,
        GPIO_pin_mux__AF1_TIM1,
        GPIO_pin_mux__AF1_TIM16,
        GPIO_pin_mux__AF1_TIM17,
        GPIO_pin_mux__AF1_TIM2,
        GPIO_pin_mux__AF2_FDCAN3,
        GPIO_pin_mux__AF2_HRTIM1,
        GPIO_pin_mux__AF2_SAI1,
        GPIO_pin_mux__AF2_TIM12,
        GPIO_pin_mux__AF2_TIM15,
        GPIO_pin_mux__AF2_TIM3,
        GPIO_pin_mux__AF2_TIM4,
        GPIO_pin_mux__AF2_TIM5,
        GPIO_pin_mux__AF3_DFSDM1,
        GPIO_pin_mux__AF3_HRTIM1,
        GPIO_pin_mux__AF3_LPTIM2,
        GPIO_pin_mux__AF3_LPTIM3,
        GPIO_pin_mux__AF3_LPTIM4,
        GPIO_pin_mux__AF3_LPTIM5,
        GPIO_pin_mux__AF3_LPUART,
        GPIO_pin_mux__AF3_LTDC,
        GPIO_pin_mux__AF3_OCTOSPIM_P1,
        GPIO_pin_mux__AF3_OCTOSPIM_P2,
        GPIO_pin_mux__AF3_TIM8,
        GPIO_pin_mux__AF4_CEC,
        GPIO_pin_mux__AF4_DCMI,
        GPIO_pin_mux__AF4_DFSDM1,
        GPIO_pin_mux__AF4_DFSDM2,
        GPIO_pin_mux__AF4_I2C1,
        GPIO_pin_mux__AF4_I2C2,
        GPIO_pin_mux__AF4_I2C3,
        GPIO_pin_mux__AF4_I2C4,
        GPIO_pin_mux__AF4_I2C5,
        GPIO_pin_mux__AF4_LPTIM2,
        GPIO_pin_mux__AF4_OCTOSPIM_P1,
        GPIO_pin_mux__AF4_PSSI,
        GPIO_pin_mux__AF4_TIM15,
        GPIO_pin_mux__AF4_USART1,
        GPIO_pin_mux__AF4_USART10,
        GPIO_pin_mux__AF5_CEC,
        GPIO_pin_mux__AF5_FDCAN3,
        GPIO_pin_mux__AF5_SPI1,
        GPIO_pin_mux__AF5_SPI2,
        GPIO_pin_mux__AF5_SPI3,
        GPIO_pin_mux__AF5_SPI4,
        GPIO_pin_mux__AF5_SPI5,
        GPIO_pin_mux__AF5_SPI6,
        GPIO_pin_mux__AF6_DFSDM1,
        GPIO_pin_mux__AF6_DFSDM2,
        GPIO_pin_mux__AF6_I2C4,
        GPIO_pin_mux__AF6_I2C5,
        GPIO_pin_mux__AF6_OCTOSPIM_P1,
        GPIO_pin_mux__AF6_SAI1,
        GPIO_pin_mux__AF6_SAI3,
        GPIO_pin_mux__AF6_SPI2,
        GPIO_pin_mux__AF6_SPI3,
        GPIO_pin_mux__AF6_UART4,
        GPIO_pin_mux__AF7_SDMMC1,
        GPIO_pin_mux__AF7_SPI2,
        GPIO_pin_mux__AF7_SPI3,
        GPIO_pin_mux__AF7_SPI6,
        GPIO_pin_mux__AF7_UART7,
        GPIO_pin_mux__AF7_USART1,
        GPIO_pin_mux__AF7_USART2,
        GPIO_pin_mux__AF7_USART3,
        GPIO_pin_mux__AF7_USART6,
        GPIO_pin_mux__AF8_LPUART,
        GPIO_pin_mux__AF8_SAI2,
        GPIO_pin_mux__AF8_SAI4,
        GPIO_pin_mux__AF8_SDMMC1,
        GPIO_pin_mux__AF8_SPDIF,
        GPIO_pin_mux__AF8_SPI6,
        GPIO_pin_mux__AF8_UART4,
        GPIO_pin_mux__AF8_UART5,
        GPIO_pin_mux__AF8_UART8,
        GPIO_pin_mux__AF9_FDCAN1,
        GPIO_pin_mux__AF9_FDCAN2,
        GPIO_pin_mux__AF9_FMC,
        GPIO_pin_mux__AF9_LTDC,
        GPIO_pin_mux__AF9_OCTOSPIM_P1,
        GPIO_pin_mux__AF9_OCTOSPIM_P2,
        GPIO_pin_mux__AF9_QUADSPI,
        GPIO_pin_mux__AF9_SAI4,
        GPIO_pin_mux__AF9_SDMMC2,
        GPIO_pin_mux__AF9_SPDIF,
        GPIO_pin_mux__AF9_TIM13,
        GPIO_pin_mux__AF9_TIM14,
        GPIO_pin_mux__AF10_COMP1,
        GPIO_pin_mux__AF10_COMP2,
        GPIO_pin_mux__AF10_CRS_SYNC,
        GPIO_pin_mux__AF10_FMC,
        GPIO_pin_mux__AF10_LTDC,
        GPIO_pin_mux__AF10_OCTOSPIM_P1,
        GPIO_pin_mux__AF10_OTG1_FS,
        GPIO_pin_mux__AF10_OTG1_HS,
        GPIO_pin_mux__AF10_OTG2_FS,
        GPIO_pin_mux__AF10_QUADSPI,
        GPIO_pin_mux__AF10_SAI2,
        GPIO_pin_mux__AF10_SAI4,
        GPIO_pin_mux__AF10_SDMMC2,
        GPIO_pin_mux__AF10_TIM8,
        GPIO_pin_mux__AF11_COMP1,
        GPIO_pin_mux__AF11_COMP2,
        GPIO_pin_mux__AF11_DFSDM1,
        GPIO_pin_mux__AF11_DFSDM2,
        GPIO_pin_mux__AF11_ETH,
        GPIO_pin_mux__AF11_I2C4,
        GPIO_pin_mux__AF11_LTDC,
        GPIO_pin_mux__AF11_MDIOS,
        GPIO_pin_mux__AF11_OCTOSPIM_P1,
        GPIO_pin_mux__AF11_SDMMC2,
        GPIO_pin_mux__AF11_SWP,
        GPIO_pin_mux__AF11_TIM1,
        GPIO_pin_mux__AF11_TIM8,
        GPIO_pin_mux__AF11_UART7,
        GPIO_pin_mux__AF11_UART9,
        GPIO_pin_mux__AF11_USART10,
        GPIO_pin_mux__AF12_COMP1,
        GPIO_pin_mux__AF12_COMP2,
        GPIO_pin_mux__AF12_FMC,
        GPIO_pin_mux__AF12_LTDC,
        GPIO_pin_mux__AF12_MDIOS,
        GPIO_pin_mux__AF12_OCTOSPIM_P1,
        GPIO_pin_mux__AF12_OTG1_FS,
        GPIO_pin_mux__AF12_SDMMC1,
        GPIO_pin_mux__AF12_TIM1,
        GPIO_pin_mux__AF12_TIM8,
        GPIO_pin_mux__AF13_COMP1,
        GPIO_pin_mux__AF13_COMP2,
        GPIO_pin_mux__AF13_DCMI,
        GPIO_pin_mux__AF13_DSI,
        GPIO_pin_mux__AF13_LTDC,
        GPIO_pin_mux__AF13_PSSI,
        GPIO_pin_mux__AF13_TIM1,
        GPIO_pin_mux__AF13_TIM23,
        GPIO_pin_mux__AF14_LTDC,
        GPIO_pin_mux__AF14_TIM24,
        GPIO_pin_mux__AF14_UART5,
        GPIO_pin_mux__AF15_EVENTOUT,
#endif
        GPIO_pin_mux_amount,
} GPIO_pin_mux_t;

/**
 * Type represent pin in port configuration.
 */
typedef struct {
        GPIO_pin_in_port_t pin;   /*!< Pin address.*/
        GPIO_pin_speed_t   speed; /*!< Pin speed.*/
        GPIO_pin_mode_t    mode;  /*!< Pin mode.*/
        GPIO_pin_mux_t     mux;   /*!< Pin multiplexer.*/
        bool               state; /*!< Pin state in case of output.*/
} GPIO_pin_in_port_conf_t;

/*==============================================================================
  Exported macros
==============================================================================*/
/**
 * PIN declaration
 */
#define IOCTL_GPIO_PIN_DECL(_name) \
        .port_idx = IOCTL_GPIO_PORT_IDX__##_name, .pin_idx = IOCTL_GPIO_PIN_IDX__##_name

/**
 * @brief  Not existing pin number.
 */
#define IOCTL_GPIO_PIN_IDX__NULL 0xFF

/**
 * @brief  Not existing port number.
 */
#define IOCTL_GPIO_PORT_IDX__NULL 0xFF

/**
 *  @brief  Set selected pin (set pin to Hi state).
 *  @param  [WR] @ref u8_t*                     pin index
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__SET_PIN _IOW(GPIO, 0, const u8_t *)

/**
 *  @brief  Clear selected pin (set pin to Low state).
 *  @param  [WR] @ref u8_t*                     pin index
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__CLEAR_PIN _IOW(GPIO, 1, const u8_t *)

/**
 *  @brief  Toggle pin state.
 *  @param  [WR] @ref u8_t*                     pin index
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__TOGGLE_PIN _IOW(GPIO, 2, const u8_t *)

/**
 *  @brief  Set pin state.
 *  @param  [WR] @ref GPIO_pin_state_t*         pin index and state to set
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__SET_PIN_STATE _IOW(GPIO, 3, const GPIO_pin_state_t *)

/**
 *  @brief  Gets pin state.
 *  @param  [RD] @ref GPIO_pin_state_t*         pin index and read state
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__GET_PIN_STATE _IOR(GPIO, 4, GPIO_pin_state_t *)

/**
 *  @brief  Set selected pin in port (set pin to Hi state).
 *  @param  [WR] @ref GPIO_pin_in_port_t*       pin and port index
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__SET_PIN_IN_PORT _IOW(GPIO, 5, GPIO_pin_in_port_t *)

/**
 *  @brief  Set selected pin in port (set pin to Low state).
 *  @param  [WR] @ref GPIO_pin_in_port_t*       pin and port index
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__CLEAR_PIN_IN_PORT _IOW(GPIO, 6, GPIO_pin_in_port_t *)

/**
 *  @brief  Toggle selected pin in port.
 *  @param  [WR] @ref GPIO_pin_in_port_t*       pin and port index
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__TOGGLE_PIN_IN_PORT _IOW(GPIO, 7, GPIO_pin_in_port_t *)

/**
 *  @brief  Set pin state in selected port.
 *  @param  [WR] @ref GPIO_pin_in_port_state_t* pin and port index and state to set
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__SET_PIN_IN_PORT_STATE _IOW(GPIO, 8, GPIO_pin_in_port_state_t *)

/**
 *  @brief  Gets pin state in selected port.
 *  @param  [RD] @ref GPIO_pin_in_port_state_t* pin and port index and read state
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__GET_PIN_IN_PORT_STATE _IOR(GPIO, 9, GPIO_pin_in_port_state_t *)

/**
 *  @brief  Set pin in port configuration.
 *  @param  [RD] @ref const GPIO_pin_in_port_conf_t* pin configuration
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__SET_PIN_IN_PORT_CONF _IOW(GPIO, 10, const GPIO_pin_in_port_conf_t *)

/**
 *  @brief  Get pin in port configuration.
 *  @param  [RD] @ref GPIO_pin_in_port_conf_t* pin configuration
 *  @return On success 0 is returned, otherwise -1 and @ref errno code is set
 */
#define IOCTL_GPIO__GET_PIN_IN_PORT_CONF _IOWR(GPIO, 11, GPIO_pin_in_port_conf_t *)

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

#endif /* _GPIO_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
