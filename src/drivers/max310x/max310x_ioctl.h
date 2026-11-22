/*==============================================================================
File    max310x_ioctl.h

Author  Dawid Wałach

Brief   MAX310x SPI/I2C UART (MAX3107, MAX3108, MAX3109)

        Copyright (C) 2021 Dawid Wałach <dawid.walach@3dgence.com>

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

/**
@defgroup drv-max310x MAX310X Driver

\section drv-max310x-desc Description
Driver handles UARTs with 128-word FIFOs connected by SPI or I2C: MAX3107,
MAX3108 (1 UART) and MAX3109 (2 UARTs). Each UART is visible in the system as
a character device: data written to the device file are transmitted, received
data are read from the device file.

A thread of the chip moves data between FIFOs of the chip and buffers of the
driver (RAM), so data are received also when the device file is not read.
The thread is woken by the IRQ pin of the chip (if selected and the IRQ driver
is in the kernel) or works periodically (time of about 64 characters, 1 to
50 ms).

Device (drivers) connection table
| Grade | Device       | In                  | Out              |
| ----: | :----------- | :------------------ | :--------------- |
| 0     | MAX310x      | -                   | SPI/I2C          |
| 1     | SPI/I2C      | SPI/I2C             | /dev/spi-uart    |
| 2     | MAX310X      | /dev/spi-uart       | /dev/ttyM0       |

\section drv-max310x-sup-arch Supported architectures
\li noarch

\section drv-max310x-ddesc Details
\subsection drv-max310x-ddesc-num Meaning of major and minor numbers
Major number selects the chip, minor number selects UART of the chip (0 for
MAX3107 and MAX3108, 0 or 1 for MAX3109).

\subsubsection drv-max310x-ddesc-numres Numeration restrictions
Major number can be in range 0 to 255, minor number 0 to 1. UART 0 of the
chip must be configured first: its configuration initializes the chip (type,
clock, RST and IRQ pins), configuration of UART 1 sets only the UART.

\subsection drv-max310x-ddesc-init Driver initialization
The interface device must be configured first:
\li SPI: mode 0 (or 3), MSB first, up to 26 MHz; UART 1 of MAX3109 uses the
    SPI device file of UART 0 (SPI device file can be open once),
\li I2C: address of the chip (pins A0, A1), sub-address mode 1 byte; UART 1
    of MAX3109 has address lower by 0x10 (separate I2C device file).

Example (MAX3109 on SPI, IRQ pin PB5):
@code
modprobe -i SPI 0 0 /dev/spi-uart -c cs_port=0,cs_pin=4,mode=0
modprobe -i MAX310X 0 0 /dev/ttyM0 -c path=/dev/spi-uart,chip=max3109,clock=3686400,irq_port=1,irq_pin=5,baud=115200
modprobe -i MAX310X 0 1 /dev/ttyM1 -c baud=9600
@endcode

Example (MAX3107 on I2C):
@code
modprobe -i I2C 0 0 /dev/i2c-uart -c address=0x2C,sub_addr_mode=1
modprobe -i MAX310X 0 0 /dev/ttyM0 -c path=/dev/i2c-uart,interface=i2c,clock=1843200,crystal=1,baud=9600
@endcode

\subsection drv-max310x-ddesc-release Driver release
UART 0 can be released when other UARTs of the chip are released.
@code
driver_release("MAX310X", 0, 1);
driver_release("MAX310X", 0, 0);
@endcode

\subsection drv-max310x-ddesc-cfg Driver configuration
Driver is configured by IOCTL_MAX310X__CONFIGURE (structure MAX310X_config_t)
or by string (IOCTL_MAX310X__CONFIGURE_STR, modprobe -c). Configuration of
UART resets the UART (data in FIFOs and buffers are lost), configuration of
UART 0 resets also the chip (pin RST) and configures again other UARTs.

\subsection drv-max310x-ddesc-cfgstr Configuration string
Configuration string (@ref IOCTL_MAX310X__CONFIGURE_STR, program `modprobe -c`)
is a list of `key=value` pairs separated by commas, values are described in @ref
IOCTL_DEVICE__CONFIGURE_STR. Keys of chip (chip, interface, clock, crystal,
reset and IRQ pins) not given keep current values, other keys not given are set
to values of column Omitted. Keys marked UART 0 configure the chip (used only by
configuration of UART 0).

| Key             | Value                           | Omitted             | Description |
|-----------------|---------------------------------|---------------------|-------------|
| `path`          | path                            | SPI: file of UART 0 | SPI or I2C device file of UART (required for UART 0 and for UART 1 on I2C) |
| `interface`     | `spi`, `i2c`                    | not changed         | interface of the chip (initial spi) |
| `chip`          | `max3107`, `max3108`, `max3109` | not changed         | type of chip, UART 0 (initial max3107) |
| `clock`         | number                          | not changed         | frequency of reference clock on XIN [Hz], UART 0 (required) |
| `crystal`       | bool                            | not changed         | crystal on XIN/XOUT (0: external clock), UART 0 |
| `reset_port`    | number                          | not used            | GPIO port index of RST pin (active low), UART 0, used with reset_pin |
| `reset_pin`     | number                          | not used            | GPIO pin index of RST pin |
| `irq_port`      | number                          | not used            | GPIO port index of IRQ pin (IRQ driver), UART 0, used with irq_pin |
| `irq_pin`       | number                          | not used            | GPIO pin index of IRQ pin (EXTI line) |
| `baud`          | number                          | 115200              | baud rate [bps] |
| `word_length`   | `5..8`                          | 8                   | number of data bits |
| `parity`        | `off`, `odd`, `even`            | off                 | parity |
| `stop_bits`     | `1`, `2`                        | 1                   | stop bits (2: 1.5 for 5 data bits) |
| `tx_enable`     | bool                            | 1                   | transmitter enabled |
| `rx_enable`     | bool                            | 1                   | receiver enabled |
| `tx_invert`     | bool                            | 0                   | TX pin logic inversion |
| `rx_invert`     | bool                            | 0                   | RX pin logic inversion |
| `hw_flow_ctrl`  | bool                            | 0                   | automatic RTS/CTS flow control |
| `flow_halt`     | number                          | 96                  | RX FIFO level that deactivates RTS [words, multiple of 8] |
| `flow_resume`   | number                          | 32                  | RX FIFO level that activates RTS [words, multiple of 8, lower than flow_halt] |
| `rs485`         | bool                            | 0                   | RTS controls transceiver direction (half duplex) |
| `echo_suppress` | bool                            | 0                   | received data ignored while transmitting |
| `loopback`      | bool                            | 0                   | internal loopback TX to RX |
| `rx_buffer`     | number                          | 256                 | size of RX buffer in RAM [bytes], changed only when the file is not open by other files |
| `tx_buffer`     | number                          | 128                 | size of TX buffer in RAM [bytes], changed only when the file is not open by other files |

@code
modprobe -i MAX310X 0 0 /dev/ttyM0 -c path=/dev/spi-uart,chip=max3109,clock=3686400,baud=9600
@endcode

\subsection drv-max310x-ddesc-write Data write
Data are written to the TX buffer and transmitted by the thread. A blocking
write waits for space in the buffer, non-blocking write writes only data that
fit to the buffer.

\subsection drv-max310x-ddesc-read Data read
Received data are read from the RX buffer. A blocking read waits for all
requested data (non-strict read returns after the first received byte and
available data), non-blocking read returns available data only. When the RX
buffer is full, data stay in the RX FIFO of the chip (128 bytes), next data
are lost (receiver overrun) unless the hardware flow control is used.

@{
*/

