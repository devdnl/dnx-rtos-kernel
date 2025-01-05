/*==============================================================================
@file    port_helper.c

@author  Daniel Zorychta

@brief   Port helper functions.

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
#include "port_helper.h"
#include "config.h"
#include "cpuctl.h"
#include "core_cm0plus.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define SIO_BASE             0xd0000000UL
#define SIO_CPUID_OFFSET     0x00000000UL
#define SIO_SPINLOCK0_OFFSET 0x00000100UL
#define SIO_FIFO_ST_OFFSET   0x00000050UL
#define SIO_FIFO_ST_VLD_BITS 0x00000001UL
#define SIO_FIFO_ST_RDY_BITS 0x00000002UL
#define SIO_FIFO_RD_OFFSET   0x00000058UL
#define SIO_FIFO_WR_OFFSET   0x00000054UL
#define ARRAY_SIZE(array)    (sizeof(array) / sizeof(array[0]))

/*==============================================================================
  Local object types
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local objects
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  External objects
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/
//==============================================================================
/**
 * @brief  Get core number.
 *
 * @return Current core number.
 */
//==============================================================================
uint8_t get_core_num(void)
{
        return (*(volatile uint32_t *)(SIO_BASE + SIO_CPUID_OFFSET));
}

//==============================================================================
/**
 * @brief  Return spin lock instance.
 *
 * @param  lock_num     spin lock number
 *
 * @return Spin lock instance.
 */
//==============================================================================
spin_lock_t *spin_lock_instance(uint8_t lock_num)
{
        return (spin_lock_t *)(SIO_BASE + SIO_SPINLOCK0_OFFSET + lock_num * 4);
}

//==============================================================================
/**
 * @brief  Check if FIFO has valid data.
 *
 * @return true/false.
 */
//==============================================================================
inline bool multicore_fifo_rvalid(void)
{
        return (*(volatile uint32_t *)(SIO_BASE + SIO_FIFO_ST_OFFSET)) & SIO_FIFO_ST_VLD_BITS;
}

//==============================================================================
/**
 * @brief  Clear FIFO data.
 */
//==============================================================================
inline void multicore_fifo_drain(void)
{
        while (multicore_fifo_rvalid()) {
                (void)(*(volatile uint32_t *)(SIO_BASE + SIO_FIFO_RD_OFFSET));
        }
}

//==============================================================================
/**
 * @brief  Clear FIFO interrupt flag.
 */
//==============================================================================
inline void multicore_fifo_clear_irq(void)
{
        // Write any value to clear the error flags
        *((volatile uint32_t *)(SIO_BASE + SIO_FIFO_ST_OFFSET)) = 0xFF;
}

//==============================================================================
/**
 * @brief  Check the write FIFO to see if it has space for more data
 *
 * @return true if the FIFO has room for more data, false otherwise
 */
//==============================================================================
bool multicore_fifo_wready(void)
{
        // return sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS;
        return *((volatile uint32_t *)(SIO_BASE + SIO_FIFO_ST_OFFSET)) & SIO_FIFO_ST_RDY_BITS;
}

//==============================================================================
/**
 * @brief  Push data on to the write FIFO (data to the other core).
 *
 * This function will block until there is space for the data to be sent.
 * Use multicore_fifo_wready() to check if it is possible to write to the
 * FIFO if you don't want to block.
 *
 * @param  data         A 32 bit value to push on to the FIFO
 */
//==============================================================================
void multicore_fifo_push_blocking(uint32_t data)
{
        // We wait for the fifo to have some space
        while (!multicore_fifo_wready()) {
        }

        // sio_hw->fifo_wr = data;
        *((volatile uint32_t *)(SIO_BASE + SIO_FIFO_WR_OFFSET)) = data;

        // Fire off an event to the other core
        __SEV();
}

