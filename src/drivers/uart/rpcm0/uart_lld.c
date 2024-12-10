/*==============================================================================
@file    uart_lld.c

@author  Daniel Zorychta

@brief   This file support USART peripherals for RP2040.

@note    Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

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

#include "uart.h"
#include "uart_ioctl.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
/* UART specific handle */
typedef struct {
} uarthdl_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local object definitions
==============================================================================*/
MODULE_NAME(UART);

/*==============================================================================
  Function definitions
==============================================================================*/
//==============================================================================
/**
 * @brief Function enable USART clock
 *
 * @param hdl           uart handle
 *
 * @return One of errno value
 */
//==============================================================================
int _UART_LLD__turn_on(struct UART_mem *hdl)
{
        int err = ENOTSUP;

        return err;
}

//==============================================================================
/**
 * @brief Function disable USART clock
 *
 * @param hdl           uart handle
 *
 * @return One of errno value.
 */
//==============================================================================
int _UART_LLD__turn_off(struct UART_mem *hdl)
{
        return ESUCC;
}

//==============================================================================
/**
 * @brief Function resume transmission of buffer.
 *
 * @param hdl           uart handle
 */
//==============================================================================
void _UART_LLD__resume_transmit(struct UART_mem *hdl)
{
}

//==============================================================================
/**
 * @brief Function configure selected UART.
 *
 * @param hdl           uart handle
 * @param config        configuration structure
 */
//==============================================================================
int _UART_LLD__configure(struct UART_mem *hdl, const struct UART_rich_config *config)
{
        return ESUCC;
}

/*==============================================================================
  End of file
==============================================================================*/
