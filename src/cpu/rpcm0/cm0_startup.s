/*==============================================================================
@file    cm0_startup.s

@author  Daniel Zorychta

@brief   Cortex-M0+ startup code.

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

#define CONTROL_THREAD_UNPRIVILEGED_bit     0
#define CONTROL_ALTERNATE_STACK_bit         1

#define CONTROL_THREAD_UNPRIVILEGED         (1 << CONTROL_THREAD_UNPRIVILEGED_bit)
#define CONTROL_ALTERNATE_STACK             (1 << CONTROL_ALTERNATE_STACK_bit)

#define SIO_BASE                            0xd0000000UL
#define SIO_CPUID_OFFSET                    0x00000000UL
#define PPB_BASE                            0xe0000000UL
#define BOOTROM_VTABLE_OFFSET               0x00
#define M0PLUS_VTOR_OFFSET                  0x0000ed08UL
#define PSM_BASE                            0x40010000UL
#define PSM_FRCE_OFF                        (PSM_BASE + 0x00000004UL)
#define PROC1_BIT                           0x00010000UL

/*==============================================================================
Cortex-M0+ startup code
==============================================================================*/

.text
.balign 2
.syntax unified
.thumb
.thumb_func
.global Reset_Handler

Reset_Handler:
/*==============================================================================
Lock/reset CORE1
==============================================================================*/
    // Only core 0 should run the C runtime startup code; core 1 is normally
    // sleeping in the bootrom at this point but check to be sure (e.g. if
    // debugger put core 1 at the ELF entry point for some reason)
    ldr r0, =(SIO_BASE + SIO_CPUID_OFFSET)
    ldr r0, [r0]
    cmp r0, #0
    beq 1f

    // Send back to the ROM to wait for core 0 to launch it.
    ldr r0, =BOOTROM_VTABLE_OFFSET
    ldr r1, =(PPB_BASE + M0PLUS_VTOR_OFFSET)
    str r0, [r1]
    ldmia r0!, {r1, r2}
    msr msp, r1
    bx r2

1:
    // turn off core 1
    ldr r0, =PSM_FRCE_OFF
    ldr r1, =PROC1_BIT
    str r1, [r0]

    nop
    nop
    nop

    // turn on core 1
    ldr r0, =PSM_FRCE_OFF
    ldr r1, =0
    str r1, [r0]

/*==============================================================================
Initialize the process stack pointer
==============================================================================*/
    ldr r0, =__process_stack_end
    msr PSP, r0

/*==============================================================================
Thread mode uses process stack (PSP) and is privileged
==============================================================================*/
    movs r0, #CONTROL_ALTERNATE_STACK
    msr CONTROL, r0
    isb

/*==============================================================================
Branch to low_level_init_0() function (.data and .bss are not initialized!)
==============================================================================*/
    ldr r0, =low_level_init_0
    blx r0

/*==============================================================================
Initialize .data section
==============================================================================*/
    ldr r0, =__data_start
    ldr r1, =__data_end
    ldr r2, =__data_init_start
    movs r3, #0
    b loop_copy_data_init

copy_data_init:
    ldr r4, [r2, r3]
    str r4, [r0, r3]
    adds r3, r3, #4

loop_copy_data_init:
    adds r4, r0, r3
    cmp r4, r1
    bcc copy_data_init

/*==============================================================================
Zero-init .bss section
==============================================================================*/
    ldr r2, =__bss_start
    ldr r4, =__bss_end
    movs r3, #0
    b loop_fill_zero_bss

fill_zero_bss:
    str  r3, [r2]
    adds r2, r2, #4

loop_fill_zero_bss:
    cmp r2, r4
    bcc fill_zero_bss

/*==============================================================================
Call C++ constructors for global and static objects
==============================================================================*/
#ifdef __USES_CXX
    ldr r0, =__libc_init_array
    blx r0
#endif

/*==============================================================================
Branch to low_level_init_1() function
==============================================================================*/
    ldr r0, =low_level_init_1
    blx r0

/*==============================================================================
Branch to main() with link
==============================================================================*/
    ldr r0, =main
    blx r0

/*==============================================================================
Call C++ destructors for global and static objects
==============================================================================*/
#ifdef __USES_CXX
    ldr r0, =__libc_fini_array
    blx r0
#endif

/*==============================================================================
On return - loop till the end of the world
==============================================================================*/
    b .

/*==============================================================================
__default_low_level_init() - replacement for undefined low_level_init_0()
and/or low_level_init_1(). This function just returns.
==============================================================================*/

.text
.balign 2
.syntax unified
.thumb
.thumb_func
.global __default_low_level_init

__default_low_level_init:
    bx lr

/*==============================================================================
assign undefined low_level_init_0() and/or low_level_init_1() to
__default_low_level_init()
==============================================================================*/

.weak   low_level_init_0
.global low_level_init_0
.set    low_level_init_0, __default_low_level_init

.weak   low_level_init_1
.global low_level_init_1
.set    low_level_init_1, __default_low_level_init

/*******************************************************************************
END OF FILE
*******************************************************************************/
