/*==============================================================================
@file    uart_lld.c

@author  Daniel Zorychta

@brief   UART low level driver (POSIX simulator). UART0 is connected to the
         host terminal (standard input and output).

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
#include "../uart.h"
#include "../uart_ioctl.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define RX_POLL_INTERVAL_MS 5
#define RX_CHUNK_SIZE       64
#define TX_CHUNK_SIZE       256

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
/* UART specific handle */
typedef struct {
        tid_t rx_thread;
        u32_t rx_overrun;
} uarthdl_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int rx_service(void *arg);

/*==============================================================================
  Local object definitions
==============================================================================*/
MODULE_NAME(UART);

static const _thread_attr_t RX_SERVICE_ATTR = {
        .stack_depth = _STACK_DEPTH_LOW,
        .priority = _PRIORITY_NORMAL,
        .detached = true,
};

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Function enable UART.
 *
 * @param hdl           uart handle
 *
 * @return One of errno value
 */
//==============================================================================
int _UART_LLD__turn_on(struct UART_mem *hdl)
{
        if (not _host_uart_is_available(hdl->major)) {
                return ENODEV;
        }

        uarthdl_t *uarthdl;
        int err = sys_zalloc(sizeof(*uarthdl), cast(void **, &uarthdl));
        if (not err) {
                hdl->uarthdl = uarthdl;

                err = sys_thread_create(rx_service, &RX_SERVICE_ATTR, hdl, &uarthdl->rx_thread);
                if (err) {
                        sys_free(&hdl->uarthdl);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function disable UART.
 *
 * @param hdl           uart handle
 *
 * @return One of errno value.
 */
//==============================================================================
int _UART_LLD__turn_off(struct UART_mem *hdl)
{
        uarthdl_t *uarthdl = hdl->uarthdl;

        if (uarthdl) {
                if (sys_thread_is_valid(uarthdl->rx_thread)) {
                        sys_thread_destroy(uarthdl->rx_thread);
                }

                sys_free(&hdl->uarthdl);
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief Function resume transmission of buffer. Data is sent to the host
 *        immediately.
 *
 * @param hdl           uart handle
 */
//==============================================================================
void _UART_LLD__resume_transmit(struct UART_mem *hdl)
{
        u8_t buf[TX_CHUNK_SIZE];
        size_t len = 0;

        while ((len < sizeof(buf)) && (sys_queue_receive(hdl->tx_queue, &buf[len], 0) == ESUCC)) {
                len++;
        }

        if (len > 0) {
                _host_uart_write(hdl->major, buf, len);
        }
}

//==============================================================================
/**
 * @brief Function configure selected UART (configuration is ignored).
 *
 * @param hdl           uart handle
 * @param config        configuration structure
 *
 * @return One of errno value.
 */
//==============================================================================
int _UART_LLD__configure(struct UART_mem *hdl, const struct UART_rich_config *config)
{
        UNUSED_ARG2(hdl, config);
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Receiver service. Thread polls host input and puts data to the
 *         receive queue.
 *
 * @param  arg          uart handle
 *
 * @return Never exit.
 */
//==============================================================================
static int rx_service(void *arg)
{
        struct UART_mem *hdl = arg;
        uarthdl_t *uarthdl = hdl->uarthdl;

        for (;;) {
                u8_t buf[RX_CHUNK_SIZE];

                int n = _host_uart_read(hdl->major, buf, sizeof(buf));

                if (n > 0) {
                        for (int i = 0; i < n; i++) {
                                if (sys_queue_send(hdl->rx_queue, &buf[i], _WAIT_FOREVER_MS) != ESUCC) {
                                        uarthdl->rx_overrun++;
                                }
                        }
                } else {
                        sys_sleep_ms(RX_POLL_INTERVAL_MS);
                }
        }

        return 0;
}

/*==============================================================================
  End of file
==============================================================================*/