#ifndef _MAX310X_IOCTL_H_
#define _MAX310X_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/ioctl_macros.h"
#include "drivers/class/device/ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/**
 *  @brief  Set UART configuration (the UART is reset, UART 0: also the chip).
 *  @param  [WR] @ref MAX310X_config_t * configuration
 *  @return On success 0 is returned, otherwise -1 and @ref errno is set.
 */
#define IOCTL_MAX310X__CONFIGURE _IOW(MAX310X, 0x00, const MAX310X_config_t *)

/**
 *  @brief  Get UART configuration (field path is NULL).
 *  @param  [RD] @ref MAX310X_config_t * configuration
 *  @return On success 0 is returned, otherwise -1 and @ref errno is set.
 */
#define IOCTL_MAX310X__GET_CONFIGURATION _IOR(MAX310X, 0x01, MAX310X_config_t *)

/**
 *  @brief  Configure device by configuration string, keys and values:
 *          @ref drv-max310x-ddesc-cfgstr.
 *  @param  [WR] const char * configuration string
 *  @return On success 0 is returned, otherwise -1 and @ref errno is set.
 */
#define IOCTL_MAX310X__CONFIGURE_STR IOCTL_DEVICE__CONFIGURE_STR

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * Type of chip.
 */
typedef enum {
        MAX310X_CHIP__MAX3107, //!< MAX3107 (1 UART).
        MAX310X_CHIP__MAX3108, //!< MAX3108 (1 UART).
        MAX310X_CHIP__MAX3109, //!< MAX3109 (2 UARTs).
} MAX310X_chip_t;

