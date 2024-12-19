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

// #pragma GCC optimize("Og")

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"

#include "uart.h"
#include "uart_ioctl.h"

#include "hardware/regs/clocks.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/clocks.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
typedef struct {
        uart_hw_t *uart_hw;
        u32_t      resets_mask;
        u32_t      resets_done_mask;
        IRQn_Type  IRQn;
        u32_t      IRQ_priority;
} UART_setup_t;

/* UART specific handle */
typedef struct {
        u32_t rx_overrun;
} uarthdl_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static uint32_t uart_disable_before_lcr_write(struct UART_mem *hdl);
static void     uart_set_baudrate(struct UART_mem *hdl, uint baudrate);

/*==============================================================================
  Local object definitions
==============================================================================*/
MODULE_NAME(UART);

static const UART_setup_t UART[] = {
        {
                .uart_hw          = uart0_hw,
                .resets_mask      = RESETS_RESET_UART0_BITS,
                .resets_done_mask = RESETS_RESET_DONE_UART0_BITS,
                .IRQn             = UART0_IRQ_IRQn,
                .IRQ_priority     = __UART_UART0_IRQ_PRIORITY__,
        },
        {
                .uart_hw          = uart1_hw,
                .resets_mask      = RESETS_RESET_UART1_BITS,
                .resets_done_mask = RESETS_RESET_DONE_UART1_BITS,
                .IRQn             = UART1_IRQ_IRQn,
                .IRQ_priority     = __UART_UART1_IRQ_PRIORITY__,
        },
};

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

        uarthdl_t *uarthdl;
        err = sys_zalloc(sizeof(*uarthdl), cast(void **, &uarthdl));
        if (not err) {
                hdl->uarthdl = uarthdl;

                SET_BIT(clocks_hw->clk[clk_peri].ctrl, CLOCKS_CLK_PERI_CTRL_ENABLE_BITS);

                SET_BIT(resets_hw->reset, UART[hdl->major].resets_mask);
                CLEAR_BIT(resets_hw->reset, UART[hdl->major].resets_mask);

                while ((resets_hw->reset_done & UART[hdl->major].resets_done_mask) == 0) {
                        sys_sleep_ms(1);
                }

                NVIC_EnableIRQ(UART[hdl->major].IRQn);
                NVIC_SetPriority(UART[hdl->major].IRQn, UART[hdl->major].IRQ_priority);

        } else {
                SET_BIT(resets_hw->reset, UART[hdl->major].resets_mask);
        }

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
        NVIC_DisableIRQ(UART[hdl->major].IRQn);
        SET_BIT(resets_hw->reset, UART[hdl->major].resets_mask);
        sys_free(&hdl->uarthdl);

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
        uart_hw_t *uart_hw = UART[hdl->major].uart_hw;

        if (not(uart_hw->imsc & UART_UARTIMSC_TXIM_BITS)) {
                if (uart_hw->fr & UART_UARTFR_TXFF_BITS) {
                        SET_BIT(uart_hw->imsc, UART_UARTIMSC_TXIM_BITS);

                } else {
                        u8_t byte;
                        if (sys_queue_receive(hdl->tx_queue, &byte, 0) == 0) {
                                uart_hw->dr = byte;
                        }
                }
        }
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
        uart_hw_t *uart_hw = UART[hdl->major].uart_hw;

        if (config->basic.features & UART_FEATURE__SINGLE_WIRE) {
                dev_dbg(hdl, "single wire mode not supported.");
        }

        if (config->basic.mode == UART_MODE__RS485) {
                dev_dbg(hdl, "RS485 mode not supported.");
        }

        if (config->basic.mode == UART_MODE__LIN) {
                dev_dbg(hdl, "LIN mode not supported.");
        }

        uart_set_baudrate(hdl, config->basic.baud);

        bool hwfc = (config->basic.features & UART_FEATURE__HARDWARE_FLOW_CTRL);

        uart_hw->imsc = UART_UARTIMSC_RTIM_BITS;

        uart_hw->ifls = (1 << UART_UARTIFLS_RXIFLSEL_LSB) | (0 << UART_UARTIFLS_TXIFLSEL_LSB);

        uart_hw->lcr_h = ((8 - 5u) << UART_UARTLCR_H_WLEN_LSB) |
                ((config->basic.stop_bits) << UART_UARTLCR_H_STP2_LSB) |
                ((config->basic.parity != UART_PARITY__OFF) << UART_UARTLCR_H_PEN_LSB) |
                ((config->basic.parity == UART_PARITY__EVEN) << UART_UARTLCR_H_EPS_LSB) |
                UART_UARTLCR_H_FEN_BITS;

        uart_hw->cr = UART_UARTCR_UARTEN_BITS | (config->basic.tx_enable * UART_UARTCR_TXE_BITS) |
                (config->basic.rx_enable * UART_UARTCR_RXE_BITS) | (hwfc * UART_UARTCR_RTSEN_BITS) |
                (hwfc * UART_UARTCR_CTSEN_BITS);

        uart_hw->dmacr = UART_UARTDMACR_TXDMAE_BITS | UART_UARTDMACR_RXDMAE_BITS;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Disable UART before change LCR register.
 *
 * @param  hdl          driver handle
 *
 * @return CR register.
 */
