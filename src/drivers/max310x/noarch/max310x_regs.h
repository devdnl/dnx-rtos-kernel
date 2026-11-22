/*==============================================================================
File    max310x_regs.h

Author  Dawid Wałach

Brief   MAX310x registers (MAX3107, MAX3108, MAX3109)

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

#pragma once

/*==============================================================================
  Include files
==============================================================================*/

/*==============================================================================
  Exported macros
==============================================================================*/
#define MAX310X_FIFO_SIZE 128U

/* Registers (address of register, SPI: bit 7 of command = 1 for write) */
#define MAX310X_REG_RHR_THR      0x00 /* RX holding / TX holding register (FIFO) */
#define MAX310X_REG_IRQEN        0x01 /* IRQ enable */
#define MAX310X_REG_ISR          0x02 /* Interrupt status */
#define MAX310X_REG_LSRINTEN     0x03 /* LSR interrupt enable */
#define MAX310X_REG_LSR          0x04 /* Line status */
#define MAX310X_REG_SPCLCHRINTEN 0x05 /* Special character interrupt enable */
#define MAX310X_REG_SPCLCHARINT  0x06 /* Special character interrupt */
#define MAX310X_REG_STSINTEN     0x07 /* Status interrupt enable */
#define MAX310X_REG_STSINT       0x08 /* Status interrupt */
#define MAX310X_REG_MODE1        0x09 /* Mode 1 */
#define MAX310X_REG_MODE2        0x0A /* Mode 2 */
#define MAX310X_REG_LCR          0x0B /* Line control */
#define MAX310X_REG_RXTIMEOUT    0x0C /* RX timeout */
#define MAX310X_REG_HDPLXDELAY   0x0D /* Half duplex delay (setup [7:4], hold [3:0]) */
#define MAX310X_REG_IRDA         0x0E /* IrDA */
#define MAX310X_REG_FLOWLVL      0x0F /* Flow control levels */
#define MAX310X_REG_FIFOTRGLVL   0x10 /* FIFO interrupt trigger levels */
#define MAX310X_REG_TXFIFOLVL    0x11 /* TX FIFO level */
#define MAX310X_REG_RXFIFOLVL    0x12 /* RX FIFO level */
#define MAX310X_REG_FLOWCTRL     0x13 /* Flow control */
#define MAX310X_REG_XON1         0x14 /* XON1 character */
#define MAX310X_REG_XON2         0x15 /* XON2 character */
#define MAX310X_REG_XOFF1        0x16 /* XOFF1 character */
#define MAX310X_REG_XOFF2        0x17 /* XOFF2 character */
#define MAX310X_REG_GPIOCONFIG   0x18 /* GPIO configuration */
#define MAX310X_REG_GPIODATA     0x19 /* GPIO data */
#define MAX310X_REG_PLLCONFIG    0x1A /* PLL configuration */
#define MAX310X_REG_BRGCONFIG    0x1B /* Baud rate generator configuration */
#define MAX310X_REG_DIVLSB       0x1C /* Baud rate divisor LSB */
#define MAX310X_REG_DIVMSB       0x1D /* Baud rate divisor MSB */
#define MAX310X_REG_CLKSOURCE    0x1E /* Clock source */
#define MAX310X_REG_REVID        0x1F /* MAX3107: revision ID */
#define MAX310X_REG_GLOBALCMD    0x1F /* MAX3109: global command (write) */
#define MAX310X_REG_GLOBALIRQ    0x1F /* MAX3109: global IRQ (read) */

/* MAX3109 extended registers (enabled by global command) */
#define MAX310X_GLOBALCMD_EXTREG_EN  0xCE /* enable extended registers */
#define MAX310X_GLOBALCMD_EXTREG_DIS 0xCD /* disable extended registers */
#define MAX310X_EXTREG_REVID_SPI     0x05 /* revision ID (SPI) */
#define MAX310X_EXTREG_REVID_I2C     0x25 /* revision ID (I2C) */

/* Revision ID: bits [2:0] silicon revision */
#define MAX310X_REVID_MASK    0xF8
#define MAX310X_REVID_MAX3107 0xA0
#define MAX310X_REVID_MAX3109 0xC0

/* MAX3108 has no revision ID: reset value of CLKSource register */
#define MAX310X_CLKSOURCE_RESET 0x18

/* SPI command: bit 7 write, bits 6..5 port (MAX3109), bits 4..0 register */
#define MAX310X_SPI_WRITE_BIT  0x80
#define MAX310X_SPI_PORT_SHIFT 5

/* I2C: address of port 1 (MAX3109) is lower by 0x10 than address of port 0 */
#define MAX310X_I2C_PORT1_OFFSET 0x10

