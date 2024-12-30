/*
 * FreeRTOS Kernel V11.1.0
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: MIT AND BSD-3-Clause
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/*----------------------------------------------------------------------
 * Implementation of functions defined in portable.h for the RP2040 port.
 *----------------------------------------------------------------------*/

//#pragma GCC optimize("Og")

#include "FreeRTOS.h"
#include "task.h"
#include "port_helper.h"
#include "core_cm0plus.h"

/* Constants required to manipulate the NVIC. */
#define portNVIC_SYSTICK_CTRL_REG          (*((volatile uint32_t *)0xe000e010))
#define portNVIC_SYSTICK_LOAD_REG          (*((volatile uint32_t *)0xe000e014))
#define portNVIC_SYSTICK_CURRENT_VALUE_REG (*((volatile uint32_t *)0xe000e018))
#define portNVIC_SHPR3_REG                 (*((volatile uint32_t *)0xe000ed20))
#define portNVIC_SYSTICK_CLK_BIT           (1UL << 2UL)
#define portNVIC_SYSTICK_INT_BIT           (1UL << 1UL)
#define portNVIC_SYSTICK_ENABLE_BIT        (1UL << 0UL)
#define portNVIC_SYSTICK_COUNT_FLAG_BIT    (1UL << 16UL)
#define portMIN_INTERRUPT_PRIORITY         (255UL)
#define portNVIC_PENDSV_PRI                (portMIN_INTERRUPT_PRIORITY << 16UL)
#define portNVIC_SYSTICK_PRI               (portMIN_INTERRUPT_PRIORITY << 24UL)
#define portUSE_DIVIDER_SAVE_RESTORE       1

#ifndef SIO_BASE
#define SIO_BASE 0xd0000000UL
#endif

#ifndef SIO_FIFO_WR_OFFSET
#define SIO_FIFO_WR_OFFSET 0x00000054UL
#endif

/* Constants required to set up the initial stack. */
#define portINITIAL_XPSR (0x01000000)

/* The systick is a 24-bit counter. */
#define portMAX_24_BIT_NUMBER (0xffffffUL)

/* A fiddle factor to estimate the number of SysTick counts that would have
 * occurred while the SysTick counter is stopped during tickless idle
 * calculations. */
#ifndef portMISSED_COUNTS_FACTOR
#define portMISSED_COUNTS_FACTOR (45UL)
#endif

/* Let the user override the pre-loading of the initial LR with the address of
 * prvTaskExitError() in case it messes up unwinding of the stack in the
 * debugger. */
#ifdef configTASK_RETURN_ADDRESS
#define portTASK_RETURN_ADDRESS configTASK_RETURN_ADDRESS
#else
#define portTASK_RETURN_ADDRESS prvTaskExitError
#endif

/*
 * Setup the timer to generate the tick interrupts.  The implementation in this
 * file is weak to allow application writers to change the timer used to
 * generate the tick interrupt.
 */
void vPortSetupTimerInterrupt(void);

/*
 * Exception handlers.
 */
void xPortPendSVHandler(void) __attribute__((naked));
void xPortSysTickHandler(void);
void vPortSVCHandler(void);

/*
 * Start first task is a separate function so it can be tested in isolation.
 */
static void vPortStartFirstTask(void) __attribute__((naked));

/*
 * Used to catch tasks that attempt to return from their implementing function.
 */
static void prvTaskExitError(void);

/*-----------------------------------------------------------*/

/* Each task maintains its own interrupt status in the critical nesting
 * variable. This is initialized to 0 to allow vPortEnter/ExitCritical
 * to be called before the scheduler is started */
UBaseType_t uxCriticalNestings[configNUMBER_OF_CORES] = {0};

/*-----------------------------------------------------------*/

/*
 * The number of SysTick increments that make up one tick period.
 */
#if (configUSE_TICKLESS_IDLE == 1)
static uint32_t ulTimerCountsForOneTick = 0;
#endif /* configUSE_TICKLESS_IDLE */

/*
 * The maximum number of tick periods that can be suppressed is limited by the
 * 24 bit resolution of the SysTick timer.
 */
