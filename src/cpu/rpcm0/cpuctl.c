/*==============================================================================
@file    cpuctl.c

@author  Daniel Zorychta

@brief   This file support CPU control

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
// clang-format off
#include "rpcm0/cpuctl.h"
#include "config.h"
#include "kernel/kpanic.h"
#include "kernel/kwrapper.h"
#include "kernel/sysfunc.h"
#include "core_cm0plus.h"
#include "misc/misc.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sio.h"
#include "hardware/regs/watchdog.h"
#include "hardware/regs/psm.h"
#include "hardware/regs/resets.h"
#include "hardware/regs/vreg_and_chip_reset.h"
#include "hardware/structs/vreg_and_chip_reset.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/resets.h"
// clang-format on

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define SRAM_HEAP_START ((void *)&__heap_start)
#define SRAM_HEAP_SIZE  ((size_t)&__heap_size)

// Memory Management Fault Status Register
#define NVIC_MFSR (*(volatile unsigned char *)(0xE000ED28u))

// Bus Fault Status Register
#define NVIC_BFSR (*(volatile unsigned char *)(0xE000ED29u))

// Usage Fault Status Register
#define NVIC_UFSR (*(volatile unsigned short *)(0xE000ED2Au))

// Hard Fault Status Register
#define NVIC_HFSR (*(volatile unsigned int *)(0xE000ED2Cu))

// Debug Fault Status Register
#define NVIC_DFSR (*(volatile unsigned int *)(0xE000ED30u))

// MemManage Fault Address Register
#define NVIC_MFAR (*(volatile unsigned int *)(0xE000ED34u))

// Bus Fault Manage Address Register
#define NVIC_BFAR (*(volatile unsigned int *)(0xE000ED38u))

// Auxiliary Fault Status Register
#define NVIC_AFSR (*(volatile unsigned int *)(0xE000ED3Cu))

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
typedef struct {
        uint32_t R0;
        uint32_t R1;
        uint32_t R2;
        uint32_t R3;
        uint32_t R12;
        uint32_t LR;   // Link register.
        uint32_t PC;   // Program counter.
        uint32_t PSR;  // Program status register.
        uint32_t MFSR; // Memory Management Fault Status Register.
        uint32_t BFSR; // Bus Fault Status Register.
        uint32_t UFSR; // Usage Fault Status Register.
        uint32_t HFSR; // Hard Fault Status Register.
        uint32_t DFSR; // Debug Fault Status Register.
        uint32_t BFAR; // Bus Fault Manage Address Register.
        uint32_t AFSR; // Auxiliary Fault Status Register.
        uint32_t MFAR; // MemManage Fault Address Register
} reg_dump_t;

typedef enum {
        multicore_cmd__update_systick_frequency = 0x7a1aea11,
} multicore_cmd_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void calculate_ticks_per_us(void);
void get_registers_from_stack(uint32_t *stack_address, int cause);

/*==============================================================================
  Local object definitions
==============================================================================*/
extern void *__heap_start;
extern void *__heap_size;

static _mm_region_t sram;

static u32_t ticks_per_us;

static volatile reg_dump_t reg_dump __attribute__((section(".noinit")));

/*==============================================================================
  External object definitions
==============================================================================*/
const char *_cpuctl_preferred_APP_RAM[] = {__CPU_PREFFERED_APP_RAM__, NULL};
const char *_cpuctl_preferred_FS_RAM[] = {__CPU_PREFFERED_FS_RAM__, NULL};
const char *_cpuctl_fast_mem[] = {__CPU_PREFFERED_FAST_RAM__, NULL};

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Set CPU safe clock configuration.
 */