/* Reset value of DIVLSB register (UART ready after reset) */
#define MAX310X_DIVLSB_RESET 0x01

/* IRQ register bits */
#define MAX310X_IRQ_LSR_BIT     (1 << 0) /* LSR interrupt */
#define MAX310X_IRQ_SPCHR_BIT   (1 << 1) /* Special char interrupt */
#define MAX310X_IRQ_STS_BIT     (1 << 2) /* Status interrupt */
#define MAX310X_IRQ_RXFIFO_BIT  (1 << 3) /* RX FIFO interrupt */
#define MAX310X_IRQ_TXFIFO_BIT  (1 << 4) /* TX FIFO interrupt */
#define MAX310X_IRQ_TXEMPTY_BIT (1 << 5) /* TX FIFO empty interrupt */
#define MAX310X_IRQ_RXEMPTY_BIT (1 << 6) /* RX FIFO empty interrupt */
#define MAX310X_IRQ_CTS_BIT     (1 << 7) /* CTS interrupt */

/* LSR register bits */
#define MAX310X_LSR_RXTO_BIT    (1 << 0) /* RX timeout */
#define MAX310X_LSR_RXOVR_BIT   (1 << 1) /* RX overrun */
#define MAX310X_LSR_RXPAR_BIT   (1 << 2) /* RX parity error */
#define MAX310X_LSR_FRERR_BIT   (1 << 3) /* Frame error */
#define MAX310X_LSR_RXBRK_BIT   (1 << 4) /* RX break */
#define MAX310X_LSR_RXNOISE_BIT (1 << 5) /* RX noise */
#define MAX310X_LSR_UNDEF6_BIT  (1 << 6) /* Undefined/not used */
#define MAX310X_LSR_CTS_BIT     (1 << 7) /* CTS pin state */

/* Special character register bits */
#define MAX310X_SPCHR_XON1_BIT      (1 << 0) /* XON1 character */
#define MAX310X_SPCHR_XON2_BIT      (1 << 1) /* XON2 character */
#define MAX310X_SPCHR_XOFF1_BIT     (1 << 2) /* XOFF1 character */
#define MAX310X_SPCHR_XOFF2_BIT     (1 << 3) /* XOFF2 character */
#define MAX310X_SPCHR_BREAK_BIT     (1 << 4) /* RX break */
#define MAX310X_SPCHR_MULTIDROP_BIT (1 << 5) /* 9-bit multidrop addr char */
#define MAX310X_SPCHR_UNDEF6_BIT    (1 << 6) /* Undefined/not used */
#define MAX310X_SPCHR_UNDEF7_BIT    (1 << 7) /* Undefined/not used */

/* Status register bits */
#define MAX310X_STS_GPIO0_BIT    (1 << 0) /* GPIO 0 interrupt */
#define MAX310X_STS_GPIO1_BIT    (1 << 1) /* GPIO 1 interrupt */
#define MAX310X_STS_GPIO2_BIT    (1 << 2) /* GPIO 2 interrupt */
#define MAX310X_STS_GPIO3_BIT    (1 << 3) /* GPIO 3 interrupt */
#define MAX310X_STS_UNDEF4_BIT   (1 << 4) /* Undefined/not used */
#define MAX310X_STS_CLKREADY_BIT (1 << 5) /* Clock ready */
#define MAX310X_STS_SLEEP_BIT    (1 << 6) /* Sleep interrupt */
#define MAX310X_STS_UNDEF7_BIT   (1 << 7) /* Undefined/not used */

/* MODE1 register bits */
#define MAX310X_MODE1_RXDIS_BIT      (1 << 0) /* RX disable */
#define MAX310X_MODE1_TXDIS_BIT      (1 << 1) /* TX disable */
#define MAX310X_MODE1_TXHIZ_BIT      (1 << 2) /* TX pin three-state */
#define MAX310X_MODE1_RTSHIZ_BIT     (1 << 3) /* RTS pin three-state */
#define MAX310X_MODE1_TRNSCVCTRL_BIT (1 << 4) /* Transceiver ctrl enable */
#define MAX310X_MODE1_FORCESLEEP_BIT (1 << 5) /* Force sleep mode */
#define MAX310X_MODE1_AUTOSLEEP_BIT  (1 << 6) /* Auto sleep enable */
#define MAX310X_MODE1_IRQSEL_BIT     (1 << 7) /* IRQ pin enable */