#if (configUSE_TICKLESS_IDLE == 1)
static uint32_t xMaximumPossibleSuppressedTicks = 0;
#endif /* configUSE_TICKLESS_IDLE */

/*
 * Compensate for the CPU cycles that pass while the SysTick is stopped (low
 * power functionality only).
 */
#if (configUSE_TICKLESS_IDLE == 1)
static uint32_t ulStoppedTimerCompensation = 0;
#endif /* configUSE_TICKLESS_IDLE */

/*-----------------------------------------------------------*/

#define INVALID_PRIMARY_CORE_NUM 0xffu
/* The primary core number (the own which has the SysTick handler) */
static uint8_t ucPrimaryCoreNum = INVALID_PRIMARY_CORE_NUM;

/* Note: portIS_FREE_RTOS_CORE() also returns false until the scheduler is started */
#define portIS_FREE_RTOS_CORE() (ucPrimaryCoreNum != INVALID_PRIMARY_CORE_NUM)

/*
 * See header file for description.
 */
StackType_t *pxPortInitialiseStack(StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters)
{
        /* Simulate the stack frame as it would be created by a context switch
         * interrupt. */
        pxTopOfStack--; /* Offset added to account for the way the MCU uses the stack on entry/exit of
                           interrupts. */
        *pxTopOfStack = portINITIAL_XPSR; /* xPSR */
        pxTopOfStack--;
        *pxTopOfStack = (StackType_t)pxCode; /* PC */
        pxTopOfStack--;
        *pxTopOfStack = (StackType_t)portTASK_RETURN_ADDRESS; /* LR */
        pxTopOfStack -= 5;                                    /* R12, R3, R2 and R1. */
        *pxTopOfStack = (StackType_t)pvParameters;            /* R0 */
        pxTopOfStack -= 8;                                    /* R11..R4. */

        return pxTopOfStack;
}

static void prvTaskExitError(void)
{
        /* A function that implements a task must not exit or attempt to return to
         * its caller as there is nothing to return to.  If a task wants to exit it
         * should instead call vTaskDelete( NULL ). */
        while (1) {
                __asm volatile("nop");
        }
}

void vPortSVCHandler(void)
{
        /* This function is no longer used, but retained for backward
         * compatibility. */
}

void vPortStartFirstTask(void)
{
        __asm volatile(
                "    .syntax unified                \n"
#if configRESET_STACK_POINTER
                "   ldr  r0, =0xE000ED08            \n" /* Use the NVIC offset register to locate the stack.
                                                         */
                "   ldr r0, [r0]                    \n"
                "   ldr r0, [r0]                    \n"
                "   msr msp, r0                     \n" /* Set the msp back to the start of the stack. */
#endif                                                  /* configRESET_STACK_POINTER */
                "   adr r1, ulAsmLocals             \n" /* Get the location of the current TCB for the current
                                                           core. */
                "   ldmia r1!, {r2, r3}             \n"
                "   ldr r2, [r2]                    \n" /* r2 = Core number */
                "   lsls r2, #2                     \n"
                "   ldr r3, [r3, r2]                \n" /* r3 = pxCurrentTCBs[get_core_num()] */

                "    ldr  r0, [r3]                  \n" /* The first item in pxCurrentTCB is the task top
                                                           of stack. */
                "    adds r0, #32                   \n" /* Discard everything up to r0. */
                "    msr  psp, r0                   \n" /* This is now the new top of stack to use in the
                                                           task. */
                "    movs r0, #2                    \n" /* Switch to the psp stack. */
                "    msr  CONTROL, r0               \n"
                "    isb                            \n"
                "    pop  {r0-r5}                   \n" /* Pop the registers that are saved
                                                           automatically. */
                "    mov  lr, r5                    \n" /* lr is now in r5. */
                "    pop  {r3}                      \n" /* Return address is now in r3. */
                "    pop  {r2}                      \n" /* Pop and discard XPSR. */
                "    cpsie i                        \n" /* The first task has its context and interrupts
                                                           can be enabled. */
                "    bx   r3                        \n" /* Finally, jump to the user defined task code.
                                                         */
                "                                   \n"
                "     .align 4                      \n"
                "ulAsmLocals:                       \n"
                "    .word 0xD0000000               \n" /* SIO */
                "    .word pxCurrentTCBs            \n");
}