//==============================================================================
/**
 * @brief  Pop data from the read FIFO (data from the other core).
 *
 * This function will block until there is data ready to be read
 * Use multicore_fifo_rvalid() to check if data is ready to be read if you don't
 * want to block.
 *
 * @return 32 bit data from the read FIFO.
 */
//==============================================================================
uint32_t multicore_fifo_pop_blocking(void)
{
        // If nothing there yet, we wait for an event first,
        // to try and avoid too much busy waiting
        while (!multicore_fifo_rvalid()) {
                __WFE();
        }

        // return sio_hw->fifo_rd;
        return *((volatile uint32_t *)(SIO_BASE + SIO_FIFO_RD_OFFSET));
}

//==============================================================================
/**
 * @brief  Core 1 trampoline code.
 */
//==============================================================================
static void __attribute__((naked)) core1_trampoline(void)
{
        __asm volatile("pop {r0, r1, pc}");
}

//==============================================================================
/**
 * @brief  Core 1 wrapper
 *
 * @param  entry        code entry
 * @param  stack_base   stack base address
 *
 * @return Value from executed entry function.
 */
//==============================================================================
static inline int core1_wrapper(int (*entry)(void), void *stack_base)
{
        (void)stack_base;
        return (*entry)();
}

//==============================================================================
/**
 * @brief  Launch Core 1.
 *
 * @param  entry        entry function
 * @param  sp           stack pointer top address
 * @param  vector_table vector table address
 */
//==============================================================================
static void multicore_launch_core1_raw(void (*entry)(void), uint32_t *sp, uint32_t vector_table)
{
        // Allow for the fact that the caller may have already enabled the FIFO IRQ for their
        // own purposes (expecting FIFO content after core 1 is launched). We must disable
        // the IRQ during the handshake, then restore afterwards.
        uint32_t irq_num = SIO_IRQ_PROC0_IRQn;
        bool     enabled = NVIC->ISER[irq_num / 32] & (1 << (irq_num % 32));
        NVIC_DisableIRQ(irq_num);

        // Values to be sent in order over the FIFO from core 0 to core 1
        //
        // vector_table is value for VTOR register
        // sp is initial stack pointer (SP)
        // entry is the initial program counter (PC) (don't forget to set the thumb bit!)
        const uint32_t cmd_sequence[] = {0, 0, 1, (uintptr_t)vector_table, (uintptr_t)sp, (uintptr_t)entry};

        uint32_t seq = 0;
        do {
                uint32_t cmd = cmd_sequence[seq];
                // Always drain the READ FIFO (from core 1) before sending a 0
                if (!cmd) {
                        multicore_fifo_drain();
                        // Execute a SEV as core 1 may be waiting for FIFO space via WFE
                        __SEV();
                }
                multicore_fifo_push_blocking(cmd);
                uint32_t response = multicore_fifo_pop_blocking();
                // Move to next state on correct response (echo-d value) otherwise start over
                seq = (cmd == response) ? seq + 1 : 0;
        } while (seq < ARRAY_SIZE(cmd_sequence));

        if (enabled) {
                NVIC_EnableIRQ(irq_num);
        }
}

//==============================================================================
/**
 * @brief  Launch Core 1 with stack address.
 *
 * @param  entry                entry function
 * @param  stack_bottom         stack pointer bottom address
 * @param  stack_size_bytes     stack size in bytes
 */
//==============================================================================
void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom, size_t stack_size_bytes)
{
        uint32_t *stack_ptr = stack_bottom + (stack_size_bytes / sizeof(uint32_t));

        stack_ptr -= 3;
        uint32_t vector_table = SCB->VTOR;

        stack_ptr[0] = (uintptr_t)entry;
        stack_ptr[1] = (uintptr_t)stack_bottom;
        stack_ptr[2] = (uintptr_t)core1_wrapper;

        multicore_launch_core1_raw(core1_trampoline, stack_ptr, vector_table);
}

/*==============================================================================
  End of file
==============================================================================*/
