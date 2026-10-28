/*==============================================================================
@file    board.h

@author  Daniel Zorychta

@brief   Interface of the board model (POSIX simulator). The board model
         (board.c) simulates hardware connected to the microcontroller
         (GPIO, I2C devices, ...) described by the Lua script selected by the
         simulator option --board.

@note    Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/*
 * NOTE: This file is used by the host part of the simulator only (host
 *       headers). Functions are called from simulator threads, the module
 *       must be thread safe and functions must not block for a long time.
 */

#ifndef _HOST_BOARD_H_
#define _HOST_BOARD_H_

/*==============================================================================
  Include files
==============================================================================*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/** version of board interface */
#define _HOST_BOARD_INTERFACE_VERSION 3

/** I2C address flag: 10-bit address */
#define _HOST_BOARD_I2C_ADDR_10BIT 0x8000

/** I2C transfer results */
#define _HOST_BOARD_I2C_ACK        0    /*!< device acknowledged */
#define _HOST_BOARD_I2C_NACK       1    /*!< device not acknowledged (or not exist) */
#define _HOST_BOARD_I2C_ERROR      (-1) /*!< board error (e.g. board does not respond) */

/** SPI transfer flags: bits 0-1 SPI mode (CPOL, CPHA) */
#define _HOST_BOARD_SPI_MODE_MASK  0x03
#define _HOST_BOARD_SPI_LSB_FIRST  0x04 /*!< LSB first bit order */

/*==============================================================================
  Exported functions
==============================================================================*/
/**
 * @brief  Load board description and start board model. Called before the
 *         system start.
 *
 * @param  script       board description file
 *
 * @return 0 on success, otherwise -1 (error message is printed by module).
 */
extern int _host_board_init(const char *script);

/**
 * @brief  Stop board model (simulator exit).
 */
extern void _host_board_exit(void);

/**
 * @brief  GPIO output state was changed by system.
 *
 * @param  port         port index
 * @param  state        port output state
 * @param  changed      mask of changed pins
 */
extern void _host_board_gpio_output(int port, uint32_t state, uint32_t changed);

/**
 * @brief  Get state of GPIO pins driven by board.
 *
 * @param  port         port index
 * @param  level        levels of driven pins
 *
 * @return Mask of pins driven by board.
 */
extern uint32_t _host_board_gpio_input(int port, uint32_t *level);

/**
 * @brief  I2C master: START or repeated START condition.
 *
 * @param  bus          bus index (0: I2C1)
 *
 * @return _HOST_BOARD_I2C_ACK or _HOST_BOARD_I2C_ERROR.
 */
extern int _host_board_i2c_start(int bus);

/**
 * @brief  I2C master: send device address.
 *
 * @param  bus          bus index (0: I2C1)
 * @param  address      7-bit address or 10-bit address with _HOST_BOARD_I2C_ADDR_10BIT flag
 * @param  read         true: read transfer, false: write transfer
 *
 * @return _HOST_BOARD_I2C_ACK, _HOST_BOARD_I2C_NACK or _HOST_BOARD_I2C_ERROR.
 */
extern int _host_board_i2c_address(int bus, uint16_t address, bool read);

/**
 * @brief  I2C master: transmit bytes to selected device.
 *
 * @param  bus          bus index (0: I2C1)
 * @param  src          data
 * @param  count        number of bytes
 * @param  written      number of acknowledged bytes
 *
 * @return _HOST_BOARD_I2C_ACK, _HOST_BOARD_I2C_NACK or _HOST_BOARD_I2C_ERROR.
 */
extern int _host_board_i2c_write(int bus, const uint8_t *src, size_t count, size_t *written);

/**
 * @brief  I2C master: receive bytes from selected device.
 *
 * @param  bus          bus index (0: I2C1)
 * @param  dst          data destination
 * @param  count        number of bytes
 *
 * @return _HOST_BOARD_I2C_ACK, _HOST_BOARD_I2C_NACK or _HOST_BOARD_I2C_ERROR.
 */
extern int _host_board_i2c_read(int bus, uint8_t *dst, size_t count);

/**
 * @brief  I2C master: STOP condition.
 *
 * @param  bus          bus index (0: I2C1)
 */
extern void _host_board_i2c_stop(int bus);

/**
 * @brief  SPI master: full duplex transfer. Devices are selected by CS pins
 *         (GPIO events sent before transfer).
 *
 * @param  bus          bus index (0: SPI1)
 * @param  tx           data to transmit (NULL: fill byte is transmitted)
 * @param  rx           received data (can be NULL)
 * @param  count        number of bytes
 * @param  fill         byte transmitted when tx is NULL
 * @param  flags        SPI mode and bit order (_HOST_BOARD_SPI_*)
 *
 * @return 0 on success, -1 on error (board does not respond).
 */
extern int _host_board_spi_transfer(int bus, const uint8_t *tx, uint8_t *rx, size_t count, uint8_t fill,
                                    unsigned flags);

#ifdef __cplusplus
}
#endif

#endif /* _HOST_BOARD_H_ */
/*==============================================================================
  End of file
==============================================================================*/