void SIO_IRQ_PROC0_IRQHandler(void)
{
        /* We must remove the contents (which we don't care about)
         * to clear the IRQ */
        multicore_fifo_drain();

        /* And explicitly clear any other IRQ flags. */
        multicore_fifo_clear_irq();

        portYIELD_FROM_ISR(pdTRUE);
}

void SIO_IRQ_PROC1_IRQHandler(void)
{
        /* We must remove the contents (which we don't care about)
         * to clear the IRQ */
        multicore_fifo_drain();

        /* And explicitly clear any other IRQ flags. */
        multicore_fifo_clear_irq();

        portYIELD_FROM_ISR(pdTRUE);
}

/*
 * See header file for description.
 */
static BaseType_t xPortStartSchedulerOnCore()
{
        if (ucPrimaryCoreNum == get_core_num()) {
                /* Start the timer that generates the tick ISR.  Interrupts are disabled
                 * here already. */
                vPortSetupTimerInterrupt();

                /* Make PendSV, CallSV and SysTick the same priority as the kernel. */
                portNVIC_SHPR3_REG |= portNVIC_SYSTICK_PRI;
        }

        portNVIC_SHPR3_REG |= portNVIC_PENDSV_PRI;

        /* Install FIFO handler to receive interrupt from other core */
        multicore_fifo_clear_irq();
        multicore_fifo_drain();
        uint32_t ulIRQNum = SIO_IRQ_PROC0_IRQn + get_core_num();
        NVIC_SetPriority(ulIRQNum, portMIN_INTERRUPT_PRIORITY);
        NVIC_EnableIRQ(ulIRQNum);

        /* Start the first task. */
        vPortStartFirstTask();

        /* Should never get here as the tasks will now be executing!  Call the task
         * exit error function to prevent compiler warnings about a static function
         * not being called in the case that the application writer overrides this
         * functionality by defining configTASK_RETURN_ADDRESS.  Call
         * vTaskSwitchContext() so link time optimisation does not remove the
         * symbol. */
        vTaskSwitchContext(portGET_CORE_ID());
        prvTaskExitError();

        /* Should not get here! */
        return 0;
}

static void prvDisableInterruptsAndPortStartSchedulerOnCore(void)
{
        portDISABLE_INTERRUPTS();
        xPortStartSchedulerOnCore();
}

/*
 * See header file for description.
 */
BaseType_t xPortStartScheduler(void)
{
        configASSERT(ucPrimaryCoreNum == INVALID_PRIMARY_CORE_NUM);

        spin_lock_t *spin_lock = spin_lock_instance(configSMP_SPINLOCK_0);
        __mem_fence_release();
        *spin_lock = 1;

        spin_lock = spin_lock_instance(configSMP_SPINLOCK_1);
        __mem_fence_release();
        *spin_lock = 1;

        ucPrimaryCoreNum = configTICK_CORE;
        configASSERT(get_core_num() == 0); /* we must be started on core 0 */

        StaticTask_t *IdleTaskTCBBuffer;
        StackType_t  *IdleTaskStackBuffer;
        uint32_t      IdleTaskStackSize;
        vApplicationGetPassiveIdleTaskMemory(&IdleTaskTCBBuffer, &IdleTaskStackBuffer, &IdleTaskStackSize, 0);

        multicore_launch_core1_with_stack(prvDisableInterruptsAndPortStartSchedulerOnCore,
                IdleTaskStackBuffer, IdleTaskStackSize);

        xPortStartSchedulerOnCore();

        /* Should not get here! */
        return 0;
}

void vPortEndScheduler(void)
{
        /* Not implemented in ports where there is nothing to return to. */
        while (1) {
                __asm volatile("nop");
        }
}

void vPortYield(void)
{
        /* Set a PendSV to request a context switch. */
        portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;

        /* Barriers are normally not required but do ensure the code is completely
         * within the specified behaviour for the architecture. */
        __asm volatile("dsb" ::: "memory");
        __asm volatile("isb");
}