//==============================================================================
static void set_safe_clock_configuration(void)
{
        clocks__configure_internal(clk_adc, 0,
                CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH << CLOCKS_CLK_ADC_CTRL_AUXSRC_LSB, (1 << 8));

        clocks__configure_internal(clk_ref, CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH, 0, (1 << 8));

        clocks__configure_internal(clk_usb, 0,
                CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH << CLOCKS_CLK_USB_CTRL_AUXSRC_LSB, (1 << 8));

        clocks__configure_internal(clk_peri, 0,
                CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH << CLOCKS_CLK_PERI_CTRL_AUXSRC_LSB, (1 << 8));

        clocks__configure_internal(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF, 0, (1 << 8));
}

//==============================================================================
/**
 * @brief  Basic (first) CPU/microcontroller configuration. This function is
 *         called before system start.
 *
 * @param  None
 *
 * @return None
 */
//==============================================================================
void _cpuctl_init(void)
{
        SCB->VTOR = (__CPU_VTOR_TAB_POSITION__ & 0x1FFFFF80UL);

        set_safe_clock_configuration();

#if __CPU_DISABLE_INTER_OF_MCYCLE_INSTR__ == _YES_
        /* disable interrupting multi-cycle instructions */
        SCnSCB->ACTLR |= SCnSCB_ACTLR_DISMCYCINT_Msk;
#endif

        /* enable sleep on idle debug */
        // SET_BIT(DBGMCU->CR, DBGMCU_CR_DBG_SLEEP);

#if (__OS_MONITOR_CPU_LOAD__ > 0)
        _cpuctl_init_CPU_load_counter();
#endif

        _mm_register_region(&sram, SRAM_HEAP_START, SRAM_HEAP_SIZE, _MM_FLAG__DMA, "SRAM");

#if __CPU_SHOW_AND_CLEAR_RESET_SOURCE__
        u32_t reset_flags = vreg_and_chip_reset_hw->chip_reset;

        printk("Reset source:");

        if (reset_flags & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS) {
                printk("  Debug port");
        }

        if (reset_flags & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS) {
                printk("  Pin reset");
        }

        if (reset_flags & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS) {
                printk("  POR or BOR reset");
        }
#endif
}

//==============================================================================
/**
 * @brief  This function restart CPU.
 */
//==============================================================================
void _cpuctl_restart_system(void)
{
        set_safe_clock_configuration();

        CLEAR_BIT(watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);

        SET_BIT(psm_hw->wdsel, PSM_WDSEL_BITS);

        CLEAR_BIT(watchdog_hw->ctrl,
                WATCHDOG_CTRL_PAUSE_DBG0_BITS | WATCHDOG_CTRL_PAUSE_DBG1_BITS | WATCHDOG_CTRL_PAUSE_JTAG_BITS);

        SET_BIT(watchdog_hw->ctrl, WATCHDOG_CTRL_TRIGGER_BITS);

        while (true) {
        }
}

//==============================================================================
/**
 * @brief  This function restart CPU.
 */
//==============================================================================
void _cpuctl_shutdown_system(void)
{
        __disable_irq();

        for (;;) {
                __NOP();
        }
}

//==============================================================================
/**
 * @brief  Start counter used for CPU load measurement. Timer should be set
 *         to at least 1MHz and should not overflow until 1 second.
 *
 * @param  None
 *
 * @return None
 */
//==============================================================================
#if (__OS_MONITOR_CPU_LOAD__ > 0)
void _cpuctl_init_CPU_load_counter(void)
{
}
#endif

//==============================================================================
/**
 * @brief  Function return value that was counted from last call of this function.
 *         This function must reset timer after read. Function is called from
 *         IRQs.
 *
 * @param  None
 *
 * @return Timer value for last read (time delta).
 */
//==============================================================================
#if (__OS_MONITOR_CPU_LOAD__ > 0)
u32_t _cpuctl_get_CPU_load_counter_delta(void)
{
        static uint32_t last[_CPUCTL_CORE_NUM];

        u8_t cpuid = _cpuctl_get_cpu_id();
        bool ovf = SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk;
        u32_t now = SysTick->VAL;

        u32_t delta;
        if (ovf) {
                delta = ((SysTick->LOAD + 1) - now) + last[cpuid];
        } else {
                delta = last[cpuid] - now;
        }

        last[cpuid] = now;

        return delta;
}
#endif

