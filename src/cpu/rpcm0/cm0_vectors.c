/*==============================================================================
File     rpcm0_vectors.c

Author   Daniel Zorychta

Brief    RPCM0 vectors.

         Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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
#include "config.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

/** define weak assign to the default function */
#define WEAK_DEFAULT __attribute__((interrupt, weak, alias("__Default_Handler")))

/*============================================================================*/
/**
 * \brief Default interrupt handler.
 * \details Default interrupt handler, used for interrupts that don't have their
 * own handler defined.
 */
/*============================================================================*/
static void __Default_Handler(void) __attribute__((interrupt));
static void __Default_Handler(void)
{
        while (1) {
                __asm volatile("nop");
        }
}

/*==============================================================================
Assign all unused interrupts to the default handler
==============================================================================*/
WEAK_DEFAULT void NMI_Handler(void);
WEAK_DEFAULT void HardFault_Handler(void);
WEAK_DEFAULT void __Reserved_0x0C_Handler(void);
WEAK_DEFAULT void __Reserved_0x10_Handler(void);
WEAK_DEFAULT void __Reserved_0x14_Handler(void);
WEAK_DEFAULT void __Reserved_0x18_Handler(void);
WEAK_DEFAULT void __Reserved_0x1C_Handler(void);
WEAK_DEFAULT void __Reserved_0x20_Handler(void);
WEAK_DEFAULT void __Reserved_0x24_Handler(void);
WEAK_DEFAULT void SVC_Handler(void);
WEAK_DEFAULT void __Reserved_0x2C_Handler(void);
WEAK_DEFAULT void __Reserved_0x30_IRQHandler(void);
WEAK_DEFAULT void PendSV_Handler(void);
WEAK_DEFAULT void SysTick_Handler(void);
WEAK_DEFAULT void TIMER_IRQ_0_IRQHandler(void);
WEAK_DEFAULT void TIMER_IRQ_1_IRQHandler(void);
WEAK_DEFAULT void TIMER_IRQ_2_IRQHandler(void);
WEAK_DEFAULT void TIMER_IRQ_3_IRQHandler(void);
WEAK_DEFAULT void PWM_IRQ_WRAP_IRQHandler(void);
WEAK_DEFAULT void USBCTRL_IRQ_IRQHandler(void);
WEAK_DEFAULT void XIP_IRQ_IRQHandler(void);
WEAK_DEFAULT void PIO0_IRQ_0_IRQHandler(void);
WEAK_DEFAULT void PIO0_IRQ_1_IRQHandler(void);
WEAK_DEFAULT void PIO1_IRQ_0_IRQHandler(void);
WEAK_DEFAULT void PIO1_IRQ_1_IRQHandler(void);
WEAK_DEFAULT void DMA_IRQ_0_IRQHandler(void);
WEAK_DEFAULT void DMA_IRQ_1_IRQHandler(void);
WEAK_DEFAULT void IO_IRQ_BANK0_IRQHandler(void);
WEAK_DEFAULT void IO_IRQ_QSPI_IRQHandler(void);
WEAK_DEFAULT void SIO_IRQ_PROC0_IRQHandler(void);
WEAK_DEFAULT void SIO_IRQ_PROC1_IRQHandler(void);
WEAK_DEFAULT void CLOCKS_IRQ_IRQHandler(void);
WEAK_DEFAULT void SPI0_IRQ_IRQHandler(void);
WEAK_DEFAULT void SPI1_IRQ_IRQHandler(void);
WEAK_DEFAULT void UART0_IRQ_IRQHandler(void);
WEAK_DEFAULT void UART1_IRQ_IRQHandler(void);
WEAK_DEFAULT void ADC_IRQ_FIFO_IRQHandler(void);
WEAK_DEFAULT void I2C0_IRQ_IRQHandler(void);
WEAK_DEFAULT void I2C1_IRQ_IRQHandler(void);
WEAK_DEFAULT void RTC_IRQ_IRQHandler(void);

