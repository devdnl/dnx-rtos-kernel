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
#define _HOST_BOARD_INTERFACE_VERSION 7

/** I2C address flag: 10-bit address */
#define _HOST_BOARD_I2C_ADDR_10BIT 0x8000

/** I2C transfer results */
#define _HOST_BOARD_I2C_ACK        0    /*!< device acknowledged */
#define _HOST_BOARD_I2C_NACK       1    /*!< device not acknowledged (or not exist) */
#define _HOST_BOARD_I2C_ERROR      (-1) /*!< board error (e.g. board does not respond) */

/** SPI transfer flags: bits 0-1 SPI mode (CPOL, CPHA) */
#define _HOST_BOARD_SPI_MODE_MASK  0x03
#define _HOST_BOARD_SPI_LSB_FIRST  0x04 /*!< LSB first bit order */

/** number of UARTs that can be connected to board models */
#define _HOST_BOARD_UART_PORTS     16

/*==============================================================================
  Exported functions
==============================================================================*/
/**
 * @brief  Load board description and start board model. Called before the
 *         system start.
 *
 * @param  script       board description file
 * @param  panel        path of panel socket (Unix domain socket created by
 *                      module, tools/simpanel.py connects to it), NULL: no panel
 *
 * @return 0 on success, otherwise -1 (error message is printed by module).
 */
extern int _host_board_init(const char *script, const char *panel);

/**
 * @brief  Stop board model (simulator exit).
 */
extern void _host_board_exit(void);

/**
 * @brief  LCD framebuffer of the LTDC driver is shown by the panel as LCD
 *         "ltdc" (changed lines are sent periodically). Framebuffer must be
 *         valid until it is detached.
 *
 * @param  fb           framebuffer (NULL: detach)
 * @param  width        width in pixels
 * @param  height       height in pixels
 * @param  format       pixel format (LTDC_pixel_format_t: 0 ARGB8888, 1 RGB888,
 *                      2 RGB565, 3 ARGB1555, 4 ARGB4444, 5 L8, 6 AL44, 7 AL88)
 * @param  bg_r         background color (red)
 * @param  bg_g         background color (green)
 * @param  bg_b         background color (blue)
 */
extern void _host_board_lcd_attach(const void *fb, uint16_t width, uint16_t height, int format, uint8_t bg_r,
                                   uint8_t bg_g, uint8_t bg_b);

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
 * @param  bus          bus index (I2C driver major, board script name "I2C:<major>")
 *
 * @return _HOST_BOARD_I2C_ACK or _HOST_BOARD_I2C_ERROR.
 */
extern int _host_board_i2c_start(int bus);

/**
 * @brief  I2C master: send device address.
 *
 * @param  bus          bus index (I2C driver major, board script name "I2C:<major>")
 * @param  address      7-bit address or 10-bit address with _HOST_BOARD_I2C_ADDR_10BIT flag
 * @param  read         true: read transfer, false: write transfer
 *
 * @return _HOST_BOARD_I2C_ACK, _HOST_BOARD_I2C_NACK or _HOST_BOARD_I2C_ERROR.
 */
extern int _host_board_i2c_address(int bus, uint16_t address, bool read);

/**
 * @brief  I2C master: transmit bytes to selected device.
 *
 * @param  bus          bus index (I2C driver major, board script name "I2C:<major>")
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
 * @param  bus          bus index (I2C driver major, board script name "I2C:<major>")
 * @param  dst          data destination
 * @param  count        number of bytes
 *
 * @return _HOST_BOARD_I2C_ACK, _HOST_BOARD_I2C_NACK or _HOST_BOARD_I2C_ERROR.
 */
extern int _host_board_i2c_read(int bus, uint8_t *dst, size_t count);

/**
 * @brief  I2C master: STOP condition.
 *
 * @param  bus          bus index (I2C driver major, board script name "I2C:<major>")
 */
extern void _host_board_i2c_stop(int bus);

/**
 * @brief  SPI master: full duplex transfer. Devices are selected by CS pins
 *         (GPIO events sent before transfer).
 *
 * @param  bus          bus index (SPI driver major, board script name "SPI:<major>")
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

/**
 * @brief  Allow connection of UART to board model (uart.attach() of board
 *         script). Called before _host_board_init() for UARTs that are not
 *         connected to the terminal, PTY or host serial port.
 *
 * @param  port         UART index (UART driver major: /dev/ttyS<n>, board script name "UART:<n>")
 * @param  allowed      true: board model can be connected
 */
extern void _host_board_uart_allow(int port, bool allowed);

/**
 * @brief  Check if UART is connected to board model.
 *
 * @param  port         UART index
 *
 * @return true if board model is attached to UART.
 */
extern bool _host_board_uart_attached(int port);

/**
 * @brief  UART configuration changed by system (model is notified).
 *
 * @param  port         UART index
 * @param  baud         baud rate
 * @param  parity       0: none, 1: odd, 2: even
 * @param  stop_bits    1 or 2
 *
 * @return 0 on success, otherwise errno value.
 */
extern int _host_board_uart_configure(int port, uint32_t baud, int parity, int stop_bits);

/**
 * @brief  UART: data received by system (sent by board model). With
 *         transmission timing bytes are available at time of their reception
 *         (baud rate).
 *
 * @param  port         UART index
 * @param  buf          buffer
 * @param  len          buffer length
 *
 * @return Number of read bytes (0: no data).
 */
extern int _host_board_uart_read(int port, void *buf, size_t len);

/**
 * @brief  UART: data transmitted by system (received by board model). With
 *         transmission timing bytes are accepted at baud rate (0: transmitter
 *         busy, the system tries again).
 *
 * @param  port         UART index
 * @param  buf          data
 * @param  len          number of bytes
 *
 * @return Number of accepted bytes.
 */
extern int _host_board_uart_write(int port, const void *buf, size_t len);

/**
 * @brief  Check if Ethernet model is attached by board script (eth.attach()).
 *         Models, the system and host TAP interface are connected as hub: each
 *         frame is delivered to all other parts.
 *
 * @return true if Ethernet model is attached.
 */
extern bool _host_board_eth_attached(void);

/**
 * @brief  Ethernet: frame of the network (sent by system or received by host
 *         TAP interface) is delivered to models (frame is copied).
 *
 * @param  frame        frame (without FCS)
 * @param  len          frame length
 */
extern void _host_board_eth_input(const void *frame, size_t len);

/**
 * @brief  Ethernet: frame sent by models to the network.
 *
 * @param  buf          buffer
 * @param  len          buffer length
 *
 * @return Frame length (0: no frame).
 */
extern int _host_board_eth_output(void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* _HOST_BOARD_H_ */
/*==============================================================================
  End of file
==============================================================================*/