//==============================================================================
/**
 * @brief  Function sleep CPU weakly. All IRQs must be able to wake up CPU.
 *
 * @param  None
 *
 * @return None
 */
//==============================================================================
void _cpuctl_sleep(void)
{
        __WFI();
}

//==============================================================================
/**
 * @brief  Function enable low power mode.
 *
 * @param  mode         mode
 *
 * @return One of errno value.
 */
//==============================================================================
int _cpuctl_low_power_mode(int mode)
{
        int err = EINVAL;

        switch (mode) {
        case low_power_mode__sleep:
                err = 0;
                break;

        default:
                break;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Return CPU ID.
 *
 * @return CPU ID.
 */
//==============================================================================
u8_t _cpuctl_get_cpu_id(void)
{
        return (*(uint32_t *)(SIO_BASE + SIO_CPUID_OFFSET));
}

//==============================================================================
/**
 * @brief
 *
 * @param  ?
 *
 * @return ?
 */
//==============================================================================
static void update_context_timers(u8_t cpuid)
{
#if (__OS_MONITOR_CPU_LOAD__ > 0)
        _cpuctl_init_CPU_load_counter();
#endif

        u32_t clk_sys = clocks__get_clk_sys_frequency();
        SysTick_Config((clk_sys / (u32_t)__OS_TASK_SCHED_FREQ__) - 1);

        if (cpuid != 0) {
                CLEAR_BIT(SysTick->CTRL, SysTick_CTRL_TICKINT_Msk);
                NVIC_ClearPendingIRQ(SysTick_IRQn);
        }

        calculate_ticks_per_us();
}

//==============================================================================
/**
 * @brief  Processor 0 FIFO interrupt.
 */
//==============================================================================
void SIO_IRQ_PROC0_IRQHandler(void)
{
        while (multicore_fifo_rvalid()) {
                uint32_t code = multicore_fifo_pop_blocking();

                if (code == multicore_cmd__update_systick_frequency) {
                        update_context_timers(0);
                }
        }

        /* And explicitly clear any other IRQ flags. */
        multicore_fifo_clear_irq();

        portYIELD_FROM_ISR(pdTRUE);
}

//==============================================================================
/**
 * @brief  Processor 1 FIFO interrupt.
 */
//==============================================================================
void SIO_IRQ_PROC1_IRQHandler(void)
{
        while (multicore_fifo_rvalid()) {
                uint32_t code = multicore_fifo_pop_blocking();

                if (code == multicore_cmd__update_systick_frequency) {
                        update_context_timers(1);
                }
        }

        /* And explicitly clear any other IRQ flags. */
        multicore_fifo_clear_irq();

        portYIELD_FROM_ISR(pdTRUE);
}

//==============================================================================
/**
 * @brief  Function update all system clock after CPU frequency change.
 *         Function must update all devices which base on main clock oscillator.
 *         Function is called after clock/frequency change from clock management driver.
 *
 * @param  None
 *
 * @return None
 */
//==============================================================================
void _cpuctl_update_system_clocks(void)
{
        u8_t cpuid = _cpuctl_get_cpu_id();

        _critical_section_begin();
        update_context_timers(cpuid);
        _critical_section_end();

        multicore_fifo_push_blocking(multicore_cmd__update_systick_frequency);
}

//==============================================================================
/**
 * @brief  Function delay code processing in microseconds.
 *
 * @note   Function should block CPU for specified amount of time.
 * @note   Function should work in critical section and interrupts.
 *
 * @param  microseconds         microsecond delay
 */
//==============================================================================
void _cpuctl_delay_us(u16_t microseconds)
{
        while (microseconds > 0) {

                u32_t ticks = ticks_per_us;

                while (ticks > 0) {
                        __asm volatile("nop");
                        ticks--;
                }

                microseconds--;
        }
}

//==============================================================================
/**
 * @brief  Function calculate number of loop needed to generate 1us delay.
 */
//==============================================================================
static void calculate_ticks_per_us(void)
{
        ticks_per_us = 0;

        u32_t ticks = ((u64_t)250 * SysTick->LOAD * __OS_TASK_SCHED_FREQ__) / 1000000;

        while (SysTick->VAL >= (SysTick->LOAD / 2)) {
                ;
        }

        u32_t target = SysTick->VAL - ticks;

        while (SysTick->VAL > target) {
                ticks_per_us++;
        }

        ticks_per_us /= 185;

        if (ticks_per_us == 0) {
                ticks_per_us = 1;
        }
}

//==============================================================================
/**
 * @brief  Function printout dumped registers.
 */
//==============================================================================
void _cpuctl_print_exception(void)
{
        printk("R0   : %08Xh", reg_dump.R0);
        printk("R1   : %08Xh", reg_dump.R1);
        printk("R2   : %08Xh", reg_dump.R2);
        printk("R3   : %08Xh", reg_dump.R3);
        printk("R12  : %08Xh", reg_dump.R12);
        printk("LR   : %08Xh", reg_dump.LR);
        printk("PC   : %08Xh", reg_dump.PC);
        printk("PSR  : %08Xh", reg_dump.PSR);
        printk("MFSR : %08Xh", reg_dump.MFSR);
        printk("BFSR : %08Xh", reg_dump.BFSR);
        printk("UFSR : %08Xh", reg_dump.UFSR);
        printk("HFSR : %08Xh", reg_dump.HFSR);
        printk("DFSR : %08Xh", reg_dump.DFSR);
        printk("BFAR : %08Xh", reg_dump.BFAR);
        printk("AFSR : %08Xh", reg_dump.AFSR);
        printk("MFAR : %08Xh", reg_dump.MFAR);
}

//==============================================================================
/**
 * @brief  Infinite loop.
 */
//==============================================================================
void _task_infinite_loop(void)
{
        while (true) {
                __asm volatile("nop");
        }
}

//==============================================================================
/**
 * @brief Hard Fault ISR
 */
//==============================================================================
void HardFault_Handler(void)
{
        // remember registers
        __asm volatile("mov r1, #4                   \n"
                       "mov r0, lr                   \n"
                       "tst r0, r1                   \n"
                       "beq equal1HF                 \n"
                       "  mrs r0, psp                \n"
                       "  b end1HF                   \n"
                       "equal1HF:                    \n"
                       "  mrs r0, msp                \n"
                       "end1HF:                      \n"
                       "movs r1, #0                  \n" // _KERNEL_PANIC_DESC_CAUSE_SEGFAULT
                       "b get_registers_from_stack   \n");

        // return to infinite loop instead of broken code
        __asm volatile("mov r1, #4                   \n"
                       "mov r0, lr                   \n"
                       "tst r0, r1                   \n"
                       "beq equal2HF                 \n"
                       "  mrs r0, psp                \n"
                       "  b end2HF                   \n"
                       "equal2HF:                    \n"
                       "  mrs r0, msp                \n"
                       "end2HF:                      \n"
                       "ldr r1, =_task_infinite_loop \n"
                       "str r1, [r0, #24]            \n");
}

//==============================================================================
/**
 * @brief Memory Management failure ISR
 */
//==============================================================================
void MemManage_Handler(void)
{
        // remember registers
        __asm volatile("mov r1, #4                   \n"
                       "mov r0, lr                   \n"
                       "tst r0, r1                   \n"
                       "beq equal1MM                 \n"
                       "  mrs r0, psp                \n"
                       "  b end1MM                   \n"
                       "equal1MM:                    \n"
                       "  mrs r0, msp                \n"
                       "end1MM:                      \n"
                       "movs r1, #2                  \n" // _KERNEL_PANIC_DESC_CAUSE_CPUFAULT_1
                       "b get_registers_from_stack   \n");

        // return to infinite loop instead of broken code
        __asm volatile("mov r1, #4                   \n"
                       "mov r0, lr                   \n"
                       "tst r0, r1                   \n"
                       "beq equal2MM                 \n"
                       "  mrs r0, psp                \n"
                       "  b end2MM                   \n"
                       "equal2MM:                    \n"
                       "  mrs r0, msp                \n"
                       "end2MM:                      \n"
                       "ldr r1, =_task_infinite_loop \n"
                       "str r1, [r0, #24]            \n");
}

//==============================================================================
/**
 * @brief Bus Fault ISR
 */
//==============================================================================
void BusFault_Handler(void)
{
        // remember registers
        __asm volatile("mov r1, #4                   \n"
                       "mov r0, lr                   \n"
                       "tst r0, r1                   \n"
                       "beq equal1BF                 \n"
                       "  mrs r0, psp                \n"
                       "  b end1BF                   \n"
                       "equal1BF:                    \n"
                       "  mrs r0, msp                \n"
                       "end1BF:                      \n"
                       "movs r1, #3                  \n" // _KERNEL_PANIC_DESC_CAUSE_CPUFAULT_2
                       "b get_registers_from_stack   \n");

        // return to infinite loop instead of broken code
        __asm volatile("mov r1, #4                   \n"
                       "mov r0, lr                   \n"
                       "tst r0, r1                   \n"
                       "beq equal2BF                 \n"
                       "  mrs r0, psp                \n"
                       "  b end2BF                   \n"
                       "equal2BF:                    \n"
                       "  mrs r0, msp                \n"
                       "end2BF:                      \n"
                       "ldr r1, =_task_infinite_loop \n"
                       "str r1, [r0, #24]            \n");
}

//==============================================================================
/**
 * @brief Usage Fault ISR
 */
//==============================================================================
void UsageFault_Handler(void)
{
        // remember registers
        __asm volatile("mov r1, #4                   \n"
                       "mov r0, lr                   \n"
                       "tst r0, r1                   \n"
                       "beq equal1UF                 \n"
                       "  mrs r0, psp                \n"
                       "  b end1UF                   \n"
                       "equal1UF:                    \n"
                       "  mrs r0, msp                \n"
                       "end1UF:                      \n"
                       "movs r1, #4                  \n" // _KERNEL_PANIC_DESC_CAUSE_CPUFAULT_3
                       "b get_registers_from_stack   \n");

        // return to infinite loop instead of broken code
        __asm volatile("mov r1, #4                   \n"
                       "mov r0, lr                   \n"
                       "tst r0, r1                   \n"
                       "beq equal2UF                 \n"
                       "  mrs r0, psp                \n"
                       "  b end2UF                   \n"
                       "equal2UF:                    \n"
                       "  mrs r0, msp                \n"
                       "end2UF:                      \n"
                       "ldr r1, =_task_infinite_loop \n"
                       "str r1, [r0, #24]            \n");
}

//==============================================================================
/**
 * @brief  Function dump registers stored in stack.
 *
 * @param  stack_address        stack address
 */
//==============================================================================
void get_registers_from_stack(uint32_t *stack_address, int cause)
{
        reg_dump.R0 = stack_address[0];
        reg_dump.R1 = stack_address[1];
        reg_dump.R2 = stack_address[2];
        reg_dump.R3 = stack_address[3];
        reg_dump.R12 = stack_address[4];
        reg_dump.LR = stack_address[5];
        reg_dump.PC = stack_address[6];
        reg_dump.PSR = stack_address[7];
        reg_dump.MFSR = NVIC_MFSR;
        reg_dump.BFSR = NVIC_BFSR;
        reg_dump.UFSR = NVIC_UFSR;
        reg_dump.HFSR = NVIC_HFSR;
        reg_dump.DFSR = NVIC_DFSR;
        reg_dump.BFAR = NVIC_BFAR;
        reg_dump.AFSR = NVIC_AFSR;
        reg_dump.MFAR = NVIC_MFAR;

        _kernel_panic_report_from_ISR(cause);
}

/*==============================================================================
  End of file
==============================================================================*/