void vPortEnableInterrupts(void)
{
        __asm volatile(" cpsie i " ::: "memory");
}

uint32_t ulSetInterruptMaskFromISR(void)
{
        __asm volatile(" mrs r0, PRIMASK    \n"
                       " cpsid i            \n"
                       " bx lr                " ::
                               : "memory");
}

void vClearInterruptMaskFromISR(__attribute__((unused)) uint32_t ulMask)
{
        __asm volatile(" msr PRIMASK, r0    \n"
                       " bx lr                " ::
                               : "memory");
}

void vYieldCore(int xCoreID)
{
        /* Remove warning if configASSERT is not defined.
         * xCoreID is not used in this function due to this is a dual-core system. The yielding core must be
         * different from the current core. */
        (void)xCoreID;

        configASSERT(xCoreID != (int)portGET_CORE_ID());

        /* Non blocking, will cause interrupt on other core if the queue isn't already full,
         * in which case an IRQ must be pending */
        // sio_hw->fifo_wr = 0;
        *((volatile uint32_t *)(SIO_BASE + SIO_FIFO_WR_OFFSET)) = 0;
}

void xPortPendSVHandler(void)
{
        /* This is a naked function. */
        __asm volatile(
                "   .syntax unified                     \n"
                "   mrs r1, psp                         \n"
                "                                       \n"
                "   adr    r0, ulAsmLocals2             \n" /* Get the location of the current TCB for the
                                                               current core. */
                "   ldmia r0!, {r2, r3}                 \n"

                "   ldr r0, [r2]                        \n" /* r0 = Core number */
                "   lsls r0, r0, #2                     \n"
                "   adds r3, r0                         \n" /* r3 = &pxCurrentTCBs[get_core_num()] */

                "   ldr    r0, [r3]                     \n" /* r0 = pxCurrentTCB */
                "                                       \n"
                "   subs r1, r1, #32                    \n" /* Make space for the remaining low registers. */
                "   str r1, [r0]                        \n" /* Save the new top of stack. */
                "   stmia r1!, {r4-r7}                  \n" /* Store the low registers that are not saved
                                                               automatically. */
                "   mov r4, r8                          \n" /* Store the high registers. */
                "   mov r5, r9                          \n"
                "   mov r6, r10                         \n"
                "   mov r7, r11                         \n"
                "   stmia r1!, {r4-r7}                  \n"
#if portUSE_DIVIDER_SAVE_RESTORE

                /* We expect that the divider is ready at this point (which is
                 * necessary to safely save/restore), because:
                 * a) if we have not been interrupted since we entered this method,
                 *    then >8 cycles have clearly passed, so the divider is done
                 * b) if we were interrupted in the interim, then any "safe" - i.e.
                 *    does the right thing in an IRQ - use of the divider should
                 *    have waited for any in-process divide to complete, saved and
                 *    then fully restored the result, thus the result is ready in
                 *    that case too. */
                "   ldr r4, [r2, #0x60]                 \n" /* SIO_DIV_UDIVIDEND_OFFSET */
                "   ldr r5, [r2, #0x64]                 \n" /* SIO_DIV_UDIVISOR_OFFSET */
                "   ldr r6, [r2, #0x74]                 \n" /* SIO_DIV_REMAINDER_OFFSET */
                "   ldr r7, [r2, #0x70]                 \n" /* SIO_DIV_QUOTIENT_OFFSET */

                /* We actually save the divider state in the 4 words below
                 * our recorded stack pointer, so as not to disrupt the stack
                 * frame expected by debuggers - this is addressed by
                 * portEXTRA_STACK_SIZE */
                "   subs r1, r1, #48                    \n"
                "   stmia r1!, {r4-r7}                  \n"
#endif                                                      /* portUSE_DIVIDER_SAVE_RESTORE */
                "   ldr r0, [r2]                        \n" /* r0 = Core number */

                "   push {r3, r14}                      \n"
                "   cpsid i                             \n"
                "   bl vTaskSwitchContext               \n"
                "   cpsie i                             \n"
                "   pop {r2, r3}                        \n" /* lr goes in r3. r2 now holds tcb pointer. */
                "                                       \n"
                "   ldr r1, [r2]                        \n"
                "   ldr r0, [r1]                        \n" /* The first item in pxCurrentTCB is the task top
                                                               of stack. */
                "   adds r0, r0, #16                    \n" /* Move to the high registers. */
                "   ldmia r0!, {r4-r7}                  \n" /* Pop the high registers. */
                "    mov r8, r4                         \n"
                "    mov r9, r5                         \n"
                "    mov r10, r6                        \n"
                "    mov r11, r7                        \n"
                "                                       \n"
                "   msr psp, r0                         \n" /* Remember the new top of stack for the task. */
                "                                       \n"
#if portUSE_DIVIDER_SAVE_RESTORE
                "   movs r2, #0xd                       \n" /* Pop the divider state. */
                "   lsls r2, #28                        \n"
                "   subs r0, r0, #48                    \n" /* Go back for the divider state */
                "   ldmia r0!, {r4-r7}                  \n" /* Pop the divider state. */

                /* Note always restore via SIO_DIV_UDIVI*, because we will overwrite the
                 * results stopping the calculation anyway, however the sign of results
                 * is adjusted by the h/w at read time based on whether the last started
                 * division was signed and the inputs' signs differed */
                "   str r4, [r2, #0x60]                 \n" /* SIO_DIV_UDIVIDEND_OFFSET */
                "   str r5, [r2, #0x64]                 \n" /* SIO_DIV_UDIVISOR_OFFSET */
                "   str r6, [r2, #0x74]                 \n" /* SIO_DIV_REMAINDER_OFFSET */
                "   str r7, [r2, #0x70]                 \n" /* SIO_DIV_QUOTIENT_OFFSET */
#else                                                       /* if portUSE_DIVIDER_SAVE_RESTORE */
                "   subs r0, r0, #32                    \n" /* Go back for the low registers that are not
                                                               automatically restored. */
#endif                                                      /* portUSE_DIVIDER_SAVE_RESTORE */
                "   ldmia r0!, {r4-r7}                  \n" /* Pop low registers.  */
                "                                       \n"
                "   bx r3                               \n"
                "                                       \n"
                "   .align 4                            \n"
                "ulAsmLocals2:                          \n"
                "   .word 0xD0000000                    \n" /* SIO */
                "   .word pxCurrentTCBs                 \n");
}