//==============================================================================
static uint32_t uart_disable_before_lcr_write(struct UART_mem *hdl)
{
        uart_hw_t *uart_hw = UART[hdl->major].uart_hw;

        // Notes from PL011 reference manual:
        //
        // - Before writing the LCR, if the UART is enabled it needs to be
        //   disabled and any current TX + RX activity has to be completed
        //
        // - There is a BUSY flag which waits for the current TX char, but this is
        //   OR'd with TX FIFO !FULL, so not usable when FIFOs are enabled and
        //   potentially nonempty
        //
        // - FIFOs can't be set to disabled whilst a character is in progress
        //   (else "FIFO integrity is not guaranteed")
        //
        // Combination of these means there is no general way to halt and poll for
        // end of TX character, if FIFOs may be enabled. Either way, there is no
        // way to poll for end of RX character.
        //
        // So, insert a 15 Baud period delay before changing the settings.
        // 15 Baud is comfortably higher than start + max data + parity + stop.
        // Anything else would require API changes to permit a non-enabled UART
        // state after init() where settings can be changed safely.
        uint32_t cr_save = uart_hw->cr;

        if (cr_save & UART_UARTCR_UARTEN_BITS) {
                CLEAR_BIT(uart_hw->cr, UART_UARTCR_UARTEN_BITS | UART_UARTCR_TXE_BITS | UART_UARTCR_RXE_BITS);

                uint32_t current_ibrd = uart_hw->ibrd;
                uint32_t current_fbrd = uart_hw->fbrd;

                // Note: Maximise precision here. Show working, the compiler will mop this up.
                // Create a 16.6 fixed-point fractional division ratio; then scale to 32-bits.
                uint32_t brdiv_ratio = 64u * current_ibrd + current_fbrd;
                brdiv_ratio <<= 10;
                // 3662 is ~(15 * 244.14) where 244.14 is 16e6 / 2^16
                uint32_t scaled_freq  = clocks__get_uart_frequency(hdl->major) / 3662ul;
                uint32_t wait_time_us = brdiv_ratio / scaled_freq;

                sys_sleep_ms(wait_time_us / 1000 + 1);
        }

        return cr_save;
}

//==============================================================================
/**
 * @brief  Set UART baud rate.
 *
 * @param  hdl          driver handle
 * @param  baudrate     baudrate
 */
//==============================================================================
static void uart_set_baudrate(struct UART_mem *hdl, uint baudrate)
{
        uart_hw_t *uart_hw = UART[hdl->major].uart_hw;

        uint32_t baud_rate_div = (8 * clocks__get_uart_frequency(hdl->major) / baudrate) + 1;
        uint32_t baud_ibrd     = baud_rate_div >> 7;
        uint32_t baud_fbrd;

        if (baud_ibrd == 0) {
                baud_ibrd = 1;
                baud_fbrd = 0;
        } else if (baud_ibrd >= 65535) {
                baud_ibrd = 65535;
                baud_fbrd = 0;
        } else {
                baud_fbrd = (baud_rate_div & 0x7f) >> 1;
        }

        uart_hw->ibrd = baud_ibrd;
        uart_hw->fbrd = baud_fbrd;

        // PL011 needs a (dummy) LCR_H write to latch in the divisors.
        // We don't want to actually change LCR_H contents here.
        uint32_t cr_save = uart_disable_before_lcr_write(hdl);
        uart_hw->lcr_h   = 0;
        uart_hw->cr      = cr_save;
}

//==============================================================================
/**
 * @brief Interrupt handling
 *
 * @param major         major device number
 */
//==============================================================================
static void IRQ_handle(u8_t major)
{
        bool yield = false;

        struct UART_mem *hdl     = _UART_mem[major];
        uart_hw_t       *uart_hw = UART[major].uart_hw;
        uarthdl_t       *uarthdl = hdl->uarthdl;

        /*
         * Receive interrupt
         */
        if (uart_hw->mis & UART_UARTMIS_RTMIS_BITS) {
                while (not(uart_hw->fr & UART_UARTFR_RXFE_BITS)) {
                        u8_t byte  = uart_hw->dr;
                        bool woken = false;
                        if (sys_queue_send_from_ISR(hdl->rx_queue, &byte, &woken) != 0) {
                                uarthdl->rx_overrun++;
                        }
                        yield |= woken;
                }
        }

        /* transmitter interrupt handler */
        while (uart_hw->mis & UART_UARTMIS_TXMIS_BITS) {
                bool woken = false;
                u8_t byte;
                if (sys_queue_receive_from_ISR(hdl->tx_queue, &byte, &woken) == 0) {
                        uart_hw->dr = byte;
                } else {
                        CLEAR_BIT(uart_hw->imsc, UART_UARTIMSC_TXIM_BITS);
                        uart_hw->icr = UART_UARTICR_TXIC_BITS;
                        break;
                }
                yield |= woken;
        }

        /* yield thread if data send or received */
        sys_thread_yield_from_ISR(yield);
}

//==============================================================================
/**
 * @brief UART0 Interrupt
 */
//==============================================================================
void UART0_IRQ_IRQHandler(void)
{
        IRQ_handle(_UART0);
}

//==============================================================================
/**
 * @brief UART1 Interrupt
 */
//==============================================================================
void UART1_IRQ_IRQHandler(void)
{
        IRQ_handle(_UART1);
}

/*==============================================================================
  End of file
==============================================================================*/