/*==============================================================================
Vector table
==============================================================================*/
extern const char __main_stack_end; /* imported main stack end (from linker script) */

void Reset_Handler(void); /* import the address of Reset_Handler()             */

void (*const vectors[256])(void) __attribute__((section(".vectors"))) = {
        (void (*)(void))&__main_stack_end, /* Main stack end address  */

        Reset_Handler,              /* -15 Reset Vector, invoked on Power up and warm reset */
        NMI_Handler,                /* -14 Non-maskable interrupt (RCC clock security system) */
        HardFault_Handler,          /* -13 All class of fault  */
        __Reserved_0x0C_Handler,    /* -12  */
        __Reserved_0x10_Handler,    /* -11  */
        __Reserved_0x14_Handler,    /* -10  */
        __Reserved_0x18_Handler,    /* -9 */
        __Reserved_0x1C_Handler,    /* -8 */
        __Reserved_0x20_Handler,    /* -7 */
        __Reserved_0x24_Handler,    /* -6 */
        SVC_Handler,                /* -5 System service call via SWI instruction */
        __Reserved_0x2C_Handler,    /* -4  */
        __Reserved_0x30_IRQHandler, /* -3  */
        PendSV_Handler,             /* -2 Pendable request for system service */
        SysTick_Handler,            /* -1 System tick timer */

        /* RP2040 Specific Interrupts */
        TIMER_IRQ_0_IRQHandler,   /* 0 TIMER_IRQ_0 */
        TIMER_IRQ_1_IRQHandler,   /* 1 TIMER_IRQ_1 */
        TIMER_IRQ_2_IRQHandler,   /* 2 TIMER_IRQ_2 */
        TIMER_IRQ_3_IRQHandler,   /* 3 TIMER_IRQ_3 */
        PWM_IRQ_WRAP_IRQHandler,  /* 4 PWM_IRQ_WRAP */
        USBCTRL_IRQ_IRQHandler,   /* 5 USBCTRL_IRQ */
        XIP_IRQ_IRQHandler,       /* 6 XIP_IRQ */
        PIO0_IRQ_0_IRQHandler,    /* 7 PIO0_IRQ_0 */
        PIO0_IRQ_1_IRQHandler,    /* 8 PIO0_IRQ_1 */
        PIO1_IRQ_0_IRQHandler,    /* 9 PIO1_IRQ_0 */
        PIO1_IRQ_1_IRQHandler,    /* 10 PIO1_IRQ_1 */
        DMA_IRQ_0_IRQHandler,     /* 11 DMA_IRQ_0 */
        DMA_IRQ_1_IRQHandler,     /* 12 DMA_IRQ_1 */
        IO_IRQ_BANK0_IRQHandler,  /* 13 IO_IRQ_BANK0 */
        IO_IRQ_QSPI_IRQHandler,   /* 14 IO_IRQ_QSPI */
        SIO_IRQ_PROC0_IRQHandler, /* 15 SIO_IRQ_PROC0 */
        SIO_IRQ_PROC1_IRQHandler, /* 16 SIO_IRQ_PROC1 */
        CLOCKS_IRQ_IRQHandler,    /* 17 CLOCKS_IRQ */
        SPI0_IRQ_IRQHandler,      /* 18 SPI0_IRQ */
        SPI1_IRQ_IRQHandler,      /* 19 SPI1_IRQ */
        UART0_IRQ_IRQHandler,     /* 20 UART0_IRQ */
        UART1_IRQ_IRQHandler,     /* 21 UART1_IRQ */
        ADC_IRQ_FIFO_IRQHandler,  /* 22 ADC_IRQ_FIFO */
        I2C0_IRQ_IRQHandler,      /* 23 I2C0_IRQ */
        I2C1_IRQ_IRQHandler,      /* 24 I2C1_IRQ */
        RTC_IRQ_IRQHandler,       /* 25 RTC_IRQ */
};

#pragma GCC diagnostic pop

/*==============================================================================
END OF FILE
==============================================================================*/