void xPortSysTickHandler(void)
{
        uint32_t ulPreviousMask;

        ulPreviousMask = taskENTER_CRITICAL_FROM_ISR();
        traceISR_ENTER();
        {
                /* Increment the RTOS tick. */
                if (xTaskIncrementTick() != pdFALSE) {
                        traceISR_EXIT_TO_SCHEDULER();
                        /* Pend a context switch. */
                        portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;
                } else {
                        traceISR_EXIT();
                }
        }
        taskEXIT_CRITICAL_FROM_ISR(ulPreviousMask);
}

/*
 * Setup the systick timer to generate the tick interrupts at the required
 * frequency.
 */
__attribute__((weak)) void vPortSetupTimerInterrupt(void)
{
/* Calculate the constants required to configure the tick interrupt. */
#if (configUSE_TICKLESS_IDLE == 1)
        {
                ulTimerCountsForOneTick         = (configCPU_CLOCK_HZ / configTICK_RATE_HZ);
                xMaximumPossibleSuppressedTicks = portMAX_24_BIT_NUMBER / ulTimerCountsForOneTick;
                ulStoppedTimerCompensation      = portMISSED_COUNTS_FACTOR;
        }
#endif /* configUSE_TICKLESS_IDLE */

        /* Stop and reset the SysTick. */
        portNVIC_SYSTICK_CTRL_REG          = 0UL;
        portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;

        /* Configure SysTick to interrupt at the requested rate. */
        portNVIC_SYSTICK_LOAD_REG = (configCPU_CLOCK_HZ / configTICK_RATE_HZ) - 1UL;
        portNVIC_SYSTICK_CTRL_REG = portNVIC_SYSTICK_CLK_BIT | portNVIC_SYSTICK_INT_BIT |
                portNVIC_SYSTICK_ENABLE_BIT;
}