/* MODE2 register bits */
#define MAX310X_MODE2_RST_BIT       (1 << 0) /* Chip reset */
#define MAX310X_MODE2_FIFORST_BIT   (1 << 1) /* FIFO reset */
#define MAX310X_MODE2_RXTRIGINV_BIT (1 << 2) /* RX FIFO INT invert */
#define MAX310X_MODE2_RXEMPTINV_BIT (1 << 3) /* RX FIFO empty INT invert */
#define MAX310X_MODE2_SPCHR_BIT     (1 << 4) /* Special chr detect enable */
#define MAX310X_MODE2_LOOPBACK_BIT  (1 << 5) /* Internal loopback enable */
#define MAX310X_MODE2_MULTIDROP_BIT (1 << 6) /* 9-bit multidrop enable */
#define MAX310X_MODE2_ECHOSUPR_BIT  (1 << 7) /* ECHO suppression enable */

/* LCR register bits */
#define MAX310X_LCR_LENGTH0_BIT (1 << 0) /* Word length bit 0 */
/*
 * Word length bit 1:
 * Word length bits table:
 * 00 -> 5 bit words
 * 01 -> 6 bit words
 * 10 -> 7 bit words
 * 11 -> 8 bit words
 */
#define MAX310X_LCR_LENGTH1_BIT (1 << 1)

/*
 * STOP length bit:
 * STOP length bit table:
 * 0 -> 1 stop bit
 * 1 -> 1-1.5 stop bits if
 * word length is 5,
 * 2 stop bits otherwise
 */
#define MAX310X_LCR_STOPLEN_BIT (1 << 2)

#define MAX310X_LCR_PARITY_BIT      (1 << 3) /* Parity bit enable */
#define MAX310X_LCR_EVENPARITY_BIT  (1 << 4) /* Even parity bit enable */
#define MAX310X_LCR_FORCEPARITY_BIT (1 << 5) /* 9-bit multidrop parity */
#define MAX310X_LCR_TXBREAK_BIT     (1 << 6) /* TX break enable */
#define MAX310X_LCR_RTS_BIT         (1 << 7) /* RTS pin control */
#define MAX310X_LCR_WORD_LEN_5      (0x0000)
#define MAX310X_LCR_WORD_LEN_6      (0x0001)
#define MAX310X_LCR_WORD_LEN_7      (0x0002)
#define MAX310X_LCR_WORD_LEN_8      (0x0003)

/* IRDA register bits */
#define MAX310X_IRDA_IRDAEN_BIT  (1 << 0) /* IRDA mode enable */
#define MAX310X_IRDA_SIR_BIT     (1 << 1) /* SIR mode enable */
#define MAX310X_IRDA_SHORTIR_BIT (1 << 2) /* Short SIR mode enable */
#define MAX310X_IRDA_MIR_BIT     (1 << 3) /* MIR mode enable */
#define MAX310X_IRDA_RXINV_BIT   (1 << 4) /* RX logic inversion enable */
#define MAX310X_IRDA_TXINV_BIT   (1 << 5) /* TX logic inversion enable */
#define MAX310X_IRDA_UNDEF6_BIT  (1 << 6) /* Undefined/not used */
#define MAX310X_IRDA_UNDEF7_BIT  (1 << 7) /* Undefined/not used */

/* Flow control trigger level register masks */
#define MAX310X_FLOWLVL_HALT_MASK   (0x000f) /* Flow control halt level */
#define MAX310X_FLOWLVL_RES_MASK    (0x00f0) /* Flow control resume level */
#define MAX310X_FLOWLVL_HALT(words) ((words / 8) & 0x000f)
#define MAX310X_FLOWLVL_RES(words)  (((words / 8) & 0x000f) << 4)

/* FIFO interrupt trigger level register masks */
#define MAX310X_FIFOTRIGLVL_TX(words) ((words / 8) & 0x000f)
#define MAX310X_FIFOTRIGLVL_RX(words) (((words / 8) & 0x000f) << 4)

/* Flow control register bits */
#define MAX310X_FLOWCTRL_AUTORTS_BIT (1 << 0) /* Auto RTS flow ctrl enable */
#define MAX310X_FLOWCTRL_AUTOCTS_BIT (1 << 1) /* Auto CTS flow ctrl enable */
/*
 * Enables that GPIO inputs:
 * are used in conjunction with
 * XOFF2 for definition of
 * special character
 */
#define MAX310X_FLOWCTRL_GPIADDR_BIT (1 << 2)

#define MAX310X_FLOWCTRL_SWFLOWEN_BIT (1 << 3) /* Auto SW flow ctrl enable */
#define MAX310X_FLOWCTRL_SWFLOW0_BIT  (1 << 4) /* SWFLOW bit 0 */
/*
 * SWFLOW bit 1:
 * SWFLOW bits 1 & 0 table:
 * 00 -> no transmitter flow
 * control
 * 01 -> receiver compares
 * XON2 and XOFF2
 * and controls
 * transmitter
 * 10 -> receiver compares
 * XON1 and XOFF1
 * and controls
 * transmitter
 * 11 -> receiver compares
 * XON1, XON2, XOFF1 and
 * XOFF2 and controls
 * transmitter
 */
