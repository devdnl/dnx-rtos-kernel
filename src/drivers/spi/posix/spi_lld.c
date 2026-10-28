/*==============================================================================
@file    spi_lld.c

@author  Daniel Zorychta

@brief   SPI low level driver of the POSIX simulator. Transfers are forwarded
         to the board model (simulator option --board). Devices are selected
         by the CS pin (GPIO driver), the board model receives the CS changes
         as GPIO events before transfer. Without the board model received
         data are 0xFF.

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

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "../spi.h"
#include "posix/host/host.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/

/*==============================================================================
  Local types, enums definitions
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local object definitions
==============================================================================*/

/*==============================================================================
  Exported object definitions
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Function enable SPI interface.
 *
 * @param  major        SPI major number
 *
 * @return One of errno value.
 */
//==============================================================================
int _SPI_LLD__turn_on(u8_t major)
{
        _SPI[major]->flush_byte = 0;
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function disable SPI interface.
 *
 * @param  major        SPI major number
 */
//==============================================================================
void _SPI_LLD__turn_off(u8_t major)
{
        UNUSED_ARG1(major);
}

//==============================================================================
/**
 * @brief  Function apply SPI device configuration (configuration is sent to
 *         the board model with each transfer).
 *
 * @param  hdl          SPI device handle
 */
//==============================================================================
void _SPI_LLD__apply_config(struct SPI_slave *hdl)
{
        _SPI[hdl->major]->current_slave = hdl;
}

//==============================================================================
/**
 * @brief  Function halt SPI interface.
 *
 * @param  major        SPI major number
 */
//==============================================================================
void _SPI_LLD__halt(u8_t major)
{
        UNUSED_ARG1(major);
}

//==============================================================================
/**
 * @brief  Transmit and receive data.
 *
 * @param  hdl          SPI device handle
 * @param  txbuf        source buffer (can be NULL for RX only: 0x00 is sent as in
 *                      the STM32 driver)
 * @param  rxbuf        destination buffer (can be NULL for TX only)
 * @param  count        number of bytes to transfer
 *
 * @return One of errno value.
 */
//==============================================================================
int _SPI_LLD__transceive(struct SPI_slave *hdl, const u8_t *txbuf, u8_t *rxbuf, size_t count)
{
        _SPI[hdl->major]->current_slave = hdl;

        if (count == 0) {
                return ESUCC;
        }

        unsigned flags = (hdl->config.mode & 3) | (hdl->config.msb_first ? 0 : _HOST_SPI_LSB_FIRST);

        int result = _host_spi_transfer(hdl->major, txbuf, rxbuf, count, _SPI[hdl->major]->flush_byte, flags);

        return (result == 0) ? ESUCC : EIO;
}

/*==============================================================================
  End of file
==============================================================================*/