#if (configUSE_TICKLESS_IDLE == 1)

__attribute__((weak)) void vPortSuppressTicksAndSleep(TickType_t xExpectedIdleTime)
{
        uint32_t   ulReloadValue, ulCompleteTickPeriods, ulCompletedSysTickDecrements;
        TickType_t xModifiableIdleTime;

        /* Make sure the SysTick reload value does not overflow the counter. */
        if (xExpectedIdleTime > xMaximumPossibleSuppressedTicks) {
                xExpectedIdleTime = xMaximumPossibleSuppressedTicks;
        }

        /* Stop the SysTick momentarily.  The time the SysTick is stopped for
         * is accounted for as best it can be, but using the tickless mode will
         * inevitably result in some tiny drift of the time maintained by the
         * kernel with respect to calendar time. */
        portNVIC_SYSTICK_CTRL_REG &= ~portNVIC_SYSTICK_ENABLE_BIT;

        /* Calculate the reload value required to wait xExpectedIdleTime
         * tick periods.  -1 is used because this code will execute part way
         * through one of the tick periods. */
        ulReloadValue = portNVIC_SYSTICK_CURRENT_VALUE_REG +
                (ulTimerCountsForOneTick * (xExpectedIdleTime - 1UL));

        if (ulReloadValue > ulStoppedTimerCompensation) {
                ulReloadValue -= ulStoppedTimerCompensation;
        }

        /* Enter a critical section but don't use the taskENTER_CRITICAL()
         * method as that will mask interrupts that should exit sleep mode. */
        __asm volatile("cpsid i" ::: "memory");
        __asm volatile("dsb");
        __asm volatile("isb");

        /* If a context switch is pending or a task is waiting for the scheduler
         * to be unsuspended then abandon the low power entry. */
        if (eTaskConfirmSleepModeStatus() == eAbortSleep) {
                /* Restart from whatever is left in the count register to complete
                 * this tick period. */
                portNVIC_SYSTICK_LOAD_REG = portNVIC_SYSTICK_CURRENT_VALUE_REG;

                /* Restart SysTick. */
                portNVIC_SYSTICK_CTRL_REG |= portNVIC_SYSTICK_ENABLE_BIT;

                /* Reset the reload register to the value required for normal tick
                 * periods. */
                portNVIC_SYSTICK_LOAD_REG = ulTimerCountsForOneTick - 1UL;

                /* Re-enable interrupts - see comments above the cpsid instruction()
                 * above. */
                __asm volatile("cpsie i" ::: "memory");
        } else {
                /* Set the new reload value. */
                portNVIC_SYSTICK_LOAD_REG = ulReloadValue;

                /* Clear the SysTick count flag and set the count value back to
                 * zero. */
                portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;

                /* Restart SysTick. */
                portNVIC_SYSTICK_CTRL_REG |= portNVIC_SYSTICK_ENABLE_BIT;

                /* Sleep until something happens.  configPRE_SLEEP_PROCESSING() can
                 * set its parameter to 0 to indicate that its implementation contains
                 * its own wait for interrupt or wait for event instruction, and so wfi
                 * should not be executed again.  However, the original expected idle
                 * time variable must remain unmodified, so a copy is taken. */
                xModifiableIdleTime = xExpectedIdleTime;
                configPRE_SLEEP_PROCESSING(xModifiableIdleTime);

                if (xModifiableIdleTime > 0) {
                        __asm volatile("dsb" ::: "memory");
                        __asm volatile("wfi");
                        __asm volatile("isb");
                }

                configPOST_SLEEP_PROCESSING(xExpectedIdleTime);

                /* Re-enable interrupts to allow the interrupt that brought the MCU
                 * out of sleep mode to execute immediately.  see comments above
                 * __disable_interrupt() call above. */
                __asm volatile("cpsie i" ::: "memory");
                __asm volatile("dsb");
                __asm volatile("isb");

                /* Disable interrupts again because the clock is about to be stopped
                 * and interrupts that execute while the clock is stopped will increase
                 * any slippage between the time maintained by the RTOS and calendar
                 * time. */
                __asm volatile("cpsid i" ::: "memory");
                __asm volatile("dsb");
                __asm volatile("isb");

                /* Disable the SysTick clock without reading the
                 * portNVIC_SYSTICK_CTRL_REG register to ensure the
                 * portNVIC_SYSTICK_COUNT_FLAG_BIT is not cleared if it is set.  Again,
                 * the time the SysTick is stopped for is accounted for as best it can
                 * be, but using the tickless mode will inevitably result in some tiny
                 * drift of the time maintained by the kernel with respect to calendar
                 * time*/
                portNVIC_SYSTICK_CTRL_REG = (portNVIC_SYSTICK_CLK_BIT | portNVIC_SYSTICK_INT_BIT);

                /* Determine if the SysTick clock has already counted to zero and
                 * been set back to the current reload value (the reload back being
                 * correct for the entire expected idle time) or if the SysTick is yet
                 * to count to zero (in which case an interrupt other than the SysTick
                 * must have brought the system out of sleep mode). */
                if ((portNVIC_SYSTICK_CTRL_REG & portNVIC_SYSTICK_COUNT_FLAG_BIT) != 0) {
                        uint32_t ulCalculatedLoadValue;

                        /* The tick interrupt is already pending, and the SysTick count
                         * reloaded with ulReloadValue.  Reset the
                         * portNVIC_SYSTICK_LOAD_REG with whatever remains of this tick
                         * period. */
                        ulCalculatedLoadValue = (ulTimerCountsForOneTick - 1UL) -
                                (ulReloadValue - portNVIC_SYSTICK_CURRENT_VALUE_REG);

                        /* Don't allow a tiny value, or values that have somehow
                         * underflowed because the post sleep hook did something
                         * that took too long. */
                        if ((ulCalculatedLoadValue < ulStoppedTimerCompensation) ||
                                (ulCalculatedLoadValue > ulTimerCountsForOneTick)) {
                                ulCalculatedLoadValue = (ulTimerCountsForOneTick - 1UL);
                        }

                        portNVIC_SYSTICK_LOAD_REG = ulCalculatedLoadValue;

                        /* As the pending tick will be processed as soon as this
                         * function exits, the tick value maintained by the tick is stepped
                         * forward by one less than the time spent waiting. */
                        ulCompleteTickPeriods = xExpectedIdleTime - 1UL;
                } else {
                        /* Something other than the tick interrupt ended the sleep.
                         * Work out how long the sleep lasted rounded to complete tick
                         * periods (not the ulReload value which accounted for part
                         * ticks). */
                        ulCompletedSysTickDecrements = (xExpectedIdleTime * ulTimerCountsForOneTick) -
                                portNVIC_SYSTICK_CURRENT_VALUE_REG;

                        /* How many complete tick periods passed while the processor
                         * was waiting? */
                        ulCompleteTickPeriods = ulCompletedSysTickDecrements / ulTimerCountsForOneTick;

                        /* The reload value is set to whatever fraction of a single tick
                         * period remains. */
                        portNVIC_SYSTICK_LOAD_REG = ((ulCompleteTickPeriods + 1UL) *
                                                            ulTimerCountsForOneTick) -
                                ulCompletedSysTickDecrements;
                }

                /* Restart SysTick so it runs from portNVIC_SYSTICK_LOAD_REG
                 * again, then set portNVIC_SYSTICK_LOAD_REG back to its standard
                 * value. */
                portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;
                portNVIC_SYSTICK_CTRL_REG |= portNVIC_SYSTICK_ENABLE_BIT;
                vTaskStepTick(ulCompleteTickPeriods);
                portNVIC_SYSTICK_LOAD_REG = ulTimerCountsForOneTick - 1UL;

                /* Exit with interrupts enabled. */
                __asm volatile("cpsie i" ::: "memory");
        }
}

#endif /* configUSE_TICKLESS_IDLE */
