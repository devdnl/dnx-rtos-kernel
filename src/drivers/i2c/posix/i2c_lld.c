/*==============================================================================
@file    i2c_lld.c

@author  Daniel Zorychta

@brief   I2C low level driver of the POSIX simulator. Bus operations are
         forwarded to the board model (simulator option --board), devices
         connected to the bus are described by the board script. Without the
         board model buses are empty (device address is not acknowledged).
         Only master mode is supported.

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
#include "../i2c.h"
#include "posix/host/host.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define RESULT_ACK  0
#define RESULT_NACK 1

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
 * @brief  Function initialize I2C peripheral.
 *
 * @param  major        peripheral number
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _I2C_LLD__init(u8_t major)
{
        _I2C[major]->initialized = true;
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function release I2C peripheral.
 *
 * @param  major        peripheral number
 */
//==============================================================================
void _I2C_LLD__release(u8_t major)
{
        _I2C[major]->initialized = false;
}

//==============================================================================
/**
 * @brief  Function generate START sequence on I2C bus.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _I2C_LLD__master_start(I2C_dev_t *hdl)
{
        return (_host_i2c_start(hdl->major) == RESULT_ACK) ? ESUCC : EIO;
}

//==============================================================================
/**
 * @brief  Function generate REPEAT START sequence on I2C bus.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _I2C_LLD__master_repeat_start(I2C_dev_t *hdl)
{
        return _I2C_LLD__master_start(hdl);
}

//==============================================================================
/**
 * @brief  Function generate STOP sequence on I2C bus.
 *
 * @param  hdl          device handle
 */
//==============================================================================
void _I2C_LLD__master_stop(I2C_dev_t *hdl)
{
        _host_i2c_stop(hdl->major);
}

//==============================================================================
/**
 * @brief  Function send I2C address sequence. The address of configuration
 *         is 8-bit (7-bit address shifted left, e.g. 0xA0), 10-bit address is
 *         used directly.
 *
 * @param  hdl          device handle
 * @param  write        true: compose write address
 * @param  count        number of bytes to be written/read (in transfer)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _I2C_LLD__master_send_address(I2C_dev_t *hdl, bool write, size_t count)
{
        UNUSED_ARG1(count);

        u16_t address;

        if (hdl->config.addr_10bit) {
                address = (hdl->config.address & 0x3FF) | _HOST_I2C_ADDR_10BIT;
        } else {
                address = (hdl->config.address >> 1) & 0x7F;
        }

        int result = _host_i2c_address(hdl->major, address, !write);

        if (result == RESULT_ACK) {
                return ESUCC;
        } else if (result == RESULT_NACK) {
                return ENXIO;
        } else {
                return EIO;
        }
}

//==============================================================================
/**
 * @brief  Function receive bytes from I2C bus (master-receiver).
 *
 * @param  hdl          device handle
 * @param  dst          destination buffer
 * @param  count        number of bytes to receive
 * @param  rdctr        number of read bytes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _I2C_LLD__master_receive(I2C_dev_t *hdl, u8_t *dst, size_t count, size_t *rdctr)
{
        *rdctr = 0;

        if (count == 0) {
                return ESUCC;
        }

        if (_host_i2c_read(hdl->major, dst, count) == RESULT_ACK) {
                *rdctr = count;
                return ESUCC;
        }

        return EIO;
}

//==============================================================================
/**
 * @brief  Function transmit data to I2C bus (master-transmitter).
 *
 * @param  hdl          device handle
 * @param  src          data source
 * @param  count        number of bytes to transfer
 * @param  wrctr        number of written bytes
 * @param  subaddr      true: sub-address is transmitted
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _I2C_LLD__master_transmit(I2C_dev_t *hdl, const u8_t *src, size_t count, size_t *wrctr, bool subaddr)
{
        UNUSED_ARG1(subaddr);

        *wrctr = 0;

        if (count == 0) {
                return ESUCC;
        }

        size_t n = 0;
        int result = _host_i2c_write(hdl->major, src, count, &n);
        *wrctr = n;

        return (result == RESULT_ACK) ? ESUCC : EIO;
}

//==============================================================================
/**
 * @brief  Function configure slave mode (not supported in the simulator).
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _I2C_LLD__slave_mode_setup(I2C_dev_t *hdl)
{
        return (hdl->config.mode == I2C_MODE__SLAVE) ? ENOTSUP : ESUCC;
}

//==============================================================================
/**
 * @brief  Function wait for selection in slave mode (not supported).
 *
 * @param  hdl          device handle
 * @param  event        event
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _I2C_LLD__slave_wait_for_selection(I2C_dev_t *hdl, I2C_selection_t *event)
{
        UNUSED_ARG2(hdl, event);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief  Function transmit data in slave mode (not supported).
 *
 * @param  hdl          device handle
 * @param  src          data source
 * @param  count        number of bytes
 * @param  wrctr        number of written bytes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _I2C_LLD__slave_transmit(I2C_dev_t *hdl, const u8_t *src, size_t count, size_t *wrctr)
{
        UNUSED_ARG3(hdl, src, count);
        *wrctr = 0;
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief  Function receive data in slave mode (not supported).
 *
 * @param  hdl          device handle
 * @param  dst          destination
 * @param  count        number of bytes
 * @param  rdctr        number of read bytes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _I2C_LLD__slave_receive(I2C_dev_t *hdl, u8_t *dst, size_t count, size_t *rdctr)
{
        UNUSED_ARG3(hdl, dst, count);
        *rdctr = 0;
        return ENOTSUP;
}

/*==============================================================================
  End of file
==============================================================================*/
