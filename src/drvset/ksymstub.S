/*==============================================================================
@file    ksymstub.S

@author  Daniel Zorychta

@brief   Kernel symbol stubs linked to drivers built separately (drvset).

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

/*
 * The file is not a part of the kernel. It is assembled by the drvset build
 * and provides all kernel symbols (kernel/ksymtab.h) to drivers and file
 * systems. Each symbol is a small stub that loads the function address from
 * the kernel symbol table and jumps to it. Registers and stack are not
 * changed, so stubs work for any function (also variadic ones). Each stub is
 * placed in own section, unused stubs are removed by the linker
 * (--gc-sections).
 *
 * The linker script of drvset defines the symbol __ksymtab_vector, address
 * of the vector table entry _KVECTOR_KSYMTAB (kernel/kvectors.h) of the kernel image
 * (kernel vector table address + 0x3F8). In the POSIX simulator (i386) it is
 * the first word of drvset RAM, where the kernel stores the symbol table
 * address.
 *
 * Cost: 16 bytes of flash (Cortex-M3/M4/M7), 20 bytes (Cortex-M0+) or 16 bytes
 * (simulator) per used symbol, no RAM, 3 loads and a jump per call (2 loads in
 * the simulator).
 */

#include "kernel/ksymtab.h"

        .set    ksym_idx, 0

#if defined(__i386__)
        /* POSIX simulator: eax is not used for arguments (cdecl) */
        .macro  KSYM_STUB name
        .section .text.ksym.\name, "ax", @progbits
        .global \name
        .type   \name, @function
        .align  4
\name:
        movl    __ksymtab_vector, %eax  /* symbol table */
        jmp     *(_KSYMTAB_SYM_OFFSET + 4 * ksym_idx)(%eax)
        .size   \name, . - \name
        .set    ksym_idx, ksym_idx + 1
        .endm
#else
        .syntax unified
        .thumb

        .macro  KSYM_STUB name
        .section .text.ksym.\name, "ax", %progbits
        .global \name
        .type   \name, %function
        .thumb_func
        .align  2
\name:
#if defined(__ARM_ARCH_6M__)
        /* Thumb-1: r12 is not usable by loads, address is passed on stack */
        push    {r0, r1, r2}            // r2 slot receives function address
        ldr     r0, 1f
        ldr     r0, [r0]                // symbol table
        ldr     r1, 2f
        ldr     r0, [r0, r1]            // function address
        str     r0, [sp, #8]
        pop     {r0, r1, pc}            // restore r0, r1 and jump (r2 unchanged)
        .align  2
1:      .word   __ksymtab_vector
2:      .word   _KSYMTAB_SYM_OFFSET + 4 * ksym_idx
#else
        ldr     r12, 1f
        ldr     r12, [r12]              // symbol table
        ldr     r12, [r12, #(_KSYMTAB_SYM_OFFSET + 4 * ksym_idx)]
        bx      r12
        .align  2
1:      .word   __ksymtab_vector
#endif
        .size   \name, . - \name
        .set    ksym_idx, ksym_idx + 1
        .endm
#endif

#define _KSYM_STUB(name) KSYM_STUB name;

        _KSYMTAB_LIST(_KSYM_STUB)

#if defined(__i386__)
        .section .note.GNU-stack, "", @progbits
#endif

/*==============================================================================
  End of file
==============================================================================*/
