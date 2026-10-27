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
#define POLL_INTERVAL_MS 5
#define RX_CHUNK_SIZE    64
#define TX_CHUNK_SIZE    256
#define TX_MUTEX_TIMEOUT 1000

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
/* UART specific handle */
typedef struct {
        tid_t service;
        kmtx_t *tx_mtx;
        u32_t rx_overrun;
        size_t tx_len;
        size_t tx_pos;
        u8_t tx_buf[TX_CHUNK_SIZE];
} uarthdl_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int service(void *arg);
static bool transmit(struct UART_mem *hdl, u32_t timeout);

/*==============================================================================
  Local object definitions
==============================================================================*/
MODULE_NAME(UART);

static const _thread_attr_t SERVICE_ATTR = {
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

                err = sys_mutex_create(KMTX_TYPE_NORMAL, &uarthdl->tx_mtx);
                if (not err) {
                        err = sys_thread_create(service, &SERVICE_ATTR, hdl, &uarthdl->service);
                        if (err) {
                                sys_mutex_destroy(uarthdl->tx_mtx);
                        }
                }

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
                if (sys_thread_is_valid(uarthdl->service)) {
                        sys_thread_destroy(uarthdl->service);
                }

                sys_mutex_destroy(uarthdl->tx_mtx);

                sys_free(&hdl->uarthdl);
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief Function resume transmission of buffer. Driver puts data to the
 *        queue byte by byte, so data is sent to the host by chunks: by writer
 *        when chunk is collected, otherwise by service thread (at least every
 *        POLL_INTERVAL_MS).
 *
 * @param hdl           uart handle
 */
//==============================================================================
void _UART_LLD__resume_transmit(struct UART_mem *hdl)
{
        size_t items = 0;
        sys_queue_get_number_of_items(hdl->tx_queue, &items);

        if (items >= TX_CHUNK_SIZE) {
                transmit(hdl, 0);
        }
}

//==============================================================================
/**
 * @brief Function configure selected UART. Configuration is used only by
 *        UART connected to the host serial port.
 *
 * @param hdl           uart handle
 * @param config        configuration structure
 *
 * @return One of errno value.
 */
//==============================================================================
int _UART_LLD__configure(struct UART_mem *hdl, const struct UART_rich_config *config)
{
        int parity = 0;
        switch (config->basic.parity) {
        case UART_PARITY__ODD:
                parity = 1;
                break;
        case UART_PARITY__EVEN:
                parity = 2;
                break;
        default:
                parity = 0;
                break;
        }

        int stop_bits = (config->basic.stop_bits == UART_STOP_BIT__2) ? 2 : 1;

        int err = _host_uart_configure(hdl->major, config->basic.baud, parity, stop_bits);

        return (err == 0) ? ESUCC : EINVAL;
}

//==============================================================================
/**
 * @brief  Function send data from transmit queue to the host. Data which
 *         cannot be sent now is kept in the local buffer.
 *
 * @param  hdl          uart handle
 * @param  timeout      mutex timeout
 *
 * @return If all data was sent then true is returned.
 */
//==============================================================================
static bool transmit(struct UART_mem *hdl, u32_t timeout)
{
        uarthdl_t *uarthdl = hdl->uarthdl;
        bool done = false;

        if (sys_mutex_lock(uarthdl->tx_mtx, timeout) == ESUCC) {

                for (;;) {
                        if (uarthdl->tx_pos >= uarthdl->tx_len) {
                                uarthdl->tx_pos = 0;
                                uarthdl->tx_len = 0;

                                while ((uarthdl->tx_len < sizeof(uarthdl->tx_buf)) &&
                                        (sys_queue_receive(hdl->tx_queue, &uarthdl->tx_buf[uarthdl->tx_len], 0) ==
                                                ESUCC)) {
                                        uarthdl->tx_len++;
                                }

                                if (uarthdl->tx_len == 0) {
                                        done = true;
                                        break;
                                }
                        }

                        int n = _host_uart_write(hdl->major, &uarthdl->tx_buf[uarthdl->tx_pos],
                                uarthdl->tx_len - uarthdl->tx_pos);

                        if (n > 0) {
                                uarthdl->tx_pos += cast(size_t, n);
                        } else if (n == 0) {
                                break; // UART busy
                        } else {
                                uarthdl->tx_pos = uarthdl->tx_len; // error: drop data
                        }
                }

                sys_mutex_unlock(uarthdl->tx_mtx);
        }

        return done;
}

//==============================================================================
/**
 * @brief  UART service. Thread polls host input and puts data to the receive
 *         queue, sends pending data to the host.
 *
 * @param  arg          uart handle
 *
 * @return Never exit.
 */
//==============================================================================
static int service(void *arg)
{
        struct UART_mem *hdl = arg;
        uarthdl_t *uarthdl = hdl->uarthdl;

        for (;;) {
                bool tx_done = transmit(hdl, TX_MUTEX_TIMEOUT);

                u8_t buf[RX_CHUNK_SIZE];
                int n = _host_uart_read(hdl->major, buf, sizeof(buf));

                if (n > 0) {
                        for (int i = 0; i < n; i++) {
                                if (sys_queue_send(hdl->rx_queue, &buf[i], _WAIT_FOREVER_MS) != ESUCC) {
                                        uarthdl->rx_overrun++;
                                }
                        }
                } else if (tx_done) {
                        sys_sleep_ms(POLL_INTERVAL_MS);
                } else {
                        sys_sleep_ms(1);
                }
        }

        return 0;
}

/*==============================================================================
  End of file
==============================================================================*/