#define MAX310X_FLOWCTRL_SWFLOW1_BIT (1 << 5)

#define MAX310X_FLOWCTRL_SWFLOW2_BIT (1 << 6) /* SWFLOW bit 2 */
/*
 * SWFLOW bit 3:
 * SWFLOW bits 3 & 2 table:
 * 00 -> no received flow
 * control
 * 01 -> transmitter generates
 * XON2 and XOFF2
 * 10 -> transmitter generates
 * XON1 and XOFF1
 * 11 -> transmitter generates
 * XON1, XON2, XOFF1 and
 * XOFF2
 */
#define MAX310X_FLOWCTRL_SWFLOW3_BIT (1 << 7)

/* GPIO configuration register bits */
#define MAX310X_GPIOCFG_GP0OUT_BIT (1 << 0) /* GPIO 0 output enable */
#define MAX310X_GPIOCFG_GP1OUT_BIT (1 << 1) /* GPIO 1 output enable */
#define MAX310X_GPIOCFG_GP2OUT_BIT (1 << 2) /* GPIO 2 output enable */
#define MAX310X_GPIOCFG_GP3OUT_BIT (1 << 3) /* GPIO 3 output enable */
#define MAX310X_GPIOCFG_GP0OD_BIT  (1 << 4) /* GPIO 0 open-drain enable */
#define MAX310X_GPIOCFG_GP1OD_BIT  (1 << 5) /* GPIO 1 open-drain enable */
#define MAX310X_GPIOCFG_GP2OD_BIT  (1 << 6) /* GPIO 2 open-drain enable */
#define MAX310X_GPIOCFG_GP3OD_BIT  (1 << 7) /* GPIO 3 open-drain enable */

/* GPIO DATA register bits */
#define MAX310X_GPIODATA_GP0OUT_BIT (1 << 0) /* GPIO 0 output value */
#define MAX310X_GPIODATA_GP1OUT_BIT (1 << 1) /* GPIO 1 output value */
#define MAX310X_GPIODATA_GP2OUT_BIT (1 << 2) /* GPIO 2 output value */
#define MAX310X_GPIODATA_GP3OUT_BIT (1 << 3) /* GPIO 3 output value */
#define MAX310X_GPIODATA_GP0IN_BIT  (1 << 4) /* GPIO 0 input value */
#define MAX310X_GPIODATA_GP1IN_BIT  (1 << 5) /* GPIO 1 input value */
#define MAX310X_GPIODATA_GP2IN_BIT  (1 << 6) /* GPIO 2 input value */
#define MAX310X_GPIODATA_GP3IN_BIT  (1 << 7) /* GPIO 3 input value */

/* PLL configuration register masks */
#define MAX310X_PLLCFG_PREDIV_MASK    (0x003f) /* PLL predivision value */
#define MAX310X_PLLCFG_PLLFACTOR_MASK (0x00c0) /* PLL multiplication factor */

/* Baud rate generator configuration register masks and bits */
/*
 * Fractional portion of:
 * Baud rate generator divisor
 */
#define MAX310X_BRGCFG_FRACT_MASK (0x000f)
#define MAX310X_BRGCFG_2XMODE_BIT (1 << 4) /* Double baud rate */
#define MAX310X_BRGCFG_4XMODE_BIT (1 << 5) /* Quadruple baud rate */
#define MAX310X_BRGCFG_UNDEF6_BIT (1 << 6) /* Undefined/not used */
#define MAX310X_BRGCFG_UNDEF7_BIT (1 << 7) /* Undefined/not used */

/* Clock source register bits */
#define MAX310X_CLKSRC_CRYST_BIT   (1 << 1) /* Crystal osc enable */
#define MAX310X_CLKSRC_PLL_BIT     (1 << 2) /* PLL enable */
#define MAX310X_CLKSRC_PLLBYP_BIT  (1 << 3) /* PLL bypass */
#define MAX310X_CLKSRC_EXTCLK_BIT  (1 << 4) /* External clock enable */
#define MAX310X_CLKSRC_UNDEF5_BIT  (1 << 5) /* Undefined/not used */
#define MAX310X_CLKSRC_UNDEF6_BIT  (1 << 6) /* Undefined/not used */
#define MAX310X_CLKSRC_CLK2RTS_BIT (1 << 7) /* Baud clk to RTS pin */

/*==============================================================================
  End of file
==============================================================================*/