/**
 * Interface of the chip.
 */
typedef enum {
        MAX310X_INTERFACE__SPI, //!< SPI interface.
        MAX310X_INTERFACE__I2C, //!< I2C interface.
} MAX310X_interface_t;

/**
 * Parity.
 */
typedef enum {
        MAX310X_PARITY__OFF,  //!< Parity disabled.
        MAX310X_PARITY__ODD,  //!< Odd parity.
        MAX310X_PARITY__EVEN, //!< Even parity.
} MAX310X_parity_t;

/**
 * Stop bits.
 */
typedef enum {
        MAX310X_STOP_BITS__1, //!< 1 stop bit.
        MAX310X_STOP_BITS__2, //!< 2 stop bits (1.5 for 5 data bits).
} MAX310X_stop_bits_t;

/**
 * UART configuration. Fields marked "UART 0" configure the chip and are used
 * only by configuration of UART 0.
 */
typedef struct {
        const char *path;              /*!< SPI or I2C device file of UART (UART 1 on SPI: NULL).*/
        MAX310X_interface_t interface; /*!< Interface of the chip.*/
        MAX310X_chip_t chip;           /*!< Type of chip (UART 0).*/
        u32_t clock;                   /*!< Frequency of reference clock on pin XIN [Hz] (UART 0).*/
        bool crystal;                  /*!< Crystal on XIN/XOUT, false: external clock (UART 0).*/
        bool reset_pin_used;           /*!< GPIO pin connected to RST is used (UART 0).*/
        u8_t reset_port_idx;           /*!< Port index of RST pin (active low).*/
        u8_t reset_pin_idx;            /*!< Pin index of RST pin (active low).*/
        bool irq_pin_used;             /*!< GPIO pin connected to IRQ is used (UART 0, IRQ driver).*/
        u8_t irq_port_idx;             /*!< Port index of IRQ pin (active low).*/
        u8_t irq_pin_idx;              /*!< Pin index of IRQ pin (EXTI line).*/
        u32_t baud;                    /*!< Baud rate [bps].*/
        MAX310X_parity_t parity;       /*!< Parity.*/
        MAX310X_stop_bits_t stop_bits; /*!< Stop bits.*/
        u8_t word_length;              /*!< Number of data bits (5..8).*/
        bool tx_enable;                /*!< Transmitter enable.*/
        bool rx_enable;                /*!< Receiver enable.*/
        bool tx_invert;                /*!< TX pin logic inversion.*/
        bool rx_invert;                /*!< RX pin logic inversion.*/
        bool hw_flow_ctrl;             /*!< Automatic RTS/CTS flow control.*/
        u8_t flow_halt;                /*!< RX FIFO level that deactivates RTS [words, multiple of 8].*/
        u8_t flow_resume;              /*!< RX FIFO level that activates RTS [words, multiple of 8].*/
        bool rs485;                    /*!< RTS controls transceiver direction (half duplex).*/
        bool echo_suppress;            /*!< Received data are ignored while transmitting.*/
        bool loopback;                 /*!< Internal loopback TX to RX.*/
        u16_t rx_buffer;               /*!< Size of RX buffer in RAM [bytes].*/
        u16_t tx_buffer;               /*!< Size of TX buffer in RAM [bytes].*/
} MAX310X_config_t;

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

#endif /* _MAX310X_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
